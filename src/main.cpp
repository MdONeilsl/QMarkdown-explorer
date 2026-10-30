#include "AppIcon.h"
#include "MainWindow.h"
#include "config.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QTranslator>

#include <algorithm>
#include <cstdio>

namespace {

/**
 * @brief Loads a translation catalogue from the first search path that
 *        contains it.
 *
 * @param translator  Receives the loaded catalogue.
 * @param baseName    Catalogue base name, without extension (e.g.
 *                    `markdown_explorer_de`).
 * @param searchPaths Directories to try in order.
 *
 * @return `true` if a catalogue was loaded into `translator`.
 */
bool loadFromFirstAvailable(QTranslator &translator,
                            const QString &baseName,
                            const QStringList &searchPaths)
{
    for (const QString &path : searchPaths) {
        if (translator.load(baseName, path))
            return true;
    }
    return false;
}

/**
 * @brief Expands a locale name into a fallback chain, most-specific first.
 *
 * Examples:
 *   "zh_CN"      → { "zh_CN", "zh" }
 *   "sr_Latn_RS" → { "sr_Latn_RS", "sr_Latn", "sr" }
 *   "de"         → { "de" }
 *
 * The chain lets the loader find `markdown_explorer_zh.qm` when only the
 * Chinese system reports `zh_CN`, and `markdown_explorer_sr_Latn.qm` when
 * a Serbian-Latin system reports `sr_Latn_RS`.  Duplicates are removed;
 * order is preserved.
 */
QStringList expandLocaleChain(const QString &localeName)
{
    QStringList chain;
    const QStringList parts = localeName.split(u'_', Qt::SkipEmptyParts);
    for (int i = static_cast<int>(parts.size()); i > 0; --i) {
        const QString candidate = parts.mid(0, i).join(u'_');
        if (!chain.contains(candidate))
            chain.append(candidate);
    }
    return chain;
}

/**
 * @brief Whether any entry in `chain` is an English locale.
 *
 * English is the source language, so `tr()` returns the literal with no
 * catalogue needed.  Tested against every element so `en_GB`, `en_US`,
 * etc. all skip.
 */
bool chainIsEnglish(const QStringList &chain)
{
    return std::any_of(chain.cbegin(), chain.cend(),
                       [](const QString &c) {
                           return c.compare(QStringLiteral("en"),
                                            Qt::CaseInsensitive) == 0;
                       });
}

/**
 * @brief Prints every application language catalogue found under
 *        `<exe-dir>/translations/`.
 *
 * Output is one line per catalogue: `code<TAB>Language name`.  Written
 * to stdout with the system text encoding so it is readable on every
 * platform’s console.  On Windows, this requires launching from a
 * terminal (cmd.exe, PowerShell, Windows Terminal); launching from
 * Explorer discards the output silently, which is standard for GUI apps.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE when the translations
 *         directory is missing or empty.
 */
int printAvailableLanguages()
{
    const QDir dir(QCoreApplication::applicationDirPath()
                   + QStringLiteral("/translations"));
    if (!dir.exists()) {
        qCritical().noquote()
            << QStringLiteral("translations directory not found: %1")
                   .arg(dir.absolutePath());
        return EXIT_FAILURE;
    }

    const QStringList qms = dir.entryList(
        {QStringLiteral("markdown_explorer_*.qm")},
        QDir::Files,
        QDir::Name);
    if (qms.isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("no language catalogues in %1")
                   .arg(dir.absolutePath());
        return EXIT_FAILURE;
    }

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::System);
    for (const QString &qm : qms) {
        // markdown_explorer_zh_CN.qm -> zh_CN
        QString code = qm;
        code.remove(0, static_cast<qsizetype>(
                           QStringLiteral("markdown_explorer_").size()));
        code.chop(3);   // ".qm"

        const QLocale loc(code);
        out << code << u'\t'
            << QLocale::languageToString(loc.language()) << u'\n';
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Prints every `.qss` file found under `<exe-dir>/styles/`.
 *
 * One name per line, in lexicographic order.  Same encoding and Windows
 * caveats as printAvailableLanguages().
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE when the styles
 *         directory is missing or empty.
 */
int printAvailableStyles()
{
    const QDir dir(QCoreApplication::applicationDirPath()
                   + QStringLiteral("/styles"));
    if (!dir.exists()) {
        qCritical().noquote()
            << QStringLiteral("styles directory not found: %1")
                   .arg(dir.absolutePath());
        return EXIT_FAILURE;
    }

    const QStringList qss = dir.entryList(
        {QStringLiteral("*.qss")}, QDir::Files, QDir::Name);
    if (qss.isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("no styles in %1").arg(dir.absolutePath());
        return EXIT_FAILURE;
    }

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::System);
    for (const QString &file : qss) {
        QString name = file;
        name.chop(4);   // ".qss"
        out << name << u'\n';
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Application identity, sourced from the CMake-generated config.h.
    // Must be set before anything keys off it — QSettings, the locale
    // preference below, and QCommandLineParser::addVersionOption()’s
    // output all do.
    QCoreApplication::setOrganizationName(QStringLiteral("MarkdownExplorer"));
    QCoreApplication::setOrganizationDomain(
        QStringLiteral("markdown-explorer.local"));
    QCoreApplication::setApplicationName(QStringLiteral(APP_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // -----------------------------------------------------------------
    //  Command line
    //
    //  QCommandLineParser provides --help / --version for free once
    //  addHelpOption() / addVersionOption() are called; process() prints
    //  them and exits before any of our own logic runs.
    //
    //  CLI text is not translated.  Rationale: the project’s “every
    //  user-facing string is wrapped in tr()” rule applies to the GUI;
    //  CLI help is a developer-facing tool, and translating it into 47
    //  languages would triple the catalogue size for negligible gain.
    //  If that changes, swap QStringLiteral for
    //  QCoreApplication::translate("main", ...) here and run lupdate.
    // -----------------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(APP_DESCRIPTION));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption folderOpt(
        QStringList{QStringLiteral("f"), QStringLiteral("folder")},
        QStringLiteral("Open <path> directly, skipping the folder picker."),
        QStringLiteral("path"));
    QCommandLineOption languageOpt(
        QStringList{QStringLiteral("l"), QStringLiteral("language")},
        QStringLiteral("Override the UI language (e.g. de, zh_CN, sr_Latn). "
                       "Persisted for subsequent launches."),
        QStringLiteral("locale"));
    QCommandLineOption styleOpt(
        QStringList{QStringLiteral("s"), QStringLiteral("style")},
        QStringLiteral("Override the starting style (name of a .qss file). "
                       "Use an empty value to reset to the platform default. "
                       "Persisted for subsequent launches."),
        QStringLiteral("name"));
    QCommandLineOption noPickerOpt(
        QStringLiteral("no-picker"),
        QStringLiteral("Open the window without showing the folder picker."));
    QCommandLineOption listLanguagesOpt(
        QStringLiteral("list-languages"),
        QStringLiteral("Print the available language catalogues and exit."));
    QCommandLineOption listStylesOpt(
        QStringLiteral("list-styles"),
        QStringLiteral("Print the available style names and exit."));

    parser.addOption(folderOpt);
    parser.addOption(languageOpt);
    parser.addOption(styleOpt);
    parser.addOption(noPickerOpt);
    parser.addOption(listLanguagesOpt);
    parser.addOption(listStylesOpt);
    parser.process(app);

    // -----------------------------------------------------------------
    //  Informational commands (early exit)
    //
    //  These write to stdout and return without opening a window.  They
    //  run before any QSettings writes so they cannot change the user’s
    //  persisted state as a side effect of a query.
    // -----------------------------------------------------------------
    if (parser.isSet(listLanguagesOpt))
        return printAvailableLanguages();
    if (parser.isSet(listStylesOpt))
        return printAvailableStyles();

    // -----------------------------------------------------------------
    //  Apply state-changing overrides to QSettings
    //
    //  MainWindow reads LastFolder and Style from QSettings in its
    //  constructor; writing them here means the overrides take effect
    //  without any change to MainWindow’s construction contract.
    //
    //  LaunchFolder / SkipInitialPicker are transient signals:
    //  MainWindow’s deferred picker consumes and clears them, then
    //  proceeds normally.  They are deliberately distinct from
    //  LastFolder so a bad --folder or a one-off --no-picker cannot
    //  corrupt the durable “remember this” settings.
    // -----------------------------------------------------------------
    QSettings settings;

    if (parser.isSet(folderOpt)) {
        const QString raw = parser.value(folderOpt).trimmed();
        const QFileInfo info(raw);
        if (raw.isEmpty() || !info.isDir()) {
            qCritical().noquote()
                << QStringLiteral("--folder: not a directory: %1").arg(raw);
            return EXIT_FAILURE;
        }
        const QString absolute = info.absoluteFilePath();
        settings.setValue(QStringLiteral("LaunchFolder"), absolute);
        settings.setValue(QStringLiteral("LastFolder"), absolute);
    }

    if (parser.isSet(languageOpt)) {
        // An empty value means “fall back to the system locale”, which
        // is what an unset QSettings key does.
        settings.setValue(QStringLiteral("Language"),
                          parser.value(languageOpt).trimmed());
    }

    if (parser.isSet(styleOpt)) {
        const QString style = parser.value(styleOpt).trimmed();

        // Warn (but do not abort) when the requested style file does
        // not exist.  MainWindow falls back to the platform default in
        // that case, so the launch still succeeds — the warning just
        // explains why the style did not apply.
        if (!style.isEmpty()) {
            const QString qssPath =
                QCoreApplication::applicationDirPath()
                + QStringLiteral("/styles/")
                + style
                + QStringLiteral(".qss");
            if (!QFileInfo::exists(qssPath)) {
                qWarning().noquote()
                    << QStringLiteral("--style: file not found: %1")
                           .arg(qssPath);
                qWarning().noquote()
                    << QStringLiteral("--style: available styles live in %1")
                           .arg(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/styles/"));
            }
        }
        settings.setValue(QStringLiteral("Style"), style);
    }

    if (parser.isSet(noPickerOpt))
        settings.setValue(QStringLiteral("SkipInitialPicker"), true);

    settings.sync();

    // Install the window icon before any widget exists, so every
    // top-level window inherits it from its first frame.
    AppIcon::loadAndApply();

    // -----------------------------------------------------------------
    //  Locale and translation installation
    //
    //  Order matters here in three places:
    //
    //    1. The locale name is expanded into a fallback chain
    //       (“zh_CN” → {“zh_CN”, “zh”}), because the shipped catalogues
    //       follow the short form and the OS may report a longer one.
    //
    //    2. English is the source language, so a locale chain whose
    //       primary language is English short-circuits and no catalogue
    //       is loaded — tr() already returns the English literal.
    //
    //    3. qtbase_*.qm must be installed *before* the application
    //       catalogue.  Qt’s standard-dialog strings live in qtbase;
    //       installing our catalogue first would make Qt search our
    //       (incomplete) translation for those strings before falling
    //       back.
    //
    //  All of this must happen before MainWindow is constructed, so
    //  every tr() call inside the widget constructors resolves to the
    //  translated string on the first render.
    // -----------------------------------------------------------------

    QString localeName =
        settings.value(QStringLiteral("Language")).toString();
    if (localeName.isEmpty())
        localeName = QLocale::system().name();

    const QStringList candidates = expandLocaleChain(localeName);

    const QStringList searchPaths{
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
    };

    QTranslator qtTranslator;
    QTranslator appTranslator;
    bool        qtTranslatorInstalled = false;
    bool        appTranslatorInstalled = false;

    if (!chainIsEnglish(candidates)) {
        for (const QString &candidate : candidates) {
            if (!qtTranslatorInstalled) {
                if (loadFromFirstAvailable(
                        qtTranslator,
                        QStringLiteral("qtbase_%1").arg(candidate),
                        searchPaths)) {
                    QApplication::installTranslator(&qtTranslator);
                    qtTranslatorInstalled = true;
                }
            }
            if (!appTranslatorInstalled) {
                if (loadFromFirstAvailable(
                        appTranslator,
                        QStringLiteral("markdown_explorer_%1").arg(candidate),
                        searchPaths)) {
                    QApplication::installTranslator(&appTranslator);
                    appTranslatorInstalled = true;
                }
            }
            if (qtTranslatorInstalled && appTranslatorInstalled)
                break;
        }
    }

    MainWindow window;
    window.show();

    const int rc = QApplication::exec();

    // Detach translators before leaving main(): QCoreApplication tears
    // down before these stack objects go out of scope, and a stale
    // installed-translator pointer would be dereferenced during that
    // teardown.
    if (appTranslatorInstalled)
        QApplication::removeTranslator(&appTranslator);
    if (qtTranslatorInstalled)
        QApplication::removeTranslator(&qtTranslator);

    return rc;
}
