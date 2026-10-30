#pragma once

// -----------------------------------------------------------------------------
//  Optional diagnostic helpers.
//
//  Not included by any source file in the default build.  Kept in the
//  tree so that when a bug needs investigation, a developer can add
//
//      #include "DebugLog.h"
//
//  to the relevant .cpp, call a helper, and remove the include when the
//  investigation is finished.  Nothing here is wired into the UI,
//  translation system, or build.
//
//  Every function is `inline` inside `namespace mddebug`, so including
//  this header adds zero symbols to the binary unless a helper is
//  actually called.  Depends only on Qt Core/Gui/Widgets and the
//  standard library — no project headers.
//
//  All output is prefixed with `[MD-DBG]` so it can be grepped out.
// -----------------------------------------------------------------------------

#include <QByteArray>
#include <QDebug>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>
#include <QVariant>

#include <filesystem>
#include <system_error>

namespace mddebug {

/**
 * @brief Portable `std::filesystem::path` → `QString`, via UTF-8.
 *
 * Matches the conversion direction used throughout the project
 * (`ImageResolver.cpp`, `MarkdownView.cpp`, `MarkdownFilterProxyModel.cpp`,
 * `StyleLoader.cpp`).
 */
inline QString fsToQString(const std::filesystem::path &p)
{
    const auto u8 = p.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(u8.data()),
                             static_cast<qsizetype>(u8.size()));
}

/**
 * @brief Prints a filesystem path plus the two facts that matter most
 *        when a file “should be there but isn’t”.
 *
 * Answers: does the path exist, and is it a regular file (as opposed to
 * a directory or a broken symlink).
 */
inline void dumpPath(const char *label, const std::filesystem::path &p)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(p, ec);
    const bool isFile = exists && std::filesystem::is_regular_file(p, ec);
    qDebug().noquote() << "[MD-DBG]" << label
                       << "\n         path    =" << fsToQString(p)
                       << "\n         exists  =" << exists
                       << "\n         isFile  =" << isFile;
}

/**
 * @brief Asks `QImageReader` to decode a file and prints what it reports.
 *
 * Unlike `QImage::load`, `QImageReader` exposes the failure reason — a
 * “cannot load” is almost always a misnamed extension (a WebP file
 * called `.png`, etc.), and `errorString()` makes that obvious.
 */
inline void tryLoadImage(const char *label, const std::filesystem::path &p)
{
    QImageReader reader(fsToQString(p));
    const QImage img = reader.read();
    qDebug().noquote() << "[MD-DBG]" << label
                       << "\n         format  ="
                       << QString::fromLatin1(reader.format())
                       << "\n         size    =" << img.size()
                       << "\n         null    =" << img.isNull()
                       << "\n         error   =" << reader.errorString();
}

/**
 * @brief Prints every image fragment in a document: its URI, whether
 *        the document currently answers the URI with a non-null
 *        resource, and the decoded image’s dimensions.
 *
 * Passing a non-null `stage` label makes multiple calls in one session
 * readable.  Works with any `QTextDocument` subclass, including
 * `MarkdownDocument` whose `loadResource()` override is what makes
 * resources survive `setMarkdown()`.
 */
inline void dumpDocumentImages(QTextDocument *doc, const char *stage)
{
    if (!doc) {
        qDebug().noquote() << "[MD-DBG] dumpDocumentImages: null document";
        return;
    }
    qDebug().noquote() << "[MD-DBG] === document images (" << stage << ") ===";
    int n = 0;
    for (auto blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat fmt = frag.charFormat();
            if (!fmt.isImageFormat())
                continue;
            const QTextImageFormat imgFmt = fmt.toImageFormat();
            const QUrl url = imgFmt.name();
            const QVariant res = doc->resource(QTextDocument::ImageResource, url);
            const QImage img = res.value<QImage>();
            ++n;
            qDebug().noquote() << "  [" << n << "]"
                               << "name=" << url.toString()
                               << "resourceNull=" << res.isNull()
                               << "imageNull=" << img.isNull()
                               << "size=" << img.size();
        }
    }
    if (n == 0)
        qDebug().noquote() << "  (no image fragments present)";
}

/**
 * @brief Prints the portion of `toHtml()` surrounding the first `<img>`.
 *
 * When a document has an image fragment but the image does not visibly
 * render, this shows whether the importer rewrote the source URI (it
 * often normalises `mdimg:/0` to `mdimg:0` or similar).  The culprit is
 * usually visible in the first ~600 characters.
 */
inline void dumpDocumentHtml(QTextDocument *doc, int maxLen = 600)
{
    if (!doc) {
        qDebug().noquote() << "[MD-DBG] dumpDocumentHtml: null document";
        return;
    }
    const QString html = doc->toHtml();
    int imgIdx = html.indexOf(QStringLiteral("<img"));
    if (imgIdx < 0)
        imgIdx = 0;
    qDebug().noquote() << "[MD-DBG] HTML around first <img>:\n"
                       << html.mid(imgIdx, maxLen);
}

/**
 * @brief Regression guard for the `QTextDocument` quirk that motivates
 *        `MarkdownDocument`.
 *
 * `QTextDocument::setMarkdown()` internally calls `clear()`, which wipes
 * every resource registered with `addResource()`.  If the “AFTER” line
 * prints `true`, that quirk is no longer present in this Qt version and
 * `MarkdownDocument` may be reconsiderable; while it prints `false`, the
 * override is doing real work.
 */
inline void testResourceSurvival()
{
    QTextDocument doc;
    QImage red(16, 16, QImage::Format_RGB32);
    red.fill(Qt::red);
    const QUrl url(QStringLiteral("qrc://mdimg/0"));
    doc.addResource(QTextDocument::ImageResource, url, red);
    qDebug().noquote()
        << "[MD-DBG] resource survival BEFORE setMarkdown: present ="
        << !doc.resource(QTextDocument::ImageResource, url).isNull();
    doc.setMarkdown(QStringLiteral("![](qrc://mdimg/0)"));
    qDebug().noquote()
        << "[MD-DBG] resource survival AFTER  setMarkdown: present ="
        << !doc.resource(QTextDocument::ImageResource, url).isNull();
    dumpDocumentImages(&doc, "resource-survival-test");
}

/**
 * @brief Prints the image formats `QImageReader` can decode in this
 *        build.
 *
 * Useful to confirm whether a specific extension is actually supported
 * — some Qt builds omit WebP or TIFF depending on the plugins shipped.
 */
inline void listSupportedFormats()
{
    QStringList fmts;
    for (const auto &b : QImageReader::supportedImageFormats())
        fmts << QString::fromLatin1(b);
    qDebug().noquote() << "[MD-DBG] QImageReader supports:"
                       << fmts.join(QStringLiteral(", "));
}

} // namespace mddebug
