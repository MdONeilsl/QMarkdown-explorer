#include <QtTest>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include "ImageResolver.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

// Same UTF-8 bridge used by the production code; `QString::toUtf8()`
// yields a byte sequence that std::filesystem::path reads natively on
// every platform.
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
 * @brief Tests for ImageResolver.
 *
 * Organisation:
 *
 *   * rewrite rules     — every transformation the resolver is documented
 *                         to perform, verified against its exact output;
 *   * leave-alone rules — every reference the resolver is documented to
 *                         leave untouched, verified byte-for-byte;
 *   * integration       — resolved images carry a decoded QImage and the
 *                         result is byte-identical when nothing resolves.
 *
 * Exact-output assertions (QCOMPARE on the full text) are used instead of
 * "contains" checks so that any silent change to the rewrite logic — an
 * added space, a different URI scheme, an extra rewrite pass — fails the
 * test with a diff rather than passing quietly.
 */
class TestImageResolver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- Rewrite rules --------------------------------------------------
    void simpleRelativeImageIsResolved();
    void emptyAltIsRewritten();
    void headingBeforeImageIsSeparated();
    void nestedRelativeImageIsResolved();
    void multipleImagesAreNumberedSequentially();
    void inlineImageInsideParagraphIsUntouched();
    void percentEncodedPathIsDecoded();
    void angleBracketPathWithSpaces();
    void htmlImgDoubleQuoted();
    void htmlImgSingleQuoted();
    void htmlImgCaseInsensitive();

    // ---- Leave-alone rules ----------------------------------------------
    void emptyTextIsUntouched();
    void absoluteUrlIsUntouched();
    void dataUrlIsUntouched();
    void fileUrlIsUntouched();
    void protocolRelativeUrlIsUntouched();
    void windowsDriveIsUntouched();
    void missingFileIsLeftAlone();
    void corruptFileIsLeftAlone();
    void markdownWithoutImagesIsByteIdentical();

    // ---- Integration ----------------------------------------------------
    void resolvedImagesCarryDecodedPixels();

private:
    // Writes `content` to `m_tmp/<relPath>`, creating parent directories.
    // Returns the absolute path, or an empty QString on failure.
    QString writeFile(const QString &relPath, const QByteArray &content);

    // Convenience wrapper around ImageResolver with m_tmp as baseDir.
    ResolveResult resolve(const QString &markdown) const
    {
        return ImageResolver::resolveRelativeImages(
            markdown, toFsPath(m_tmp.path()));
    }

    QTemporaryDir m_tmp;

    // Absolute paths to two valid PNGs of distinct sizes so multi-image
    // tests can verify that each one was decoded, not that they all
    // collapsed to the same QImage.
    QString m_smallPngPath;
    QString m_bigPngPath;
    QSize   m_smallPngSize{8, 8};
    QSize   m_bigPngSize{16, 12};
};

// -----------------------------------------------------------------------------
//  Fixture
// -----------------------------------------------------------------------------

void TestImageResolver::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    QImage small(m_smallPngSize, QImage::Format_RGB32);
    small.fill(Qt::red);
    m_smallPngPath = m_tmp.filePath(QStringLiteral("small.png"));
    QVERIFY(small.save(m_smallPngPath, "PNG"));

    QImage big(m_bigPngSize, QImage::Format_RGB32);
    big.fill(Qt::blue);
    m_bigPngPath = m_tmp.filePath(QStringLiteral("big.png"));
    QVERIFY(big.save(m_bigPngPath, "PNG"));
}

QString TestImageResolver::writeFile(const QString &relPath,
                                     const QByteArray &content)
{
    const QString abs = m_tmp.filePath(relPath);
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
//  Rewrite rules
// -----------------------------------------------------------------------------

void TestImageResolver::simpleRelativeImageIsResolved()
{
    const auto r = resolve(QStringLiteral("![alt](small.png)"));

    QCOMPARE(r.text, QStringLiteral("![alt](mdimg:/0)"));
    QCOMPARE(r.images.size(), 1);
    QCOMPARE(r.images[0].url, QUrl(QStringLiteral("mdimg:/0")));
    QCOMPARE(r.images[0].image.size(), m_smallPngSize);
}

void TestImageResolver::emptyAltIsRewritten()
{
    // The resolver injects `image` as alt text so that
    // QTextDocument::setMarkdown() does not drop the fragment.
    const auto r = resolve(QStringLiteral("![](small.png)"));

    QCOMPARE(r.text, QStringLiteral("![image](mdimg:/0)"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::headingBeforeImageIsSeparated()
{
    // The heading-image rewrite inserts a blank line so the importer does
    // not absorb the image into the heading.  Input has no blank line;
    // output does.
    const auto r = resolve(QStringLiteral("# Title\n![](small.png)"));

    QCOMPARE(r.text, QStringLiteral("# Title\n\n![image](mdimg:/0)"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::nestedRelativeImageIsResolved()
{
    QVERIFY(!writeFile(QStringLiteral("sub/deep/pic.png"),
                       QByteArray()).isEmpty() == false
            || QFile::exists(m_tmp.filePath(QStringLiteral("sub/deep/pic.png"))));

    // Build a real PNG in a nested directory.
    QImage img(4, 4, QImage::Format_RGB32);
    img.fill(Qt::green);
    const QString rel = QStringLiteral("sub/deep/pic.png");
    const QString abs = m_tmp.filePath(rel);
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QVERIFY(img.save(abs, "PNG"));

    const auto r = resolve(QStringLiteral("![a](sub/deep/pic.png)"));

    QCOMPARE(r.text, QStringLiteral("![a](mdimg:/0)"));
    QCOMPARE(r.images.size(), 1);
    QCOMPARE(r.images[0].image.size(), QSize(4, 4));
}

void TestImageResolver::multipleImagesAreNumberedSequentially()
{
    const auto r = resolve(
        QStringLiteral("![a](small.png) ![b](big.png)"));

    QCOMPARE(r.text,
             QStringLiteral("![a](mdimg:/0) ![b](mdimg:/1)"));
    QCOMPARE(r.images.size(), 2);
    QCOMPARE(r.images[0].image.size(), m_smallPngSize);
    QCOMPARE(r.images[1].image.size(), m_bigPngSize);
}

void TestImageResolver::inlineImageInsideParagraphIsUntouched()
{
    // Guards the heading-image regex from over-matching: an image that
    // is not immediately after a heading must stay inline.
    const auto r = resolve(
        QStringLiteral("Some text ![alt](small.png) more text"));

    QCOMPARE(r.text,
             QStringLiteral("Some text ![alt](mdimg:/0) more text"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::percentEncodedPathIsDecoded()
{
    // The resolver decodes the reference before touching the filesystem,
    // so a file whose name contains a space is referenced with %20.
    QImage img(3, 3, QImage::Format_RGB32);
    img.fill(Qt::yellow);
    QVERIFY(img.save(m_tmp.filePath(QStringLiteral("a b.png")), "PNG"));

    const auto r = resolve(QStringLiteral("![x](a%20b.png)"));

    QCOMPARE(r.text, QStringLiteral("![x](mdimg:/0)"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::angleBracketPathWithSpaces()
{
    // Angle-bracket form allows a literal space in the path.
    QImage img(5, 5, QImage::Format_RGB32);
    img.fill(Qt::cyan);
    QVERIFY(img.save(m_tmp.filePath(QStringLiteral("with space.png")),
                     "PNG"));

    const auto r = resolve(QStringLiteral("![x](<with space.png>)"));

    // The resolver rewrites only the *captured URL* — the content between
    // the angle brackets — preserving the brackets themselves.  Qt’s
    // markdown importer treats `<url>` and `url` as equivalent, so
    // `![x](<mdimg:/0>)` renders identically to `![x](mdimg:/0)`.
    QCOMPARE(r.text, QStringLiteral("![x](<mdimg:/0>)"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::htmlImgDoubleQuoted()
{
    const auto r = resolve(QStringLiteral("<img src=\"small.png\">"));

    QCOMPARE(r.text, QStringLiteral("<img src=\"mdimg:/0\">"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::htmlImgSingleQuoted()
{
    const auto r = resolve(QStringLiteral("<img src='small.png'>"));

    QCOMPARE(r.text, QStringLiteral("<img src='mdimg:/0'>"));
    QCOMPARE(r.images.size(), 1);
}

void TestImageResolver::htmlImgCaseInsensitive()
{
    // Tag and attribute names are case-insensitive; the value is
    // rewritten without touching the tag’s casing.
    const auto r = resolve(QStringLiteral("<IMG SRC=\"small.png\">"));

    QCOMPARE(r.text, QStringLiteral("<IMG SRC=\"mdimg:/0\">"));
    QCOMPARE(r.images.size(), 1);
}

// -----------------------------------------------------------------------------
//  Leave-alone rules
// -----------------------------------------------------------------------------

void TestImageResolver::emptyTextIsUntouched()
{
    const auto r = resolve(QString());

    QVERIFY(r.text.isEmpty());
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::absoluteUrlIsUntouched()
{
    const QString md =
        QStringLiteral("![a](https://example.com/x.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::dataUrlIsUntouched()
{
    const QString md = QStringLiteral(
        "![a](data:image/png;base64,iVBORw0KGgoAAAANSUhEUg==)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::fileUrlIsUntouched()
{
    const QString md =
        QStringLiteral("![a](file:///C:/somewhere/x.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::protocolRelativeUrlIsUntouched()
{
    const QString md = QStringLiteral("![a](//example.com/x.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::windowsDriveIsUntouched()
{
    // A drive-letter path looks like a URI scheme to the reference
    // scanner; it must be left alone on every platform.
    const QString md = QStringLiteral("![a](C:/somewhere/x.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::missingFileIsLeftAlone()
{
    const QString md = QStringLiteral("![a](does-not-exist.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::corruptFileIsLeftAlone()
{
    // A file that exists but cannot be decoded (e.g. truncated download)
    // falls through to the same leave-alone path as a missing file.
    writeFile(QStringLiteral("corrupt.png"),
              QByteArrayLiteral("this is not a PNG"));

    const QString md = QStringLiteral("![a](corrupt.png)");
    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

void TestImageResolver::markdownWithoutImagesIsByteIdentical()
{
    // No `![](` and no heading-image sequence: the input must come out
    // byte-identical.  This is the resolver’s “no-op contract”.
    const QString md = QStringLiteral(
        "# Title\n\n"
        "Some paragraph.\n\n"
        "## Section\n\n"
        "More text with a [link](https://example.com).\n");

    const auto r = resolve(md);

    QCOMPARE(r.text, md);
    QVERIFY(r.images.isEmpty());
}

// -----------------------------------------------------------------------------
//  Integration
// -----------------------------------------------------------------------------

void TestImageResolver::resolvedImagesCarryDecodedPixels()
{
    // Every entry in ResolveResult::images must carry a valid QImage of
    // the right dimensions — the caller registers these directly on a
    // QTextDocument without further decoding.
    const auto r = resolve(
        QStringLiteral("![a](small.png) ![b](big.png)"));

    QCOMPARE(r.images.size(), 2);
    for (const ResolvedImage &entry : r.images) {
        QVERIFY(!entry.url.isEmpty());
        QVERIFY(!entry.image.isNull());
    }
    QCOMPARE(r.images[0].image.size(), m_smallPngSize);
    QCOMPARE(r.images[1].image.size(), m_bigPngSize);
}

QTEST_MAIN(TestImageResolver)
#include "tst_imageresolver.moc"

