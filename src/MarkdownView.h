#pragma once

#include <QColor>
#include <QString>
#include <QTextEdit>

class QWheelEvent;

/**
 * @brief Read-only Markdown renderer.
 *
 * Load-and-render pipeline:
 *
 *     loadFile(path)
 *       ├─ read the file portably (std::filesystem + std::ifstream)
 *       ├─ convert web links to plain text        (hyperlink rule 2)
 *       ├─ ImageResolver::resolveRelativeImages
 *       ├─ register resolved images on the document
 *       ├─ set per-file CSS on the document       (HTML reachability)
 *       └─ { ScopedLayoutDisable
 *            setMarkdown(resolvedText)
 *            applyDocumentStyles()                 (single pass)
 *          }
 *
 * Hyperlinks:
 *   * Local links are clickable and emit linkToLocalFileRequested with
 *     the resolved absolute path.  The path is validated against the
 *     root folder — anything outside the root is rejected (rule 1).
 *   * Web links (http:// and https://) are converted to plain text
 *     before markdown parsing, so they are visible and copyable but
 *     never clickable (rule 2).
 *
 * Per-file CSS:
 *   Loading `<root>/x/y/abc.md` also loads `<root>/css/abc.css`, if it
 *   exists, and applies it via setDefaultStyleSheet() before setMarkdown
 *   runs.  setMarkdown does not consult this stylesheet for its own
 *   element rendering, but it *is* reached by HTML that the markdown
 *   importer delegates to setHtml().  Document authors who want styled
 *   HTML blocks place a CSS file there.  Always set (to content or to
 *   empty) so a previous file’s CSS does not leak into the next.
 *
 * Post-processing scope (applyDocumentStyles):
 *   * Fenced code blocks: block background from the theme’s
 *     `code-*.css`, colour extracted by StyleLoader; no char format
 *     mutation, so Ctrl+wheel zoom keeps working on code text.
 *   * Headings h1, h2, h3: additional top margin in line-height units.
 *     Block format only, so font zoom keeps working on headings.
 *   * Everything else: Qt’s importer defaults, untouched.
 */
class MarkdownView : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    /**
     * @brief Sets the root folder used to validate local link targets
     *        and to locate per-file CSS.
     *
     * Must be called before loading any file.  An empty root rejects
     * all local links and disables per-file CSS (the safety default
     * for standalone use and tests).
     */
    void setRootFolder(const QString &rootFolder);

    /**
     * @brief Sets the active theme and its code-block background.
     *
     * @param themeName             `"dark"`, `"light"`, or empty for
     *                              System.  Used only to short-circuit
     *                              identical re-applications.
     * @param codeBlockBackground   Colour to paint behind fenced blocks,
     *                              or an invalid QColor to leave Qt’s
     *                              default in place.  MainWindow reads
     *                              this from `StyleLoader::codeBlockBackground()`.
     *
     * When a document is already loaded, the post-processing pass
     * re-runs on the current document so the change is visible
     * immediately.
     */
    void setTheme(const QString &themeName,
                  const QColor &codeBlockBackground);

    bool loadFile(const QString &filePath);

signals:
    /**
     * @brief Emitted when the user clicks a local link inside a
     *        rendered document.
     *
     * The path is absolute, canonical, and guaranteed to lie inside the
     * current root folder.
     */
    void linkToLocalFileRequested(const QString &absolutePath);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void applyDocumentStyles();

    QString m_theme;
    QColor  m_codeBg;            // invalid => System behaviour
    QString m_currentFilePath;
    QString m_rootFolder;
};
