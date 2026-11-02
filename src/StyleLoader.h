#pragma once

#include <QString>
#include <QStringList>

#include <filesystem>

/**
 * @brief Runtime-loaded QSS style sheets for the application chrome.
 *
 * At construction the loader scans a directory (conventionally
 * `<exe-dir>/styles`) for `*.qss` files, recording each file’s stem as an
 * available style name.  `apply()` reads a chosen `.qss` file and hands it
 * to `QApplication::setStyleSheet()`; an empty name clears the
 * application style sheet and reverts to the platform default.
 *
 * These stylesheets control the widget chrome only — window background,
 * toolbar, tree view, combo box, status bar, scroll bars.  They do not
 * reach inside the QTextEdit that renders markdown content: QSS is a
 * widget-level mechanism and cannot target document elements.  Content
 * styling (code blocks, tables, headings as rendered) is a separate
 * concern and is not handled by this class.
 *
 * The loader is a pure Qt consumer: every filesystem interaction goes
 * through `std::filesystem`, so the same code runs unchanged on Windows,
 * Linux and macOS.  It holds no `QObject` state and creates none.
 *
 * Ownership: the caller owns the `StyleLoader`.  The application uses a
 * single function-local instance in `MainWindow.cpp`; nothing in this
 * class allocates `QObjects`.
 */
class StyleLoader
{
public:
    /**
     * @brief Constructs a loader bound to `directory`.
     *
     * The directory is scanned immediately; results are available through
     * `availableStyles()`.  A missing or unreadable directory is not an
     * error — the loader behaves as if it contained no `.qss` files, and
     * `apply()` on any name will fall back to the platform default.
     *
     * @param directory  Native filesystem path.  The caller is responsible
     *                   for any conversion from a Qt string (typically
     *                   `QCoreApplication::applicationDirPath() +
     *                   "/styles"`).
     */
    explicit StyleLoader(const std::filesystem::path &directory);

    /// @return The directory this loader was constructed with.
    const std::filesystem::path &directory() const noexcept;

    /**
     * @brief The `.qss` file stems discovered during construction.
     *
     * Sorted lexicographically, case-insensitively.  Example: a directory
     * containing `default.qss`, `dark.qss`, `light.qss` yields
     * `{ "dark", "default", "light" }`.
     *
     * @return An empty list if the directory did not exist or contained
     *         no `.qss` files.
     */
    QStringList availableStyles() const;

    /**
     * @brief Applies the named style to the whole application via
     *        `qApp->setStyleSheet()`.
     *
     * Every failure path — empty name, missing file, unreadable file,
     * empty file — produces the same outcome: the stylesheet is cleared
     * and the platform default applies.  This is deliberate.  Leaving the
     * previous stylesheet in place would mean the UI shows a style that
     * does not correspond to the current selection, which is worse than
     * the platform default.
     *
     * @param styleName  Bare style name as reported by
     *                   `availableStyles()` (extension-less).  Matching
     *                   is case-insensitive; `Dark` and `dark` both
     *                   resolve to `dark.qss`.
     *
     * @note The `.qss` file is re-read on every call.  Editing a file and
     *       re-selecting it in the combo box picks up the change without
     *       restarting the application — a small courtesy that costs
     *       nothing at the frequency this is called at.
     */
    void apply(const QString &styleName) const;

    /**
     * @brief Full path to the `.qss` file backing `styleName`.
     *
     * @return An empty path if no such file exists.  Useful for
     *         diagnostics and for a caller that wants to report the
     *         resolved path in a message.
     */
    std::filesystem::path pathFor(const QString &styleName) const;

private:
    std::filesystem::path m_directory;
    QStringList           m_styles;   // sorted, extension-less names
};