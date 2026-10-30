#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QModelIndex>
#include <QTemporaryDir>

#include "MarkdownFilterProxyModel.h"

#include <cstddef>
#include <filesystem>

namespace {

// Same UTF-8 bridge used throughout the project.
std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

} // namespace

/**
 * @brief Tests for MarkdownFilterProxyModel.
 *
 * Visibility is checked through QSortFilterProxyModel::mapFromSource(),
 * which is the same API the tree view uses to decide whether to render a
 * row.  A valid mapped proxy index means the row and its entire parent
 * chain are accepted; an invalid one means the row is filtered out (or
 * unreachable because an ancestor was).  Testing this way — rather than
 * subclassing to expose the protected filterAcceptsRow() — means the
 * tests exercise the exact code path the running application uses.
 *
 * The central test in this file is rootAndAncestorsAlwaysAccepted: it
 * reproduces, in isolation, the scenario that made the file tree render
 * blank before the ancestor-acceptance rule was added.  Every other test
 * in this file protects one of the four documented rules.
 */
class TestMarkdownFilterProxyModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // ---- Rule 1: root and ancestors always accepted ---------------------
    void rootIsReachableThroughProxy();           // regression: blank tree
    void ancestorsOfRootAreReachable();           // regression: blank tree
    void emptyFixtureRootIsStillReachable();      // rule 1 applies even
                                                  // with no .md files

    // ---- Rule 2: folder pruning ----------------------------------------
    void emptyFolderIsHidden();
    void folderWithMarkdownIsVisible();
    void nestedFolderWithMarkdownIsVisible();
    void folderWithOnlyReadmeIsVisible();

    // ---- Rule 3: README exemption --------------------------------------
    void rootReadmeIsVisible();
    void subfolderReadmeIsHidden();
    void subfolderWithOnlyReadmeStillShowsFolder();

    // ---- Rule 4: file visibility ---------------------------------------
    void markdownFilesAreVisible();
    void markdownLongExtensionIsVisible();
    void nonMarkdownFilesAreHidden();

    // ---- Search ---------------------------------------------------------
    void emptySearchShowsEveryMarkdownFile();
    void searchByNameFilters();
    void searchByContentFilters();
    void searchIsCaseInsensitive();
    void searchMatchesNameOrContent();
    void clearingSearchRestoresEverything();
    void changingSearchReEvaluatesTree();

    // ---- Refresh --------------------------------------------------------
    void refreshReEvaluatesContentAfterFileEdit();

private:
    // Writes `content` under m_currentDir/relPath, creating parent
    // directories as needed.  Returns the absolute path, or an empty
    // QString on failure.
    QString writeFile(const QString &relPath, const QByteArray &content);

    // Writes a Markdown file with plain text content (convenience).
    QString writeMarkdown(const QString &relPath, const QString &text)
    {
        return writeFile(relPath, text.toUtf8());
    }

    // Returns the mapped proxy index for an absolute path, or an
    // invalid QModelIndex if the path is filtered out.  Uses the
    // fixture’s shared QFileSystemModel.
    QModelIndex proxyIndexForPath(const QString &absPath) const;

    // (Re-)initialises the proxy’s source root.  Must be called after
    // the source model has its root set.  Also returns the source model’s
    // reported form of the fixture root (needed because Windows drive
    // letters can be reported in different cases).
    void configureProxy();

    QTemporaryDir         m_tmp;
    QString               m_currentDir;
    int                   m_counter = 0;

    QFileSystemModel         *m_sourceModel = nullptr;
    MarkdownFilterProxyModel *m_proxy       = nullptr;
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    // One QFileSystemModel per test class.  Reusing it across tests
    // keeps the file-watching overhead at one directory per test, and
    // matches how the running application uses a single model for the
    // lifetime of MainWindow.
    m_sourceModel = new QFileSystemModel(this);
    m_sourceModel->setReadOnly(true);
    m_sourceModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_sourceModel->setNameFilters({
        QStringLiteral("*.md"),
        QStringLiteral("*.markdown"),
    });
    m_sourceModel->setNameFilterDisables(false);

    m_proxy = new MarkdownFilterProxyModel(this);
    m_proxy->setSourceModel(m_sourceModel);
}

void TestMarkdownFilterProxyModel::init()
{
    // Fresh subdirectory per test so a stale cache or leftover file
    // from a partial failure cannot affect the next test.
    m_currentDir = m_tmp.filePath(QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));
    configureProxy();
}

void TestMarkdownFilterProxyModel::cleanup()
{
    // Reset the search text between tests so a search that was set in
    // one test cannot quietly apply to the next.  The source root will
    // be reset by the next init()’s configureProxy() call.
    m_proxy->setSearchText(QString());
}

QString TestMarkdownFilterProxyModel::writeFile(const QString &relPath,
                                                const QByteArray &content)
{
    const QString abs = m_currentDir + QStringLiteral("/") + relPath;
    const QFileInfo info(abs);
    QDir().mkpath(info.absolutePath());
    QFile f(abs);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(content);
    f.close();

    // Refresh the cache so subsequent visibility checks see this file.
    // init() configures the proxy on the empty per-test directory, so
    // without an explicit rebuild every file written afterwards is
    // invisible to filterAcceptsRow() until setSearchText() or refresh()
    // is called.  Making writeFile do it here keeps each test focused on
    // what it actually asserts.
    if (m_proxy)
        m_proxy->refresh();

    return abs;
}

QModelIndex TestMarkdownFilterProxyModel::proxyIndexForPath(
    const QString &absPath) const
{
    const QModelIndex src = m_sourceModel->index(absPath);
    if (!src.isValid())
        return {};
    return m_proxy->mapFromSource(src);
}

void TestMarkdownFilterProxyModel::configureProxy()
{
    // QFileSystemModel::setRootPath returns the normalized form of the
    // path as the model reports it.  Using that — rather than
    // m_currentDir directly — is important on Windows, where the drive
    // letter’s case can differ between the OS and the model, and would
    // otherwise cause the ancestor prefix check to fail spuriously.
    const QModelIndex srcRoot = m_sourceModel->setRootPath(m_currentDir);
    QVERIFY(srcRoot.isValid());

    m_proxy->setSourceRootPath(m_sourceModel->filePath(srcRoot));
}

// -----------------------------------------------------------------------------
//  Rule 1: root and ancestors always accepted
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::rootIsReachableThroughProxy()
{
    // The whole point of the ancestor-acceptance rule: without it,
    // mapFromSource() on the root’s own index returns invalid because
    // the root’s parent chain includes directories (/tmp, /) that are
    // not in m_visibleDirs — and MainWindow::setRootFolder() would call
    // setRootIndex(invalid), leaving the tree blank.
    //
    // The fixture has one .md file so the root would be in m_visibleDirs
    // even without rule 1; the rule is what makes the *ancestors*
    // reachable, which is tested separately below.  This test just
    // documents the baseline.
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));

    const QModelIndex proxyRoot = proxyIndexForPath(m_currentDir);
    QVERIFY2(proxyRoot.isValid(),
             "Root folder is not reachable through the proxy — the file "
             "tree would render blank.  Ancestor-acceptance rule is "
             "missing or broken.");
}

void TestMarkdownFilterProxyModel::ancestorsOfRootAreReachable()
{
    // Direct regression test for the ancestor rule.  If a future change
    // removes the "root or ancestor of root" branch in filterAcceptsRow,
    // the parent directory (which is not in m_visibleDirs because it
    // contains no .md files itself) is rejected, the parent chain for
    // the root becomes invalid, and setRootIndex() blanks the tree.
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));

    const QFileInfo fixtureInfo(m_currentDir);
    const QString parentDir = fixtureInfo.absolutePath();
    QVERIFY(parentDir != m_currentDir);
    QVERIFY(!parentDir.isEmpty());

    const QModelIndex proxyParent = proxyIndexForPath(parentDir);
    QVERIFY2(proxyParent.isValid(),
             "Ancestor of the configured root is not reachable through the "
             "proxy.  Without ancestor-acceptance, mapFromSource(root) "
             "returns invalid and the tree view is blank.");
}

void TestMarkdownFilterProxyModel::emptyFixtureRootIsStillReachable()
{
    // Even a root with no .md files anywhere underneath it must be
    // reachable — otherwise the user sees an empty window and cannot
    // tell whether the app failed or the folder is genuinely empty.
    // Rule 1 accepts the root regardless of its contents.
    const QModelIndex proxyRoot = proxyIndexForPath(m_currentDir);
    QVERIFY2(proxyRoot.isValid(),
             "An empty root folder is not reachable through the proxy; "
             "rule 1 (root and ancestors always accepted) is broken.");
}

// -----------------------------------------------------------------------------
//  Rule 2: folder pruning
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::emptyFolderIsHidden()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    QVERIFY(QDir().mkpath(m_currentDir + QStringLiteral("/empty")));

    const QString emptyPath = m_currentDir + QStringLiteral("/empty");
    QVERIFY2(!proxyIndexForPath(emptyPath).isValid(),
             "Folder with no Markdown files should be hidden");
}

void TestMarkdownFilterProxyModel::folderWithMarkdownIsVisible()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("sub/b.md"), QStringLiteral("content"));

    const QString subPath = m_currentDir + QStringLiteral("/sub");
    QVERIFY2(proxyIndexForPath(subPath).isValid(),
             "Folder containing a Markdown file should be visible");
}

void TestMarkdownFilterProxyModel::nestedFolderWithMarkdownIsVisible()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("deep/sub/b.md"),
                  QStringLiteral("content"));

    QVERIFY(proxyIndexForPath(
                m_currentDir + QStringLiteral("/deep")).isValid());
    QVERIFY(proxyIndexForPath(
                m_currentDir + QStringLiteral("/deep/sub")).isValid());
    QVERIFY(proxyIndexForPath(
                m_currentDir + QStringLiteral("/deep/sub/b.md")).isValid());
}

void TestMarkdownFilterProxyModel::folderWithOnlyReadmeIsVisible()
{
    // The README itself is hidden (rule 3), but its parent folder must
    // stay visible so the user can click it and load the README via
    // MainWindow::loadFolderReadme().  Without this, a project that
    // documents itself entirely through per-folder READMEs would show
    // an empty tree.
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("onlyreadme/README.md"),
                  QStringLiteral("content"));

    const QString folderPath =
        m_currentDir + QStringLiteral("/onlyreadme");
    QVERIFY2(proxyIndexForPath(folderPath).isValid(),
             "Folder whose only Markdown file is a README must still be "
             "visible, so the README can be loaded by selecting the folder");
}

// -----------------------------------------------------------------------------
//  Rule 3: README exemption
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::rootReadmeIsVisible()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("README.md"), QStringLiteral("content"));

    QVERIFY2(proxyIndexForPath(m_currentDir + QStringLiteral("/README.md"))
                 .isValid(),
             "README.md at the root must be visible");
}

void TestMarkdownFilterProxyModel::subfolderReadmeIsHidden()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("sub/b.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("sub/README.md"),
                  QStringLiteral("content"));

    QVERIFY2(!proxyIndexForPath(
                 m_currentDir + QStringLiteral("/sub/README.md")).isValid(),
             "README.md outside the root must be hidden");
}

void TestMarkdownFilterProxyModel::subfolderWithOnlyReadmeStillShowsFolder()
{
    // Companion to folderWithOnlyReadmeIsVisible — the folder is
    // visible even though its only .md file (the README) is hidden.
    // Selecting the folder loads the README.
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("sub/README.md"),
                  QStringLiteral("content"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/sub"))
                .isValid());
    QVERIFY(!proxyIndexForPath(
                 m_currentDir + QStringLiteral("/sub/README.md")).isValid());
}

// -----------------------------------------------------------------------------
//  Rule 4: file visibility
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::markdownFilesAreVisible()
{
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/a.md"))
                .isValid());
}

void TestMarkdownFilterProxyModel::markdownLongExtensionIsVisible()
{
    // The source model’s name filter includes *.markdown as well as
    // *.md.  The proxy’s hasMarkdownExtension() must agree, or a
    // .markdown file present in the model would never enter
    // m_matchingFiles and would be hidden by the proxy’s file check.
    writeMarkdown(QStringLiteral("a.markdown"),
                  QStringLiteral("content"));
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/a.markdown"))
                .isValid());
}

void TestMarkdownFilterProxyModel::nonMarkdownFilesAreHidden()
{
    // The source model’s filter already excludes non-Markdown files, so
    // this test mostly confirms that no path in the pipeline
    // accidentally un-hides one.  If QFileSystemModel::index() returns a
    // valid source index for a .txt file (it usually does — direct index
    // lookup bypasses row filtering), mapFromSource must still return
    // invalid because m_matchingFiles does not contain it.
    writeMarkdown(QStringLiteral("a.md"), QStringLiteral("content"));
    writeFile(QStringLiteral("notes.txt"),
              QByteArrayLiteral("plain text"));

    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/notes.txt"))
                 .isValid());
}

// -----------------------------------------------------------------------------
//  Search
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::emptySearchShowsEveryMarkdownFile()
{
    writeMarkdown(QStringLiteral("alpha.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("beta.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("sub/gamma.md"), QStringLiteral("content"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                .isValid());
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                .isValid());
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/sub/gamma.md"))
                .isValid());
}

void TestMarkdownFilterProxyModel::searchByNameFilters()
{
    writeMarkdown(QStringLiteral("alpha.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("beta.md"), QStringLiteral("content"));

    m_proxy->setSearchText(QStringLiteral("alpha"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                .isValid());
    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                 .isValid());
}

void TestMarkdownFilterProxyModel::searchByContentFilters()
{
    writeMarkdown(QStringLiteral("alpha.md"),
                  QStringLiteral("the quick brown fox"));
    writeMarkdown(QStringLiteral("beta.md"),
                  QStringLiteral("lorem ipsum dolor"));

    m_proxy->setSearchText(QStringLiteral("quick"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                .isValid());
    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                 .isValid());
}

void TestMarkdownFilterProxyModel::searchIsCaseInsensitive()
{
    writeMarkdown(QStringLiteral("CamelCase.md"), QStringLiteral("content"));

    m_proxy->setSearchText(QStringLiteral("camel"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/CamelCase.md"))
                .isValid());
}

void TestMarkdownFilterProxyModel::searchMatchesNameOrContent()
{
    // Two files: one matches by name, one by content, a third matches
    // neither.  All three must be classified correctly — an AND
    // implementation would hide both of the first two.
    writeMarkdown(QStringLiteral("target.md"),
                  QStringLiteral("nothing to see here"));
    writeMarkdown(QStringLiteral("other.md"),
                  QStringLiteral("the target word appears here"));
    writeMarkdown(QStringLiteral("unrelated.md"),
                  QStringLiteral("nothing of interest"));

    m_proxy->setSearchText(QStringLiteral("target"));

    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/target.md"))
                .isValid());
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/other.md"))
                .isValid());
    QVERIFY(!proxyIndexForPath(
                 m_currentDir + QStringLiteral("/unrelated.md")).isValid());
}

void TestMarkdownFilterProxyModel::clearingSearchRestoresEverything()
{
    writeMarkdown(QStringLiteral("alpha.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("beta.md"), QStringLiteral("content"));

    m_proxy->setSearchText(QStringLiteral("alpha"));
    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                 .isValid());

    m_proxy->setSearchText(QString());
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                .isValid());
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                .isValid());
}

void TestMarkdownFilterProxyModel::changingSearchReEvaluatesTree()
{
    // setSearchText is a no-op when the string is unchanged.  This
    // test proves the *change* path re-runs rebuildCache and
    // invalidateFilter — without the invalidation, the proxy’s row
    // classification would remain stale after a search-string change.
    writeMarkdown(QStringLiteral("alpha.md"), QStringLiteral("content"));
    writeMarkdown(QStringLiteral("beta.md"), QStringLiteral("content"));

    m_proxy->setSearchText(QStringLiteral("zzz"));
    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                 .isValid());

    m_proxy->setSearchText(QStringLiteral("alpha"));
    QVERIFY(proxyIndexForPath(m_currentDir + QStringLiteral("/alpha.md"))
                .isValid());
    QVERIFY(!proxyIndexForPath(m_currentDir + QStringLiteral("/beta.md"))
                 .isValid());
}

// -----------------------------------------------------------------------------
//  Refresh
// -----------------------------------------------------------------------------

void TestMarkdownFilterProxyModel::refreshReEvaluatesContentAfterFileEdit()
{
    // The cache stores which files matched when it was last built.
    // refresh() re-reads the files and updates the cache.  Without it,
    // editing a file so that it no longer matches the search would
    // leave the stale result visible until the user reopened the folder.
    //
    // Uses the search path specifically because it does not depend on
    // QFileSystemModel noticing a file change — the file path stays the
    // same across the edit, only its content changes.
    const QString mdPath = writeMarkdown(
        QStringLiteral("editable.md"),
        QStringLiteral("hello"));
    QVERIFY(!mdPath.isEmpty());

    m_proxy->setSearchText(QStringLiteral("hello"));
    QVERIFY(proxyIndexForPath(mdPath).isValid());

    // Rewrite the file with content that no longer matches the search.
    QFile f(mdPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("goodbye");
    f.close();

    // Refresh re-reads every file and rebuilds the cache; the search
    // no longer matches, so the file is filtered out.
    m_proxy->refresh();
    QVERIFY2(!proxyIndexForPath(mdPath).isValid(),
             "refresh() should rebuild the cache from current file "
             "content; a stale cache is why refresh() exists.");
}

QTEST_GUILESS_MAIN(TestMarkdownFilterProxyModel)
#include "tst_markdownfilterproxymodel.moc"
