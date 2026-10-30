#include "StyleLoader.h"

#include <QApplication>
#include <QByteArray>
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

QString StyleLoader::documentCss(const QString &styleName) const
{
    if (m_directory.empty())
        return {};

    // Helper: try to read one file from the styles directory, returning
    // empty when the file does not exist or cannot be opened.
    auto tryLoad = [this](const QString &filename) -> QString {
        const std::filesystem::path p = m_directory / toFsPath(filename);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(p, ec) || ec)
            return {};
        return readTextFile(p);
    };

    // Prefer the theme-specific file, then the default.
    if (!styleName.isEmpty()) {
        const QString primary =
            QStringLiteral("code-%1.css").arg(styleName);
        const QString content = tryLoad(primary);
        if (!content.isEmpty())
            return content;
    }

    return tryLoad(QStringLiteral("code-default.css"));
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