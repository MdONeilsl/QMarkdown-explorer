#include "MarkdownView.h"

#include "ImageResolver.h"
#include "MarkdownDocument.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSettings>
#include <QString>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QUrl>
#include <QWheelEvent>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

/**
 * @brief RAII guard that disables layout on a QTextDocument during a
 *        bulk-edit sequence, then re-enables it and triggers a single
 *        layout pass on destruction.
 *
 * QTextDocument re-lays out on every edit unless layout is explicitly
 * disabled.  For a bulk insertion — the markdown importer adding every
 * row of a table, every paragraph of a document — that means one
 * layout per edit.  In practice this turned a 500-row markdown table
 * from ~1.25 s into over 5 minutes.
 *
 * The destructor fires even on early `return` paths, so no caller can
 * accidentally leave layout off.
 */
class ScopedLayoutDisable
{
public:
    explicit ScopedLayoutDisable(QTextDocument *doc)
        : m_doc(doc)
    {
        if (m_doc)
            m_doc->setLayoutEnabled(false);
    }

    ~ScopedLayoutDisable()
    {
        if (!m_doc)
            return;
        m_doc->setLayoutEnabled(true);
        m_doc->markContentsDirty(0, m_doc->characterCount());
    }

    ScopedLayoutDisable(const ScopedLayoutDisable &) = delete;
    ScopedLayoutDisable &operator=(const ScopedLayoutDisable &) = delete;

private:
    QTextDocument *m_doc = nullptr;
};

// Zoom range applied by wheelEvent().
constexpr int kMinZoomPt = 10;
constexpr int kMaxZoomPt = 40;
constexpr int kDefaultZoomPt = 11;

// QSettings key holding the user’s preferred reading size.
constexpr const char *kSettingsKey = "FontPointSize";

/**
 * @brief Returns the markdown heading level of a block, or 0 if it is
 *        not a heading.
 */
int detectHeadingLevel(const QTextBlock &block, double basePt)
{
    const int explicitLevel = block.blockFormat().headingLevel();
    if (explicitLevel >= 1 && explicitLevel <= 6)
        return explicitLevel;

    if (basePt <= 0.0)
        return 0;

    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid() || frag.length() <= 0)
            continue;
        const double size = frag.charFormat().fontPointSize();
        if (size <= 0.0)
            continue;
        const double ratio = size / basePt;
        if (ratio >= 1.75) return 1;
        if (ratio >= 1.35) return 2;
        if (ratio >= 1.10) return 3;
        return 0;
    }
    return 0;
}

/**
 * @brief Converts markdown links whose target is a web URL into plain
 *        text showing the full URL.
 *
 * Hyperlink rule 2: web links must not be clickable, and the full URL
 * must be visible so the user can copy/paste it.
 */
QString convertWebLinksToPlainText(const QString &markdown)
{
    static const QRegularExpression re(
        QStringLiteral(
            R"RX((?<!!)\[([^\]]*)\]\(\s*(?:<(https?://[^>]+)>|(https?://[^)\s]+))(?:\s+(?:"[^"]*"|'[^']*'))?\s*\))RX"),
        QRegularExpression::CaseInsensitiveOption);

    QString result;
    result.reserve(markdown.size());

    qsizetype lastEnd = 0;
    auto it = re.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result.append(markdown.mid(lastEnd, m.capturedStart() - lastEnd));

        const QString text = m.captured(1).trimmed();
        const QString url = (m.capturedStart(2) >= 0)
                                ? m.captured(2)
                                : m.captured(3);

        if (text.isEmpty() || text == url) {
            // Avoid `https://x (https://x)`; the URL alone is enough.
            result.append(url);
        } else {
            result.append(text);
            result.append(QStringLiteral(" ("));
            result.append(url);
            result.append(QStringLiteral(")"));
        }

        lastEnd = m.capturedEnd();
    }
    result.append(markdown.mid(lastEnd));

    return result;
}

// Highlight colours.  Yellow for all matches, orange for the current
// one — the same palette every browser uses for find-in-page.  Black
// foreground is explicit so the highlights are readable on the dark
// QSS theme (where the document default text is light grey).
const QColor kAllMatchBg(0xFF, 0xF1, 0x76);   // material yellow 300
const QColor kCurrentMatchBg(0xFF, 0xB7, 0x4D); // material orange 300
const QColor kMatchFg(0x00, 0x00, 0x00);

} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setAcceptRichText(true);
    setTabChangesFocus(true);

    // Enable link activation.  setReadOnly(true) only allows text
    // selection; links need the explicit interaction flag.  QTextEdit
    // has no setOpenExternalLinks property — that is a QTextBrowser-only
    // convenience — so anchors are handled via mouseReleaseEvent.
    setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *doc = new MarkdownDocument(this);
    setDocument(doc);
    doc->setDocumentMargin(16);

    // Restore the saved font size.
    int savedPt = QSettings().value(QString::fromLatin1(kSettingsKey),
                                     kDefaultZoomPt).toInt();
    if (savedPt < kMinZoomPt)
        savedPt = kMinZoomPt;
    else if (savedPt > kMaxZoomPt)
        savedPt = kMaxZoomPt;

    QFont f = font();
    f.setPointSize(savedPt);
    setFont(f);
}

void MarkdownView::setRootFolder(const QString &rootFolder)
{
    m_rootFolder = rootFolder;
}

void MarkdownView::setTheme(const QString &themeName,
                            const QColor &codeBlockBackground)
{
    const bool nameChanged  = (m_theme != themeName);
    const bool colorChanged = (m_codeBg != codeBlockBackground);

    m_theme = themeName;
    m_codeBg = codeBlockBackground;

    if (!nameChanged && !colorChanged)
        return;

    // Re-apply the post-processing pass when a document is already
    // loaded.  The guard matters: without it each styled block would
    // trigger a relayout during the pass.
    if (!document()->isEmpty()) {
        ScopedLayoutDisable layoutGuard(document());
        applyDocumentStyles();
    }
}

void MarkdownView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QTextEdit::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const int current = font().pointSize() > 0
                            ? font().pointSize()
                            : kDefaultZoomPt;

    if (delta > 0 && current < kMaxZoomPt) {
        zoomIn();
        event->accept();
        QSettings().setValue(QString::fromLatin1(kSettingsKey),
                             font().pointSize());
    } else if (delta < 0 && current > kMinZoomPt) {
        zoomOut();
        event->accept();
        QSettings().setValue(QString::fromLatin1(kSettingsKey),
                             font().pointSize());
    } else {
        event->accept();
    }
}

void MarkdownView::mouseReleaseEvent(QMouseEvent *event)
{
    // QTextEdit has no anchorClicked signal — that is QTextBrowser-only.
    // The documented workaround is to intercept the mouse release and
    // ask anchorAt() whether a link sits under the cursor position.
    // anchorAt() returns the href string, or an empty QString when
    // there is no anchor at that point.
    if (event->button() == Qt::LeftButton) {
        const QString href = anchorAt(event->pos());
        if (!href.isEmpty()) {
            const QUrl url(href);

            // Rule 2 guard: web links should already have been
            // converted to plain text by convertWebLinksToPlainText().
            // If an http/https URL somehow survives as an anchor,
            // reject it — do not let a click open a browser.
            const QString scheme = url.scheme().toLower();
            if (scheme == QStringLiteral("http")
                || scheme == QStringLiteral("https")) {
                event->accept();
                return;
            }

            // In-document anchor (e.g. `[top](#section)`) — not
            // supported yet.  Reject cleanly.
            if (url.path().isEmpty() && !url.fragment().isEmpty()) {
                event->accept();
                return;
            }

            // Resolve the target to an absolute path.
            QString targetPath;
            if (url.isLocalFile()) {
                targetPath = url.toLocalFile();
            } else if (scheme.isEmpty()) {
                const QDir currentDir =
                    QFileInfo(m_currentFilePath).absoluteDir();
                targetPath = currentDir.absoluteFilePath(url.path());
            } else {
                event->accept();
                return;
            }

            // Rule 1 guard: reject anything outside the root.  Canonical
            // comparison handles symlinks and Windows case normalisation.
            if (m_rootFolder.isEmpty()) {
                event->accept();
                return;
            }

            const QFileInfo targetInfo(targetPath);
            if (!targetInfo.isFile()) {
                event->accept();
                return;
            }

            const QString targetCanonical = targetInfo.canonicalFilePath();
            const QString rootCanonical =
                QFileInfo(m_rootFolder).canonicalFilePath();
            if (targetCanonical.isEmpty() || rootCanonical.isEmpty()) {
                event->accept();
                return;
            }

            // Trailing separator so "/root/foo" matches but
            // "/rootbar/foo" does not.
            const QString rootPrefix = rootCanonical + u'/';
            if (!targetCanonical.startsWith(rootPrefix)) {
                event->accept();
                return;
            }

            emit linkToLocalFileRequested(targetCanonical);
            event->accept();
            return;
        }
    }

    // Not a link click — let the base class handle selection, etc.
    QTextEdit::mouseReleaseEvent(event);
}

void MarkdownView::applyDocumentStyles()
{
    auto *doc = document();
    if (!doc)
        return;

    // Code-block background comes from the theme’s code-*.css file.
    // MainWindow reads it via StyleLoader::codeBlockBackground() and
    // hands it in through setTheme().  An invalid QColor means “no
    // code-block styling” — the expected result for System, where the
    // absence of a code-system.css file leaves Qt’s importer defaults
    // in place.
    const bool styleCodeBlocks = m_codeBg.isValid();

    if (doc->characterCount() <= 1)
        return;

    // Snapshot the base font metrics once — used both for heading
    // detection (font-size ratio) and for computing margins in
    // line-height units so they stay visually proportional at any zoom.
    const double basePt = font().pointSizeF() > 0.0
                              ? font().pointSizeF()
                              : static_cast<double>(kDefaultZoomPt);
    const double lineHeight = QFontMetricsF(font()).height();

    QTextCursor batch(doc);
    batch.beginEditBlock();

    QTextBlock block = doc->begin();
    while (block.isValid()) {
        const bool isCodeBlock = styleCodeBlocks
                                 && block.blockFormat().nonBreakableLines();

        if (isCodeBlock) {
            // blockFormat().setBackground() paints the whole row.
            // No char format is applied — that would materialize the
            // inherited font size and freeze the block against zoom.
            QTextBlockFormat bf = block.blockFormat();
            bf.setBackground(m_codeBg);
            QTextCursor c(block);
            c.setBlockFormat(bf);
        } else if (block.blockNumber() != 0) {
            // Heading margins for h1 / h2 / h3 only.  Block-format
            // margin, not char format — font zoom continues to work.
            // Skipping block 0 avoids a large gap above the title when
            // the document opens with a heading.
            const int level = detectHeadingLevel(block, basePt);
            if (level >= 1 && level <= 3) {
                double factor = 0.0;
                switch (level) {
                case 1: factor = 3.0; break;
                case 2: factor = 2.2; break;
                case 3: factor = 1.5; break;
                }
                QTextBlockFormat bf = block.blockFormat();
                bf.setTopMargin(lineHeight * factor);
                QTextCursor c(block);
                c.setBlockFormat(bf);
            }
        }

        block = block.next();
    }

    batch.endEditBlock();
}

// -----------------------------------------------------------------------------
//  In-document search
// -----------------------------------------------------------------------------

int MarkdownView::searchInDocument(const QString &needle)
{
    m_searchTerm = needle;
    m_currentMatchIndex = 0;
    rebuildSearchHighlights();
    return m_matchCount;
}

void MarkdownView::clearSearchHighlights()
{
    m_searchTerm.clear();
    m_matchCount = 0;
    m_currentMatchIndex = 0;
    setExtraSelections({});
    emit searchStateChanged(0, 0);
}

bool MarkdownView::nextMatch()
{
    if (m_matchCount == 0)
        return false;
    m_currentMatchIndex = (m_currentMatchIndex + 1) % m_matchCount;
    rebuildSearchHighlights();
    return true;
}

bool MarkdownView::previousMatch()
{
    if (m_matchCount == 0)
        return false;
    m_currentMatchIndex =
        (m_currentMatchIndex - 1 + m_matchCount) % m_matchCount;
    rebuildSearchHighlights();
    return true;
}

void MarkdownView::rebuildSearchHighlights()
{
    auto *doc = document();
    if (!doc || m_searchTerm.length() < 3) {
        m_matchCount = 0;
        m_currentMatchIndex = 0;
        setExtraSelections({});
        emit searchStateChanged(0, 0);
        return;
    }

    // Collect all matches with QTextDocument::find (case-insensitive
    // by default; no FindCaseSensitively flag).  Each find() call
    // starts after the previous match’s selection, so a single cursor
    // walks the whole document.
    QList<QTextEdit::ExtraSelection> selections;
    QTextCursor cursor(doc);
    while (true) {
        cursor = doc->find(m_searchTerm, cursor);
        if (cursor.isNull())
            break;

        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format.setBackground(
            (selections.size() == m_currentMatchIndex)
                ? kCurrentMatchBg
                : kAllMatchBg);
        sel.format.setForeground(kMatchFg);
        selections.append(sel);
    }

    m_matchCount = selections.size();

    if (m_matchCount == 0) {
        m_currentMatchIndex = 0;
        setExtraSelections({});
        emit searchStateChanged(0, 0);
        return;
    }

    if (m_currentMatchIndex >= m_matchCount)
        m_currentMatchIndex = 0;

    setExtraSelections(selections);

    // Scroll the current match into view without creating a text
    // selection.  Setting the cursor at the match start positions the
    // caret for the next Tab out of the search box, and
    // ensureCursorVisible() brings it into the viewport.
    const QTextCursor &cur = selections[m_currentMatchIndex].cursor;
    QTextCursor clean(doc);
    clean.setPosition(cur.selectionStart());
    setTextCursor(clean);
    ensureCursorVisible();

    emit searchStateChanged(m_currentMatchIndex + 1, m_matchCount);
}

// -----------------------------------------------------------------------------
//  Load
// -----------------------------------------------------------------------------

bool MarkdownView::loadFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    // The markdown importer blocks the UI thread for as long as it takes
    // to parse the document.  The wait cursor does not shorten that
    // time, but it tells the user that the click was received and the
    // app is working.
    struct WaitCursorScope
    {
        WaitCursorScope()  { QApplication::setOverrideCursor(Qt::WaitCursor); }
        ~WaitCursorScope() { QApplication::restoreOverrideCursor(); }
    } waitCursorScope;

    // -- 1. Portably read the file --------------------------------------
    const std::filesystem::path fsPath = toFsPath(filePath);

    std::error_code ec;
    if (!std::filesystem::is_regular_file(fsPath, ec))
        return false;

    std::ifstream in(fsPath, std::ios::binary);
    if (!in.is_open())
        return false;

    const std::string raw((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    in.close();

    // Strip an optional UTF-8 BOM: without this, the first rendered glyph
    // is a zero-width invisible character that the Markdown parser treats
    // as stray text before the first heading.
    std::size_t offset = 0;
    if (raw.size() >= 3
        && static_cast<unsigned char>(raw[0]) == 0xEF
        && static_cast<unsigned char>(raw[1]) == 0xBB
        && static_cast<unsigned char>(raw[2]) == 0xBF) {
        offset = 3;
    }

    QString markdown = QString::fromUtf8(
        raw.data() + offset,
        static_cast<qsizetype>(raw.size() - offset));

    // -- 2. Convert web links to plain text -----------------------------
    //
    // Hyperlink rule 2.  Must run before the image resolver so that a
    // `[text](https://url)` link is transformed even when the same
    // document also contains image references — the two regexes are
    // independent, but converting first keeps the pipeline readable.
    markdown = convertWebLinksToPlainText(markdown);

    // -- 3. Resolve relative images -------------------------------------
    auto *doc = qobject_cast<MarkdownDocument *>(document());
    if (!doc)
        return false;

    // Resolve images at the target width.  ImageResolver uses
    // QImageReader::setScaledSize() so the codec decodes directly at
    // maxWidth instead of producing a full-size buffer that we then
    // resample.  For a README with 5–10 medium images this cuts the
    // image-handling cost by 50–70%.  Files with no image references
    // take ImageResolver's fast path and skip the regexes entirely.
    ResolveOptions options;
    options.maxImageWidth = qMax(120, viewport()->width() - 40);

    ResolveResult resolved = ImageResolver::resolveRelativeImages(
        markdown, fsPath.parent_path(), options);

    // Fallback: some image formats ignore setScaledSize() (animated GIF,
    // a few exotic codecs).  The loop below scales those — and only
    // those — the slow way.  For PNG / JPEG / SVG, the resolver already
    // produced a correctly sized image and the condition is never taken.
    for (ResolvedImage &entry : resolved.images) {
        if (options.maxImageWidth > 0
            && entry.image.width() > options.maxImageWidth) {
            entry.image = entry.image.scaledToWidth(
                options.maxImageWidth, Qt::SmoothTransformation);
        }
    }

    doc->clearImages();
    for (const ResolvedImage &entry : resolved.images)
        doc->setImage(entry.url, entry.image);

    // -- 4. Render + post-process under one layout pass -----------------
    //
    // setMarkdown() and applyDocumentStyles() both mutate the document.
    // Wrapping them in one ScopedLayoutDisable guard avoids a layout
    // pass per edit — the single biggest factor in load performance for
    // table-heavy documents.
        m_currentFilePath = filePath;

    // -- 3b. Per-file document CSS --------------------------------------
    //
    // Load <root>/css/<basename>.css if it exists, hand its contents to
    // QTextDocument::setDefaultStyleSheet().  setMarkdown does not
    // consult this stylesheet for its own element rendering, but the
    // markdown importer delegates HTML blocks to setHtml(), which does.
    // Document authors who want styled HTML in their markdown place
    // their CSS at that location.
    //
    // The stylesheet is always set — to the file content, or to an empty
    // string when the file is absent — so a previous file’s CSS cannot
    // leak into the next.
    {
        QString perFileCss;
        if (!m_rootFolder.isEmpty()) {
            const QFileInfo mdInfo(filePath);
            const QString cssPath = m_rootFolder
                                  + QStringLiteral("/css/")
                                  + mdInfo.completeBaseName()
                                  + QStringLiteral(".css");
            QFile cssFile(cssPath);
            if (cssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                perFileCss = QString::fromUtf8(cssFile.readAll());
                cssFile.close();
            }
        }
        document()->setDefaultStyleSheet(perFileCss);
    }

    // -- 4. Render + post-process ---------------------------------------
    {
        ScopedLayoutDisable layoutGuard(doc);
        setMarkdown(resolved.text);
        applyDocumentStyles();
    }

    // -- 5. Re-apply search to the new content --------------------------
    //
    // setMarkdown replaces the document, which invalidates any previous
    // extraSelections.  Rebuilding them against the new content keeps
    // the counter and highlights consistent with what the user sees.
    // The search term itself is not affected by loadFile.
    if (m_searchTerm.length() >= 3) {
        m_currentMatchIndex = 0;
        rebuildSearchHighlights();
    } else {
        m_matchCount = 0;
        m_currentMatchIndex = 0;
        setExtraSelections({});
        // Do not emit — no visible change from the user’s perspective
        // (the counter was already empty).
    }

    return true;
}