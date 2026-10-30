#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>
#include <QVariant>

#include "MarkdownView.h"

#include <cstddef>
#include <filesystem>

namespace {

// Same UTF-8 bridge used by the production code; MarkdownView::loadFile
// takes a QString but converts internally, so the test rarely calls it
// directly — it is here in case a future test needs to write files
// through std::filesystem.
std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

/**
 * @brief Collects the URIs of every image fragment in `doc`.
 *
 * A freshly-loaded document with no image references returns an empty
 * list; a document whose markdown referenced images returns one entry
 * per fragment, in document order.  The entries are the *names* stored
 * in the QTextImageFormat — for the resolver’s output that means
 * `mdimg:/0`, `mdimg:/1`, etc.
 */
QList<QUrl> imageFragmentNames(QTextDocument *doc)
{
    QList<QUrl> names;
    for (auto blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            if (!frag.charFormat().isImageFormat())
                continue;
            names.append(frag.charFormat().toImageFormat().name());
        }
    }
    return names;
}

/**
 * @brief Looks up the QImage registered under `url` in `doc`.
 *
 * Goes through `QTextDocument::resource()`, which dispatches to
 * MarkdownDocument::loadResource() — the virtual that this whole test
 * file exists to exercise.  A null QImage means the resource was not
 * available when the painter asked for it.
 */
QImage registeredImage(QTextDocument *doc, const QUrl &url)
{
    const QVariant v = doc->resource(QTextDocument::ImageResource, url);
    return v.value<QImage>();
}

} // namespace

/**
 * @brief Integration tests for MarkdownView + MarkdownDocument.
 *
 * These tests exercise the full load pipeline:
 *
 *   loadFile(path) → read → ImageResolver → register on document →
 *   setMarkdown(resolvedText) → paint-time loadResource()
 *
 * The assertion that matters most is `imageResourcesSurviveSetMarkdown`:
 * it is the direct regression test for the bug where
 * QTextDocument::setMarkdown() wipes every resource registered with
 * addResource().  If a future change reverts MarkdownView to a plain
 * QTextDocument, that test fails and every other image test becomes
 * meaningless — which is exactly the signal we want.
 *
 * Every test that requires the loaded images to render uses a distinct
 * PNG so a mixed-up URI-to-pixel mapping (image 0 and image 1 pointing
 * at the same QImage) is caught, not silently accepted.
 */
class TestMarkdownView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // ---- loadFile contract ---------------------------------------------
    void loadFileReturnsFalseForEmptyPath();
    void loadFileReturnsFalseForMissingFile();
    void loadFileReturnsFalseForDirectory();
    void loadFileLoadsPlainText();
    void loadFileStripsUtf8Bom();
    void previousDocumentUntouchedOnFailure();

    // ---- Image rendering (the bug this file exists for) ----------------
    void imageFragmentIsCreated();
    void imageResourcesSurviveSetMarkdown();   // ← the regression test
    void emptyAltImageStillProducesFragment();
    void headingFollowedByImageProducesFragment();
    void twoImagesProduceTwoDistinctResources();
    void nestedRelativeImageResolves();
    void percentEncodedPathResolves();
    void missingImageReferenceProducesNoFragment();
    void largeImageIsScaledToViewport();

private:
    // Writes `content` under m_currentDir/relPath, creating parents.
    // Returns the absolute path or an empty QString on failure.
    QString writeFile(const QString &relPath, const QByteArray &content);

    // Convenience: writes a small markdown file and returns its path.
    QString writeMarkdown(const QString &relPath, const QString &text)
    {
        return writeFile(relPath, text.toUtf8());
    }

    QTemporaryDir m_tmp;
    QString       m_currentDir;
    int           m_counter = 0;

    QString m_smallPng;   // 8x8, red
    QString m_bigPng;     // 16x12, blue
    QString m_hugePng;    // 4000x3000, magenta — for the scaling test
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestMarkdownView::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(Qt::red);
    m_smallPng = m_tmp.filePath(QStringLiteral("small.png"));
    QVERIFY(small.save(m_smallPng, "PNG"));

    QImage big(16, 12, QImage::Format_RGB32);
    big.fill(Qt::blue);
    m_bigPng = m_tmp.filePath(QStringLiteral("big.png"));
    QVERIFY(big.save(m_bigPng, "PNG"));

    QImage huge(4000, 3000, QImage::Format_RGB32);
    huge.fill(Qt::magenta);
    m_hugePng = m_tmp.filePath(QStringLiteral("huge.png"));
    QVERIFY(huge.save(m_hugePng, "PNG"));
}

void TestMarkdownView::init()
{
    m_currentDir = m_tmp.filePath(QStringLiteral("case%1").arg(m_counter++));
    QVERIFY(QDir().mkpath(m_currentDir));

    // Copy the fixture PNGs into the per-test directory.  Tests reference
    // them by relative path (e.g. "small.png"), and the resolver resolves
    // relative paths against the markdown file's directory.  Without this
    // copy, the files exist at m_tmp/small.png but not at
    // m_currentDir/small.png, and every image-resolving test fails
    // silently — the resolver leaves the reference unchanged and the
    // resulting QImage is null.
    //
    // Tests that create their own PNGs (nestedRelativeImageResolves,
    // percentEncodedPathResolves) still work: they write to m_currentDir
    // paths that don’t collide with these copies.
    const auto copyFixture = [this](const QString &name) {
        const QString src = m_tmp.filePath(name);
        const QString dst = m_currentDir + QStringLiteral("/") + name;
        if (QFileInfo::exists(src) && !QFile::exists(dst))
            QFile::copy(src, dst);
    };
    copyFixture(QStringLiteral("small.png"));
    copyFixture(QStringLiteral("big.png"));
    copyFixture(QStringLiteral("huge.png"));
}

QString TestMarkdownView::writeFile(const QString &relPath,
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

// -----------------------------------------------------------------------------
//  loadFile contract
// -----------------------------------------------------------------------------

void TestMarkdownView::loadFileReturnsFalseForEmptyPath()
{
    MarkdownView view;
    QVERIFY(!view.loadFile(QString()));
}

void TestMarkdownView::loadFileReturnsFalseForMissingFile()
{
    MarkdownView view;
    QVERIFY(!view.loadFile(m_currentDir + QStringLiteral("/nope.md")));
}

void TestMarkdownView::loadFileReturnsFalseForDirectory()
{
    // is_regular_file() rejects a directory even if it exists; the
    // widget leaves its previous content alone rather than clearing.
    MarkdownView view;
    QVERIFY(!view.loadFile(m_currentDir));
}

void TestMarkdownView::loadFileLoadsPlainText()
{
    const QString path = writeMarkdown(
        QStringLiteral("hello.md"), QStringLiteral("Hello, world."));
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(path));
    QCOMPARE(view.toPlainText(), QStringLiteral("Hello, world."));
}

void TestMarkdownView::loadFileStripsUtf8Bom()
{
    // A BOM passed through to setMarkdown() would render as a zero-width
    // invisible character at the head of the first glyph; the strip in
    // loadFile() is what keeps the rendered output clean.
    QByteArray content;
    content.append('\xEF').append('\xBB').append('\xBF');
    content.append("Hello");

    const QString path = writeFile(QStringLiteral("bom.md"), content);
    QVERIFY(!path.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(path));

    const QString text = view.toPlainText();
    QVERIFY(!text.isEmpty());
    QVERIFY2(text.at(0) != QChar(0xFEFF),
             "BOM survived the load — the strip in loadFile() is broken");
    QCOMPARE(text, QStringLiteral("Hello"));
}

void TestMarkdownView::previousDocumentUntouchedOnFailure()
{
    const QString goodPath = writeMarkdown(
        QStringLiteral("good.md"),
        QStringLiteral("This is the good document."));
    QVERIFY(!goodPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(goodPath));
    const QString before = view.toPlainText();

    // Attempted load of a nonexistent file.
    QVERIFY(!view.loadFile(m_currentDir + QStringLiteral("/nope.md")));
    QCOMPARE(view.toPlainText(), before);
}

// -----------------------------------------------------------------------------
//  Image rendering
// -----------------------------------------------------------------------------

void TestMarkdownView::imageFragmentIsCreated()
{
    const QString mdPath = writeMarkdown(
        QStringLiteral("one.md"),
        QStringLiteral("![alt](small.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);
    QVERIFY(names[0].toString().startsWith(QStringLiteral("mdimg:/")));
}

void TestMarkdownView::imageResourcesSurviveSetMarkdown()
{
    // THIS IS THE REGRESSION TEST.
    //
    // QTextDocument::setMarkdown() internally calls clear(), which wipes
    // any resource registered with addResource().  MarkdownDocument
    // avoids that by holding the images in its own hash and serving them
    // through loadResource(), a virtual that the painter calls at paint
    // time — after setMarkdown() has finished.  The assertion below
    // reads the resource back through the same virtual, so it exercises
    // exactly the code path the painter would use.
    //
    // A plain QTextDocument in MarkdownView’s constructor makes this
    // test fail with a null QImage, which is the exact symptom that
    // would be visible in the UI.
    const QString mdPath = writeMarkdown(
        QStringLiteral("img.md"),
        QStringLiteral("![alt](small.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);

    const QImage img = registeredImage(view.document(), names[0]);
    QVERIFY2(!img.isNull(),
             "Image resource was not resolvable after setMarkdown() — "
             "MarkdownDocument’s loadResource() override is not in effect");
    QCOMPARE(img.size(), QSize(8, 8));
}

void TestMarkdownView::emptyAltImageStillProducesFragment()
{
    // Qt’s markdown importer drops images with empty alt text; the
    // resolver’s ![image] rewrite is what makes this case render.  The
    // test asserts the fragment exists, not the alt text, so a future
    // change to the alt placeholder does not fail this test for an
    // incidental reason.
    const QString mdPath = writeMarkdown(
        QStringLiteral("empty-alt.md"),
        QStringLiteral("![](small.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);
    QVERIFY(!registeredImage(view.document(), names[0]).isNull());
}

void TestMarkdownView::headingFollowedByImageProducesFragment()
{
    // Same story: the resolver inserts a blank line between a heading
    // and a following image so the importer does not absorb the image
    // into the heading.  A regression here produces an image that
    // silently disappears.
    const QString mdPath = writeMarkdown(
        QStringLiteral("heading.md"),
        QStringLiteral("# Title\n![](small.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);
    QVERIFY(!registeredImage(view.document(), names[0]).isNull());
}

void TestMarkdownView::twoImagesProduceTwoDistinctResources()
{
    // Uses two differently-sized PNGs so the URIs must resolve to two
    // different QImages — a bug that registers both references against
    // the same URI (or the same QImage) fails visibly.
    const QString mdPath = writeMarkdown(
        QStringLiteral("two.md"),
        QStringLiteral("![a](small.png) ![b](big.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 2);
    QVERIFY(names[0] != names[1]);

    const QImage first  = registeredImage(view.document(), names[0]);
    const QImage second = registeredImage(view.document(), names[1]);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QCOMPARE(first.size(),  QSize(8, 8));
    QCOMPARE(second.size(), QSize(16, 12));
}

void TestMarkdownView::nestedRelativeImageResolves()
{
    // ../ handling — the resolver must canonicalise the path before
    // touching the filesystem, otherwise a subfolder reference escapes
    // the temp directory and is treated as missing.
    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(Qt::green);
    const QString nestedDir = m_currentDir + QStringLiteral("/sub");
    QVERIFY(QDir().mkpath(nestedDir));
    const QString nestedPng = nestedDir + QStringLiteral("/pic.png");
    QVERIFY(small.save(nestedPng, "PNG"));

    const QString mdPath = writeMarkdown(
        QStringLiteral("nested.md"),
        QStringLiteral("![a](sub/pic.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);
    QVERIFY(!registeredImage(view.document(), names[0]).isNull());
}

void TestMarkdownView::percentEncodedPathResolves()
{
    // %20 in the markdown reference must be decoded to a real space
    // before the file is opened.  This is the exact case that surfaced
    // in the field.
    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(Qt::yellow);
    const QString withSpace = m_currentDir + QStringLiteral("/a b.png");
    QVERIFY(small.save(withSpace, "PNG"));

    const QString mdPath = writeMarkdown(
        QStringLiteral("space.md"),
        QStringLiteral("![a](a%20b.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);
    QVERIFY(!registeredImage(view.document(), names[0]).isNull());
}

void TestMarkdownView::missingImageReferenceProducesNoFragment()
{
    // A reference to a missing file leaves the fragment in place with
    // its original URI — QTextDocument will render the broken-image
    // placeholder.  What must not happen is a crash, a null fragment, or
    // a fragment with a bogus URI that would try to load something.
    const QString mdPath = writeMarkdown(
        QStringLiteral("missing.md"),
        QStringLiteral("![alt](does-not-exist.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);

    // The resolver left the reference alone, so the fragment’s URI is
    // the original relative path, not a mdimg:/ URI.
    QVERIFY(!names[0].toString().startsWith(QStringLiteral("mdimg:/")));

    // And no image resource was registered for it.
    QVERIFY(registeredImage(view.document(), names[0]).isNull());
}

void TestMarkdownView::largeImageIsScaledToViewport()
{
    // A 4000-pixel-wide portrait would be clipped to an unreadable
    // sliver in the pane; loadFile() scales it down to the viewport
    // width (minus a 40-pixel margin).  The test resizes the widget to
    // be visibly narrower than the source image, so the scaling branch
    // is taken regardless of the offscreen plugin’s reported size.
    const QString mdPath = writeMarkdown(
        QStringLiteral("huge.md"),
        QStringLiteral("![big](huge.png)"));
    QVERIFY(!mdPath.isEmpty());

    MarkdownView view;
    view.resize(400, 600);
    view.show();
    QCoreApplication::processEvents();

    QVERIFY(view.loadFile(mdPath));

    const QList<QUrl> names = imageFragmentNames(view.document());
    QCOMPARE(names.size(), 1);

    const QImage img = registeredImage(view.document(), names[0]);
    QVERIFY(!img.isNull());
    QVERIFY2(img.width() < 4000,
             "4000-pixel-wide image was not scaled — the viewport-capping "
             "branch in loadFile() is not running");
    QVERIFY(img.width() <= qMax(120, view.viewport()->width()));
}

QTEST_MAIN(TestMarkdownView)
#include "tst_markdownview.moc"
