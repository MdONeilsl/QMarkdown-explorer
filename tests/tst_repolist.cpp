#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include "RepoList.h"

#include <cstddef>
#include <filesystem>

namespace {

// Same UTF-8 bridge as the production code — needed only because the
// RepoList constructor takes std::filesystem::path.
std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

// QUrl's operator== compares normalized forms, which is correct for
// duplicate detection but noisy in test failures — a mismatch prints the
// whole serialized string, making it hard to see *which* component
// diverged.  Component-wise comparison gives a precise diff.
void assertSameUrl(const QUrl &actual, const QUrl &expected)
{
    QCOMPARE(actual.scheme(), expected.scheme());
    QCOMPARE(actual.host(),   expected.host());
    QCOMPARE(actual.path(),   expected.path());
}

} // namespace

/**
 * @brief Tests for RepoList.
 *
 * Organised around the two contracts the class publishes:
 *
 *   * **Persistence** — add() mutates memory, save() writes, reload()
 *     reads; the file survives a round-trip, save() truncates, and an
 *     absent file is a valid empty state, not an error.
 *
 *   * **Validation** — every AddResult enum value is reachable through
 *     the public API.  This is what guarantees the caller’s four-case
 *     switch in MainWindow::addRepository() can never fall through.
 *
 * Each test gets its own fresh subdirectory of the fixture’s
 * QTemporaryDir so a partial failure in one test cannot pollute another.
 */
class TestRepoList : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // ---- Persistence ----------------------------------------------------
    void emptyFileReloadsToEmpty();
    void missingFileReloadsToEmpty();
    void addSaveReloadRoundtrip();
    void saveOverwritesPreviousContent();
    void addDoesNotPersistWithoutSave();

    // ---- AddResult branches ---------------------------------------------
    void addInvalidUrlIsRejected_data();
    void addInvalidUrlIsRejected();
    void addApplicationRepoIsRejected();
    void addDuplicateIsRejected();
    void addCaseInsensitiveDuplicateIsRejected();
    void addValidHttpsSucceeds();
    void addValidHttpSucceeds();

    // ---- Reload robustness ----------------------------------------------
    void malformedLinesAreSkipped();
    void bareHostIsNormalizedOnRead();
    void windowsLineEndingsAreAccepted();

    // ---- Removal --------------------------------------------------------
    void removeValidIndex();
    void removeOutOfRangeIndex();

    // ---- Save failure ---------------------------------------------------
    void saveToInvalidPathReturnsFalse();

private:
    QString filePath() const { return m_currentDir + QStringLiteral("/repos.txt"); }

    QTemporaryDir m_tmp;
    QString       m_currentDir;
    int           m_counter = 0;
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestRepoList::initTestCase()
{
    QVERIFY(m_tmp.isValid());
}

void TestRepoList::init()
{
    m_currentDir = m_tmp.filePath(
        QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));
}

// -----------------------------------------------------------------------------
//  Persistence
// -----------------------------------------------------------------------------

void TestRepoList::emptyFileReloadsToEmpty()
{
    // Zero-byte file — the state a user produces with `: > repos.txt`.
    QFile f(filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QVERIFY(list.userRepos().isEmpty());
}

void TestRepoList::missingFileReloadsToEmpty()
{
    // The documented "fresh install" contract: a missing file is not an
    // error, it is simply an empty list.
    RepoList list(toFsPath(filePath()));
    QVERIFY(!QFileInfo::exists(filePath()));
    QVERIFY(list.reload());
    QVERIFY(list.userRepos().isEmpty());
}

void TestRepoList::addSaveReloadRoundtrip()
{
    const QUrl first(QStringLiteral("https://github.com/a/b"));
    const QUrl second(QStringLiteral("https://github.com/c/d"));

    {
        RepoList list(toFsPath(filePath()));
        QVERIFY(list.reload());
        QCOMPARE(list.add(first),  RepoList::AddResult::Ok);
        QCOMPARE(list.add(second), RepoList::AddResult::Ok);
        QVERIFY(list.save());
    }

    // Fresh instance: nothing is shared with the writer above except the
    // file on disk.  If the round-trip loses or mutates either URL, the
    // component comparison below tells us exactly which one.
    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QCOMPARE(list.userRepos().size(), 2);
    assertSameUrl(list.userRepos()[0], first);
    assertSameUrl(list.userRepos()[1], second);
}

void TestRepoList::saveOverwritesPreviousContent()
{
    const QUrl first(QStringLiteral("https://github.com/a/b"));
    const QUrl second(QStringLiteral("https://github.com/c/d"));

    {
        RepoList list(toFsPath(filePath()));
        list.reload();
        QCOMPARE(list.add(first), RepoList::AddResult::Ok);
        QVERIFY(list.save());   // file = [first]
    }
    {
        // Do NOT reload — the point of this test is that save() truncates.
        // A fresh instance with one add() must overwrite [first] with
        // [second], not append to it.
        RepoList list(toFsPath(filePath()));
        QCOMPARE(list.add(second), RepoList::AddResult::Ok);
        QVERIFY(list.save());   // file = [second]
    }

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QCOMPARE(list.userRepos().size(), 1);
    assertSameUrl(list.userRepos()[0], second);
}

void TestRepoList::addDoesNotPersistWithoutSave()
{
    // add() is documented as memory-only; the caller chooses when to
    // commit.  This is what makes a batch of adds produce a single write.
    const QUrl url(QStringLiteral("https://github.com/a/b"));

    {
        RepoList list(toFsPath(filePath()));
        list.reload();
        QCOMPARE(list.add(url), RepoList::AddResult::Ok);
        // deliberately no save()
    }

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QVERIFY(list.userRepos().isEmpty());
}

// -----------------------------------------------------------------------------
//  AddResult branches
// -----------------------------------------------------------------------------

void TestRepoList::addInvalidUrlIsRejected_data()
{
    QTest::addColumn<QString>("url");

    // Every entry below is malformed or carries a scheme RepoList does
    // not accept.  QUrl::fromUserInput is used at the call site so that
    // "github.com/foo" is repaired to https:// — anything that still
    // fails to acquire an http/https scheme after that repair belongs in
    // this table.
    QTest::newRow("empty")           << QString();
    QTest::newRow("ftp scheme")      << QStringLiteral("ftp://example.com/x");
    QTest::newRow("mailto scheme")   << QStringLiteral("mailto:test@example.com");
    QTest::newRow("file scheme")     << QStringLiteral("file:///etc/passwd");
    QTest::newRow("invalid host")    << QStringLiteral("https://");
}

void TestRepoList::addInvalidUrlIsRejected()
{
    QFETCH(QString, url);

    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(QUrl::fromUserInput(url)),
             RepoList::AddResult::InvalidUrl);
    QVERIFY(list.userRepos().isEmpty());
}

void TestRepoList::addApplicationRepoIsRejected()
{
    // applicationRepoUrl() reads APP_HOMEPAGE_URL, which is empty when
    // the build did not set HOMEPAGE_URL.  A build without a homepage
    // simply has no app-repo entry, so the branch is unreachable — skip
    // rather than fail.
    const QUrl appUrl = RepoList::applicationRepoUrl();
    if (!appUrl.isValid())
        QSKIP("APP_HOMEPAGE_URL is not set; the IsApplicationRepo branch "
              "is not reachable in this build");

    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(appUrl), RepoList::AddResult::IsApplicationRepo);
    QVERIFY(list.userRepos().isEmpty());
}

void TestRepoList::addDuplicateIsRejected()
{
    const QUrl url(QStringLiteral("https://github.com/a/b"));

    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(url), RepoList::AddResult::Ok);
    QCOMPARE(list.add(url), RepoList::AddResult::Duplicate);
    QCOMPARE(list.userRepos().size(), 1);
}

void TestRepoList::addCaseInsensitiveDuplicateIsRejected()
{
    // QUrl lowercases the host on parse, so "GitHub.com" and "github.com"
    // compare equal.  The test locks that in: if a future change switches
    // duplicate detection to raw string comparison, this fires.
    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(QUrl(QStringLiteral("https://github.com/a/b"))),
             RepoList::AddResult::Ok);
    QCOMPARE(list.add(QUrl(QStringLiteral("https://GitHub.com/a/b"))),
             RepoList::AddResult::Duplicate);
}

void TestRepoList::addValidHttpsSucceeds()
{
    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(QUrl(QStringLiteral("https://github.com/a/b"))),
             RepoList::AddResult::Ok);
    QCOMPARE(list.userRepos().size(), 1);
}

void TestRepoList::addValidHttpSucceeds()
{
    // The scheme check accepts http as well as https — some enterprise
    // git installations use http only.
    RepoList list(toFsPath(filePath()));
    list.reload();
    QCOMPARE(list.add(QUrl(QStringLiteral("http://example.com/repo"))),
             RepoList::AddResult::Ok);
    QCOMPARE(list.userRepos().size(), 1);
}

// -----------------------------------------------------------------------------
//  Reload robustness
// -----------------------------------------------------------------------------

void TestRepoList::malformedLinesAreSkipped()
{
    // The file is user-editable.  A single bad line must not abort the
    // whole read: one typo would otherwise hide every repository after
    // it.  Each skipped line is documented as either an empty line, a
    // comment, or a URL with a scheme RepoList rejects.
    QFile f(filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(
        "https://github.com/good/one\n"   // good
        "\n"                               // blank
        "# a comment\n"                    // comment
        "ftp://example.com/nope\n"         // wrong scheme
        "https://github.com/good/two\n"    // good
        "mailto:test@example.com\n"        // wrong scheme
        "   \n"                            // whitespace only
        "   https://github.com/good/three   \n"  // good, padded
    );
    f.close();

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QCOMPARE(list.userRepos().size(), 3);
    QCOMPARE(list.userRepos()[0].path(), QStringLiteral("/good/one"));
    QCOMPARE(list.userRepos()[1].path(), QStringLiteral("/good/two"));
    QCOMPARE(list.userRepos()[2].path(), QStringLiteral("/good/three"));
}

void TestRepoList::bareHostIsNormalizedOnRead()
{
    QFile f(filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("github.com/foo/bar\n");
    f.close();

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QCOMPARE(list.userRepos().size(), 1);

    // QUrl::fromUserInput prepends http:// on Qt 6.5; earlier Qt versions
    // may prepend https://.  Both schemes are accepted by isHttpUrl(), so
    // the test asserts only that a scheme was added.
    const QString scheme = list.userRepos()[0].scheme();
    QVERIFY2(scheme == QStringLiteral("http")
             || scheme == QStringLiteral("https"),
             qPrintable(QStringLiteral(
                 "Expected http or https, got [%1]").arg(scheme)));
    QCOMPARE(list.userRepos()[0].host(), QStringLiteral("github.com"));
    QCOMPARE(list.userRepos()[0].path(), QStringLiteral("/foo/bar"));
}

void TestRepoList::windowsLineEndingsAreAccepted()
{
    // Reading is binary-mode; Windows files carry '\r'.  The reload()
    // strip must remove it before the URL is parsed, otherwise every
    // line would end with a stray CR that QUrl encodes as %0D and the
    // duplicate check against a hand-typed URL would fail.
    QFile f(filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("https://github.com/a/b\r\n"
            "https://github.com/c/d\r\n");
    f.close();

    RepoList list(toFsPath(filePath()));
    QVERIFY(list.reload());
    QCOMPARE(list.userRepos().size(), 2);
    QCOMPARE(list.userRepos()[0].path(), QStringLiteral("/a/b"));
    QCOMPARE(list.userRepos()[1].path(), QStringLiteral("/c/d"));
}

// -----------------------------------------------------------------------------
//  Removal
// -----------------------------------------------------------------------------

void TestRepoList::removeValidIndex()
{
    RepoList list(toFsPath(filePath()));
    list.reload();
    list.add(QUrl(QStringLiteral("https://github.com/a/b")));
    list.add(QUrl(QStringLiteral("https://github.com/c/d")));
    QCOMPARE(list.userRepos().size(), 2);

    QVERIFY(list.remove(0));
    QCOMPARE(list.userRepos().size(), 1);
    QCOMPARE(list.userRepos()[0].path(), QStringLiteral("/c/d"));
}

void TestRepoList::removeOutOfRangeIndex()
{
    RepoList list(toFsPath(filePath()));
    list.reload();
    // Empty list: every index is out of range.
    QVERIFY(!list.remove(0));
    QVERIFY(!list.remove(-1));
    QVERIFY(!list.remove(42));
}

// -----------------------------------------------------------------------------
//  Save failure
// -----------------------------------------------------------------------------

void TestRepoList::saveToInvalidPathReturnsFalse()
{
    // save() cannot create parent directories — that is the caller’s job.
    // A regular file in the parent-directory position makes the open()
    // fail on every platform, which is what the contract says must be
    // surfaced (MainWindow shows a warning dialog).
    const QString blocker = m_currentDir + QStringLiteral("/not-a-dir");
    {
        QFile f(blocker);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    RepoList list(toFsPath(blocker + QStringLiteral("/repos.txt")));
    QCOMPARE(list.add(QUrl(QStringLiteral("https://github.com/a/b"))),
             RepoList::AddResult::Ok);
    QVERIFY(!list.save());
}

QTEST_GUILESS_MAIN(TestRepoList)
#include "tst_repolist.moc"
