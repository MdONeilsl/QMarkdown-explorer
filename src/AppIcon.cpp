#include "AppIcon.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

namespace {

// Resource path of the compile-time embedded icon.  Kept as a constant
// rather than a literal at the call site so a future move of the qrc
// entry is a one-line change.
constexpr const char *kResourcePath = ":/icons/app.png";

// External candidates, tried in order after the resource misses.  The
// order reflects decode quality: PNG first (lossless, scales cleanly),
// ICO second (multi-frame but blockier under non-native scale), then the
// lossy and niche formats.  The bare "icon" entry is last because
// content sniffing is a fallback, not a first choice.
const char *const kExternalCandidates[] = {
    "icon.png",
    "icon.ico",
    "icon.svg",
    "icon.jpg",
    "icon.jpeg",
    "icon.bmp",
    "icon.webp",
    "icon",
};

} // namespace

namespace AppIcon
{

LoadResult load()
{
    LoadResult result;

    // Resource first: a deployed build must never depend on a loose
    // file being present next to the binary.
    {
        QIcon icon(QString::fromLatin1(kResourcePath));
        if (!icon.isNull()) {
            result.icon = icon;
            result.path = QString::fromLatin1(kResourcePath);
            return result;
        }
    }

    // External file — development-time convenience only.
    if (!QCoreApplication::instance())
        return result;

    const QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty())
        return result;

    for (const char *name : kExternalCandidates) {
        const QString path = dir + QLatin1Char('/') + QLatin1String(name);

        if (!QFileInfo::exists(path))
            continue;

        // A corrupt or zero-byte placeholder in a candidate slot must
        // not shadow a later valid candidate; falling through is
        // deliberate, not an oversight.
        QIcon icon(path);
        if (icon.isNull())
            continue;

        result.icon = icon;
        result.path = path;
        return result;
    }

    return result;
}

LoadResult loadAndApply()
{
    LoadResult result = load();

    // qApp is null outside a live QApplication; the guard makes this
    // function safe to call from test harnesses that use a bare
    // QCoreApplication.
    if (!result.icon.isNull() && qApp)
        QApplication::setWindowIcon(result.icon);

    return result;
}

} // namespace AppIcon
