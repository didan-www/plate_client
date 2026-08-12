#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PlateRecognitionTeachingSystem"));
    QCoreApplication::setApplicationName(QStringLiteral("plate_client"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLATE_CLIENT_VERSION));

    QWidget window;
    window.setWindowTitle(QStringLiteral("plate_client"));
    window.resize(640, 360);

    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel(
        QStringLiteral("plate_client %1\nBuild %2")
            .arg(QStringLiteral(PLATE_CLIENT_VERSION), QStringLiteral(PLATE_CLIENT_BUILD_DATE)),
        &window);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    window.show();

    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
