#pragma once

#include <QString>
#include <QTextEdit>

class QWheelEvent;

/**
 * @brief Read-only Markdown renderer.
 *
 * Encapsulates the full “file → image-resolved text → rendered document”
 * pipeline behind a single entry point, `loadFile()`:
 *
 *     loadFile(path)
 *       ├─ read the file portably (std::filesystem + std::ifstream)
 *       ├─ ImageResolver::resolveRelativeImages(text, baseDir)
 *       ├─ register every resolved image on the document
 *       └─ setMarkdown(resolvedText)
 *
 * @note The document owned by this widget is a `MarkdownDocument`, not a
 *       plain `QTextDocument`.  That subclass exists so that images
 *       registered before `setMarkdown()` survive the importer’s
 *       internal `clear()`; a future maintainer who replaces it with a
 *       plain document will find that images silently disappear from
 *       the rendered output.
 *
 * Layout: `setMarkdown()` runs under a `ScopedLayoutDisable` guard so
 * the document lays out once instead of once per edit.  Without this a
 * 500-row table takes minutes to load; with it, the same file loads in
 * ~1.25 s.  Do not remove the guard.
 *
 * Text size: the base font is 11pt (larger than the 9pt application
 * default, comfortable for long-form reading).  The user can adjust it
 * with Ctrl + mouse wheel; the size is clamped to 10–40pt and persisted
 * in QSettings.
 *
 * Document content is not styled by this class.  The appearance of the
 * rendered markdown is entirely whatever QTextMarkdownImporter produces,
 * plus whatever `setDefaultStyleSheet` accepts — see the note in the
 * .cpp about that method’s limited reach.  Widget chrome (toolbar, tree,
 * status bar, this widget’s own colours) is styled by the QSS files in
 * styles/ via StyleLoader; that is a separate mechanism and is not
 * affected by this class.
 */
class MarkdownView : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    /**
     * @brief Loads `filePath`, resolves image references, and renders.
     *
     * @param filePath  Native path to a Markdown file.  Must be a
     *                  regular file; the check is performed inside.
     *
     * @return `true` on success.  On failure — missing file, unreadable
     *         stream, or a `MarkdownDocument` that could not be
     *         retrieved from the widget — the previous document is left
     *         untouched, so the caller can surface an error without the
     *         view going blank.
     */
    bool loadFile(const QString &filePath);

protected:
    /**
     * @brief Handles Ctrl + wheel as font zoom.
     *
     * Plain wheel scrolling behaves normally (passes through to the
     * base class); Ctrl + wheel up increases the base font size by 1pt,
     * Ctrl + wheel down decreases it.  The size is clamped to 10–40pt
     * and persisted to QSettings so the next launch uses the same size.
     *
     * The event is consumed when the zoom bounds are reached, so the
     * viewport does not start scrolling at 10pt or 40pt.
     */
    void wheelEvent(QWheelEvent *event) override;
};