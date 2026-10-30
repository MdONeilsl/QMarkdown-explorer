#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "StyleLoader.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

// Same UTF-8 bridge used throughout the project.  Needed here because
// both directions of the Qt <-> filesystem boundary appear in the test:
// the loader’s constructor takes a std::filesystem::path, and pathFor()
// returns one.
std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

QString fsToQString(const std::filesystem::path &p)
{
    const auto u8 = p.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(u8.data()),
                             static_cast<qsizetype>(u8.size()));
}

} // namespace

/**
 * @brief Tests for StyleLoader.
 *
 * Organised around the two contracts the class publishes:
 *
 *   * **Directory scan** — only regular files whose extension is `.qss`
 *     (case-insensitively) are discovered, only at the top level, and
 *     the resulting names are sorted case-insensitively.  The stem
 *     preserves on-disk case so the combo box shows what the user wrote.
 *
 *   * **Apply contract** — every failure path (empty name, missing
 *     file, unreadable file, empty file) clears the application
 *     stylesheet.  Leaving a stale stylesheet behind would mean the UI
 *     shows a style that does not match the current selection, which is
 *     worse than the platform default.
 *
 * The apply tests need a real QApplication: QApplication::setStyleSheet
 * and styleSheet do not exist on QCoreApplication.  QTEST_MAIN creates
 * one; the offscreen platform plugin (set in CMake) makes this work in a
 * headless environment without any additional configuration.
 */
class TestStyleLoader : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // ---- Directory scan -------------------------------------------------
    void emptyDirectoryYieldsNoStyles();
    void missingDirectoryYieldsNoStyles();
    void qssFilesAreDiscovered();
    void mixedCaseExtensionsAreDiscovered();
    void nonQssFilesAreIgnored();
    void subdirectoriesAreNotScanned();
    void stylesAreSortedCaseInsensitively();

    // ---- pathFor --------------------------------------------------------
    void pathForExactCase();
    void pathForMissingReturnsEmpty();
    void pathForEmptyReturnsEmpty();

    // ---- apply contract -------------------------------------------------
    void applyEmptyNameClearsStylesheet();
    void applyMissingFileClearsStylesheet();
    void applyEmptyFileClearsStylesheet();
    void applyValidFileSetsStylesheet();

private:
    // Creates a file in the current per-test directory.  Returns the
    // absolute path, or an empty QString on failure.  Default content is
    // a syntactically valid QSS fragment so the file is not empty.
    QString makeStyleFile(const QString &name,
                          const QByteArray &content =
                              QByteArrayLiteral("QWidget { }"))
    {
        const QString path = m_currentDir + QStringLiteral("/") + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(content);
        f.close();
        return path;
    }

    QTemporaryDir m_tmp;
    QString       m_currentDir;
    int           m_counter = 0;
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestStyleLoader::initTestCase()
{
    QVERIFY(m_tmp.isValid());
}

void TestStyleLoader::init()
{
    // Fresh subdirectory per test: a file left behind by a partial
    // failure cannot contaminate the next test’s directory scan.
    m_currentDir = m_tmp.filePath(QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));
}

// -----------------------------------------------------------------------------
//  Directory scan
// -----------------------------------------------------------------------------

void TestStyleLoader::emptyDirectoryYieldsNoStyles()
{
    StyleLoader loader(toFsPath(m_currentDir));
    QVERIFY(loader.availableStyles().isEmpty());
}

void TestStyleLoader::missingDirectoryYieldsNoStyles()
{
    // A missing directory is not an error — the loader behaves as if it
    // contained no styles, and apply() on any name falls back to the
    // platform default.
    const QString nonexistent =
        m_currentDir + QStringLiteral("/does-not-exist");
    StyleLoader loader(toFsPath(nonexistent));
    QVERIFY(loader.availableStyles().isEmpty());
}

void TestStyleLoader::qssFilesAreDiscovered()
{
    makeStyleFile(QStringLiteral("dark.qss"));
    makeStyleFile(QStringLiteral("light.qss"));

    StyleLoader loader(toFsPath(m_currentDir));
    const QStringList names = loader.availableStyles();
    QCOMPARE(names.size(), 2);
    QVERIFY(names.contains(QStringLiteral("dark")));
    QVERIFY(names.contains(QStringLiteral("light")));
}

void TestStyleLoader::mixedCaseExtensionsAreDiscovered()
{
    // Extension comparison is case-insensitive; the stem preserves
    // on-disk case so the user sees the name they actually wrote.
    makeStyleFile(QStringLiteral("Theme.QSS"));

    StyleLoader loader(toFsPath(m_currentDir));
    QCOMPARE(loader.availableStyles().size(), 1);
    QCOMPARE(loader.availableStyles()[0], QStringLiteral("Theme"));
}

void TestStyleLoader::nonQssFilesAreIgnored()
{
    makeStyleFile(QStringLiteral("dark.qss"));
    makeStyleFile(QStringLiteral("readme.txt"));
    makeStyleFile(QStringLiteral("config.json"));
    makeStyleFile(QStringLiteral("dark.qss.bak"));

    StyleLoader loader(toFsPath(m_currentDir));
    QCOMPARE(loader.availableStyles().size(), 1);
    QCOMPARE(loader.availableStyles()[0], QStringLiteral("dark"));
}

void TestStyleLoader::subdirectoriesAreNotScanned()
{
    // The loader is documented as non-recursive: only files directly in
    // the styles directory are considered.  A nested .qss must not be
    // picked up, even though it would be found by a naive recursive walk.
    makeStyleFile(QStringLiteral("dark.qss"));

    QVERIFY(QDir().mkpath(m_currentDir + QStringLiteral("/sub")));
    QFile nested(m_currentDir + QStringLiteral("/sub/hidden.qss"));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("QWidget { }");
    nested.close();

    StyleLoader loader(toFsPath(m_currentDir));
    QCOMPARE(loader.availableStyles().size(), 1);
    QCOMPARE(loader.availableStyles()[0], QStringLiteral("dark"));
}

void TestStyleLoader::stylesAreSortedCaseInsensitively()
{
    // Deliberately chose names whose ASCII order differs from
    // case-insensitive order: ASCII puts all uppercase before all
    // lowercase, so a plain byte comparison would sort them
    // {Mango, Zebra, apple, banana}.  The loader sorts by
    // QString::compare(..., Qt::CaseInsensitive), which produces the
    // natural human ordering below.
    makeStyleFile(QStringLiteral("Zebra.qss"));
    makeStyleFile(QStringLiteral("apple.qss"));
    makeStyleFile(QStringLiteral("Mango.qss"));
    makeStyleFile(QStringLiteral("banana.qss"));

    StyleLoader loader(toFsPath(m_currentDir));
    const QStringList names = loader.availableStyles();
    QCOMPARE(names.size(), 4);
    QCOMPARE(names[0], QStringLiteral("apple"));
    QCOMPARE(names[1], QStringLiteral("banana"));
    QCOMPARE(names[2], QStringLiteral("Mango"));
    QCOMPARE(names[3], QStringLiteral("Zebra"));
}

// -----------------------------------------------------------------------------
//  pathFor
// -----------------------------------------------------------------------------

void TestStyleLoader::pathForExactCase()
{
    // The contract for pathFor is: resolve exactly what
    // availableStyles() reports.  The combo box item data is always the
    // on-disk stem, so callers never pass a mismatched case.
    makeStyleFile(QStringLiteral("dark.qss"));

    StyleLoader loader(toFsPath(m_currentDir));
    const auto path = loader.pathFor(QStringLiteral("dark"));
    QVERIFY(!path.empty());
    QCOMPARE(QFileInfo(fsToQString(path)).fileName(),
             QStringLiteral("dark.qss"));
}

void TestStyleLoader::pathForMissingReturnsEmpty()
{
    StyleLoader loader(toFsPath(m_currentDir));
    QVERIFY(loader.pathFor(QStringLiteral("nonexistent")).empty());
}

void TestStyleLoader::pathForEmptyReturnsEmpty()
{
    // Empty name is a query for "the reset-to-default entry", which has
    // no file.  Returning an empty path here rather than the directory
    // itself keeps the empty-name case identical between pathFor() and
    // apply(), so callers can rely on a single contract.
    makeStyleFile(QStringLiteral("dark.qss"));

    StyleLoader loader(toFsPath(m_currentDir));
    QVERIFY(loader.pathFor(QString()).empty());
}

// -----------------------------------------------------------------------------
//  Apply contract
// -----------------------------------------------------------------------------

void TestStyleLoader::applyEmptyNameClearsStylesheet()
{
    // Precondition: something is currently applied.  Without this, the
    // test would pass even if apply() never cleared the stylesheet —
    // the assertion would compare two empty strings.
    qApp->setStyleSheet(QStringLiteral("QWidget { background: red; }"));
    QVERIFY(!qApp->styleSheet().isEmpty());

    StyleLoader loader(toFsPath(m_currentDir));
    loader.apply(QString());
    QVERIFY(qApp->styleSheet().isEmpty());
}

void TestStyleLoader::applyMissingFileClearsStylesheet()
{
    qApp->setStyleSheet(QStringLiteral("QWidget { background: red; }"));
    QVERIFY(!qApp->styleSheet().isEmpty());

    StyleLoader loader(toFsPath(m_currentDir));
    loader.apply(QStringLiteral("does-not-exist"));
    QVERIFY(qApp->styleSheet().isEmpty());
}

void TestStyleLoader::applyEmptyFileClearsStylesheet()
{
    // An empty .qss file is a valid file with no rules.  Applying it
    // verbatim would be equivalent to clearing the stylesheet, so the
    // loader takes the explicit path — but the outcome is the same, and
    // this test locks that in against a future change that might
    // distinguish "file is empty" from "file has rules".
    makeStyleFile(QStringLiteral("empty.qss"), QByteArray());

    qApp->setStyleSheet(QStringLiteral("QWidget { background: red; }"));
    QVERIFY(!qApp->styleSheet().isEmpty());

    StyleLoader loader(toFsPath(m_currentDir));
    loader.apply(QStringLiteral("empty"));
    QVERIFY(qApp->styleSheet().isEmpty());
}

void TestStyleLoader::applyValidFileSetsStylesheet()
{
    const QByteArray content = "QWidget { background: #123456; }";
    makeStyleFile(QStringLiteral("dark.qss"), content);

    // Precondition: a different stylesheet is in place so a no-op apply
    // would leave the assertion comparing the old content.
    qApp->setStyleSheet(QStringLiteral("QWidget { background: red; }"));

    StyleLoader loader(toFsPath(m_currentDir));
    loader.apply(QStringLiteral("dark"));
    QCOMPARE(qApp->styleSheet(), QString::fromLatin1(content));
}

QTEST_MAIN(TestStyleLoader)
#include "tst_styleloader.moc"
