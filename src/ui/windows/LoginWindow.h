#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

    [[nodiscard]] QString username() const;
    [[nodiscard]] QString password() const;
    void setUsername(const QString &username);
    void setSubmitting(bool submitting);
    void showError(const QString &message);
    void clearPassword();

signals:
    void loginRequested(const QString &username, const QString &password);

private:
    void submit();

    QLineEdit *usernameEdit_;
    QLineEdit *passwordEdit_;
    QPushButton *loginButton_;
    QLabel *errorLabel_;
    bool submitting_ = false;
};
