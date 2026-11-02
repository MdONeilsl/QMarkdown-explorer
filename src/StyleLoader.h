#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <filesystem>

/**
 * @brief Runtime-loaded QSS style sheets for the application chrome,
 *        plus the code-block background colour for the viewer.
 *
 * At construction the loader scans a directory (conventionally
 * `<exe-dir>/styles`) for `*.qss` files, recording each file’s stem as an
 * available style name.  `apply()` reads a chosen `.qss` file and hands it
 * to `QApplication::setStyleSheet()`; an empty name clears the
 * application style sheet and reverts to the platform default.
 *
 * These stylesheets control the widget chrome only — window background,
 * toolbar, tree view, combo box, status bar, scroll bars.  They do not
 * reach inside the QTextEdit that renders markdown content.
 *
 * `codeBlockBackground()` separately reads `code-<theme>.css` to obtain
 * the background colour applied to fenced code blocks.  Only the
 * `background` / `background-color` declaration on the `pre` rule is
 * extracted; the file is not otherwise applied to the document.  An
 * invalid QColor means “use Qt’s default” — the expected result for the
 * System theme, which has no code-*.css file.
 *
 * The loader is a pure Qt consumer: every filesystem interaction goes
 * through `std::filesystem`.  It holds no `QObject` state and creates
 * none.  The application uses a single function-local instance in
 * `MainWindow.cpp`.
 */
class StyleLoader
{
public:
    explicit StyleLoader(const std::filesystem::path &directory);

    /// @return The directory this loader was constructed with.
    const std::filesystem::path &directory() const noexcept;

    /// @return The `.qss` file stems discovered during construction.
    QStringList availableStyles() const;

    /// @brief Applies the named widget stylesheet via qApp->setStyleSheet().
    void apply(const QString &styleName) const;

    /// @return Full path to the `.qss` file backing `styleName`, or empty.
    std::filesystem::path pathFor(const QString &styleName) const;

    /**
     * @brief Reads the code-block background colour for a theme.
     *
     * Looks for `<styles>/code-<themeName>.css` and extracts the first
     * `background` or `background-color` declaration inside the `pre`
     * rule.  Only the colour is used — the file is not otherwise applied.
     *
     * @param themeName  Empty for System, `"dark"`, `"light"`, or any
     *                   other style name reported by the combo.  An empty
     *                   name always returns an invalid QColor.
     *
     * @return The extracted colour, or an invalid QColor when the file
     *         is missing, contains no `pre` rule, or the value cannot be
     *         parsed.  An invalid QColor is the expected “no code-block
     *         styling” signal.
     */
    QColor codeBlockBackground(const QString &themeName) const;

private:
    std::filesystem::path m_directory;
    QStringList           m_styles;
};