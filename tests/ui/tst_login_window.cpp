#include "ui/windows/LoginWindow.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

class LoginWindowTest final : public QObject
{
    Q_OBJECT

private slots:
    void hasRequiredControlsInOrder();
    void hasIndustrialTitleBarAndWindowControls();
    void loadsResourcesAndAlignsFormRows();
    void layoutFitsWithoutOverlap();
    void keepsButtonGeometryWhenErrorChanges();
    void passwordIsMaskedAndEnterSubmits();
    void preventsDuplicateSubmissionUntilCompleted();
    void displaysErrorsAndClearsPassword();
};

void LoginWindowTest::hasRequiredControlsInOrder()
{
    LoginWindow window;
    QCOMPARE(window.size(), QSize(520, 420));
    QCOMPARE(window.minimumSize(), QSize(520, 420));
    QCOMPARE(window.maximumSize(), QSize(520, 420));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("titleLabel"))->text(),
             QStringLiteral("车牌识别系统"));
    QVERIFY(window.findChild<QLineEdit *>(QStringLiteral("usernameEdit")) != nullptr);
    QVERIFY(window.findChild<QLineEdit *>(QStringLiteral("passwordEdit")) != nullptr);
    QCOMPARE(window.findChild<QPushButton *>(QStringLiteral("loginButton"))->text(),
             QStringLiteral("登录"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("usernameTextLabel"))->text(),
             QStringLiteral("用户名"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("passwordTextLabel"))->text(),
             QStringLiteral("密码"));
}

void LoginWindowTest::hasIndustrialTitleBarAndWindowControls()
{
    LoginWindow window;
    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(!window.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    auto *titleBar = window.findChild<QWidget *>(QStringLiteral("titleBar"));
    auto *minimize = window.findChild<QToolButton *>(QStringLiteral("minimizeButton"));
    auto *close = window.findChild<QToolButton *>(QStringLiteral("closeButton"));
    QVERIFY(titleBar != nullptr);
    QVERIFY(minimize != nullptr);
    QVERIFY(close != nullptr);
    QCOMPARE(titleBar->height(), 44);
    QCOMPARE(titleBar->palette().color(QPalette::Window), QColor(QStringLiteral("#1F5F99")));
    QCOMPARE(minimize->size(), QSize(44, 44));
    QCOMPARE(close->size(), QSize(44, 44));

    window.show();
    QTRY_VERIFY(window.isVisible());
    minimize->click();
    QTRY_VERIFY(window.isMinimized());
    window.showNormal();
    QTRY_VERIFY(!window.isMinimized());
    close->click();
    QTRY_VERIFY(!window.isVisible());
}

void LoginWindowTest::loadsResourcesAndAlignsFormRows()
{
    LoginWindow window;
    window.show();
    QTRY_VERIFY(window.isVisible());

    const QStringList resourcePaths {
        QStringLiteral(":/images/login/login_logo.png"),
        QStringLiteral(":/images/login/login_user.png"),
        QStringLiteral(":/images/login/login_pwd.png"),
    };
    for (const QString &path : resourcePaths) {
        const QPixmap pixmap(path);
        QVERIFY2(!pixmap.isNull(), qPrintable(path));
        QVERIFY(pixmap.width() >= 48);
        QVERIFY(pixmap.height() >= 48);
    }

    auto *body = window.findChild<QWidget *>(QStringLiteral("loginBody"));
    auto *brand = window.findChild<QWidget *>(QStringLiteral("brandWidget"));
    auto *logo = window.findChild<QLabel *>(QStringLiteral("logoLabel"));
    auto *usernameIcon = window.findChild<QLabel *>(QStringLiteral("usernameIconLabel"));
    auto *passwordIcon = window.findChild<QLabel *>(QStringLiteral("passwordIconLabel"));
    auto *usernameLabel = window.findChild<QLabel *>(QStringLiteral("usernameTextLabel"));
    auto *passwordLabel = window.findChild<QLabel *>(QStringLiteral("passwordTextLabel"));
    auto *username = window.findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
    auto *password = window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    QVERIFY(body != nullptr);
    QVERIFY(brand != nullptr);
    QVERIFY(logo != nullptr);
    QCOMPARE(logo->size(), QSize(32, 32));
    QCOMPARE(usernameIcon->size(), QSize(22, 22));
    QCOMPARE(passwordIcon->size(), QSize(22, 22));
    QVERIFY(!logo->pixmap().isNull());
    QVERIFY(!usernameIcon->pixmap().isNull());
    QVERIFY(!passwordIcon->pixmap().isNull());
    QVERIFY(qAbs(brand->geometry().center().x() - body->rect().center().x()) <= 1);

    QCOMPARE(usernameIcon->mapTo(&window, QPoint()).x(),
             passwordIcon->mapTo(&window, QPoint()).x());
    QCOMPARE(usernameLabel->mapTo(&window, QPoint()).x(),
             passwordLabel->mapTo(&window, QPoint()).x());
    QCOMPARE(username->mapTo(&window, QPoint()).x(), password->mapTo(&window, QPoint()).x());
    QCOMPARE(usernameLabel->alignment(), Qt::AlignLeft | Qt::AlignVCenter);
    QCOMPARE(passwordLabel->alignment(), Qt::AlignLeft | Qt::AlignVCenter);
    QCOMPARE(username->size(), QSize(288, 44));
    QCOMPARE(password->size(), QSize(288, 44));
}

void LoginWindowTest::layoutFitsWithoutOverlap()
{
    LoginWindow window;
    window.show();
    QTRY_VERIFY(window.isVisible());
    window.showError(QStringLiteral("登录失败，请检查用户名和密码后重试。"));
    QCoreApplication::processEvents();

    auto mappedGeometry = [&window](QWidget *widget) {
        return QRect(widget->mapTo(&window, QPoint()), widget->size());
    };
    const QStringList textLabelNames {
        QStringLiteral("windowTitleLabel"),
        QStringLiteral("titleLabel"),
        QStringLiteral("usernameTextLabel"),
        QStringLiteral("passwordTextLabel"),
        QStringLiteral("errorLabel"),
    };
    for (const QString &name : textLabelNames) {
        auto *label = window.findChild<QLabel *>(name);
        QVERIFY(label != nullptr);
        QVERIFY2(label->sizeHint().width() <= label->width(), qPrintable(name));
        QVERIFY2(label->sizeHint().height() <= label->height(), qPrintable(name));
    }

    auto *usernameIcon = window.findChild<QLabel *>(QStringLiteral("usernameIconLabel"));
    auto *passwordIcon = window.findChild<QLabel *>(QStringLiteral("passwordIconLabel"));
    auto *usernameLabel = window.findChild<QLabel *>(QStringLiteral("usernameTextLabel"));
    auto *passwordLabel = window.findChild<QLabel *>(QStringLiteral("passwordTextLabel"));
    auto *username = window.findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
    auto *password = window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("loginButton"));

    QVERIFY(!mappedGeometry(usernameIcon).intersects(mappedGeometry(usernameLabel)));
    QVERIFY(!mappedGeometry(usernameLabel).intersects(mappedGeometry(username)));
    QVERIFY(!mappedGeometry(passwordIcon).intersects(mappedGeometry(passwordLabel)));
    QVERIFY(!mappedGeometry(passwordLabel).intersects(mappedGeometry(password)));
    QVERIFY(!mappedGeometry(username).intersects(mappedGeometry(password)));
    QVERIFY(!mappedGeometry(password).intersects(mappedGeometry(error)));
    QVERIFY(!mappedGeometry(error).intersects(mappedGeometry(button)));

    window.setSubmitting(true);
    QCoreApplication::processEvents();
    QVERIFY(button->sizeHint().width() <= button->width());
    QVERIFY(button->sizeHint().height() <= button->height());
}

void LoginWindowTest::keepsButtonGeometryWhenErrorChanges()
{
    LoginWindow window;
    window.show();
    QTRY_VERIFY(window.isVisible());
    auto *button = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    const QRect initialButtonGeometry = button->geometry();
    QCOMPARE(button->size(), QSize(220, 48));
    const int buttonBottom = button->mapTo(&window, button->rect().bottomRight()).y();
    QCOMPARE(window.height() - buttonBottom - 1, 36);
    QCOMPARE(error->height(), 36);

    window.showError(QStringLiteral("登录失败，请重试。"));
    QCoreApplication::processEvents();
    QCOMPARE(button->geometry(), initialButtonGeometry);
    QVERIFY(error->isVisible());
    window.showError({});
    QCoreApplication::processEvents();
    QCOMPARE(button->geometry(), initialButtonGeometry);
}

void LoginWindowTest::passwordIsMaskedAndEnterSubmits()
{
    LoginWindow window;
    window.show();
    auto *username = window.findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
    auto *password = window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    username->setText(QStringLiteral("operator"));
    password->setText(QStringLiteral("secret"));
    QSignalSpy submitted(&window, &LoginWindow::loginRequested);
    QTest::keyClick(password, Qt::Key_Return);
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.first().at(0).toString(), QStringLiteral("operator"));
    QCOMPARE(submitted.first().at(1).toString(), QStringLiteral("secret"));
}

void LoginWindowTest::preventsDuplicateSubmissionUntilCompleted()
{
    LoginWindow window;
    auto *password = window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    window.setUsername(QStringLiteral("operator"));
    password->setText(QStringLiteral("secret"));
    QSignalSpy submitted(&window, &LoginWindow::loginRequested);
    button->click();
    button->click();
    QCOMPARE(submitted.count(), 1);
    QVERIFY(!button->isEnabled());
    QVERIFY(!password->isEnabled());
    window.setSubmitting(false);
    button->click();
    QCOMPARE(submitted.count(), 2);
}

void LoginWindowTest::displaysErrorsAndClearsPassword()
{
    LoginWindow window;
    auto *password = window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    password->setText(QStringLiteral("secret"));
    window.showError(QStringLiteral("登录失败"));
    QCOMPARE(error->text(), QStringLiteral("登录失败"));
    QVERIFY(!error->isHidden());
    window.clearPassword();
    QVERIFY(password->text().isEmpty());
}

QTEST_MAIN(LoginWindowTest)

#include "tst_login_window.moc"
