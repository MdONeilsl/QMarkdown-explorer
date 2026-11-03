#include "MainWindow.h"

#include "AppIcon.h"
#include "MarkdownFilterProxyModel.h"
#include "MarkdownView.h"
#include "RepoList.h"
#include "StatusBarIcons.h"
#include "StyleLoader.h"
#include "config.h"

#include <QAction>
#include <QAbstractItemView>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QWidget>

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

StyleLoader &styleLoader()
{
    static StyleLoader instance(
        toFsPath(QCoreApplication::applicationDirPath()
                 + QStringLiteral("/styles")));
    return instance;
}

} // namespace

// -----------------------------------------------------------------------------
//  Construction / destruction
// -----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral(APP_NAME));
    resize(1200, 800);

    // Belt-and-braces on top of main.cpp’s QApplication::setWindowIcon().
    // A few Windows configurations do not propagate the application icon
    // to secondary top-level windows; setting it here makes the icon
    // deterministic across platforms.
    const AppIcon::LoadResult icon = AppIcon::load();
    if (!icon.icon.isNull())
        setWindowIcon(icon.icon);

    m_repoList = std::make_unique<RepoList>(
        toFsPath(QCoreApplication::applicationDirPath()
                 + QStringLiteral("/repos.txt")));
    m_repoList->reload();

    buildCentralWidget();
    buildToolBar();
    buildStatusBar();
    wireModelAndView();
    populateStyleCombo();

    // Apply the saved style *before* connecting the combo’s signal.
    // Otherwise the initial setCurrentIndex() inside populateStyleCombo()
    // would emit currentIndexChanged and apply the style twice.
    handleStyleChanged(m_styleCombo->currentIndex());
    connect(m_styleCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::handleStyleChanged);

    // Deferred folder picker: run after show() so the dialog is parented
    // to an already-visible window and the event loop is live.  A
    // synchronous call here would show a modal dialog before the main
    // window is on screen.
    QTimer::singleShot(0, this, [this]() {
        QSettings launchSettings;

        // --folder <path>: load the folder directly and skip the picker.
        const QString launchFolder =
            launchSettings.value(QStringLiteral("LaunchFolder")).toString();
        if (!launchFolder.isEmpty()) {
            launchSettings.remove(QStringLiteral("LaunchFolder"));
            launchSettings.remove(QStringLiteral("SkipInitialPicker"));
            launchSettings.sync();
            if (QFileInfo(launchFolder).isDir()) {
                setRootFolder(launchFolder);
                return;
            }
        }

        // --no-picker: do not show the picker and do not auto-load.
        if (launchSettings.value(QStringLiteral("SkipInitialPicker"), false)
                .toBool()) {
            launchSettings.remove(QStringLiteral("SkipInitialPicker"));
            launchSettings.sync();
            return;
        }

        // Test mode: never show a modal picker.  A QFileDialog blocks
        // the event loop indefinitely because no user is present to
        // dismiss it — the test suite would freeze.  QStandardPaths’
        // test-mode flag is set by the test binaries via
        // setTestModeEnabled(true); production builds never enable it,
        // so real launches still see the picker.
        if (QStandardPaths::isTestModeEnabled())
            return;

        const QString remembered = lastFolder();
        const QString startDir =
            (!remembered.isEmpty() && QFileInfo(remembered).isDir())
                ? remembered
                : QString();

        const QString folder = QFileDialog::getExistingDirectory(
            this,
            tr("Select Root Folder"),
            startDir,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

        if (!folder.isEmpty())
            setRootFolder(folder);
        else if (!startDir.isEmpty())
            setRootFolder(startDir);
    });
}

MainWindow::~MainWindow() = default;

// -----------------------------------------------------------------------------
//  One-time construction helpers
// -----------------------------------------------------------------------------

void MainWindow::buildCentralWidget()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_treeView = new QTreeView;
    m_treeView->setObjectName(QStringLiteral("fileTree"));
    m_treeView->setHeaderHidden(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAnimated(false);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);

    m_markdownView = new MarkdownView;

    splitter->addWidget(m_treeView);
    splitter->addWidget(m_markdownView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({320, 880});

    setCentralWidget(splitter);
}

void MainWindow::buildToolBar()
{
    auto *toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    auto *openAction = toolbar->addAction(tr("Open Folder…"));
    openAction->setObjectName(QStringLiteral("openFolderAction"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setToolTip(tr("Choose the root folder to browse"));
    connect(openAction, &QAction::triggered,
            this, &MainWindow::openFolderDialog);

    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    m_searchEdit->setPlaceholderText(
        tr("Search in file names and contents…"));
    m_searchEdit->setToolTip(
        tr("Filter files by name or by content (case-insensitive)"));
    m_searchEdit->setClearButtonEnabled(true);
    toolbar->addWidget(m_searchEdit);

    // Counter label — shows “N / M” for the viewer’s current match.
    // Fixed minimum width prevents the toolbar from jittering as the
    // number grows from “0 / 9” to “10 / 99”.
    m_searchCounter = new QLabel(toolbar);
    m_searchCounter->setObjectName(QStringLiteral("searchCounter"));
    m_searchCounter->setMinimumWidth(60);
    m_searchCounter->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(m_searchCounter);

    m_searchPrev = new QToolButton(toolbar);
    m_searchPrev->setObjectName(QStringLiteral("searchPrev"));
    m_searchPrev->setAutoRaise(true);
    m_searchPrev->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_searchPrev->setToolTip(tr("Previous match"));
    m_searchPrev->setEnabled(false);
    toolbar->addWidget(m_searchPrev);

    m_searchNext = new QToolButton(toolbar);
    m_searchNext->setObjectName(QStringLiteral("searchNext"));
    m_searchNext->setAutoRaise(true);
    m_searchNext->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_searchNext->setToolTip(tr("Next match"));
    m_searchNext->setEnabled(false);
    toolbar->addWidget(m_searchNext);

    connect(m_searchPrev, &QToolButton::clicked, this, [this]() {
        if (m_markdownView)
            m_markdownView->previousMatch();
    });
    connect(m_searchNext, &QToolButton::clicked, this, [this]() {
        if (m_markdownView)
            m_markdownView->nextMatch();
    });
    connect(m_markdownView, &MarkdownView::searchStateChanged,
            this, &MainWindow::handleSearchStateChanged);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(300);
    connect(m_searchDebounce, &QTimer::timeout,
            this, &MainWindow::applySearchText);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MainWindow::handleSearchTextChanged);

    // Expanding spacer pushes the style selector to the right edge of
    // the toolbar.  Qt’s toolbar layout stretches the only expanding
    // item, so the cluster after it stays flush against the right
    // regardless of window width.
    auto *spacer = new QWidget(toolbar);
    spacer->setObjectName(QStringLiteral("toolbarSpacer"));
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    auto *styleLabel = new QLabel(tr("Style:"), toolbar);
    styleLabel->setObjectName(QStringLiteral("styleLabel"));
    toolbar->addWidget(styleLabel);

    m_styleCombo = new QComboBox(toolbar);
    m_styleCombo->setObjectName(QStringLiteral("styleCombo"));
    m_styleCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    toolbar->addWidget(m_styleCombo);
}

void MainWindow::buildStatusBar()
{
    QStatusBar *sb = statusBar();
    sb->setObjectName(QStringLiteral("appStatusBar"));
    sb->setSizeGripEnabled(true);

    m_qtButton = new QToolButton(sb);
    m_qtButton->setObjectName(QStringLiteral("qtButton"));
    m_qtButton->setAutoRaise(true);
    m_qtButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_qtButton->setIconSize(QSize(20, 20));
    m_qtButton->setToolTip(tr("About Qt"));
    m_qtButton->setIcon(StatusBarIcons::qtLogoIcon());
    connect(m_qtButton, &QToolButton::clicked,
            this, &MainWindow::showQtAbout);
    sb->addPermanentWidget(m_qtButton);

    m_githubButton = new QToolButton(sb);
    m_githubButton->setObjectName(QStringLiteral("githubButton"));
    m_githubButton->setAutoRaise(true);
    m_githubButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_githubButton->setIconSize(QSize(20, 20));
    m_githubButton->setToolTip(tr("Repositories"));

    QIcon githubIcon = StatusBarIcons::githubIcon();
    if (githubIcon.isNull()) {
        // Missing resource — a build error (rcc would have failed), so
        // this is defensive only.  Substituting a generic Qt icon is
        // preferable to hiding the button and losing the entire feature.
        githubIcon = style()->standardIcon(QStyle::SP_DirIcon);
    }
    m_githubButton->setIcon(githubIcon);
    connect(m_githubButton, &QToolButton::clicked,
            this, &MainWindow::showRepoMenu);
    sb->addPermanentWidget(m_githubButton);

    // Parented to the button so it inherits the correct style; the menu
    // is reconstructed on every open (see rebuildRepoMenu()).
    m_repoMenu = new QMenu(m_githubButton);
    m_repoMenu->setObjectName(QStringLiteral("repoMenu"));
}

void MainWindow::wireModelAndView()
{
    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setReadOnly(true);
    // AllDirs keeps directories visible to the proxy, which then hides
    // empty ones itself; the name filter restricts files.
    m_fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fileModel->setNameFilters({
        QStringLiteral("*.md"),
        QStringLiteral("*.markdown"),
    });
    m_fileModel->setNameFilterDisables(false);

    m_filterProxy = new MarkdownFilterProxyModel(this);
    m_filterProxy->setSourceModel(m_fileModel);
    m_filterProxy->setDynamicSortFilter(true);

    m_treeView->setModel(m_filterProxy);

    // Only the name column is meaningful in this view.
    for (int col = 1; col < m_fileModel->columnCount(); ++col)
        m_treeView->setColumnHidden(col, true);

    connect(m_treeView, &QTreeView::activated,
            this, &MainWindow::handleTreeActivated);
    connect(m_treeView, &QTreeView::clicked,
            this, &MainWindow::handleTreeClicked);

    connect(m_markdownView, &MarkdownView::linkToLocalFileRequested,
        this, [this](const QString &absolutePath) {
            // The signal already validates that the target is a
            // file inside the root.  openMarkdownFile() re-checks
            // the suffix and refuses anything that is not .md /
            // .markdown, which is the correct policy for a
            // click-navigated link.
            openMarkdownFile(absolutePath);
        });
}

void MainWindow::populateStyleCombo()
{
    m_styleCombo->clear();

    // Three fixed entries.  "System" clears the stylesheet, which is
    // what Qt's default looks like with no QSS applied.  "Dark" and
    // "Light" resolve to dark.qss / light.qss in the styles directory
    // via StyleLoader::apply().
    m_styleCombo->addItem(tr("System"), QString());
    m_styleCombo->addItem(tr("Dark"),   QStringLiteral("dark"));
    m_styleCombo->addItem(tr("Light"),  QStringLiteral("light"));

    // Restore the saved selection.  If QSettings still holds the old
    // "default" value (or any name no longer in the list), findData()
    // returns -1 and we fall back to System.
    const QString saved = lastStyle();
    int idx = 0;
    if (!saved.isEmpty()) {
        const int found = m_styleCombo->findData(saved);
        if (found >= 0)
            idx = found;
    }
    m_styleCombo->setCurrentIndex(idx);
}

// -----------------------------------------------------------------------------
//  Runtime state transitions
// -----------------------------------------------------------------------------

void MainWindow::openFolderDialog()
{
    const QString startDir =
        m_rootFolder.isEmpty() ? lastFolder() : m_rootFolder;

    const QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select Root Folder"),
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!folder.isEmpty())
        setRootFolder(folder);
}

void MainWindow::setRootFolder(const QString &folderPath)
{
    if (folderPath.isEmpty() || !m_fileModel || !m_filterProxy || !m_treeView)
        return;

    // Order matters — see MarkdownFilterProxyModel’s usage contract.
    // The source model first, then the proxy’s cache (before the view
    // starts asking filterAcceptsRow about the new children), then the
    // mapped root index.
    const QModelIndex srcRoot = m_fileModel->setRootPath(folderPath);
    m_filterProxy->setSourceRootPath(folderPath);
    const QModelIndex proxyRoot = m_filterProxy->mapFromSource(srcRoot);
    m_treeView->setRootIndex(proxyRoot);

    m_rootFolder = folderPath;
     if (m_markdownView)
        m_markdownView->setRootFolder(folderPath);
    m_currentFile.clear();
    saveLastFolder(folderPath);

    // Report the folder first.  If a README is loaded immediately below,
    // its “Opened: README.md” message overwrites this one — QStatusBar
    // shows only the most recent call, which is the more specific result.
    statusBar()->showMessage(
        tr("Opened folder: %1").arg(folderPath), 5000);

    loadFolderReadme(folderPath);
}

bool MainWindow::loadFolderReadme(const QString &folderPath)
{
    if (folderPath.isEmpty())
        return false;

    const QString readmePath = folderPath + QStringLiteral("/README.md");
    const QFileInfo info(readmePath);
    if (!info.isFile())
        return false;

    openMarkdownFile(readmePath);

    // openMarkdownFile() sets m_currentFile on success and leaves it
    // untouched on failure, so this comparison is a reliable
    // “did-it-display?” signal that does not require extra state on
    // openMarkdownFile().
    return m_currentFile == readmePath;
}

void MainWindow::handleTreeActivated(const QModelIndex &index)
{
    if (!index.isValid() || !m_filterProxy || !m_fileModel)
        return;

    const QModelIndex src = m_filterProxy->mapToSource(index);
    if (!src.isValid())
        return;

    const QString path = m_fileModel->filePath(src);

    if (m_fileModel->isDir(src)) {
        loadFolderReadme(path);
        return;
    }

    openMarkdownFile(path);
}

void MainWindow::handleTreeClicked(const QModelIndex &index)
{
    if (!index.isValid() || !m_filterProxy || !m_fileModel)
        return;

    const QModelIndex src = m_filterProxy->mapToSource(index);
    if (!src.isValid())
        return;

    const QString path = m_fileModel->filePath(src);

    if (m_fileModel->isDir(src)) {
        loadFolderReadme(path);
        return;
    }

    openMarkdownFile(path);
}

void MainWindow::openMarkdownFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    // Guard against the duplicate signal a double-click produces:
    // `clicked` and `activated` both fire for the same file.
    if (filePath == m_currentFile)
        return;

    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().toLower();
    if (suffix != QStringLiteral("md") && suffix != QStringLiteral("markdown"))
        return;

    if (!m_markdownView->loadFile(filePath)) {
        statusBar()->showMessage(
            tr("Failed to open: %1").arg(fi.fileName()), 5000);
        return;
    }

    m_currentFile = filePath;
    statusBar()->showMessage(tr("Opened: %1").arg(fi.fileName()), 3000);
    // loadFile() re-applies the search term automatically; no explicit
    // searchInDocument call is needed here.
}

void MainWindow::handleStyleChanged(int index)
{
    if (index < 0)
        return;

    const QString styleName = m_styleCombo->itemData(index).toString();
    styleLoader().apply(styleName);
    saveLastStyle(styleName);

    // Forward the theme name plus its code-block background.  The name
    // is used only for short-circuit comparisons; the colour is what
    // paints fenced blocks.  An invalid QColor (System, or missing
    // code-*.css) leaves Qt’s default code-block appearance untouched.
    if (m_markdownView) {
        m_markdownView->setTheme(
            styleName,
            styleLoader().codeBlockBackground(styleName));
    }
}

// -----------------------------------------------------------------------------
//  Search
// -----------------------------------------------------------------------------

void MainWindow::handleSearchTextChanged(const QString &text)
{
    // Fast path: clearing the box applies the empty filter immediately.
    if (text.isEmpty()) {
        if (m_searchDebounce)
            m_searchDebounce->stop();
        applySearchText();
        if (m_markdownView)
            m_markdownView->searchInDocument(QString());
        return;
    }

    if (m_searchDebounce)
        m_searchDebounce->start();

    // In-document search runs as-you-type (no debounce — the search
    // itself is fast, and debouncing it would make navigation feel
    // laggy).  searchInDocument handles the <3-char case by clearing.
    if (m_markdownView)
        m_markdownView->searchInDocument(text);
}

void MainWindow::applySearchText()
{
    if (!m_filterProxy || !m_searchEdit)
        return;

    // Whitespace-only input becomes empty, which hits the proxy’s
    // empty-string fast path.
    m_filterProxy->setSearchText(m_searchEdit->text().trimmed());
}

void MainWindow::handleSearchStateChanged(int current, int total)
{
    if (!m_searchCounter || !m_searchPrev || !m_searchNext)
        return;

    if (total == 0) {
        m_searchCounter->clear();
        m_searchPrev->setEnabled(false);
        m_searchNext->setEnabled(false);
    } else {
        m_searchCounter->setText(
            QStringLiteral("%1 / %2").arg(current).arg(total));
        m_searchPrev->setEnabled(true);
        m_searchNext->setEnabled(true);
    }
}

// -----------------------------------------------------------------------------
//  Status-bar buttons
// -----------------------------------------------------------------------------

void MainWindow::showQtAbout()
{
    // QMessageBox::aboutQt() renders the standard Qt About dialog: logo,
    // version, and the LGPL v3 licensing text required for a Qt
    // application distributed under that license.  Its strings are
    // translated by the qtbase_*.qm catalogues installed alongside the
    // application.
    QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::showRepoMenu()
{
    if (!m_repoMenu || !m_githubButton)
        return;

    // Rebuild every time so changes to repos.txt (from the Add dialog
    // below, or from an external editor) are reflected without a
    // restart.
    rebuildRepoMenu();

    // Ask the menu for its accurate size *before* positioning it.
    // adjustSize() forces layout; sizeHint() is the fallback when the
    // menu has not been shown yet on this platform.
    m_repoMenu->ensurePolished();
    m_repoMenu->adjustSize();
    QSize sz = m_repoMenu->size();
    if (sz.isEmpty())
        sz = m_repoMenu->sizeHint();
    if (sz.isEmpty())
        sz = QSize(220, 1);

    // Align the menu’s right edge with the button’s right edge, and its
    // bottom edge with the button’s top edge, so the menu opens upward.
    // The status bar sits at the bottom of the window; a downward menu
    // would fall off-screen for any menu taller than the space below.
    const QPoint popupPos = m_githubButton->mapToGlobal(
        QPoint(m_githubButton->width() - sz.width(),
               -sz.height()));

    // QMenu clamps to the screen if the computed position overflows it,
    // so an unusually tall menu still appears — it opens downward as a
    // last resort.
    m_repoMenu->popup(popupPos);
}

void MainWindow::rebuildRepoMenu()
{
    if (!m_repoMenu || !m_repoList)
        return;

    m_repoMenu->clear();

    const QUrl appUrl = RepoList::applicationRepoUrl();
    bool appShown = false;
    if (appUrl.isValid()) {
        QIcon appRepoIcon(QStringLiteral(":/icons/app.png"));
        if (appRepoIcon.isNull())
            appRepoIcon = style()->standardIcon(QStyle::SP_DirHomeIcon);

        auto *appAction = m_repoMenu->addAction(
            appRepoIcon,
            tr("QMarkdown-explorer (this app)"));
        appAction->setToolTip(appUrl.toString());
        appAction->setStatusTip(appUrl.toString());

        // Capture `appUrl` by value: the lambda outlives the loop
        // iteration that created it, and the action outlives the local
        // it was built from.
        connect(appAction, &QAction::triggered,
                this, [appUrl]() {
                    QDesktopServices::openUrl(appUrl);
                });
        appShown = true;
    }

    const QList<QUrl> &userRepos = m_repoList->userRepos();
    if (!userRepos.isEmpty()) {
        if (appShown)
            m_repoMenu->addSeparator();

        const QIcon userIcon = style()->standardIcon(QStyle::SP_DirIcon);
        for (const QUrl &url : userRepos) {
            auto *action = m_repoMenu->addAction(userIcon, url.toString());
            action->setToolTip(url.toString());
            action->setStatusTip(url.toString());
            // Same reasoning as above: capture by value, not reference.
            connect(action, &QAction::triggered, this, [url]() {
                QDesktopServices::openUrl(url);
            });
        }
    }

    m_repoMenu->addSeparator();
    auto *addAction = m_repoMenu->addAction(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder),
        tr("Add repository…"));
    addAction->setStatusTip(
        tr("Add a new GitHub repository to this list"));
    connect(addAction, &QAction::triggered,
            this, &MainWindow::addRepository);
}

void MainWindow::addRepository()
{
    if (!m_repoList)
        return;

    // The menu is already closed when this slot runs (QAction::triggered
    // closes the menu it was invoked from), so opening a modal dialog
    // here does not fight the menu for focus.
    bool ok = false;
    const QString text = QInputDialog::getText(
        this,
        tr("Add Repository"),
        tr("Repository URL:"),
        QLineEdit::Normal,
        QStringLiteral("https://github.com/"),
        &ok);

    if (!ok)
        return;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    // fromUserInput accepts the common "github.com/foo/bar" shorthand by
    // prepending https://.  RepoList::add() still rejects anything that
    // is not http(s) afterwards, so a stray scheme cannot slip through.
    const QUrl url = QUrl::fromUserInput(trimmed);
    const RepoList::AddResult result = m_repoList->add(url);

    switch (result) {
    case RepoList::AddResult::Ok:
        if (!m_repoList->save()) {
            QMessageBox::warning(
                this,
                tr("Could Not Save"),
                tr("The repository list could not be written next to "
                   "the executable."));
            return;
        }
        statusBar()->showMessage(
            tr("Repository added: %1").arg(url.toString()), 3000);
        break;

    case RepoList::AddResult::InvalidUrl:
        QMessageBox::warning(
            this,
            tr("Invalid URL"),
            tr("Only http and https URLs are accepted."));
        break;

    case RepoList::AddResult::IsApplicationRepo:
        statusBar()->showMessage(
            tr("This repository is already at the top of the menu."), 3000);
        break;

    case RepoList::AddResult::Duplicate:
        statusBar()->showMessage(
            tr("This repository is already in the list."), 3000);
        break;
    }
}

// -----------------------------------------------------------------------------
//  QSettings helpers
// -----------------------------------------------------------------------------

QString MainWindow::lastFolder() const
{
    return QSettings().value(QStringLiteral("LastFolder")).toString();
}

void MainWindow::saveLastFolder(const QString &folder) const
{
    QSettings().setValue(QStringLiteral("LastFolder"), folder);
}

QString MainWindow::lastStyle() const
{
    return QSettings().value(QStringLiteral("Style")).toString();
}

void MainWindow::saveLastStyle(const QString &styleName) const
{
    QSettings().setValue(QStringLiteral("Style"), styleName);
}