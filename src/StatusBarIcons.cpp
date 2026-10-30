#include "StatusBarIcons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStyle>

namespace StatusBarIcons
{

QIcon qtLogoIcon()
{
    // A project-supplied override wins.  Present when the user has
    // uncommented the qt.png line in resources.qrc; absent otherwise,
    // which is the documented default.
    {
        QIcon icon(QStringLiteral(":/icons/qt.png"));
        if (!icon.isNull())
            return icon;
    }

    // Library fallback.  SP_TitleBarMenuButton is the standard icon the
    // active style draws for the window’s title-bar menu button — on
    // Windows and Linux that is the application window icon rendered by
    // the style, which for an un-.rc’d Qt app is the Qt logo.  Chosen
    // over other candidates (SP_ComputerIcon, SP_DesktopIcon) because it
    // is the only one guaranteed to reflect the app itself on every
    // platform.
    if (qApp)
        return qApp->style()->standardIcon(QStyle::SP_TitleBarMenuButton);

    return QIcon();
}

QIcon githubIcon()
{
    // Resource only.  No fallback is deliberately provided — a wrong
    // brand mark is worse than a missing one, and the caller already
    // handles a null return (see MainWindow::buildStatusBar).
    return QIcon(QStringLiteral(":/icons/github.png"));
}

} // namespace StatusBarIcons
