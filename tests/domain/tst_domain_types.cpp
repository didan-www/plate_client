#include "domain/AccessListRecord.h"
#include "domain/HttpEnvelope.h"
#include "domain/PageResult.h"
#include "domain/RecognitionSnapshot.h"
#include "domain/RequestContext.h"
#include "domain/SessionContext.h"

#include <QtTest>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<SessionContext>);
static_assert(!std::is_copy_assignable_v<SessionContext>);
static_assert(std::is_move_constructible_v<SessionContext>);
static_assert(std::is_move_assignable_v<SessionContext>);

class DomainTypesTest final : public QObject
{
    Q_OBJECT

private slots:
    void storesSessionAndMqttCredentialsInOneContext();
    void representsRecognitionNullableFields();
    void representsAccessListAndPagination();
    void capturesRequestFilterByValue();
    void representsEnvelopeDataAndEmptyResponse();
};

void DomainTypesTest::storesSessionAndMqttCredentialsInOneContext()
{
    const QDateTime expiresAt = QDateTime::fromString(
        QStringLiteral("2026-08-12T18:00:00+08:00"), Qt::ISODate);
    const SessionContext session(QStringLiteral("操作员"), QStringLiteral("access-token"),
                                 expiresAt, QStringLiteral("broker.test"), 8883, true,
                                 QStringLiteral("mqtt-user"), QStringLiteral("mqtt-password"),
                                 QStringLiteral("plate/events"));

    QCOMPARE(session.displayName(), QStringLiteral("操作员"));
    QCOMPARE(session.accessToken(), QStringLiteral("access-token"));
    QCOMPARE(session.expiresAt(), expiresAt);
    QCOMPARE(session.mqttHost(), QStringLiteral("broker.test"));
    QCOMPARE(session.mqttPort(), quint16(8883));
    QVERIFY(session.mqttTls());
    QCOMPARE(session.mqttUsername(), QStringLiteral("mqtt-user"));
    QCOMPARE(session.mqttPassword(), QStringLiteral("mqtt-password"));
    QCOMPARE(session.mqttEventTopic(), QStringLiteral("plate/events"));
    QCOMPARE(session.generation(), quint64(0));
}

void DomainTypesTest::representsRecognitionNullableFields()
{
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-12T10:00:00+08:00"), Qt::ISODate);
    const RecognitionSnapshot snapshot(
        1, QStringLiteral("rec-1"), 2, QStringLiteral("device-1"),
        RecognitionStatus::Processing, std::nullopt, std::nullopt, std::nullopt,
        capturedAt, capturedAt.addSecs(1), std::nullopt, std::nullopt);

    QCOMPARE(snapshot.schemaVersion(), 1);
    QCOMPARE(snapshot.recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(snapshot.revision(), qint64(2));
    QCOMPARE(snapshot.status(), RecognitionStatus::Processing);
    QVERIFY(!snapshot.plateNumber().has_value());
    QVERIFY(!snapshot.completedAt().has_value());
}

void DomainTypesTest::representsAccessListAndPagination()
{
    const QDateTime createdAt = QDateTime::fromString(
        QStringLiteral("2026-08-12T11:00:00+08:00"), Qt::ISODate);
    QList<AccessListRecord> items;
    items.append(AccessListRecord(7, AccessListType::White, QStringLiteral("京A12345"),
                                  QString(), QStringLiteral("operator"), createdAt));
    const PageResult<AccessListRecord> page(std::move(items), 1, 100, 1);

    QCOMPARE(page.page(), 1);
    QCOMPARE(page.pageSize(), 100);
    QCOMPARE(page.total(), qint64(1));
    QCOMPARE(page.items().size(), 1);
    QCOMPARE(page.items().first().listType(), AccessListType::White);
    QVERIFY(page.items().first().remark().isEmpty());
}

void DomainTypesTest::capturesRequestFilterByValue()
{
    QVariantMap filter {{QStringLiteral("deviceId"), QStringLiteral("device-1")},
                        {QStringLiteral("page"), 2}};
    const RequestContext context(3, 9, QStringLiteral("history.query"),
                                 QStringLiteral("rec-1"), filter);
    filter.insert(QStringLiteral("page"), 4);

    QCOMPARE(context.sessionGeneration(), quint64(3));
    QCOMPARE(context.requestGeneration(), quint64(9));
    QCOMPARE(context.operation(), QStringLiteral("history.query"));
    QCOMPARE(*context.recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(context.filterSnapshot().value(QStringLiteral("page")).toInt(), 2);
}

void DomainTypesTest::representsEnvelopeDataAndEmptyResponse()
{
    const HttpEnvelope<EmptyResponse> success(
        true, QStringLiteral("OK"), QString(), QStringLiteral("request-1"), EmptyResponse {});
    const HttpEnvelope<EmptyResponse> failure(
        false, QStringLiteral("NOT_FOUND"), QStringLiteral("not found"),
        QStringLiteral("request-2"), std::nullopt);

    QVERIFY(success.success());
    QVERIFY(success.data().has_value());
    QVERIFY(!failure.success());
    QVERIFY(!failure.data().has_value());
}

QTEST_MAIN(DomainTypesTest)

#include "tst_domain_types.moc"
