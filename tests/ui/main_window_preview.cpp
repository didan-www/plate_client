#include "ui/windows/MainWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PlateRecognitionTeachingSystem"));
    QCoreApplication::setApplicationName(QStringLiteral("plate_client_ui_preview"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLATE_CLIENT_VERSION));

    MainWindow window(QStringLiteral("UI 预览用户"));
    window.show();

    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
