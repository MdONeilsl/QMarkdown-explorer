#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <filesystem>

/**
 * @brief Persistent list of user-managed GitHub repository URLs.
 *
 * Storage is a UTF-8 text file, one URL per line, conventionally
 * `<exe-dir>/repos.txt`.  Blank lines and lines beginning with `#` are
 * ignored on read; the writer emits one URL per line with `\n` endings
 * and no comments, so the file stays diff-friendly.
 *
 * The application’s own repository (from `applicationRepoUrl()`) is *not*
 * part of the user-managed list.  It is always presented by the caller at
 * the top of the GitHub dropdown and cannot be added or removed through
 * this class; an attempt to `add()` it is reported as
 * `AddResult::IsApplicationRepo` so the caller can produce a specific
 * message.
 *
 * All file I/O goes through `std::filesystem` / `<fstream>`; nothing here
 * depends on `QFile` or platform-specific APIs.  The class is not a
 * `QObject` and every failure is reported through a return value — the
 * caller translates `AddResult` into user-visible text, keeping every
 * `tr()` call in one place.
 */
class RepoList
{
public:
    /**
     * @brief Outcome of `add()`.
     */
    enum class AddResult
    {
        Ok,                 ///< Appended; caller is responsible for `save()`.
        InvalidUrl,         ///< Empty, malformed, or not http/https.
        IsApplicationRepo,  ///< Equals `applicationRepoUrl()`.
        Duplicate,          ///< Already present in the user-managed list.
    };

    /**
     * @brief Binds the list to a storage file.
     *
     * @param filePath  Location of the backing file.  The parent directory
     *                  must already exist; `save()` does not create it.
     *
     * The constructor does not read the file — call `reload()` explicitly
     * so the caller controls when I/O happens.
     */
    explicit RepoList(const std::filesystem::path &filePath);

    /**
     * @brief The application’s own repository URL.
     *
     * Sourced from the CMake-generated `APP_HOMEPAGE_URL` (see
     * `config.h.in`); the CMake-side value is what a fork changes, never
     * this function.
     *
     * @return A valid `QUrl` when the build set a homepage, otherwise an
     *         invalid `QUrl` (callers should skip the top menu entry in
     *         that case).
     */
    static QUrl applicationRepoUrl();

    /// @return The path passed to the constructor.
    const std::filesystem::path &filePath() const noexcept;

    /// @return The user-managed repositories, in insertion order.
    const QList<QUrl> &userRepos() const noexcept;

    /**
     * @brief Validates and appends `url`.
     *
     * Does not touch the filesystem on success — the caller persists the
     * change with `save()`.  Keeping the write out of this function means
     * a batch of additions produces a single write, and an `add()` that
     * succeeded in memory does not silently leave a half-updated file if
     * the save later fails.
     *
     * @param url  Candidate URL; compared against the application repo
     *             and existing entries using `QUrl` equality (scheme and
     *             host compared case-insensitively).
     *
     * @return `Ok` on success; otherwise the specific rejection reason.
     */
    AddResult add(const QUrl &url);

    /**
     * @brief Removes the entry at `index`.
     *
     * @return `true` on success, `false` if `index` is out of range.
     */
    bool remove(int index);

    /**
     * @brief Replaces the in-memory list with the file’s current content.
     *
     * A missing file is a valid empty state, not an error — a fresh
     * install has no `repos.txt`.  A single malformed line is skipped
     * without aborting the read, because the file is user-editable and
     * one typo must not lose the rest of the list.
     *
     * @return `true` on success (including “file absent”); `false` only
     *         when the file exists but cannot be opened for reading.
     */
    bool reload();

    /**
     * @brief Writes the current list to disk, truncating previous content.
     *
     * The list conventionally lives next to the executable, so `save()`
     * can legitimately fail on a system-wide install (Program Files on
     * Windows) where the user lacks write access.  The caller is
     * expected to surface that failure rather than ignore the return
     * value.
     *
     * @return `true` on success, `false` if the file could not be written.
     */
    bool save() const;

private:
    std::filesystem::path m_filePath;
    QList<QUrl>           m_repos;
};
