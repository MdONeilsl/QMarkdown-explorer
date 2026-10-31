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
 *       ├─ setMarkdown(resolvedText)
 *       └─ applyCodeBlockStyles()
 *
 * @note The document owned by this widget is a `MarkdownDocument`, not a
 *       plain `QTextDocument`.  That subclass exists so that images
 *       registered before `setMarkdown()` survive the importer’s
 *       internal `clear()`; a future maintainer who replaces it with a
 *       plain document will find that images silently disappear from
 *       the rendered output.
 *
 * Code-block styling: fenced code blocks receive a
 * `QTextBlockFormat` background from `styles/code-<theme>.css`, applied
 * after `setMarkdown()` completes.  Inline code is intentionally not
 * styled — QTextCursor::mergeCharFormat() on inline ranges crashes
 * inside Qt 6.x.
 *
 * Layout: both `setMarkdown()` and `applyCodeBlockStyles()` run under a
 * `ScopedLayoutDisable` guard so the document lays out once instead of
 * once per edit.  Without this a 500-row table takes minutes to load;
 * with it, the same file loads in ~1.25 s.
 *
 * Text size: the base font is set to 13pt in the constructor — larger
 * than the 9pt application default, comfortable for long-form reading.
 * The user can adjust it further with Ctrl + mouse wheel; the wheel
 * handler clamps the range to 10–40pt so the text cannot shrink into
 * illegibility or grow until a single header fills the viewport.
 */
class MarkdownView : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    /**
     * @brief Loads `filePath`, resolves image references, and renders.
     *
     * @return `true` on success.  On failure the previous document is
     *         left untouched.
     */
    bool loadFile(const QString &filePath);

    /**
     * @brief Applies a theme and its document CSS.
     *
     * @param themeName    The style name as reported by StyleLoader
     *                     (`"dark"`, `"light"`, or empty for default).
     * @param documentCss  Contents of the theme’s `code-<name>.css`.
     *                     Parsed for `background` / `background-color`
     *                     and `color`.
     */
    void setTheme(const QString &themeName, const QString &documentCss);

protected:
    /**
     * @brief Handles Ctrl + wheel as font zoom.
     *
     * Plain wheel scrolling behaves normally (passes through to the
     * base class); Ctrl + wheel up increases the base font size by 1pt,
     * Ctrl + wheel down decreases it.  The size is clamped to 10–40pt.
     */
    void wheelEvent(QWheelEvent *event) override;

private:
    void applyCodeBlockStyles();

    QString m_theme;
    QString m_documentCss;
};