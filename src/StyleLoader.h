#pragma once

#include <QString>
#include <QStringList>

#include <filesystem>

/**
 * @brief Runtime-loaded QSS style sheets, plus per-theme document CSS.
 *
 * The `.qss` scan and `apply()` behaviour are unchanged from earlier
 * versions.  The addition is `documentCss()`, which reads the CSS file
 * used to style *document content* (code blocks, in particular) for a
 * given theme.  Widget-level QSS cannot reach inside a QTextEdit's
 * rendered document, so the content styling lives in a separate file
 * per theme:
 *
 *     <styles>/code-<themeName>.css
 *
 * `documentCss("<theme>")` returns the contents of that file, or the
 * contents of `code-default.css` when the theme-specific file does not
 * exist, or an empty string when neither is present.  The caller
 * (`MainWindow`) passes the returned string to `MarkdownView::setTheme()`.
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
     * @brief Reads the document CSS for a theme.
     *
     * Lookup order: `code-<styleName>.css`, then `code-default.css`,
     * then nothing.  The returned text is what the caller hands to
     * `MarkdownView` — StyleLoader itself does not parse it.
     *
     * @return File contents, or an empty string when neither file exists.
     */
    QString documentCss(const QString &styleName) const;

private:
    std::filesystem::path m_directory;
    QStringList           m_styles;   // sorted, extension-less names
};