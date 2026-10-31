#include "MarkdownView.h"

#include "ImageResolver.h"
#include "MarkdownDocument.h"

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
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

QColor extractColor(const QString &css, const QString &propPattern)
{
    if (css.isEmpty())
        return {};

    const QRegularExpression re(
        QStringLiteral("(?<![\\w-])%1\\s*:\\s*([^;\\}]+)")
            .arg(propPattern));
    const auto m = re.match(css);
    if (!m.hasMatch())
        return {};

    const QString value = m.captured(1).trimmed();
    if (value.isEmpty())
        return {};

    return QColor(value);
}

/**
 * @brief RAII guard that disables layout on a QTextDocument during a
 *        bulk-edit sequence, then re-enables it and triggers a single
 *        layout pass on destruction.
 *
 * QTextDocument re-lays out the document on every edit unless layout is
 * explicitly disabled.  For a bulk sequence — inserting a large
 * markdown document, then styling every fenced code block — that means
 * one layout per edit.  In practice this is what turned a 500-row
 * markdown table from 1.3 s into over 5 minutes: QTextTable's per-edit
 * layout work is expensive, and every row added by the importer
 * triggered one.
 *
 * With the guard in place, no layout runs until all edits have been
 * applied.  The destructor fires even on early `return` paths, so no
 * caller can accidentally leave layout off.
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

// Zoom range applied by wheelEvent().  Exposed as constants so a future
// tuning pass does not need to search for the literals.
constexpr int kMinZoomPt = 10;
constexpr int kMaxZoomPt = 40;
constexpr int kDefaultZoomPt = 11;

// QSettings key holding the user's preferred reading size.  Matches the
// pattern of MainWindow's keys ("Style", "LastFolder", "Language") —
// same organization and application identity, so the value lives
// alongside the rest of the user's preferences.
constexpr const char *kSettingsKey = "FontPointSize";

} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setAcceptRichText(true);
    setTabChangesFocus(true);

    auto *doc = new MarkdownDocument(this);
    setDocument(doc);
    doc->setDocumentMargin(16);

    // Restore the saved font size.  QTextEdit inherits the application
    // default, which on Windows is typically 9pt — too small for
    // long-form reading.  The first launch uses kDefaultZoomPt (13pt),
    // which is a comfortable base for prose; subsequent launches use
    // whatever the user last selected with Ctrl + mouse wheel.
    //
    // Clamped on read: a corrupted or out-of-range value in the settings
    // store cannot produce text smaller than kMinZoomPt or larger than
    // kMaxZoomPt.  This is the same safeguard wheelEvent applies on
    // write, so the persisted value and the in-memory value can never
    // diverge outside the allowed range.
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

void MarkdownView::wheelEvent(QWheelEvent *event)
{
    // Plain wheel: normal scrolling.  Do not intercept.
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QTextEdit::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        // Horizontal or zero-delta wheel event with Ctrl held — nothing
        // for us to do; let it through in case some platform attaches
        // meaning to it.
        event->ignore();
        return;
    }

    // The bounds check reads font().pointSize() because zoomIn() and
    // zoomOut() operate relative to the widget's current font.  Reading
    // it before each call keeps the value in sync with the last zoom.
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
        // Already at the bound — consume the event so it does not fall
        // through to the base class and scroll the viewport.
        event->accept();
    }
}

void MarkdownView::setTheme(const QString &themeName,
                            const QString &documentCss)
{
    const bool changed = (m_theme != themeName)
                      || (m_documentCss != documentCss);

    m_theme = themeName;
    m_documentCss = documentCss;

    if (!changed)
        return;

    // A theme switch re-applies block backgrounds to every fenced code
    // block in the loaded document.  Wrapping the styling pass in the
    // same layout guard used by loadFile() collapses what would
    // otherwise be one layout per styled block into a single layout at
    // the end.  For documents with many code blocks the difference is
    // measurable; for documents with none, the guard costs nothing.
    ScopedLayoutDisable layoutGuard(document());
    applyCodeBlockStyles();
}

void MarkdownView::applyCodeBlockStyles()
{
    auto *doc = document();
    if (!doc)
        return;

    const QColor bg = extractColor(m_documentCss,
                                   QStringLiteral("background(?:-color)?"));
    const QColor fg = extractColor(m_documentCss,
                                   QStringLiteral("color"));

    if (!bg.isValid() && !fg.isValid())
        return;

    if (doc->characterCount() <= 1)
        return;

    QList<int> fencedPositions;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().nonBreakableLines())
            fencedPositions.append(b.position());
    }

    if (fencedPositions.isEmpty())
        return;

    QTextCursor batch(doc);
    batch.beginEditBlock();

    for (int pos : fencedPositions) {
        QTextBlock b = doc->findBlock(pos);
        if (!b.isValid())
            continue;

        if (bg.isValid()) {
            QTextBlockFormat bf = b.blockFormat();
            bf.setBackground(bg);
            QTextCursor c(b);
            c.setBlockFormat(bf);
        }

        if (fg.isValid() && b.length() > 1) {
            QTextCursor c(doc);
            c.setPosition(b.position());
            c.setPosition(b.position() + b.length() - 1,
                          QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            cf.setForeground(fg);
            c.mergeCharFormat(cf);
        }
    }

    batch.endEditBlock();
}

bool MarkdownView::loadFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    // The markdown importer blocks the UI thread for as long as it takes
    // to parse the document.  The wait cursor does not shorten that
    // time, but it tells the user that the click was received and the
    // app is working — which is what makes the difference between
    // "responsive but slow" and "hung".
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

    const QString markdown = QString::fromUtf8(
        raw.data() + offset,
        static_cast<qsizetype>(raw.size() - offset));

    // -- 2. Resolve relative images -------------------------------------
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

    // -- 3. Render and style under a single layout pass -----------------
    //
    // Both setMarkdown() (which builds the document) and
    // applyCodeBlockStyles() (which mutates block formats) trigger
    // QTextDocument layout work on every edit.  For a 500-row markdown
    // table that compounds to minutes.  The ScopedLayoutDisable guard
    // suppresses layout for both operations and lets it run once, at
    // scope exit.  This is the single biggest factor in load performance
    // for table-heavy documents.
    {
        ScopedLayoutDisable layoutGuard(doc);

        setMarkdown(resolved.text);

        // Must run after setMarkdown(): the markdown importer rewrites
        // the document, so formats applied beforehand would be lost.
        applyCodeBlockStyles();
    }

    return true;
}