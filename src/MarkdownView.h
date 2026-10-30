#pragma once

#include <QString>
#include <QTextEdit>

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
 * Code-block styling: `QTextDocument::setDefaultStyleSheet()` is not
 * consulted by `setMarkdown()` — markdown is parsed by
 * `QTextMarkdownImporter`, which does not go through `setHtml()`.  As a
 * result, CSS cannot be applied to the rendered content directly.  This
 * widget works around that by post-processing the document after
 * `setMarkdown()`: fenced code blocks receive a `QTextBlockFormat` with
 * a background brush, and inline code receives the equivalent
 * `QTextCharFormat` merge.  The colour values come from a CSS file
 * supplied by `setTheme()` (conventionally `styles/code-<theme>.css`);
 * the CSS itself is parsed only for the `background` and `color` values.
 */
class MarkdownView : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    /// @brief Loads `filePath`, resolves image references, and renders.
    bool loadFile(const QString &filePath);

    /**
     * @brief Applies a theme and its document CSS.
     *
     * @param themeName    The style name as reported by StyleLoader
     *                     (`"dark"`, `"light"`, or empty for default).
     *                     Used only to remember the current theme; the
     *                     colour values come from `documentCss`.
     * @param documentCss  Contents of the theme’s `code-<name>.css`.
     *                     Parsed for `background` / `background-color`
     *                     and `color` — see the class doc.  Empty CSS
     *                     disables code-block styling.
     *
     * When a document is already loaded, re-applies the styling without
     * re-parsing the markdown (and without reloading images).
     */
    void setTheme(const QString &themeName, const QString &documentCss);

private:
    void applyCodeBlockStyles();

    QString m_theme;
    QString m_documentCss;
};