#include "RepoList.h"

#include "config.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

void trimAscii(std::string &s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    const auto last = s.find_last_not_of(" \t");
    s = s.substr(first, last - first + 1);
}

bool isHttpUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

} // namespace

RepoList::RepoList(const std::filesystem::path &filePath)
    : m_filePath(filePath)
{
}

QUrl RepoList::applicationRepoUrl()
{
    // The value comes from CMake’s project(HOMEPAGE_URL) via config.h.
    // A fork edits CMakeLists.txt, not this function — the empty-string
    // case below exists so a build without HOMEPAGE_URL still produces a
    // well-defined result instead of a QUrl("") that would pass an
    // is-valid check on some Qt versions.
    const QString url = QStringLiteral(APP_HOMEPAGE_URL).trimmed();
    if (url.isEmpty())
        return QUrl();
    return QUrl(url);
}

const std::filesystem::path &RepoList::filePath() const noexcept
{
    return m_filePath;
}

const QList<QUrl> &RepoList::userRepos() const noexcept
{
    return m_repos;
}

RepoList::AddResult RepoList::add(const QUrl &url)
{
    if (!isHttpUrl(url))
        return AddResult::InvalidUrl;

    // Compare using QUrl’s own equality so scheme/host casing does not
    // produce false negatives.  Two URLs that differ only in trailing
    // slash are treated as distinct on purpose: GitHub accepts both, and
    // a user who bookmarked both forms has a reason.
    if (url == applicationRepoUrl())
        return AddResult::IsApplicationRepo;

    for (const QUrl &existing : m_repos) {
        if (existing == url)
            return AddResult::Duplicate;
    }

    m_repos.append(url);
    return AddResult::Ok;
}

bool RepoList::remove(int index)
{
    if (index < 0 || index >= m_repos.size())
        return false;
    m_repos.removeAt(index);
    return true;
}

bool RepoList::reload()
{
    m_repos.clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(m_filePath, ec))
        return true;

    std::ifstream in(m_filePath, std::ios::binary);
    if (!in.is_open())
        return false;

    std::string line;
    while (std::getline(in, line)) {
        // Binary mode means Windows files carry '\r'; trim before the
        // general whitespace trim below, so the line comparison is not
        // dependent on the editor that last touched the file.
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        trimAscii(line);

        if (line.empty() || line[0] == '#')
            continue;

        const QString qline = QString::fromUtf8(
            line.data(), static_cast<qsizetype>(line.size()));

        // Lenient on read, strict on add(): a hand-edited file may
        // contain the "github.com/foo/bar" shorthand, whereas the Add
        // dialog always produces a scheme-qualified URL.  Splitting the
        // policy this way is deliberate — the alternatives are either
        // rejecting legitimate hand edits or accepting bare words from
        // the dialog.
        const QUrl url = QUrl::fromUserInput(qline);
        if (isHttpUrl(url))
            m_repos.append(url);
    }

    return true;
}

bool RepoList::save() const
{
    std::ofstream out(m_filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    for (const QUrl &url : m_repos) {
        const QByteArray line = url.toString().toUtf8();
        out.write(line.constData(), line.size());
        out.put('\n');
    }

    return out.good();
}
