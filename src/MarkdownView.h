#pragma once

#include <QColor>
#include <QString>
#include <QTextEdit>
#include <QColor>
#include <QPoint>
#include <QString>
#include <QTextEdit>

class QMouseEvent;
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
 *   * Cross-file links (`[text](other.md)`) are clickable and emit
 *     linkToLocalFileRequested with the resolved absolute path.  The
 *     path is validated against the root folder — anything outside
 *     the root is rejected (rule 1).
 *   * In-document anchors (`[text](#section)`) scroll the current
 *     view to the matching heading.  Heading text is slugified the
 *     GitHub way: lowercase, spaces to hyphens, punctuation dropped;
 *     duplicates get `-1`, `-2`, … suffixes.  An anchor never opens
 *     a document.
 *   * Mixed links (`[text](other.md#section)`) are rejected by
 *     design.  Cross-file links navigate, anchors scroll, and the
 *     two are never combined.
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
 *
 * In-document search:
 *   Highlighting uses QTextEdit::setExtraSelections — widget-level
 *   decorations, not document formats.  This is deliberate: char-format
 *   merges would materialize font sizes and break Ctrl+wheel zoom, the
 *   same failure mode that the code-block styling was careful to avoid.
 *   Search is case-insensitive, requires at least 3 characters, and
 *   re-applies automatically when a new file is loaded.
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
     *                              default in place.
     */
    void setTheme(const QString &themeName,
                  const QColor &codeBlockBackground);

    /**
     * @brief Loads `filePath`, preprocesses text, resolves images, and
     *        renders.
     *
     * @return `true` on success.  On failure the previous document is
     *         left untouched.
     *
     * If a search was active (searchInDocument with a term of 3+ chars)
     * when the previous document was replaced, the search re-runs
     * automatically against the new content.
     */
    bool loadFile(const QString &filePath);

    /**
     * @brief Dispatches a click on an in-document link.
     *
     * The `href` is the raw string returned by QTextEdit::anchorAt()
     * — for example `"other.md"`, `"#section"`, `"other.md#section"`,
     * or `"https://example.com"`.  This is the *only* dispatch point:
     * mouseReleaseEvent calls this for every non-empty anchor, and the
     * tests call it directly.
     *
     * Behaviour by case:
     *   * `http://` / `https://`         → silently rejected.
     *   * `#fragment`                    → scroll to matching heading.
     *   * `file.md#fragment`             → silently rejected (mixed).
     *   * `file.md`, `file:`, relative   → emit linkToLocalFileRequested
     *                                       when the target is a file
     *                                       inside the root folder.
     *   * Anything else                  → silently rejected.
     *
     * The dispatch never falls through — a non-empty href is always
     * consumed.  Public so tests can drive it without synthesising
     * mouse events.
     */
    void handleLink(const QString &href);

    // ---- In-document search -------------------------------------------

    /**
     * @brief Highlights every case-insensitive occurrence of `needle`
     *        in the current document and resets the current match to
     *        the first one.
     *
     * @param needle  Search term.  Lengths of 0–2 characters are
     *                treated as “no search”: existing highlights are
     *                cleared, the counter is zeroed, and 0 is returned.
     *                This threshold prevents the pathological case of
     *                highlighting hundreds of matches for a single
     *                character.
     *
     * @return The number of matches found (0 when `needle` is too
     *         short or the document is empty).
     *
     * Emits `searchStateChanged(1, count)` on success, or
     * `searchStateChanged(0, 0)` when the search is cleared.
     */
    int searchInDocument(const QString &needle);

    /**
     * @brief Clears all search highlights and resets the match count.
     *
     * Emits `searchStateChanged(0, 0)`.
     */
    void clearSearchHighlights();

    /**
     * @brief Advances to the next match, wrapping to the first at the
     *        end.
     *
     * @return `true` if a new current match was set; `false` if there
     *         are no matches (no-op).
     */
    bool nextMatch();

    /**
     * @brief Moves to the previous match, wrapping to the last at the
     *        start.
     *
     * @return `true` if a new current match was set; `false` if there
     *         are no matches (no-op).
     */
    bool previousMatch();

signals:
    /**
     * @brief Emitted when the user clicks a local link inside a
     *        rendered document.
     *
     * The path is absolute, canonical, and guaranteed to lie inside the
     * current root folder.
     */
    void linkToLocalFileRequested(const QString &absolutePath);

    /**
     * @brief Emitted whenever the search state changes.
     *
     * @param current  1-based index of the current match, or 0 when
     *                 there are no matches.
     * @param total    Total number of matches, or 0 when there are none.
     */
    void searchStateChanged(int current, int total);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void applyDocumentStyles();
    void rebuildSearchHighlights();

    /**
     * @brief Scrolls the view to the heading whose slug matches
     *        `fragment`.
     *
     * Uses setTextCursor + ensureCursorVisible — the same mechanism
     * used elsewhere for cursor positioning — rather than Qt’s
     * `scrollToAnchor`, which would require QTextCharFormat anchor
     * names on every heading block.  Keeping char formats untouched
     * is what preserves Ctrl+wheel zoom.
     *
     * Silent no-op when no heading matches, mirroring every other
     * rejected-link path.
     */
    void scrollToAnchor(const QString &fragment);

    QString m_theme;
    QColor  m_codeBg;
    QString m_currentFilePath;
    QString m_rootFolder;

    QString m_searchTerm;
    int     m_matchCount       = 0;
    int     m_currentMatchIndex = 0;

    /// Position of the last left-button press.  Used to distinguish a
    /// click (which navigates) from a drag (which selects text and must
    /// not navigate, so the user can drag-select link text and copy it).
    QPoint m_leftButtonPressPos;
};
