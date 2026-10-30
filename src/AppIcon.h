#pragma once

#include <QIcon>
#include <QString>

/**
 * @brief Application / window icon loading.
 *
 * Load order, first match wins:
 *
 *   1. `:/icons/app.png` — embedded at compile time via
 *      `resources/resources.qrc`.  Preferred for a deployed build: the
 *      icon travels inside the executable and cannot be lost.
 *
 *   2. An external `icon.*` file next to the executable.  Extensions
 *      tried in order: `.png`, `.ico`, `.svg`, `.jpg`, `.jpeg`, `.bmp`,
 *      `.webp`, then a bare `icon` whose format is sniffed from content.
 *      Useful during development: swap the icon without rebuilding.
 *
 * The external fallback is deliberately second, not first: a shipped
 * application must not depend on a loose file being present.  The
 * resource is the contract; the loose file is a convenience.
 *
 * @note This loader affects the *running* application — title bar,
 *       taskbar, system menu, Alt+Tab.  The icon Windows Explorer shows
 *       for the executable file itself is baked into the PE binary at
 *       link time (via `resources/app.rc.in`) and cannot be changed at
 *       runtime.
 */
namespace AppIcon
{

/**
 * @brief Result of a load attempt.
 *
 * Both members are empty / null when no usable source was found, so a
 * caller can test `icon.isNull()` and fall back to the platform default
 * without inspecting `path`.
 */
struct LoadResult
{
    QIcon   icon;   ///< Loaded icon, or a null QIcon on failure.
    QString path;   ///< Source that supplied the icon (resource path or
                    ///< filesystem path), or an empty string on failure.
};

/**
 * @brief Searches the resource system, then the executable's directory,
 *        and returns the first usable icon.
 *
 * Does not touch `QApplication`; use `loadAndApply()` when the icon
 * should be installed application-wide.
 *
 * @return A `LoadResult` whose `icon` is null when no source produced a
 *         valid QIcon.
 */
LoadResult load();

/**
 * @brief Convenience wrapper: calls `load()` and, if an icon was found,
 *        installs it via `QApplication::setWindowIcon()`.
 *
 * Safe to call once `QApplication` has been constructed; a no-op when no
 * icon source exists (the platform default then applies and the
 * application still runs normally).
 *
 * @return The same `LoadResult` that `load()` produced, so the caller can
 *         log the path or branch on success.
 */
LoadResult loadAndApply();

} // namespace AppIcon
