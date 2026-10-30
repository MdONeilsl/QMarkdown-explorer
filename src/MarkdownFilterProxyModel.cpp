#include "MarkdownFilterProxyModel.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

QString toQString(const std::filesystem::path &p)
{
    const std::u8string u8 = p.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char *>(u8.data()),
        static_cast<qsizetype>(u8.size()));
}

// Cache keys and runtime lookups must compare byte-for-byte, so both
// sides are normalised through here: forward slashes (QFileSystemModel
// uses them on every platform), and no trailing separator *except* on a
// bare drive root ("D:/"), where stripping the slash would turn an
// absolute path into the drive-relative "D:" — a different location
// entirely on Windows.
QString normalizePath(const QString &path)
{
    QString p = QDir::fromNativeSeparators(path);
    while (p.size() > 1 && p.endsWith(u'/')) {
        const int n = p.size();
        if (n >= 2 && p.at(n - 2) == u':')
            break;
        p.chop(1);
    }
    return p;
}

// Prefix checks require a common shape: "D:/foo/" starts with "D:/",
// but "D:/foo" does not — ensureTrailingSlash removes that asymmetry.
QString ensureTrailingSlash(QString p)
{
    if (!p.endsWith(u'/'))
        p += u'/';
    return p;
}

bool hasMarkdownExtension(const std::filesystem::path &p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return ext == ".md" || ext == ".markdown";
}

bool fileContainsText(const std::filesystem::path &p, const QString &needle)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open())
        return false;

    const std::string raw((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    in.close();

    std::size_t offset = 0;
    if (raw.size() >= 3
        && static_cast<unsigned char>(raw[0]) == 0xEF
        && static_cast<unsigned char>(raw[1]) == 0xBB
        && static_cast<unsigned char>(raw[2]) == 0xBF) {
        offset = 3;
    }

    const QString content = QString::fromUtf8(
        raw.data() + offset,
        static_cast<qsizetype>(raw.size() - offset));
    return content.contains(needle, Qt::CaseInsensitive);
}

bool isHiddenName(const std::filesystem::path &p)
{
    const std::u8string name = p.filename().u8string();
    return !name.empty() && name[0] == u8'.';
}

} // namespace

MarkdownFilterProxyModel::MarkdownFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void MarkdownFilterProxyModel::setSourceRootPath(const QString &path)
{
    const QString normalised = normalizePath(path);
    if (m_rootPath == normalised)
        return;
    m_rootPath = normalised;
    rebuildCache();
    invalidateFilter();
}

void MarkdownFilterProxyModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    rebuildCache();
    invalidateFilter();
}

void MarkdownFilterProxyModel::refresh()
{
    rebuildCache();
    invalidateFilter();
}

void MarkdownFilterProxyModel::rebuildCache()
{
    m_matchingFiles.clear();
    m_visibleDirs.clear();

    if (m_rootPath.isEmpty())
        return;

    const std::filesystem::path root = toFsPath(m_rootPath);

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
        return;

    const bool hasSearch = !m_searchText.isEmpty();

    // skip_permission_denied keeps the walk alive when a subfolder is
    // unreadable: we miss its files rather than aborting the whole scan
    // and leaving the tree empty.
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec)
        return;

    const std::filesystem::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const std::filesystem::directory_entry &entry = *it;
        const std::filesystem::path p = entry.path();

        // Dot-prefixed entries are hidden by convention (.git, .vscode,
        // .obsidian, …).  For a hidden folder we also suppress the
        // iterator’s descent, otherwise the walk would scan its entire
        // subtree only to throw the results away.
        if (isHiddenName(p)) {
            if (entry.is_directory(ec) && !ec)
                it.disable_recursion_pending();
            ec.clear();
            continue;
        }

        if (entry.is_directory(ec) && !ec) {
            ec.clear();
            continue;
        }
        ec.clear();

        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        if (!hasMarkdownExtension(p))
            continue;

        bool match = true;
        if (hasSearch) {
            const QString fileName = toQString(p.filename());
            match = fileName.contains(m_searchText, Qt::CaseInsensitive)
                    || fileContainsText(p, m_searchText);
        }
        if (!match)
            continue;

        m_matchingFiles.insert(normalizePath(toQString(p)));

        // Register every ancestor of this file, stopping at the
        // configured root.  The root itself is inserted on the final
        // iteration (when `anc == root` before the break); the explicit
        // ancestor-acceptance rule in filterAcceptsRow() guarantees the
        // rest of the chain is visible too.
        std::filesystem::path anc = p.parent_path();
        while (!anc.empty()) {
            m_visibleDirs.insert(normalizePath(toQString(anc)));
            if (anc == root)
                break;
            const std::filesystem::path parent = anc.parent_path();
            if (parent == anc)
                break;
            anc = parent;
        }
    }
}

bool MarkdownFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const
{
    if (!sourceModel())
        return true;

    auto *fsm = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsm)
        return true;

    const QModelIndex srcIdx = fsm->index(sourceRow, 0, sourceParent);
    if (!srcIdx.isValid())
        return true;

    const QString path = normalizePath(fsm->filePath(srcIdx));
    if (path.isEmpty())
        return true;

    // Rule 4 (see the class doc): the root and every ancestor of it are
    // always accepted.  Without this, mapFromSource(root) returns an
    // invalid index — the intermediate nodes are filtered out by rules
    // 1–3 — and setRootIndex() leaves the whole tree blank.
    if (!m_rootPath.isEmpty()) {
        const QString rootSlash = ensureTrailingSlash(m_rootPath);
        const QString pathSlash = ensureTrailingSlash(path);
        if (path == m_rootPath || rootSlash.startsWith(pathSlash))
            return true;
    }

    if (fsm->isDir(srcIdx))
        return m_visibleDirs.contains(path);

    // Rule 3 (see the class doc): README.md is visible only at the root.
    // A subfolder's README is still reachable by selecting that folder
    // — the folder row survives because the README backs its presence
    // in m_matchingFiles, and MainWindow::loadFolderReadme() loads the
    // file on selection.  Case-insensitive comparison handles the rare
    // case of a root picked with a differently-cased name; two paths
    // differing only in case cannot realistically coexist under one
    // root, and a false positive would merely show one extra row.
    const QFileInfo info(path);
    if (info.fileName().compare(QStringLiteral("README.md"),
                                 Qt::CaseInsensitive) == 0) {
        const QString parentPath = normalizePath(info.absolutePath());
        if (QString::compare(parentPath, m_rootPath, Qt::CaseInsensitive) != 0)
            return false;
        // The root’s own README falls through to rule 4: it is treated
        // as a normal file and still has to satisfy m_matchingFiles.
    }

    return m_matchingFiles.contains(path);
}
