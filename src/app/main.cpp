#include "app/Application.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PlateRecognitionTeachingSystem"));
    QCoreApplication::setApplicationName(QStringLiteral("plate_client"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLATE_CLIENT_VERSION));

    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
    }

    Application runtime;
    if (!runtime.startFromApplicationDirectory()) {
        return 1;
    }

    return application.exec();
}
