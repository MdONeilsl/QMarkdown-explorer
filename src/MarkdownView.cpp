#include "MarkdownView.h"

#include "ImageResolver.h"
#include "MarkdownDocument.h"

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

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

    // ------------------------------------------------------------------
    //  Fenced code blocks only.
    //
    //  Inline code (backtick spans inside prose) is intentionally NOT
    //  styled.  QTextCursor::mergeCharFormat() on inline character
    //  ranges crashes inside Qt 6.5.0 with a qlist.h “!isEmpty()” assert
    //  and corrupts the document’s internal fragment list.  The crash
    //  reproduces identically for every variation tested:
    //
    //    * mergeCharFormat on a multi-character selection;
    //    * per-character merges using setPosition + KeepAnchor;
    //    * per-character merges using movePosition(NextCharacter);
    //    * feeding the merged format through setCharFormat instead;
    //
    //  Fenced blocks go through QTextBlockFormat::setBackground() plus a
    //  block-range char merge, which is a different code path in Qt that
    //  does not touch the inline fragment structure and works correctly.
    //
    //  The fix belongs in Qt’s QTextDocumentPrivate::mergeFormat path,
    //  not in this file.  When a Qt release fixes it, inline styling can
    //  be reinstated by mirroring the block iteration below with a
    //  per-character pass on non-fenced blocks — see the previous
    //  delivery’s git history for the shape.
    // ------------------------------------------------------------------

    QList<int> fencedPositions;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().nonBreakableLines())
            fencedPositions.append(b.position());
    }

    if (fencedPositions.isEmpty())
        return;

    // Group all merges in one edit block so the document’s undo stack
    // and fragment bookkeeping are treated as a single logical change.
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

    ResolveResult resolved =
        ImageResolver::resolveRelativeImages(markdown, fsPath.parent_path());

    auto *doc = qobject_cast<MarkdownDocument *>(document());
    if (!doc)
        return false;

    const int maxW = qMax(120, viewport()->width() - 40);

    doc->clearImages();
    for (ResolvedImage &entry : resolved.images) {
        if (entry.image.width() > maxW) {
            entry.image = entry.image.scaledToWidth(
                maxW, Qt::SmoothTransformation);
        }
        doc->setImage(entry.url, entry.image);
    }

    setMarkdown(resolved.text);
    applyCodeBlockStyles();
    return true;
}