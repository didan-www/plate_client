#include "ui/widgets/AccessListCreateDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

class AccessListCreateDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsInheritedListTypeAndValidatesInput();
    void submittingPreventsDuplicateSubmitAndClosing();
};

void AccessListCreateDialogTest::showsInheritedListTypeAndValidatesInput()
{
    AccessListCreateDialog dialog(AccessListType::Black);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    QCOMPARE(dialog.listType(), AccessListType::Black);
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("accessListCreateTypeLabel"))->text(),
             QStringLiteral("黑名单"));

    auto *plate = dialog.findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"));
    auto *remark = dialog.findChild<QPlainTextEdit *>(
        QStringLiteral("accessListCreateRemarkEdit"));
    auto *submit = dialog.findChild<QPushButton *>(
        QStringLiteral("accessListCreateSubmitButton"));
    auto *error = dialog.findChild<QLabel *>(QStringLiteral("accessListCreateErrorLabel"));
    QVERIFY(plate && remark && submit && error);
    QSignalSpy submitted(&dialog, &AccessListCreateDialog::submitRequested);

    submit->click();
    QCOMPARE(submitted.count(), 0);
    QVERIFY(!error->text().isEmpty());

    plate->setText(QStringLiteral("京A 12345"));
    submit->click();
    QCOMPARE(submitted.count(), 0);

    plate->setText(QStringLiteral("  京a12345  "));
    remark->setPlainText(QString(200, QChar(0x4e2d)));
    submit->click();
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.first().at(0).toString(), QStringLiteral("  京a12345  "));
    QCOMPARE(submitted.first().at(1).toString().size(), 200);

    remark->setPlainText(QString(201, QChar(0x4e2d)));
    submit->click();
    QCOMPARE(submitted.count(), 1);
    QVERIFY(error->text().contains(QStringLiteral("200")));
}

void AccessListCreateDialogTest::submittingPreventsDuplicateSubmitAndClosing()
{
    AccessListCreateDialog dialog(AccessListType::White);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    auto *plate = dialog.findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"));
    auto *submit = dialog.findChild<QPushButton *>(
        QStringLiteral("accessListCreateSubmitButton"));
    auto *cancel = dialog.findChild<QPushButton *>(
        QStringLiteral("accessListCreateCancelButton"));
    QVERIFY(plate && submit && cancel);
    QSignalSpy submitted(&dialog, &AccessListCreateDialog::submitRequested);
    QSignalSpy rejected(&dialog, &QDialog::rejected);

    plate->setText(QStringLiteral("京A12345"));
    submit->click();
    QCOMPARE(submitted.count(), 1);
    dialog.setSubmitting(true);
    QVERIFY(dialog.isSubmitting());
    QVERIFY(!plate->isEnabled());
    QVERIFY(!submit->isEnabled());
    QVERIFY(!cancel->isEnabled());
    submit->click();
    dialog.reject();
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(rejected.count(), 0);

    dialog.setSubmitting(false);
    dialog.reject();
    QCOMPARE(rejected.count(), 1);
}

QTEST_MAIN(AccessListCreateDialogTest)

#include "tst_access_list_dialog.moc"
