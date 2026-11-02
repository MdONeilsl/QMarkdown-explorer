#include "StyleLoader.h"

#include <QApplication>
#include <QByteArray>
#include <QString>
#include <QRegularExpression>

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

QString readTextFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return {};

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

    return QString::fromUtf8(
        raw.data() + offset,
        static_cast<qsizetype>(raw.size() - offset));
}

} // namespace

StyleLoader::StyleLoader(const std::filesystem::path &directory)
    : m_directory(directory)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(m_directory, ec))
        return;

    for (const auto &entry :
         std::filesystem::directory_iterator(m_directory, ec))
    {
        if (ec)
            break;

        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc)
            continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (ext != ".qss")
            continue;

        const QString stem = toQString(entry.path().stem());
        if (!stem.isEmpty())
            m_styles.append(stem);
    }

    std::sort(m_styles.begin(), m_styles.end(),
              [](const QString &a, const QString &b) {
                  return QString::compare(a, b, Qt::CaseInsensitive) < 0;
              });
}

const std::filesystem::path &StyleLoader::directory() const noexcept
{
    return m_directory;
}

QStringList StyleLoader::availableStyles() const
{
    return m_styles;
}

std::filesystem::path StyleLoader::pathFor(const QString &styleName) const
{
    if (styleName.isEmpty())
        return {};

    const std::filesystem::path candidate =
        m_directory / (toFsPath(styleName) += std::filesystem::path(u".qss"));

    std::error_code ec;
    if (!std::filesystem::is_regular_file(candidate, ec) || ec)
        return {};

    return candidate;
}

void StyleLoader::apply(const QString &styleName) const
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;

    if (styleName.isEmpty()) {
        app->setStyleSheet(QString());
        return;
    }

    const std::filesystem::path qssPath = pathFor(styleName);
    if (qssPath.empty()) {
        app->setStyleSheet(QString());
        return;
    }

    const QString qss = readTextFile(qssPath);
    if (qss.isEmpty()) {
        app->setStyleSheet(QString());
        return;
    }

    app->setStyleSheet(qss);
}

QColor StyleLoader::codeBlockBackground(const QString &themeName) const
{
    // Empty theme name = System.  No code-block styling: the pass in
    // MarkdownView checks isValid() and becomes a no-op, which lets
    // Qt’s importer defaults show through unchanged.
    if (themeName.isEmpty())
        return {};

    const QString fileName =
        QStringLiteral("code-%1.css").arg(themeName);
    const std::filesystem::path path = m_directory / toFsPath(fileName);

    const QString css = readTextFile(path);
    if (css.isEmpty())
        return {};

    // First `background` / `background-color` declaration inside a
    // `pre { … }` rule.  Non-greedy scan: stops at the first match, so
    // a file with multiple `pre` rules contributes only the first one.
    // Values with whitespace (e.g. `background: rgb(1, 2, 3)`) or a URL
    // fallback (`background: #fff url(…)`) capture just the leading
    // token — the intent is a solid colour, and anything more elaborate
    // simply produces an invalid QColor and disables the styling.
    static const QRegularExpression re(
        QStringLiteral(
            R"RX(\bpre\s*\{[^}]*?\bbackground(?:-color)?\s*:\s*([^;}\s]+))RX"),
        QRegularExpression::CaseInsensitiveOption);

    const auto m = re.match(css);
    if (!m.hasMatch())
        return {};

    return QColor(m.captured(1).trimmed());
}
