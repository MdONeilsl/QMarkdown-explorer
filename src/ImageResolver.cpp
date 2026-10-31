#include "ImageResolver.h"

#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// QString <-> std::filesystem::path, both directions, via UTF-8.
// std::filesystem::path has no QString constructor, and QImageReader has
// no std::filesystem::path constructor, so both directions of the bridge
// are genuinely needed in this file — neither is a helper kept around
// for symmetry.
std::filesystem::path toFsPath(const QString &qstr)
{
    const QByteArray utf8 = qstr.toUtf8();
    const std::u8string u8(
        reinterpret_cast<const char8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(u8);
}

QString toQString(const std::filesystem::path &p)
{
    const std::u8string u8 = p.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char *>(u8.data()),
        static_cast<qsizetype>(u8.size()));
}

// A reference is left untouched (i.e. treated as already-resolved) if it
// is not relative to the Markdown file's directory.  This covers any URI
// scheme (http:, https:, data:, file:, …), protocol-relative URLs
// ("//host/..."), and rooted paths ("/...").  Windows drive letters
// ("C:...") are caught by the same scheme rule — without it they would be
// resolved against baseDir and produce nonsense paths on POSIX, where
// "C:" is a valid filename prefix rather than a drive designator.
bool isAbsoluteReference(const QString &ref)
{
    if (ref.isEmpty())
        return true;

    if (ref.size() >= 2 && ref.at(0).isLetter()) {
        for (qsizetype i = 1; i < ref.size(); ++i) {
            const QChar c = ref.at(i);
            if (c == u':')
                return true;
            if (!(c.isLetterOrNumber() || c == u'+' || c == u'-' || c == u'.'))
                break;
        }
    }

    if (ref.startsWith(u'/'))
        return true;

    return false;
}

// Resolves one reference to a decoded image plus its URI.  A default-
// constructed ResolvedImage (empty url) means "leave the reference as
// it is": absolute, missing, or undecodable.
ResolvedImage resolveOne(const QString &ref,
                         const std::filesystem::path &baseDir,
                         const ResolveOptions &options,
                         int &counter)
{
    if (isAbsoluteReference(ref))
        return {};

    // URL-decode before touching the filesystem: the markdown spec
    // permits percent-encoding, and the file on disk uses the literal
    // character ("%20" vs " ").
    const QString decoded = QUrl::fromPercentEncoding(ref.toUtf8());

    const std::filesystem::path rel = toFsPath(decoded);
    if (rel.is_absolute())
        return {};

    const std::filesystem::path joined =
        baseDir.empty() ? rel : (baseDir / rel);

    // weakly_canonical (rather than canonical) because a component of
    // the path may legitimately not exist; the is_regular_file check
    // below catches the cases where the path is genuinely broken.
    std::error_code canonEc;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(joined, canonEc);
    const std::filesystem::path resolved = canonEc ? joined : canonical;

    std::error_code fileEc;
    if (!std::filesystem::is_regular_file(resolved, fileEc) || fileEc)
        return {};

    // QImageReader rather than QImage::load: identical behaviour for
    // success and failure, but the reader exposes an error string for a
    // future diagnostic path without changing this function's signature.
    QImageReader reader(toQString(resolved));

    if (options.maxImageWidth > 0) {
        const QSize srcSize = reader.size();
        if (srcSize.isValid() && srcSize.width() > options.maxImageWidth) {
            const int newH = qMax(
                1, srcSize.height() * options.maxImageWidth / srcSize.width());
            reader.setScaledSize(QSize(options.maxImageWidth, newH));
        }
    }

    QImage img = reader.read();
    if (img.isNull())
        return {};

    // Scheme chosen deliberately: "qrc:" is Qt's own resource scheme and
    // receives internal normalisation during markdown import; "mdimg:/"
    // is a plain opaque-path URI that survives every round-trip
    // untouched.
    const QUrl uri(QStringLiteral("mdimg:/%1").arg(counter++));
    return { uri, img };
}

struct Replacement
{
    qsizetype start  = 0;
    qsizetype length = 0;
    QString   text;
};

} // namespace

ResolveResult ImageResolver::resolveRelativeImages(
    const QString &markdown,
    const std::filesystem::path &baseDir,
    const ResolveOptions &options)
{
    ResolveResult out;
    out.text = markdown;
    if (markdown.isEmpty())
        return out;

    // Fast path: if the text contains no image markers at all, skip the
    // two regex passes entirely.  This is a real win for prose-only
    // documents — the regexes take 10-20 ms on a 32 KB file, and there
    // is nothing to find.
    //
    // "![" is the start of every markdown image; "<img" is the start of
    // an HTML image.  A file that has neither has no image references.
    // This check is what makes a no-image file's ImageResolver cost
    // effectively zero.
    if (!markdown.contains(QStringLiteral("!["))
        && !markdown.contains(QStringLiteral("<img"), Qt::CaseInsensitive)) {
        return out;
    }

    QList<Replacement> replacements;
    int counter = 0;

    // ---- 1. Markdown inline-image syntax -----------------------------
    //
    // Matches:
    //   ![alt](path)
    //   ![alt](<path with spaces>)
    //   ![alt](path "title")
    //   ![alt](<path> 'title')
    //
    // Group 1 is the <...> form, group 2 the bare form.  At most one of
    // the two is non-empty per match; the `wrapped` flag below picks the
    // group that matched.
    static const QRegularExpression mdRe{
        QStringLiteral(
            R"RX(!\[[^\]]*\]\(\s*(?:<([^>]+)>|([^)\s]+))(?:\s+(?:"[^"]*"|'[^']*'))?\s*\))RX")
    };

    for (auto it = mdRe.globalMatch(markdown); it.hasNext(); ) {
        const QRegularExpressionMatch m = it.next();
        const bool      wrapped = m.capturedStart(1) >= 0;
        const qsizetype start   = wrapped ? m.capturedStart(1)  : m.capturedStart(2);
        const qsizetype length  = wrapped ? m.capturedLength(1) : m.capturedLength(2);
        const QString   path    = wrapped ? m.captured(1)       : m.captured(2);

        const ResolvedImage r =
            resolveOne(path, baseDir, options, counter);
        if (!r.url.isEmpty()) {
            out.images.append(r);
            replacements.append({ start, length, r.url.toString() });
        }
    }

    // ---- 2. HTML <img> syntax ----------------------------------------
    //
    // Matches <img src="..."> and <img src='...'>, case-insensitive.
    // `\s+src` rather than `\bsrc` excludes the `data-src="..."` lazy-
    // loading attribute, which is not a real image source.
    static const QRegularExpression htmlRe{
        QStringLiteral(R"RX(<img\b[^>]*?\s+src\s*=\s*["']([^"']+)["'])RX"),
        QRegularExpression::CaseInsensitiveOption
    };

    for (auto it = htmlRe.globalMatch(markdown); it.hasNext(); ) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype start  = m.capturedStart(1);
        const qsizetype length = m.capturedLength(1);
        const QString   path   = m.captured(1);

        const ResolvedImage r =
            resolveOne(path, baseDir, options, counter);
        if (!r.url.isEmpty()) {
            out.images.append(r);
            replacements.append({ start, length, r.url.toString() });
        }
    }

    if (replacements.isEmpty())
        return out;

    // ---- 3. Apply rewrites left-to-right ------------------------------
    //
    // Sorting by source offset ensures an earlier rewrite does not
    // invalidate a later one.  The `r.start < cursor` guard is
    // defensive: overlapping matches should not occur, but if they ever
    // do, dropping the later one is safer than corrupting the output.
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement &a, const Replacement &b) {
                  return a.start < b.start;
              });

    QString result;
    // +64 because rewritten URIs are typically longer than the relative
    // paths they replace; the exact number is not critical.
    result.reserve(markdown.size() + 64);

    qsizetype cursor = 0;
    for (const Replacement &r : replacements) {
        if (r.start < cursor)
            continue;
        result.append(markdown.mid(cursor, r.start - cursor));
        result.append(r.text);
        cursor = r.start + r.length;
    }
    result.append(markdown.mid(cursor));

    // ---- 4. Work around two QTextDocument::setMarkdown() quirks ------
    //
    // See the header's @note for the reasoning; the patterns below
    // implement the two rewrites exactly as described there.
    result.replace(QStringLiteral("![]("), QStringLiteral("![image]("));

    static const QRegularExpression headingImageRe{
        QStringLiteral(R"(^(#{1,6}[ \t][^\n]*)\n(!\[))"),
        QRegularExpression::MultilineOption
    };
    result.replace(headingImageRe, QStringLiteral("\\1\n\n\\2"));

    out.text = result;
    return out;
}
