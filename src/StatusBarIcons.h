#pragma once

#include <QIcon>

/**
 * @brief Brand icons for the status-bar buttons.
 *
 * Sources, in priority order:
 *
 *   1. Embedded resources under `:/icons/` — see
 *      `resources/resources.qrc`.  `github.png` is required;
 *      `qt.png` is optional and only used to override the Qt button’s
 *      library-provided default.
 *
 *   2. Qt-library-provided default, for `qtLogoIcon()` only, via
 *      `QStyle::standardIcon()`.  This is the intended default: no
 *      extra asset is needed for the Qt button to render correctly on
 *      every platform.
 *
 * The GitHub mark has no library fallback.  A third-party brand that is
 * not actually supplied should not be replaced by a lookalike — the
 * caller decides how to present a missing icon (hide the button, show
 * text-only, surface an error).  This is why `githubIcon()` may return a
 * null `QIcon` while `qtLogoIcon()` never does in a working
 * `QApplication` context.
 */
namespace StatusBarIcons
{

/**
 * @brief Qt brand mark.
 *
 * @return `:/icons/qt.png` if the resource was compiled in, otherwise the
 *         active style’s standard window-menu icon.  The fallback is
 *         available on every platform supported by Qt and requires no
 *         asset of our own.
 *
 * @note The returned icon is chosen once per call; no caching is done.
 *       Both sources are cheap to resolve and there is no stale-state
 *       hazard if the caller re-fetches after a style change.
 */
QIcon qtLogoIcon();

/**
 * @brief GitHub brand mark.
 *
 * @return `:/icons/github.png`.  A null `QIcon` means the resource was
 *         not compiled in — a build error, not a runtime condition, but
 *         detected here so the caller can present something sensible.
 */
QIcon githubIcon();

} // namespace StatusBarIcons
