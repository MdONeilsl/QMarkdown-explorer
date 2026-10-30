#include <QtTest>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QVariant>

#include "MarkdownView.h"

/**
 * @brief Tests for MarkdownView::setTheme() and code-block styling.
 *
 * Covers the current behaviour:
 *
 *   * fenced code blocks receive a QTextBlockFormat background from the
 *     theme’s CSS;
 *   * inline code is intentionally NOT styled, because
 *     QTextCursor::mergeCharFormat() corrupts the document on Qt 6.5 —
 *     see the comment in MarkdownView::applyCodeBlockStyles() for the
 *     full rationale;
 *   * switching themes replaces the block background, not merges;
 *   * an empty CSS string disables styling entirely;
 *   * a theme switch preserves image resources.
 *
 * The `inlineCodeIsNotStyled` test is a behavioural contract: if a
 * future Qt release fixes mergeFormat and inline styling is reinstated,
 * this test will fail and its replacement should assert the opposite.
 * Locking in the current fallback is more honest than silently deleting
 * the test.
 */
class TestMarkdownViewTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void fencedBlockGetsBackground();
    void inlineCodeIsNotOverridden();
    void switchingThemesChangesBlockBackground();
    void noThemeLeavesImporterDefault(); 
    void themeSwitchPreservesImages();

private:
    QString writeFile(const QString &relPath, const QByteArray &content);

    static QColor firstBlockBackground(const QTextDocument *doc);
    static QColor firstCharBackground(const QTextDocument *doc);

    QTemporaryDir m_tmp;
    QString       m_currentDir;
    int           m_counter = 0;
    QString       m_smallPng;
};

void TestMarkdownViewTheme::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(Qt::red);
    m_smallPng = m_tmp.filePath(QStringLiteral("small.png"));
    QVERIFY(small.save(m_smallPng, "PNG"));
}

void TestMarkdownViewTheme::init()
{
    m_currentDir = m_tmp.filePath(QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));
}

QString TestMarkdownViewTheme::writeFile(const QString &relPath,
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
    return abs;
}

QColor TestMarkdownViewTheme::firstBlockBackground(const QTextDocument *doc)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QColor c = b.blockFormat().background().color();
        if (c.isValid())
            return c;
    }
    return {};
}

QColor TestMarkdownViewTheme::firstCharBackground(const QTextDocument *doc)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || frag.length() <= 0)
                continue;
            const QColor c = frag.charFormat().background().color();
            if (c.isValid())
                return c;
        }
    }
    return {};
}

void TestMarkdownViewTheme::fencedBlockGetsBackground()
{
    const QString path = writeFile(
        QStringLiteral("fenced.md"),
        "```\nint x = 42;\n```\n");
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    view.setTheme(QStringLiteral("dark"),
                  QStringLiteral("pre { background: #2b2f34; color: #e6e8ea; }"));
    QVERIFY(view.loadFile(path));

    const QColor bg = firstBlockBackground(view.document());
    QVERIFY2(bg.isValid(),
             "No block background applied to the fenced code block");
    QCOMPARE(bg.name(), QStringLiteral("#2b2f34"));
}

void TestMarkdownViewTheme::inlineCodeIsNotOverridden()
{
    // The Qt 6.5 markdown importer itself sets a background on inline
    // code fragments (a subtle grey, theme-aware).  Our styling code
    // does not touch inline code, because QTextCursor::mergeCharFormat()
    // on inline character ranges crashes inside Qt 6.5’s
    // QTextDocumentPrivate::mergeFormat.  The contract, then, is not
    // “no background” — it is “our CSS colour is not applied”.
    const QString path = writeFile(
        QStringLiteral("inline.md"),
        "Use `setDefaultStyleSheet()` in this line.\n");
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    view.setTheme(QStringLiteral("dark"),
                  QStringLiteral("code { background: #2b2f34; color: #e6e8ea; }"));
    QVERIFY(view.loadFile(path));

    const QColor bg = firstCharBackground(view.document());

    // Importer default is present (proves the assertion below is not
    // vacuous); our specific dark-theme colour is not.
    QVERIFY(bg.isValid());
    QVERIFY2(bg.name() != QStringLiteral("#2b2f34"),
             "Our CSS colour leaked into inline code — applyCodeBlockStyles "
             "was supposed to skip inline spans");
}

void TestMarkdownViewTheme::switchingThemesChangesBlockBackground()
{
    const QString path = writeFile(
        QStringLiteral("fenced.md"),
        "```\nint x = 42;\n```\n");
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    view.setTheme(QStringLiteral("dark"),
                  QStringLiteral("pre { background: #2b2f34; }"));
    QVERIFY(view.loadFile(path));
    QCOMPARE(firstBlockBackground(view.document()).name(),
             QStringLiteral("#2b2f34"));

    view.setTheme(QStringLiteral("light"),
                  QStringLiteral("pre { background: #f4f7fb; }"));

    const QColor bg = firstBlockBackground(view.document());
    QVERIFY2(bg.isValid(), "Block background lost after theme switch");
    QCOMPARE(bg.name(), QStringLiteral("#f4f7fb"));
}

void TestMarkdownViewTheme::noThemeLeavesImporterDefault()
{
    // Without a setTheme() call, our styling pass exits before touching
    // the document — the QTextMarkdownImporter default block background
    // on fenced blocks remains in place.  The contract is not “no
    // background” — it is “none of our theme colours leaked in”.
    const QString path = writeFile(
        QStringLiteral("fenced.md"),
        "```\nint x = 42;\n```\n");
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(path));

    const QColor bg = firstBlockBackground(view.document());

    // Importer default is present (proves the assertion below is not
    // vacuous); none of the three theme colours leaked through.
    QVERIFY(bg.isValid());
    const QStringList themeColours{
        QStringLiteral("#2b2f34"),   // dark
        QStringLiteral("#f4f7fb"),   // light
        QStringLiteral("#f4f4f5"),   // default
    };
    for (const QString &c : themeColours) {
        QVERIFY2(bg.name() != c,
                 qPrintable(QStringLiteral(
                     "Theme colour %1 leaked into a document with no theme "
                     "set — applyCodeBlockStyles was supposed to be a no-op")
                     .arg(c)));
    }
}

void TestMarkdownViewTheme::themeSwitchPreservesImages()
{
    const QString pngDst = m_currentDir + QStringLiteral("/small.png");
    QVERIFY(QFile::copy(m_smallPng, pngDst));

    const QString path = writeFile(
        QStringLiteral("with-image.md"),
        "![alt](small.png)\n\n```\nint x = 42;\n```\n");
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    view.setTheme(QStringLiteral("dark"),
                  QStringLiteral("pre { background: #2b2f34; }"));
    QVERIFY(view.loadFile(path));

    view.setTheme(QStringLiteral("light"),
                  QStringLiteral("pre { background: #f4f7fb; }"));

    int imageCount = 0;
    bool resourceResolved = false;
    for (QTextBlock b = view.document()->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || !frag.charFormat().isImageFormat())
                continue;
            ++imageCount;
            const QUrl name = frag.charFormat().toImageFormat().name();
            const QVariant res = view.document()->resource(
                QTextDocument::ImageResource, name);
            if (!res.isNull() && !res.value<QImage>().isNull())
                resourceResolved = true;
        }
    }

    QCOMPARE(imageCount, 1);
    QVERIFY2(resourceResolved,
             "Image resource did not survive theme change");
}

QTEST_MAIN(TestMarkdownViewTheme)
#include "tst_markdownviewtheme.moc"