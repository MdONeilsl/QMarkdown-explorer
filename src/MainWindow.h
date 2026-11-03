#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <memory>

class QAction;
class QComboBox;
class QFileSystemModel;
class QLabel;
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
 *     ┌─────────────────────────────────────────────────────────────┐
 *     │ toolbar: [←][→] [Open Folder…] [search…] [N / M] [↑] [↓]  [Style:] │
 *     ├────────────────────┬────────────────────────────────────────┤
 *     │  QTreeView         │  MarkdownView                          │
 *     │  (filtered view)   │  (read-only QTextEdit)                 │
 *     ├────────────────────┴────────────────────────────────────────┤
 *     │ status:  (message…)                    [Qt] [GitHub]        │
 *     └─────────────────────────────────────────────────────────────┘
 *
 * Search box:
 *   * Filters the tree (name or content, debounced at 300 ms).
 *   * Also drives in-document search in the viewer as-you-type
 *     (no debounce; document search is fast).
 *   * The counter label shows "N / M" for the viewer’s current match
 *     position and total.  Empty when the search is under 3 characters
 *     or has no matches.
 *   * The up/down arrows navigate viewer matches (wrap at either end).
 *
 * Navigation history:
 *   * The two leftmost toolbar buttons replay browser-style back and
 *     forward through files opened in the current root folder.
 *   * History is in-memory only; cleared when the root folder changes
 *     and on app exit.  Not persisted to QSettings.
 *   * Alt+Left / Alt+Right are window-scoped so they fire regardless
 *     of which child widget has focus.
 *
 * README loading, theme selection, repo menu, and persistence behave as
 * before.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /**
     * @brief Controls how the file tree reacts when `openMarkdownFile()`
     *        succeeds.
     *
     * The two callers that open a file by clicking inside another
     * document or replaying navigation history need to bring the tree
     * selection in line with what is now displayed — the tree was not
     * where the click came from, so the selection would otherwise point
     * at a row that no longer corresponds to the visible document.
     *
     *   * `None` — do nothing.  Used by user-initiated tree clicks
     *     (the tree already has the right row selected) and by README
     *     auto-loads (the README may not even be visible in the tree,
     *     because of the README-exemption rule in the proxy).
     *   * `SelectNoScroll` — select the row and expand every
     *     intermediate folder, without moving the viewport.  Used by
     *     cross-document link clicks: the tree should reflect the
     *     navigation, but the user’s current scroll position in the
     *     tree panel is preserved.
     *   * `SelectAndScroll` — select, expand, and scroll the row into
     *     view.  Used by toolbar back/forward: the history replay is
     *     explicitly a jump, so bringing the target row into view is
     *     the intended behaviour.
     */
    enum class TreeReveal {
        None,
        SelectNoScroll,
        SelectAndScroll,
    };

    /**
     * @brief Points the model, the filter proxy, and the tree at
     *        `folderPath`, then loads the root’s README if present.
     *
     * The ordering inside this function (setRootPath → setSourceRootPath
     * → mapFromSource → setRootIndex) is what the filter proxy’s usage
     * contract requires; see `MarkdownFilterProxyModel`.
     *
     * Public so tests can drive root changes directly; production code
     * only calls it from `openFolderDialog()` and the deferred launch
     * handler.
     */
    void setRootFolder(const QString &folderPath);

    /**
     * @brief Loads `filePath` into the right-hand view.
     *
     * Ignores the call when `filePath` is already the currently displayed
     * file — that is what makes the double-click’s duplicate signals
     * harmless without any additional state on the tree side.
     *
     * @param filePath         Absolute path to the file to load.
     * @param recordInHistory  When true (the default), the successful
     *                         load is appended to the navigation history.
     *                         Back/forward replay passes false so the
     *                         stack is not re-recorded.
     * @param treeReveal       Controls whether and how the file’s row
     *                         is selected and revealed in the tree.
     *                         See `TreeReveal`.
     *
     * @return `true` if the file was loaded and displayed.
     *
     * @note The suffix check accepts only `.md` / `.markdown`.  Callers
     *       that pass arbitrary paths (such as `loadFolderReadme()`)
     *       rely on this to silently ignore non-Markdown files.
     */
    bool openMarkdownFile(const QString &filePath,
                          bool recordInHistory = true,
                          TreeReveal treeReveal = TreeReveal::None);

    // Introspection helpers for tests; stable, side-effect-free.
    int     historySize()  const { return m_history.size(); }
    int     historyIndex() const { return m_historyIndex; }
    QString currentFile()  const { return m_currentFile; }

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
    void handleSearchStateChanged(int current, int total);

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

    /// Replays the previous entry in the navigation history.
    void goBack();

    /// Replays the next entry in the navigation history.
    void goForward();

private:
    void buildCentralWidget();
    void buildToolBar();
    void buildStatusBar();
    void wireModelAndView();
    void populateStyleCombo();

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

    /// Appends `filePath` to the history, truncating the forward stack
    /// first.  Called only from `openMarkdownFile` when `recordInHistory`
    /// is true; back/forward replay must not reach this function.
    void recordHistory(const QString &filePath);

    /// Enables/disables the back and forward toolbar actions based on
    /// the current history cursor position.
    void updateHistoryActions();

    void saveLastFolder(const QString &folder) const;
    QString lastFolder() const;
    void saveLastStyle(const QString &styleName) const;
    QString lastStyle() const;

    /**
     * @brief Selects `filePath`’s row in the tree, expanding every
     *        intermediate folder so the row is reachable.
     *
     * Silent no-op when the file is not reachable through the proxy
     * (hidden by the README exemption, excluded by an active search,
     * or matched by a `.ignore` rule) — there is no row to select.
     *
     * @param scrollToItem  When true, scroll the row into view
     *                      (QAbstractItemView::EnsureVisible).  When
     *                      false, save and restore the view’s prior
     *                      scroll position around the call, so the
     *                      selection changes without the panel jumping
     *                      as folders expand.
     */
    void revealFileInTree(const QString &filePath, bool scrollToItem);

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
    QLabel                   *m_searchCounter = nullptr;
    QToolButton              *m_searchPrev    = nullptr;
    QToolButton              *m_searchNext    = nullptr;

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

    // Navigation history: browser semantics, in-memory only, cleared on
    // root change and on destruction.  `m_historyIndex` is the cursor;
    // `-1` means the stack is empty.
    QStringList m_history;
    int         m_historyIndex = -1;
    QAction    *m_backAction   = nullptr;
    QAction    *m_forwardAction = nullptr;
};
