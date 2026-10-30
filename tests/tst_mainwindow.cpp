#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include "MainWindow.h"

#include <cstddef>
#include <filesystem>

namespace {

std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

/**
 * @brief Observes MainWindow’s loaded folder without requiring a private
 *        getter.
 *
 * The tree view carries objectName "fileTree"; its model is a
 * QSortFilterProxyModel whose sourceModel is a QFileSystemModel whose
 * rootPath() is set by MainWindow::setRootFolder().  Reading that path
 * from the model chain is the closest observable equivalent to reading
 * MainWindow::m_rootFolder — and it uses the same object the running
 * application queries for rendering.
 *
 * Returns the absolute, canonicalised path, or an empty string when the
 * model chain cannot be resolved (which itself would be a test failure
 * for any test that calls this).
 */
QString loadedRootPath(MainWindow &window)
{
    auto *tree = window.findChild<QTreeView *>(QStringLiteral("fileTree"));
    if (!tree)
        return {};

    // QFileSystemModel::rootPath() defaults to the current working
    // directory (via QDir::currentPath()) even before any setRootPath()
    // call.  So "no folder loaded" is not signalled by an empty
    // rootPath().  The reliable signal is QTreeView::rootIndex(): it
    // is invalid until setRootFolder() calls setRootIndex().  Checking
    // it here lets the "should be empty" and "should equal the fixture"
    // assertions both work correctly.
    if (!tree->rootIndex().isValid())
        return {};

    auto *proxy = qobject_cast<QSortFilterProxyModel *>(tree->model());
    if (!proxy)
        return {};

    auto *fileModel =
        qobject_cast<QFileSystemModel *>(proxy->sourceModel());
    if (!fileModel)
        return {};

    const QString rp = fileModel->rootPath();
    if (rp.isEmpty())
        return {};

    return QFileInfo(rp).absoluteFilePath();
}

} // namespace

/**
 * @brief Tests for MainWindow’s startup handshake.
 *
 * The behaviour under test is the deferred-picker lambda in
 * MainWindow’s constructor, which reads three QSettings keys:
 *
 *   * LaunchFolder       — transient; set by main.cpp’s --folder option
 *   * SkipInitialPicker  — transient; set by main.cpp’s --no-picker
 *   * LastFolder         — durable; the fallback when neither of the
 *                          above is set
 *
 * Every test in this file is designed around a launch state where the
 * picker does *not* appear — LaunchFolder valid or SkipInitialPicker
 * set.  The three launch states that do show the picker (no transient
 * keys; LaunchFolder invalid; both transient keys but LaunchFolder
 * invalid) are not tested, because verifying them requires intercepting
 * a modal QFileDialog, which is timing-dependent and platform-dependent
 * to a degree that makes the test flaky without a real user.  The
 * picker’s own behaviour — showing the dialog, honouring the start
 * directory, honouring cancellation — is Qt’s, not ours.
 *
 * Test isolation:
 *
 *   * QSettings is redirected via QStandardPaths::setTestModeEnabled
 *     so the real user’s settings are never touched.
 *   * QSettings is cleared in init() so a state left by one test cannot
 *     leak into the next.
 *   * The style singleton in MainWindow.cpp scans the styles directory
 *     on first use, which in a test binary is <test-dir>/styles/.  No
 *     style files are created there, so every test starts with the
 *     platform-default stylesheet.
 */
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // ---- LaunchFolder handshake ----------------------------------------
    void launchFolderIsLoaded();
    void launchFolderKeyIsConsumedAfterUse();
    void deferredPickerRunsOnEventLoop();

    // ---- SkipInitialPicker handshake -----------------------------------
    void skipInitialPickerIsHonoured();
    void skipInitialPickerKeyIsConsumedAfterUse();

    // ---- Interaction between the two transient keys --------------------
    void launchFolderOverridesSkipInitialPicker();
    void bothTransientKeysAreConsumed();

private:
    // Creates a valid folder inside the fixture’s temp directory and
    // returns its absolute path.  Used to give LaunchFolder a target.
    QString makeFixtureFolder();

    QTemporaryDir m_tmp;
    QString       m_currentDir;
    int           m_counter = 0;
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestMainWindow::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    // Redirect QStandardPaths (and therefore QSettings) to a test-only
    // location.  Must happen before any QSettings object is created.
    QStandardPaths::setTestModeEnabled(true);

    // Match main.cpp's application identity.  Without this, the test
    // binary's QCoreApplication::organizationName() / applicationName()
    // are empty, and QSettings falls back to an executable-name-derived
    // default.  That fallback is fragile across platforms and across
    // QSettings versions; setting the real values removes the ambiguity
    // and guarantees the test's write and MainWindow's read hit the same
    // backend.
    QCoreApplication::setOrganizationName(QStringLiteral("MarkdownExplorer"));
    QCoreApplication::setOrganizationDomain(
        QStringLiteral("markdown-explorer.local"));
    QCoreApplication::setApplicationName(QStringLiteral("QMarkdown-explorer"));
}

void TestMainWindow::init()
{
    // Clear any QSettings written by a previous test.  Because
    // QStandardPaths is in test mode, this wipes only the test
    // configuration — the user’s real settings are elsewhere.
    QSettings().clear();

    m_currentDir = m_tmp.filePath(QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));
}

QString TestMainWindow::makeFixtureFolder()
{
    const QString sub = m_currentDir + QStringLiteral("/launch-target");
    if (!QDir().mkpath(sub))
        return {};
    // Add a Markdown file so the folder survives the proxy’s rule 2
    // (empty folders are hidden).  This is not strictly necessary for
    // the folder to appear in the model, but it keeps the test
    // consistent with what a real launch would look like.
    QFile md(sub + QStringLiteral("/a.md"));
    if (!md.open(QIODevice::WriteOnly))
        return {};
    md.write("placeholder");
    md.close();
    return sub;
}

// -----------------------------------------------------------------------------
//  LaunchFolder handshake
// -----------------------------------------------------------------------------

void TestMainWindow::launchFolderIsLoaded()
{
    const QString fixture = makeFixtureFolder();
    QVERIFY(!fixture.isEmpty());

    {
        QSettings s;
        s.setValue(QStringLiteral("LaunchFolder"), fixture);
        s.sync();
    }

    MainWindow window;

    // The deferred picker runs on the next event-loop iteration.
    // Without the spin, the timer has not fired and nothing has loaded.
    QTest::qWait(50);

    QCOMPARE(loadedRootPath(window), QFileInfo(fixture).absoluteFilePath());
}

void TestMainWindow::launchFolderKeyIsConsumedAfterUse()
{
    // Same setup as launchFolderIsLoaded, but the assertion is on the
    // transient key rather than the loaded folder.  Both properties
    // matter: the folder being loaded proves the branch ran, and the
    // key being cleared proves the branch is not a permanent override.
    // Splitting the two makes a future regression (loads folder but
    // leaves the key set → next launch reloads against the user’s
    // wishes) visible on its own.
    const QString fixture = makeFixtureFolder();
    QVERIFY(!fixture.isEmpty());

    {
        QSettings s;
        s.setValue(QStringLiteral("LaunchFolder"), fixture);
        s.sync();
    }

    MainWindow window;
    QTest::qWait(50);

    QVERIFY2(QSettings().value(QStringLiteral("LaunchFolder"))
                 .toString().isEmpty(),
             "LaunchFolder was not cleared after use — a subsequent launch "
             "would silently reload the same folder regardless of the "
             "user’s intent");
}

void TestMainWindow::deferredPickerRunsOnEventLoop()
{
    // Locks in the deferral: without the event-loop spin, the folder
    // must NOT be loaded yet.  A future refactor that runs the picker
    // synchronously in the constructor would break this test — and
    // would also break the picker itself, because a modal dialog shown
    // before show() has no parent window to attach to and behaves
    // inconsistently across platforms.
    const QString fixture = makeFixtureFolder();
    QVERIFY(!fixture.isEmpty());

    {
        QSettings s;
        s.setValue(QStringLiteral("LaunchFolder"), fixture);
        s.sync();
    }

    MainWindow window;

    // No qWait: the timer has not fired.
    QVERIFY2(loadedRootPath(window).isEmpty(),
             "Folder was loaded synchronously in the constructor — the "
             "deferred-picker pattern is not in effect");

    // Now spin and confirm the folder loads.
    QTest::qWait(50);
    QCOMPARE(loadedRootPath(window), QFileInfo(fixture).absoluteFilePath());
}

// -----------------------------------------------------------------------------
//  SkipInitialPicker handshake
// -----------------------------------------------------------------------------

void TestMainWindow::skipInitialPickerIsHonoured()
{
    // No LaunchFolder, SkipInitialPicker set: the picker must be
    // skipped AND no folder must be loaded.  The window opens clean and
    // the user can pick a folder with the toolbar action.
    {
        QSettings s;
        s.setValue(QStringLiteral("SkipInitialPicker"), true);
        s.sync();
    }

    MainWindow window;
    QTest::qWait(50);

    QVERIFY2(loadedRootPath(window).isEmpty(),
             "SkipInitialPicker should leave the window without a loaded "
             "folder");
}

void TestMainWindow::skipInitialPickerKeyIsConsumedAfterUse()
{
    {
        QSettings s;
        s.setValue(QStringLiteral("SkipInitialPicker"), true);
        s.sync();
    }

    MainWindow window;
    QTest::qWait(50);

    QVERIFY2(!QSettings().value(QStringLiteral("SkipInitialPicker")).toBool(),
             "SkipInitialPicker was not cleared after use — a subsequent "
             "launch would silently skip the picker again");
}

// -----------------------------------------------------------------------------
//  Interaction between the two transient keys
// -----------------------------------------------------------------------------

void TestMainWindow::launchFolderOverridesSkipInitialPicker()
{
    // Both keys set, folder valid: the specific folder is more
    // specific than “no picker”, so the folder wins.  This is the
    // documented behaviour in MainWindow.cpp’s deferred-picker comment.
    const QString fixture = makeFixtureFolder();
    QVERIFY(!fixture.isEmpty());

    {
        QSettings s;
        s.setValue(QStringLiteral("LaunchFolder"), fixture);
        s.setValue(QStringLiteral("SkipInitialPicker"), true);
        s.sync();
    }

    MainWindow window;
    QTest::qWait(50);

    QCOMPARE(loadedRootPath(window), QFileInfo(fixture).absoluteFilePath());
}

void TestMainWindow::bothTransientKeysAreConsumed()
{
    // When LaunchFolder wins, SkipInitialPicker must be cleared as a
    // side effect — otherwise it would leak into the next launch.  The
    // current MainWindow implementation clears it inside the
    // LaunchFolder branch specifically for this reason; the test locks
    // it in.
    const QString fixture = makeFixtureFolder();
    QVERIFY(!fixture.isEmpty());

    {
        QSettings s;
        s.setValue(QStringLiteral("LaunchFolder"), fixture);
        s.setValue(QStringLiteral("SkipInitialPicker"), true);
        s.sync();
    }

    MainWindow window;
    QTest::qWait(50);

    QSettings s;
    QVERIFY2(s.value(QStringLiteral("LaunchFolder")).toString().isEmpty(),
             "LaunchFolder was not cleared");
    QVERIFY2(!s.value(QStringLiteral("SkipInitialPicker")).toBool(),
             "SkipInitialPicker was not cleared when LaunchFolder won");
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
