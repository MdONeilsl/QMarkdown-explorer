#include <QtTest>

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdlib>

namespace {

struct AppResult
{
    QProcess::ExitStatus exitStatus  = QProcess::NormalExit;
    int                   exitCode    = -1;
    QByteArray            stdoutBytes;
    QByteArray            stderrBytes;
};

} // namespace

class TestCli : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void listLanguagesExitsZero();
    void listLanguagesFormatIsCodeTabName();
    void listLanguagesContainsKnownCatalogues();

    void listStylesExitsZero();
    void listStylesFormatIsOneNamePerLine();
    void listStylesContainsKnownStyles();

    void versionExitsZero();
    void versionContainsAppNameAndVersion();

    void helpExitsZero();
    void helpListsAllOptions();

    void invalidFolderExitsNonZero();
    void invalidFolderReportsOnStderr();
    void unknownOptionExitsNonZero();
    void missingFolderArgumentExitsNonZero();

private:
    AppResult runApp(const QStringList &args, int timeoutMs = 15000);

    QString m_appPath;
    QTemporaryDir m_tmp;
};

void TestCli::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    // The application binary path is provided by CTest via -E env,
    // because QTEST_GUILESS_MAIN treats positional arguments as test
    // function names.  See tests/CMakeLists.txt.
    const QByteArray envPath = qgetenv("QMD_APP_PATH");
    QVERIFY2(!envPath.isEmpty(),
             "QMD_APP_PATH environment variable must be set by CTest. "
             "Check that tests/CMakeLists.txt uses the -E env wrapper "
             "for tst_cli.");
    m_appPath = QString::fromLocal8Bit(envPath);
    QVERIFY2(QFileInfo::exists(m_appPath),
             qPrintable(QStringLiteral("Application binary not found: %1")
                            .arg(m_appPath)));

    qInfo().noquote() << "Application binary:" << m_appPath;
}

AppResult TestCli::runApp(const QStringList &args, int timeoutMs)
{
    QProcess process;
    process.setWorkingDirectory(QFileInfo(m_appPath).absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process.setProcessEnvironment(env);

    process.start(m_appPath, args);
    if (!process.waitForStarted(timeoutMs)) {
        qWarning() << "Failed to start:" << m_appPath
                   << "error:" << process.errorString();
        AppResult r;
        r.exitStatus = process.exitStatus();
        r.exitCode   = -1;
        return r;
    }

    if (!process.waitForFinished(timeoutMs)) {
        qWarning() << "Timed out after" << timeoutMs << "ms; killing";
        process.kill();
        process.waitForFinished(2000);
    }

    AppResult r;
    r.exitStatus  = process.exitStatus();
    r.exitCode    = process.exitCode();
    r.stdoutBytes = process.readAllStandardOutput();
    r.stderrBytes = process.readAllStandardError();
    return r;
}

void TestCli::listLanguagesExitsZero()
{
    const AppResult p = runApp({QStringLiteral("--list-languages")});
    QCOMPARE(p.exitStatus, QProcess::NormalExit);
    QCOMPARE(p.exitCode, 0);
}

void TestCli::listLanguagesFormatIsCodeTabName()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const QFileInfo translationsDir(
        QFileInfo(m_appPath).absolutePath() + QStringLiteral("/translations"));
    if (!translationsDir.isDir())
        QSKIP("No translations/ directory next to the binary");

    const AppResult p = runApp({QStringLiteral("--list-languages")});
    QCOMPARE(p.exitCode, 0);

    const QString out = QString::fromUtf8(p.stdoutBytes);
    const QStringList lines = out.split(u'\n', Qt::SkipEmptyParts);
    QVERIFY(!lines.isEmpty());

    for (const QString &line : lines) {
        const int tab = line.indexOf(u'\t');
        QVERIFY2(tab > 0,
                 qPrintable(QStringLiteral(
                     "Line has no code<TAB>name separator: [%1]")
                     .arg(line)));
        QVERIFY(tab < line.size() - 1);
    }
}

void TestCli::listLanguagesContainsKnownCatalogues()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const QFileInfo translationsDir(
        QFileInfo(m_appPath).absolutePath() + QStringLiteral("/translations"));
    if (!translationsDir.isDir())
        QSKIP("No translations/ directory next to the binary");

    const AppResult p = runApp({QStringLiteral("--list-languages")});
    QCOMPARE(p.exitCode, 0);

    const QString out = QString::fromUtf8(p.stdoutBytes);
    QVERIFY2(out.contains(QStringLiteral("de\t")),
             "German catalogue not listed — the glob or the build failed");
    QVERIFY2(out.contains(QStringLiteral("fr\t")),
             "French catalogue not listed");
}

void TestCli::listStylesExitsZero()
{
    const AppResult p = runApp({QStringLiteral("--list-styles")});
    QCOMPARE(p.exitStatus, QProcess::NormalExit);
    QCOMPARE(p.exitCode, 0);
}

void TestCli::listStylesFormatIsOneNamePerLine()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const QFileInfo stylesDir(
        QFileInfo(m_appPath).absolutePath() + QStringLiteral("/styles"));
    if (!stylesDir.isDir())
        QSKIP("No styles/ directory next to the binary");

    const AppResult p = runApp({QStringLiteral("--list-styles")});
    QCOMPARE(p.exitCode, 0);

    const QString out = QString::fromUtf8(p.stdoutBytes);
    const QStringList lines = out.split(u'\n', Qt::SkipEmptyParts);
    QVERIFY(!lines.isEmpty());

    for (const QString &line : lines) {
        QVERIFY2(!line.contains(u'\t'),
                 qPrintable(QStringLiteral("Style line contains a tab: [%1]")
                                .arg(line)));
        QVERIFY2(!line.endsWith(QStringLiteral(".qss")),
                 qPrintable(QStringLiteral("Style line has extension: [%1]")
                                .arg(line)));
        QVERIFY2(!line.contains(u' '),
                 qPrintable(QStringLiteral("Style line contains a space: [%1]")
                                .arg(line)));
    }
}

void TestCli::listStylesContainsKnownStyles()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const QFileInfo stylesDir(
        QFileInfo(m_appPath).absolutePath() + QStringLiteral("/styles"));
    if (!stylesDir.isDir())
        QSKIP("No styles/ directory next to the binary");

    const AppResult p = runApp({QStringLiteral("--list-styles")});
    QCOMPARE(p.exitCode, 0);

    const QString out = QString::fromUtf8(p.stdoutBytes);
    QVERIFY2(out.contains(QStringLiteral("dark")),  "dark.qss not listed");
    QVERIFY2(out.contains(QStringLiteral("light")), "light.qss not listed");
    QVERIFY2(out.contains(QStringLiteral("default")),
             "default.qss not listed");
}

void TestCli::versionExitsZero()
{
    const AppResult p = runApp({QStringLiteral("--version")});
    QCOMPARE(p.exitStatus, QProcess::NormalExit);
    QCOMPARE(p.exitCode, 0);
}

void TestCli::versionContainsAppNameAndVersion()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const AppResult p = runApp({QStringLiteral("--version")});
    const QString out = QString::fromUtf8(p.stdoutBytes);

    QVERIFY2(out.contains(QStringLiteral("QMarkdown-explorer")),
             qPrintable(QStringLiteral(
                 "--version output does not contain app name: [%1]")
                 .arg(out.trimmed())));
    QVERIFY2(out.contains(QRegularExpression(QStringLiteral("\\d+\\.\\d+"))),
             qPrintable(QStringLiteral(
                 "--version output does not contain a version number: [%1]")
                 .arg(out.trimmed())));
}

void TestCli::helpExitsZero()
{
    const AppResult p = runApp({QStringLiteral("--help")});
    QCOMPARE(p.exitStatus, QProcess::NormalExit);
    QCOMPARE(p.exitCode, 0);
}

void TestCli::helpListsAllOptions()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stdout");
#endif
    const AppResult p = runApp({QStringLiteral("--help")});
    const QString out = QString::fromUtf8(p.stdoutBytes);

    const QStringList expected{
        QStringLiteral("--folder"),
        QStringLiteral("--language"),
        QStringLiteral("--style"),
        QStringLiteral("--no-picker"),
        QStringLiteral("--list-languages"),
        QStringLiteral("--list-styles"),
        QStringLiteral("--version"),
        QStringLiteral("--help"),
    };
    for (const QString &opt : expected) {
        QVERIFY2(out.contains(opt),
                 qPrintable(QStringLiteral(
                     "--help output is missing %1").arg(opt)));
    }
}

void TestCli::invalidFolderExitsNonZero()
{
    const QString bogus =
        m_tmp.filePath(QStringLiteral("never-created/deeper"));

    const AppResult p = runApp({
        QStringLiteral("--folder"), bogus,
        QStringLiteral("--no-picker"),
    });

    QVERIFY2(p.exitCode != 0,
             "Invalid --folder did not produce a non-zero exit code");
}

void TestCli::invalidFolderReportsOnStderr()
{
#ifdef Q_OS_WIN
    QSKIP("Windows GUI-subsystem apps do not produce captureable stderr");
#endif
    const QString bogus =
        m_tmp.filePath(QStringLiteral("never-created/deeper"));

    const AppResult p = runApp({
        QStringLiteral("--folder"), bogus,
        QStringLiteral("--no-picker"),
    });

    const QString err = QString::fromUtf8(p.stderrBytes);
    QVERIFY2(!err.isEmpty(),
             "Invalid --folder produced no stderr message");
    QVERIFY2(err.contains(QStringLiteral("--folder")),
             qPrintable(QStringLiteral(
                 "stderr does not mention --folder: [%1]").arg(err.trimmed())));
}

void TestCli::unknownOptionExitsNonZero()
{
    const AppResult p = runApp({QStringLiteral("--this-option-does-not-exist")});
    QVERIFY(p.exitCode != 0);
}

void TestCli::missingFolderArgumentExitsNonZero()
{
    const AppResult p = runApp({QStringLiteral("--folder")});
    QVERIFY(p.exitCode != 0);
}

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
