#pragma once

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

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
 * Both content matching and folder pruning depend on a cache built once
 * per search / root change, so the filter hot path is a set lookup
 * rather than a filesystem walk — see `filterAcceptsRow()` below.
 *
 * Usage contract:
 *
 *   * Call `setSourceRootPath()` **after** `QFileSystemModel::setRootPath()`
 *     and **before** setting the view's root index, so the cache is
 *     populated before the view starts asking `filterAcceptsRow()`
 *     about its children.
 *   * Call `refresh()` after external file changes if the search string
 *     is non-empty — the cache stores which files matched, and an edited
 *     file may now (not) match.  With an empty search string the cache
 *     is content-independent and does not need refreshing.
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
     * @brief Binds the proxy to a source root and rebuilds the cache.
     *
     * Must be called after the source model’s `setRootPath()` so the
     * walk scans exactly the subtree the tree view will display.
     * Triggers a full cache rebuild and re-filter; a call with the same
     * path as the previous one is a no-op.
     *
     * @param path  Absolute path of the root.  Normalised internally
     *              (forward slashes, no trailing separator except on a
     *              bare drive root such as `D:/`).
     */
    void setSourceRootPath(const QString &path);

    /// @return The path last passed to `setSourceRootPath()`.
    QString sourceRootPath() const { return m_rootPath; }

    /**
     * @brief Sets the search string and rebuilds the cache.
     *
     * Empty string = no content filter (every Markdown file under the
     * root is visible, still subject to the folder-pruning rule).  A
     * call with the same string as the previous one is a no-op, so
     * rapid keystrokes that resolve to the same value after trimming
     * do not cause redundant rebuilds.
     *
     * @note Rebuilding the cache means reading every `.md` file’s
     *       content once.  Callers should debounce user keystrokes; see
     *       `MainWindow::handleSearchTextChanged()` for the 300 ms
     *       pattern this class is designed around.
     */
    void setSearchText(const QString &text);

    /// @return The search string last passed to `setSearchText()`.
    QString searchText() const { return m_searchText; }

    /**
     * @brief Rebuilds the cache without changing root or search text.
     *
     * Call after the user edits a file on disk if a search is active,
     * so its new content is considered by the next filter pass.  Also
     * useful after re-opening the same root to pick up newly added or
     * deleted files.
     */
    void refresh();

protected:
    /**
     * @brief Decides row visibility from the cache.
     *
     * Does not touch the filesystem — every decision is a `QSet` lookup
     * against data computed by `rebuildCache()`.  This is what makes
     * the cache-based design safe to call once per row on every filter
     * pass, including the bulk re-evaluation triggered by
     * `invalidateFilter()`.
     *
     * Acceptance order, first match wins:
     *
     *   1. The configured root and every ancestor of it — always
     *      accepted (see the class-level contract).
     *   2. A directory — accepted iff it appears in `m_visibleDirs`
     *      (populated by `rebuildCache()` from each surviving file’s
     *      ancestor chain).
     *   3. `README.md` outside the root — rejected.
     *   4. A Markdown file — accepted iff it appears in
     *      `m_matchingFiles`.
     */
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &parent) const override;

    QVariant data(const QModelIndex &index, int role) const override;

private:
    void rebuildCache();

    QString       m_rootPath;
    QString       m_searchText;
    QSet<QString> m_matchingFiles;   // absolute paths of .md files that pass
    QSet<QString> m_visibleDirs;     // directories with >= 1 surviving descendant
};
