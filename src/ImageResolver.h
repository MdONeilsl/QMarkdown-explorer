#pragma once

#include <QImage>
#include <QString>
#include <QUrl>
#include <QVector>

#include <filesystem>

/**
 * @brief A single image resolved from a Markdown reference.
 *
 * The pair travels together so the resolver can stay document-agnostic:
 * it decodes the image, hands back the URI to reference it by, and lets
 * the caller decide how and when to register the QImage on its
 * `QTextDocument` (see `MarkdownDocument`, which overrides
 * `loadResource()` so the order does not matter).
 */
struct ResolvedImage
{
    QUrl   url;    ///< Opaque URI the rewritten Markdown refers to.
    QImage image;  ///< Decoded image data.
};

/**
 * @brief Output of a resolve pass.
 *
 * `text` is ready to hand to `QTextDocument::setMarkdown()`; `images`
 * must be registered on that document under their respective URIs.
 * `text` is byte-identical to the input when nothing was resolved.
 */
struct ResolveResult
{
    QString                text;
    QVector<ResolvedImage> images;
};

/**
 * @brief Tuning knobs for resolveRelativeImages().
 */
struct ResolveOptions
{
    /**
     * Maximum width for decoded images.  When greater than zero, the
     * resolver decodes each image directly at this width using
     * QImageReader::setScaledSize(), which avoids the full-size decode
     * plus separate QImage::scaled() step.  For PNG and JPEG this is
     * 2–3× faster than decode-then-scale; for SVG it is equivalent;
     * for formats whose codec ignores setScaledSize the resolver falls
     * back to a full decode, in which case the caller is responsible for
     * any additional scaling.
     *
     * A value of 0 disables scaling entirely (useful for tests and for
     * callers that want the raw image).
     */
    int maxImageWidth = 0;
};

/**
 * @brief Rewrites relative image references in Markdown into opaque
 *        resource URIs.
 *
 * `QTextDocument::setMarkdown()` cannot load images from relative paths:
 * the document has no base URL and no filesystem access.  This class
 * bridges that gap in one pass over the raw text:
 *
 *   1. Locate every image reference — Markdown inline and inline HTML.
 *   2. Resolve relative paths against the source file's directory and
 *      decode each existing image.
 *   3. Rewrite the reference to a fresh URI (`mdimg:/<n>`) and return
 *      the decoded QImage so the caller can register it on the
 *      document.
 *
 * Absolute references (any URI scheme, `//host/...`, rooted paths) are
 * left untouched.  Relative references whose target is missing or
 * undecodable are also left untouched and render as the platform's
 * broken-image placeholder — the correct behaviour for a file browser,
 * where the alternative would be a fabricated placeholder that hides the
 * broken link.
 */
class ImageResolver
{
public:
    /**
     * @brief Runs the resolve pass on `markdown`.
     *
     * @param markdown  Raw Markdown text, exactly as read from disk.
     * @param baseDir   Directory against which relative references are
     *                  resolved.  An empty path resolves them against
     *                  the process working directory.
     *
     * @return The rewritten text plus the decoded images.  When no
     *         reference resolved successfully, `text` equals `markdown`
     *         and `images` is empty.
     *
     * @note Besides the URI rewrites, two additional non-semantic
     *       rewrites are applied to work around
     *       `QTextDocument::setMarkdown()` quirks that would otherwise
     *       silently drop images:
     *
     *         - `![](` becomes `![image](`, so an image with empty alt
     *           text survives the importer.
     *         - A blank line is inserted between a heading and a
     *           following image when one is missing, so the image is
     *           not absorbed into the heading.
     *
     *       Neither rewrite is visible in the rendered document.  Both
     *       are scoped to their exact patterns so legitimate inline
     *       images inside a paragraph stay untouched.
     */
    static ResolveResult resolveRelativeImages(
        const QString &markdown,
        const std::filesystem::path &baseDir,
        const ResolveOptions &options = ResolveOptions{});
};
