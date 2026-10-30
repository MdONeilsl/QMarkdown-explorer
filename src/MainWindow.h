#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QComboBox;
class QFileSystemModel;
class QLineEdit;
class QMenu;
class QModelIndex;
class QTimer;
class QToolButton;
class QTreeView;

class MarkdownFilterProxyModel;
class MarkdownView;
class RepoList;

/**
 * @brief The top-level window of QMarkdown-explorer.
 *
 * Layout:
 *
 *     ┌───────────────────────────────────────────────────────┐
 *     │ toolbar:  [Open Folder…] [search…]  ────  [Style: ▾]  │
 *     ├────────────────────┬──────────────────────────────────┤
 *     │  QTreeView         │  MarkdownView                    │
 *     │  (filtered view)   │  (read-only QTextEdit)           │
 *     ├────────────────────┴──────────────────────────────────┤
 *     │ status:  (message…)                    [Qt] [GitHub]  │
 *     └───────────────────────────────────────────────────────┘
 *
 * Behaviour summary:
 *
 *   * **Tree.**  Shows `.md` / `.markdown` files and the folders that
 *     contain them, filtered by name and content.  The rules themselves
 *     live in `MarkdownFilterProxyModel`; this class only drives it.
 *
 *   * **Search.**  The single-line search box filters on file name and
 *     file content (case-insensitive).  Keystrokes are debounced (300 ms)
 *     so the on-disk scan runs once per pause rather than once per
 *     character.  Clearing the box bypasses the debounce and applies
 *     immediately.
 *
 *   * **README loading.**  Selecting a folder loads that folder’s
 *     `README.md` if it has one.  The same rule applies to the root at
 *     startup and when the root is changed.  `README.md` files are
 *     hidden from the tree everywhere except at the root.
 *
 *   * **Status bar.**  Two right-aligned buttons.  The Qt button opens
 *     the standard About Qt dialog (LGPL v3 requires it to be reachable).
 *     The GitHub button opens an upward menu with the application’s own
 *     repository, the user’s saved repositories, and an entry to add a
 *     new one.
 *
 *   * **Persistence.**  The last folder and the last style are restored
 *     through `QSettings`.  The repository list is persisted in
 *     `<exe-dir>/repos.txt` (see `RepoList`).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    /// Toolbar action: re-pick the root folder at any time.
    void openFolderDialog();

    /**
     * @brief Handles `QTreeView::activated` (double-click and Enter).
     *
     * A directory row loads that folder’s README; a file row loads the
     * file.  The README path is documented under `loadFolderReadme()`.
     */
    void handleTreeActivated(const QModelIndex &index);

    /**
     * @brief Handles `QTreeView::clicked` (single click).
     *
     * Mirrors `handleTreeActivated()` so both interaction idioms reach
     * the same behaviour.  The duplicate signal a double-click produces
     * (`clicked` then `activated` for the same file) is swallowed by
     * `openMarkdownFile()`’s current-file guard.
     */
    void handleTreeClicked(const QModelIndex &index);

    /// Style combo: applies the selected `.qss` and persists the choice.
    void handleStyleChanged(int index);

    /**
     * @brief Search box: restarts the debounce timer on every keystroke.
     *
     * Clearing the box takes a fast path — it applies the empty filter
     * immediately instead of waiting for the debounce, so the user sees
     * their files reappear the moment they clear the search.
     */
    void handleSearchTextChanged(const QString &text);

    /// Qt status-bar button: opens the standard About Qt dialog.
    void showQtAbout();

    /**
     * @brief GitHub status-bar button: opens the repository menu.
     *
     * Rebuilds the menu contents first (so external edits to `repos.txt`
     * are reflected), then positions it so the menu opens *upward* from
     * the button — the status bar sits at the bottom of the window, and
     * a downward menu would fall off-screen.
     */
    void showRepoMenu();

    /**
     * @brief Prompts for a new repository URL and adds it to the list.
     *
     * Handles every `RepoList::AddResult` with a translated status or
     * warning message; a successful add triggers `RepoList::save()`,
     * whose failure is surfaced as a modal warning rather than silenced.
     */
    void addRepository();

private:
    void buildCentralWidget();
    void buildToolBar();
    void buildStatusBar();
    void wireModelAndView();
    void populateStyleCombo();

    /**
     * @brief Points the model, the filter proxy, and the tree at
     *        `folderPath`, then loads the root’s README if present.
     *
     * The ordering inside this function (setRootPath → setSourceRootPath
     * → mapFromSource → setRootIndex) is what the filter proxy’s usage
     * contract requires; see `MarkdownFilterProxyModel`.
     */
    void setRootFolder(const QString &folderPath);

    /**
     * @brief Loads `filePath` into the right-hand view.
     *
     * Ignores the call when `filePath` is already the currently displayed
     * file — that is what makes the double-click’s duplicate signals
     * harmless without any additional state on the tree side.
     *
     * @note The suffix check accepts only `.md` / `.markdown`.  Callers
     *       that pass arbitrary paths (such as `loadFolderReadme()`)
     *       rely on this to silently ignore non-Markdown files.
     */
    void openMarkdownFile(const QString &filePath);

    /**
     * @brief Loads `<folderPath>/README.md` into the view if it exists.
     *
     * Case-sensitivity of the existence check follows the host
     * filesystem — `README.md` works in any casing on Windows/macOS,
     * must be exact on Linux.  That matches the user’s expectation on
     * each platform and is not overridden here.
     *
     * @return `true` if a README was found and displayed.  A folder
     *         without a README leaves the previous document in place —
     *         clearing the view would feel like losing work for a folder
     *         that has nothing to show.
     */
    bool loadFolderReadme(const QString &folderPath);

    /// Pushes the trimmed search text into the filter proxy.
    void applySearchText();

    /// Reconstructs the GitHub menu from the current repo list.
    void rebuildRepoMenu();

    void saveLastFolder(const QString &folder) const;
    QString lastFolder() const;
    void saveLastStyle(const QString &styleName) const;
    QString lastStyle() const;

    // Qt-parented child widgets and models.  All owned by the
    // QMainWindow or by their own parent objects; none require manual
    // lifetime management.
    QFileSystemModel         *m_fileModel      = nullptr;
    MarkdownFilterProxyModel *m_filterProxy    = nullptr;
    QTreeView                *m_treeView       = nullptr;
    MarkdownView             *m_markdownView   = nullptr;
    QComboBox                *m_styleCombo     = nullptr;
    QLineEdit                *m_searchEdit     = nullptr;
    QTimer                   *m_searchDebounce = nullptr;
    QToolButton              *m_qtButton       = nullptr;
    QToolButton              *m_githubButton   = nullptr;
    QMenu                    *m_repoMenu       = nullptr;

    /// Owned list of user repositories.  Not a `QObject`, so it cannot
    /// rely on Qt parent-child ownership.
    std::unique_ptr<RepoList> m_repoList;

    /// Absolute path of the file currently displayed, or empty.  Used to
    /// swallow the duplicate `clicked` + `activated` pair on double-click.
    QString m_currentFile;

    /// Absolute path of the current root.  Empty until the initial
    /// folder picker completes; `openFolderDialog()` uses it as the
    /// start directory so a re-open lands where the user last was.
    QString m_rootFolder;
};
