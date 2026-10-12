#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

namespace {

// Loads the interface translation chosen in the View > Language menu (or the system language).
void installTranslators(QApplication &app)
{
    QString code = qEnvironmentVariable("BABYLON_LANG"); // handy for testing; otherwise the saved choice
    if (code.isEmpty())
        code = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("auto")).toString();
    if (code == QLatin1String("auto"))
        code = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    if (code == QLatin1String("en"))
        return;

    auto *qt = new QTranslator(&app);
    if (qt->load(QStringLiteral("qtbase_") + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(qt);
    auto *own = new QTranslator(&app);
    if (own->load(QStringLiteral(":/i18n/babylon_%1.qm").arg(code)))
        app.installTranslator(own);
}

}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("FalloutBabylon"));
    QApplication::setApplicationName(QStringLiteral("FalloutBabylon"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 16-64 px come from icon.jpg, 128-512 px from icon_hd.jpg (see tools/make_icons.py).
    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QStringLiteral(":/icons/icons/icon_%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(appIcon);

    installTranslators(app);
    Theme::apply(Theme::savedMode());

    MainWindow window;
    window.show();
    QTimer::singleShot(0, &window, &MainWindow::restoreSession);
    return app.exec();
}
