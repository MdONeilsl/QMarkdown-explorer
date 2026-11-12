#include "MarkdownFilterProxyModel.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QString>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
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

bool isHiddenName(const std::filesystem::path &p)
{
    const std::u8string name = p.filename().u8string();
    return !name.empty() && name[0] == u8'.';
}

/**
 * @brief ASCII-only case folding.  Bytes outside [A-Z] are returned
 *        unchanged.
 *
 * Deliberately does not touch bytes >= 0x80: those are the lead and
 * continuation bytes of UTF-8 multi-byte sequences, and folding them
 * would corrupt the sequence rather than case-fold it.  The result is
 * that byte-level searching is correct for any UTF-8 content as long
 * as the *needle* is ASCII — which is the common case and the one the
 * byte path is designed for.  Non-ASCII needles go through
 * QString::contains() and its Unicode-aware comparison instead.
 */
inline char asciiFold(char c)
{
    const auto uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z')
        return static_cast<char>(uc + ('a' - 'A'));
    return c;
}

/**
 * @brief Returns `true` if `needle` is non-empty and contains only
 *        ASCII bytes (< 0x80).
 */
bool isAsciiOnly(std::string_view needle)
{
    for (char c : needle) {
        if (static_cast<unsigned char>(c) >= 0x80)
            return false;
    }
    return true;
}

/**
 * @brief Case-insensitive substring search over raw bytes.
 *
 * Both ranges are treated as opaque byte sequences.  Folding is
 * ASCII-only — see `asciiFold()`.  Safe on arbitrary binary input.
 * Empty needle is treated as “match” (defensive; the caller never
 * passes an empty needle).
 */
bool asciiCaseInsensitiveContains(std::string_view haystack,
                                  std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;

    const auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return asciiFold(a) == asciiFold(b); });
    return it != haystack.end();
}

/**
 * @brief Case-insensitive content match with two paths.
 *
 *  - ASCII needle: fold the raw file bytes and search them directly.
 *    No QString conversion, no per-file allocation beyond the file
 *    read itself.
 *
 *  - Non-ASCII needle: strip the BOM if present, convert the file to
 *    QString, and use `QString::contains()` for its Unicode-aware
 *    case folding.  This is the only path where non-ASCII case
 *    folding matters, and it is rare — most search text is ASCII.
 *
 * The BOM strip is defensive: a leading U+FEFF would not match ASCII
 * needle bytes anyway (they are all < 0x80), but stripping it keeps
 * the QString path identical to the previous behaviour.
 */
bool fileContainsText(const std::filesystem::path &p,
                      const QString &needle,
                      std::string_view needleBytes,
                      bool needleIsAscii)
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

    const std::string_view content(raw.data() + offset,
                                   raw.size() - offset);

    if (needleIsAscii)
        return asciiCaseInsensitiveContains(content, needleBytes);

    const QString str = QString::fromUtf8(
        content.data(), static_cast<qsizetype>(content.size()));
    return str.contains(needle, Qt::CaseInsensitive);
}

/**
 * @brief Returns `entry`’s path relative to `root`, forward-slash
 *        separated, for matching against `.ignore` patterns.
 *
 * Returns an empty string when the two paths share no ancestor, which
 * the recursive walk never produces for an entry, but the helper is
 * defensive — a bad result from `std::filesystem::relative` must not
 * produce spurious path matches.
 */
QString relativeForwardSlashed(const std::filesystem::path &root,
                               const std::filesystem::path &entry)
{
    std::error_code ec;
    const std::filesystem::path rel =
        std::filesystem::relative(entry, root, ec);
    if (ec || rel.empty())
        return {};
    const std::u8string u8 = rel.generic_u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char *>(u8.data()),
        static_cast<qsizetype>(u8.size()));
}

/**
 * @brief Returns `true` when `p` is a `README.md` file that is not
 *        located directly inside the configured root.
 *
 * Mirrors the README-exemption rule in `filterAcceptsRow()`: a README
 * is visible only at the root, and every other README is hidden from
 * the tree.  `applySearchFilter()` uses this to decide whether a
 * matching README should contribute to folder visibility — see the
 * call site for the reasoning.
 *
 * @param p                    Candidate entry path from the walk.
 * @param normalisedRootPath   The configured root, already normalised
 *                             by `setSourceRootPath()`.  Compared
 *                             case-insensitively so a path that
 *                             differs from the root only in letter
 *                             case on Windows is still treated as
 *                             “at the root”.
 */
bool isSubfolderReadme(const std::filesystem::path &p,
                       const QString &normalisedRootPath)
{
    const QString name = toQString(p.filename());
    if (name.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) != 0)
        return false;

    const QString parent = normalizePath(toQString(p.parent_path()));
    return QString::compare(parent, normalisedRootPath,
                            Qt::CaseInsensitive) != 0;
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
    loadIgnoreRules();
    scanFilesystem();
    applySearchFilter();
    invalidateFilter();
}

void MarkdownFilterProxyModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    applySearchFilter();
    invalidateFilter();
}

void MarkdownFilterProxyModel::refresh()
{
    loadIgnoreRules();
    scanFilesystem();
    applySearchFilter();
    invalidateFilter();
}

void MarkdownFilterProxyModel::scanFilesystem()
{
    m_cachedFiles.clear();

    if (m_rootPath.isEmpty())
        return;

    const std::filesystem::path root = toFsPath(m_rootPath);

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
        return;

    const bool hasIgnore = !m_ignoreRules.isEmpty();

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

        // .ignore rules.  Checked before the directory branch below so
        // a matching directory can prune its own subtree without ever
        // having its contents walked.  The same disable_recursion_pending
        // trick the hidden-file rule uses keeps the cost bounded by the
        // ignored directory’s top level.
        if (hasIgnore) {
            const bool isDir = entry.is_directory(ec) && !ec;
            ec.clear();
            const QString relPath = relativeForwardSlashed(root, p);
            const QString base    = toQString(p.filename());
            if (matchesIgnoreRule(relPath, base, isDir)) {
                if (isDir)
                    it.disable_recursion_pending();
                continue;
            }
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

        // Everything the search filter will need, precomputed once
        // here so per-keystroke filtering does no filesystem work
        // beyond reading file content for candidate matches.
        CachedFile cf;
        cf.normalizedPath     = normalizePath(toQString(p));
        cf.name               = toQString(p.filename());
        cf.fsPath             = p;
        cf.isSubfolderReadme  = isSubfolderReadme(p, m_rootPath);

        // Register every ancestor of this file, stopping at the
        // configured root.  The root itself is included on the final
        // iteration (when `anc == root` before the break); the explicit
        // ancestor-acceptance rule in filterAcceptsRow() guarantees the
        // rest of the chain is visible too.
        std::filesystem::path anc = p.parent_path();
        while (!anc.empty()) {
            cf.ancestors.append(normalizePath(toQString(anc)));
            if (anc == root)
                break;
            const std::filesystem::path parent = anc.parent_path();
            if (parent == anc)
                break;
            anc = parent;
        }

        m_cachedFiles.append(cf);
    }
}

void MarkdownFilterProxyModel::applySearchFilter()
{
    m_matchingFiles.clear();
    m_visibleDirs.clear();

    const bool hasSearch = !m_searchText.isEmpty();

    // Pre-compute the needle bytes once per search so the per-file
    // content check does not re-encode it.  ASCII detection drives
    // the choice between the byte-level and Unicode-aware paths —
    // see fileContainsText().
    const QByteArray needleUtf8 = hasSearch
                                      ? m_searchText.toUtf8()
                                      : QByteArray();
    std::string_view needleBytes;
    if (hasSearch) {
        needleBytes = std::string_view(
            needleUtf8.constData(),
            static_cast<std::size_t>(needleUtf8.size()));
    }
    const bool needleIsAscii = hasSearch && isAsciiOnly(needleBytes);

    for (const CachedFile &cf : m_cachedFiles) {
        if (hasSearch) {
            const bool nameMatches =
                cf.name.contains(m_searchText, Qt::CaseInsensitive);

            if (cf.isSubfolderReadme) {
                // Subfolder READMEs are hidden from the tree by
                // filterAcceptsRow rule 3, so a filename-only match
                // produces no visible row and its folder must not
                // appear on that basis alone.  README.md always
                // contains substrings like "read", "me", and "md",
                // so relying on the name would surface almost every
                // folder for common short queries — the noise this
                // rule exists to prevent.
                //
                // A content match is different: the user can click
                // the folder and the README loads with the matching
                // occurrences highlighted in the viewer.  That is a
                // real hit, so the folder stays visible.
                if (!fileContainsText(cf.fsPath, m_searchText,
                                      needleBytes, needleIsAscii))
                    continue;
            } else if (!nameMatches) {
                // Non-README files keep the original name-or-content
                // behaviour; only their row (not a folder-only
                // contribution) depends on this.
                if (!fileContainsText(cf.fsPath, m_searchText,
                                      needleBytes, needleIsAscii))
                    continue;
            }
        }

        m_matchingFiles.insert(cf.normalizedPath);
        for (const QString &anc : cf.ancestors)
            m_visibleDirs.insert(anc);
    }
}

void MarkdownFilterProxyModel::loadIgnoreRules()
{
    m_ignoreRules.clear();
    if (m_rootPath.isEmpty())
        return;

    const std::filesystem::path ignorePath =
        toFsPath(m_rootPath + QStringLiteral("/.ignore"));
    std::ifstream in(ignorePath, std::ios::binary);
    if (!in.is_open())
        return;

    const std::string raw((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    in.close();

    // Same BOM strip as fileContainsText(), so a UTF-8 BOM at the head
    // of the .ignore file does not corrupt the first pattern.
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

    const QStringList lines = content.split(u'\n');
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;

        QRegularExpression re(line);
        if (!re.isValid()) {
            qWarning("MarkdownFilterProxyModel: invalid .ignore pattern "
                     "'%s': %s",
                     qPrintable(line),
                     qPrintable(re.errorString()));
            continue;
        }
        m_ignoreRules.append(re);
    }
}

bool MarkdownFilterProxyModel::matchesIgnoreRule(const QString &relPath,
                                                 const QString &basename,
                                                 bool isDir) const
{
    if (m_ignoreRules.isEmpty())
        return false;

    for (const QRegularExpression &re : m_ignoreRules) {
        // Relative path from the root — lets patterns such as
        // `^build/artifacts` target one subtree specifically.
        if (!relPath.isEmpty() && re.match(relPath).hasMatch())
            return true;

        // Trailing slash variant, only for directories — mirrors the
        // `^dir/` convention from gitignore.
        if (isDir && !relPath.isEmpty()
            && re.match(relPath + u'/').hasMatch())
            return true;

        // Basename — the form most `.ignore` writers will use
        // (`^temp$`, `\.log$`, `^draft-.+\.md$`).
        if (!basename.isEmpty() && re.match(basename).hasMatch())
            return true;
    }
    return false;
}

QVariant MarkdownFilterProxyModel::data(const QModelIndex &index,
                                        int role) const
{
    QVariant value = QSortFilterProxyModel::data(index, role);

    if (role != Qt::DisplayRole)
        return value;

    auto *fsm = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsm)
        return value;

    const QModelIndex src = mapToSource(index);
    if (!src.isValid() || fsm->isDir(src))
        return value;

    // Strip only the two Markdown extensions — never a generic "anything
    // after the last dot".  "README.backup.md" must become
    // "README.backup", not "README".
    const QString name = value.toString();
    const int dotPos = name.lastIndexOf(u'.');
    if (dotPos <= 0)   // no dot, or leading dot (hidden file)
        return value;

    const QString suffix = name.mid(dotPos + 1).toLower();
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown"))
        return name.left(dotPos);

    return value;
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
