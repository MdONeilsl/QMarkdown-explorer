#pragma once

#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

#include <filesystem>

/**
 * @brief Visibility filter for the file tree.
 *
 * `QFileSystemModel` can only filter files by name pattern.  This proxy
 * adds the rules the tree actually needs:
 *
 *   1. **Content filter.**  A `.md` / `.markdown` file is visible only if
 *      its name or its content matches the search string
 *      (case-insensitive).  Empty search shows every Markdown file.
 *
 *   2. **Folder pruning.**  A directory is visible only if it (or one of
 *      its descendants) contains at least one visible Markdown file.
 *      Folders that would open empty are never shown.
 *
 *   3. **README exemption.**  `README.md` is visible only at the
 *      configured root; elsewhere it is hidden from the tree (the file
 *      itself is still reachable by selecting its folder — the folder
 *      row stays because the file backs its presence, and
 *      `MainWindow::loadFolderReadme()` loads it on selection).
 *
 *   4. **Root traversal.**  The configured root **and every ancestor of
 *      it** are always accepted regardless of the rules above.
 *      `QSortFilterProxyModel` requires an accepted parent chain for a
 *      descendant to be reachable; without this, a root whose parent
 *      chain contains `D:/` (filtered out by rules 1–3) would map to an
 *      invalid proxy index and the whole tree would be blank.
 *
 *   5. **Ignore file.**  A `.ignore` file at the configured root is
 *      loaded once per root change (and again on every `refresh()`).
 *      Each non-empty, non-comment line is compiled to a
 *      `QRegularExpression` and matched against three forms of every
 *      entry the walk visits:
 *        * the entry’s path relative to the root, forward slashes
 *        * the entry’s basename (what `gitignore` users expect from
 *          patterns like `^temp$` and `\.log$`)
 *        * for directories, the relative path with a trailing `/`
 *          (so `^build/` matches a `build` directory)
 *
 *      A match hides the entry — for a matching directory the walk
 *      skips its subtree entirely (`disable_recursion_pending()`),
 *      so the cost is bounded by the ignored subtree’s top level, not
 *      by its contents.  Lines starting with `#` are comments; invalid
 *      patterns are skipped with a warning.  Matching is
 *      case-sensitive; prefix a rule with `(?i)` for case-folding.
 *
 * **Cache design.**
 *
 * The filesystem walk is expensive (thousands of FindNextFile calls on
 * Windows) and its result does not change between searches — only the
 * search string does.  So the walk runs once per root change or
 * `refresh()` (`scanFilesystem()`), producing an in-memory list of
 * every markdown file that survives the base rules (extension,
 * not-hidden, not-ignored).  Each search then iterates that list
 * (`applySearchFilter()`) and rebuilds `m_matchingFiles` /
 * `m_visibleDirs` without touching the filesystem again.
 *
 * Content reads during a search use a byte-level ASCII case-insensitive
 * scan against the raw file bytes — no `QString::fromUtf8()` per file.
 * Non-ASCII needles fall back to `QString::contains()` because that is
 * the case that actually requires Unicode case folding.
 *
 * The filter hot path (`filterAcceptsRow()`) is unchanged: it reads
 * only from the two sets.
 *
 * Usage contract:
 *
 *   * Call `setSourceRootPath()` **after** `QFileSystemModel::setRootPath()`
 *     and **before** setting the view's root index, so the cache is
 *     populated before the view starts asking `filterAcceptsRow()`
 *     about its children.  This call also loads the root’s `.ignore`,
 *     if present, and scans the filesystem.
 *   * Call `refresh()` after external file changes if the search string
 *     is non-empty — the cached file list is stale, and an edited file
 *     may now (not) match.  `refresh()` also reloads `.ignore`, so this
 *     is the right call after editing ignore patterns.
 *   * `setSearchText()` reuses the cached file list.  It does not
 *     re-walk the filesystem — that is the design of the cache and the
 *     reason searches are fast after the first one.
 *   * The proxy assumes its source model is a `QFileSystemModel`; it
 *     calls `QFileSystemModel::filePath()` / `isDir()` to classify a
 *     row and will not behave sensibly with any other source type.
 */
class MarkdownFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit MarkdownFilterProxyModel(QObject *parent = nullptr);

    /**
     * @brief Binds the proxy to a source root, then rebuilds the cache
     *        and re-filters.
     *
     * Must be called after the source model’s `setRootPath()` so the
     * walk scans exactly the subtree the tree view will display.
     * Triggers a full filesystem scan, then a filter against the
     * current search string, and re-loads the root’s `.ignore` file.
     * A call with the same path as the previous one is a no-op.
     *
     * @param path  Absolute path of the root.  Normalised internally
     *              (forward slashes, no trailing separator except on a
     *              bare drive root such as `D:/`).
     */
    void setSourceRootPath(const QString &path);

    /// @return The path last passed to `setSourceRootPath()`.
    QString sourceRootPath() const { return m_rootPath; }

    /**
     * @brief Sets the search string and re-filters.
     *
     * Empty string = no content filter (every Markdown file under the
     * root is visible, still subject to the folder-pruning rule).  A
     * call with the same string as the previous one is a no-op, so
     * rapid keystrokes that resolve to the same value after trimming
     * do not cause redundant rebuilds.
     *
     * Reuses the cached file list from the last filesystem scan —
     * no re-walk.  Only file *content* is re-read (and only for files
     * whose names do not match).
     *
     * Does **not** reload `.ignore` — that file changes only when the
     * user explicitly refreshes or the root changes.  Avoids
     * recompiling every ignore regex on every keystroke.
     *
     * @note Callers should debounce user keystrokes; see
     *       `MainWindow::handleSearchTextChanged()` for the 300 ms
     *       pattern this class is designed around.
     */
    void setSearchText(const QString &text);

    /// @return The search string last passed to `setSearchText()`.
    QString searchText() const { return m_searchText; }

    /**
     * @brief Rescans the filesystem, reloads `.ignore`, re-filters.
     *
     * Call after the user edits a file on disk if a search is active,
     * so its new content is considered by the next filter pass.  Also
     * the correct call after editing the root’s `.ignore` — the cached
     * ignore rules are rebuilt here, so pattern changes take effect
     * without restarting.  And the correct call after adding or
     * removing files outside the application — the cached file list
     * must be refreshed before those changes become visible.
     */
    void refresh();

protected:
    /**
     * @brief Decides row visibility from the cache.
     *
     * Does not touch the filesystem — every decision is a `QSet` lookup
     * against data computed by `applySearchFilter()`.  This is what
     * makes the cache-based design safe to call once per row on every
     * filter pass, including the bulk re-evaluation triggered by
     * `invalidateFilter()`.
     *
     * Acceptance order, first match wins:
     *
     *   1. The configured root and every ancestor of it — always
     *      accepted (see the class-level contract).
     *   2. A directory — accepted iff it appears in `m_visibleDirs`.
     *      Ignored directories never appear here because the walk
     *      skips their entire subtree.
     *   3. `README.md` outside the root — rejected.
     *   4. A Markdown file — accepted iff it appears in
     *      `m_matchingFiles`.
     */
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &parent) const override;

    QVariant data(const QModelIndex &index, int role) const override;

private:
    /**
     * @brief A single markdown file that survived the base rules during
     *        the last `scanFilesystem()` call.
     *
     * Everything that does not depend on the search string is cached
     * here so `applySearchFilter()` can iterate in memory without
     * touching the filesystem for anything other than file content
     * (and only for files whose names do not match the query).
     */
    struct CachedFile
    {
        QString                normalizedPath;     // key for m_matchingFiles
        QString                name;               // basename, for name match
        QStringList            ancestors;          // keys for m_visibleDirs
        std::filesystem::path  fsPath;             // for content reads
        bool                   isSubfolderReadme = false;
    };

    /// Walks the root once and populates `m_cachedFiles`.
    void scanFilesystem();

    /// Filters `m_cachedFiles` against `m_searchText` and populates
    /// `m_matchingFiles` and `m_visibleDirs`.
    void applySearchFilter();

    /// Reads `<root>/.ignore`, compiles each non-comment line, and
    /// populates `m_ignoreRules`.  Silent when the file is absent.
    void loadIgnoreRules();

    /**
     * @brief Returns `true` if any `.ignore` rule matches `entry`.
     *
     * @param relPath  Entry path relative to the root, forward slashes,
     *                 no trailing slash.
     * @param basename Entry filename without any directory component.
     * @param isDir    Whether the entry is a directory; when true the
     *                 relPath is also tested with a trailing `/`, so
     *                 patterns like `^build/` match a `build` directory.
     */
    bool matchesIgnoreRule(const QString &relPath,
                           const QString &basename,
                           bool isDir) const;

    QString       m_rootPath;
    QString       m_searchText;
    QList<CachedFile>         m_cachedFiles;      // populated by scanFilesystem()
    QSet<QString> m_matchingFiles;                // absolute paths of .md files that pass
    QSet<QString> m_visibleDirs;                  // directories with >= 1 surviving descendant
    QList<QRegularExpression> m_ignoreRules;      // compiled .ignore patterns
};