#pragma once

#include <QHash>
#include <QImage>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>

/**
 * @brief A `QTextDocument` whose image resources survive `setMarkdown()`.
 *
 * `QTextDocument::setMarkdown()` calls `clear()` internally, which
 * discards every resource previously registered with `addResource()`.
 * Any image referenced by the freshly parsed Markdown therefore has no
 * data to render from — the importer creates the image fragment, but the
 * pixmap lookup fails and the fragment renders blank.
 *
 * This subclass solves that by holding its images in a member hash
 * (`m_images`) rather than in the document’s internal resource table,
 * and by answering every image-resource request through the
 * `loadResource()` virtual.  The virtual is called lazily by the painter
 * on each render, so the data is available whenever a fragment needs it
 * — regardless of whether the fragment was created before or after the
 * `setImage()` call.  The hash lives on the object and `setMarkdown()`
 * cannot reach it.
 *
 * Usage contract:
 *
 *   * Call `setImage(uri, image)` for every image before the document is
 *     painted.  The order relative to `setMarkdown()` does not matter —
 *     see above — but registering before is the natural reading of the
 *     data flow.
 *   * Call `clearImages()` before loading a new file, so images from the
 *     previous document do not linger in memory.
 *   * `addResource()` still works for resources other than images: the
 *     `loadResource()` override only intercepts `ImageResource` and
 *     falls through to the base class for everything else.
 */
class MarkdownDocument : public QTextDocument
{
    Q_OBJECT

public:
    explicit MarkdownDocument(QObject *parent = nullptr)
        : QTextDocument(parent) {}

    /**
     * @brief Stores `image` under `url` for later lookup by
     *        `loadResource()`.
     *
     * Replaces any previous image stored under the same URL.  The image
     * is copied into the hash; the caller keeps ownership of its own
     * copy (implicit sharing means the copy is cheap).
     */
    void setImage(const QUrl &url, const QImage &image)
    {
        m_images.insert(url, image);
    }

    /**
     * @brief Removes every image registered via `setImage()`.
     *
     * Call before loading a new document to release the previous file’s
     * image memory.  Does not touch resources registered through the
     * base class’s `addResource()`.
     */
    void clearImages()
    {
        m_images.clear();
    }

protected:
    /**
     * @brief Serves images from `m_images`; delegates everything else to
     *        the base class.
     *
     * Called by the painter each time an image fragment is drawn, so the
     * lookup happens after `setMarkdown()` has finished — which is what
     * makes the registration order irrelevant.
     */
    QVariant loadResource(int type, const QUrl &url) override
    {
        if (type == QTextDocument::ImageResource) {
            const auto it = m_images.constFind(url);
            if (it != m_images.constEnd())
                return QVariant::fromValue(it.value());
        }
        return QTextDocument::loadResource(type, url);
    }

private:
    QHash<QUrl, QImage> m_images;
};
