#include "domain/SessionContext.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "services/SessionService.h"
#include "support/MockMqttClient.h"

#include <QSignalSpy>
#include <QtTest>

#include <chrono>

using namespace std::chrono_literals;

namespace {
SessionContext session(bool tls = true, const QString &topic = QStringLiteral("plate/events"))
{
    return SessionContext(QStringLiteral("测试操作员"),
                          QStringLiteral("access-secret"),
                          QDateTime::currentDateTime().addSecs(3600),
                          QStringLiteral("broker.test"),
                          tls ? 8883 : 1883,
                          tls,
                          QStringLiteral("mqtt-user"),
                          QStringLiteral("mqtt-secret"),
                          topic);
}
} // namespace

class MqttSubscriberTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void configuresMqtt311AndWaitsForSubscription();
    void subscriptionFailureIsPermanent();
    void transientErrorsUseBoundedExponentialBackoff();
    void credentialsAreClearedAndReinjectedForReconnect();
    void permanentErrors_data();
    void permanentErrors();
    void rejectsPlainMqttUnlessExplicitlyAllowed();
    void invalidationCancelsReconnectAndClearsCredentials();
    void newSessionResetsPermanentError();
    void staleGenerationCallbacksAreIgnored();
    void forwardsOnlyCurrentSubscribedTopic();
};

void MqttSubscriberTest::initTestCase()
{
    qRegisterMetaType<MqttError>();
    qRegisterMetaType<MqttErrorCode>();
    qRegisterMetaType<MqttConnectivityState>();
}

void MqttSubscriberTest::configuresMqtt311AndWaitsForSubscription()
{
    SessionService sessions;
    MockMqttClient client;
    const QString clientId = QStringLiteral("12345678-1234-4ABC-8DEF-1234567890AB");
    MqttRecognitionSubscriber subscriber(clientId, false, &sessions, &client, 1ms);

    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    QCOMPARE(client.configureCount, 1);
    QCOMPARE(client.connectCount, 1);
    QCOMPARE(client.lastConfiguration.host, QStringLiteral("broker.test"));
    QCOMPARE(client.lastConfiguration.port, quint16(8883));
    QVERIFY(client.lastConfiguration.tls);
    QCOMPARE(client.lastConfiguration.clientId, clientId);
    QCOMPARE(client.lastConfiguration.protocolVersion, 4);
    QVERIFY(client.lastConfiguration.cleanSession);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);

    client.reportConnected(generation);
    QVERIFY(client.lastConfiguration.username.isEmpty());
    QVERIFY(client.lastConfiguration.password.isEmpty());
    QCOMPARE(client.subscriptions.size(), 1);
    QCOMPARE(client.subscriptions.constFirst().topic, QStringLiteral("plate/events"));
    QCOMPARE(client.subscriptions.constFirst().qos, quint8(1));
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);

    client.reportSubscriptionSucceeded(generation);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Connected);
    QCOMPARE(subscriber.reconnectAttempt(), 0);
    QVERIFY(sessions.hasActiveSession());
}

void MqttSubscriberTest::subscriptionFailureIsPermanent()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QSignalSpy reconnects(&subscriber, &MqttRecognitionSubscriber::reconnectScheduled);
    QSignalSpy errors(&subscriber, &MqttRecognitionSubscriber::permanentError);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();

    client.reportConnected(generation);
    client.reportSubscriptionFailed(generation);
    QCOMPARE(subscriber.state(), MqttConnectivityState::PermanentError);
    QCOMPARE(reconnects.count(), 0);
    QCOMPARE(errors.count(), 1);
    const auto *error = static_cast<const MqttError *>(errors.at(0).at(0).constData());
    QCOMPARE(error->code(), MqttErrorCode::SubscriptionRejected);
    QVERIFY(error->isPermanent());
    QVERIFY(sessions.hasActiveSession());
}

void MqttSubscriberTest::transientErrorsUseBoundedExponentialBackoff()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QSignalSpy reconnects(&subscriber, &MqttRecognitionSubscriber::reconnectScheduled);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    const QList<int> expected {1, 2, 4, 8, 16, 32, 60, 60};

    for (qsizetype index = 0; index < expected.size(); ++index) {
        const MqttErrorCode code = index % 2 == 0
                                       ? MqttErrorCode::NetworkUnavailable
                                       : MqttErrorCode::BrokerUnavailable;
        const int previousConnects = client.connectCount;
        client.reportFailure(generation, code);
        QTRY_COMPARE(reconnects.count(), int(index + 1));
        QCOMPARE(reconnects.at(index).at(0).toInt(), expected.at(index));
        QCOMPARE(subscriber.scheduledReconnectSeconds(), expected.at(index));
        QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);
        QTRY_COMPARE(client.connectCount, previousConnects + 1);
        QCOMPARE(client.configureCount, int(index + 2));
        QCOMPARE(client.lastConfiguration.username, QStringLiteral("mqtt-user"));
        QCOMPARE(client.lastConfiguration.password, QStringLiteral("mqtt-secret"));
    }
    QCOMPARE(subscriber.reconnectAttempt(), 6);
    QVERIFY(sessions.hasActiveSession());
}

void MqttSubscriberTest::credentialsAreClearedAndReinjectedForReconnect()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 20ms);
    QSignalSpy reconnects(&subscriber, &MqttRecognitionSubscriber::reconnectScheduled);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    client.reportConnected(generation);
    client.reportSubscriptionSucceeded(generation);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Connected);
    QVERIFY(client.lastConfiguration.username.isEmpty());
    QVERIFY(client.lastConfiguration.password.isEmpty());

    const int previousConnects = client.connectCount;
    client.reportDisconnected(generation);
    client.reportFailure(generation, MqttErrorCode::NetworkUnavailable);

    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);
    QCOMPARE(reconnects.count(), 1);
    QCOMPARE(reconnects.constFirst().at(0).toInt(), 1);
    QTRY_COMPARE(client.connectCount, previousConnects + 1);
    QCOMPARE(client.configureCount, 2);
    QCOMPARE(client.lastConfiguration.username, QStringLiteral("mqtt-user"));
    QCOMPARE(client.lastConfiguration.password, QStringLiteral("mqtt-secret"));

    client.reportConnected(generation);
    QVERIFY(client.lastConfiguration.username.isEmpty());
    QVERIFY(client.lastConfiguration.password.isEmpty());
    client.reportSubscriptionSucceeded(generation);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Connected);
    QVERIFY(sessions.hasActiveSession());
}

void MqttSubscriberTest::permanentErrors_data()
{
    QTest::addColumn<MqttErrorCode>("code");
    QTest::newRow("bad credentials") << MqttErrorCode::BadCredentials;
    QTest::newRow("not authorized") << MqttErrorCode::NotAuthorized;
    QTest::newRow("client id") << MqttErrorCode::ClientIdRejected;
    QTest::newRow("protocol") << MqttErrorCode::ProtocolUnsupported;
    QTest::newRow("tls certificate") << MqttErrorCode::TlsCertificateRejected;
    QTest::newRow("protocol violation") << MqttErrorCode::ProtocolViolation;
    QTest::newRow("unknown") << MqttErrorCode::Unknown;
}

void MqttSubscriberTest::permanentErrors()
{
    QFETCH(MqttErrorCode, code);
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QSignalSpy reconnects(&subscriber, &MqttRecognitionSubscriber::reconnectScheduled);
    QVERIFY(sessions.establishSession(session()));

    client.reportFailure(sessions.currentGeneration(), code);
    QCOMPARE(subscriber.state(), MqttConnectivityState::PermanentError);
    QCOMPARE(reconnects.count(), 0);
    QVERIFY(sessions.hasActiveSession());
    QVERIFY(client.lastConfiguration.username.isEmpty());
    QVERIFY(client.lastConfiguration.password.isEmpty());
    const int connects = client.connectCount;
    QTest::qWait(10);
    QCOMPARE(client.connectCount, connects);
}

void MqttSubscriberTest::rejectsPlainMqttUnlessExplicitlyAllowed()
{
    SessionService blockedSessions;
    MockMqttClient blockedClient;
    MqttRecognitionSubscriber blocked(QStringLiteral("client-id"), false,
                                      &blockedSessions, &blockedClient, 1ms);
    QVERIFY(blockedSessions.establishSession(session(false)));
    QCOMPARE(blocked.state(), MqttConnectivityState::PermanentError);
    QCOMPARE(blocked.statusReason(), QStringLiteral("不允许明文连接"));
    QCOMPARE(blockedClient.configureCount, 0);
    QCOMPARE(blockedClient.connectCount, 0);
    QVERIFY(blockedSessions.hasActiveSession());

    SessionService allowedSessions;
    MockMqttClient allowedClient;
    MqttRecognitionSubscriber allowed(QStringLiteral("client-id"), true,
                                      &allowedSessions, &allowedClient, 1ms);
    QVERIFY(allowedSessions.establishSession(session(false)));
    QCOMPARE(allowed.state(), MqttConnectivityState::Reconnecting);
    QCOMPARE(allowedClient.configureCount, 1);
    QCOMPARE(allowedClient.connectCount, 1);
    QVERIFY(!allowedClient.lastConfiguration.tls);
}

void MqttSubscriberTest::invalidationCancelsReconnectAndClearsCredentials()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 20ms);
    QVERIFY(sessions.establishSession(session()));
    client.reportFailure(sessions.currentGeneration(), MqttErrorCode::NetworkUnavailable);
    QCOMPARE(subscriber.scheduledReconnectSeconds(), 1);
    const int connects = client.connectCount;

    sessions.clear();
    QCOMPARE(subscriber.state(), MqttConnectivityState::Disconnected);
    QVERIFY(!subscriber.isRunning());
    QVERIFY(client.lastConfiguration.username.isEmpty());
    QVERIFY(client.lastConfiguration.password.isEmpty());
    QTest::qWait(40);
    QCOMPARE(client.connectCount, connects);
}

void MqttSubscriberTest::newSessionResetsPermanentError()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QVERIFY(sessions.establishSession(session()));
    client.reportFailure(sessions.currentGeneration(), MqttErrorCode::BadCredentials);
    QCOMPARE(subscriber.state(), MqttConnectivityState::PermanentError);
    const quint64 oldGeneration = sessions.currentGeneration();

    QVERIFY(sessions.establishSession(session()));
    QVERIFY(sessions.currentGeneration() != oldGeneration);
    QCOMPARE(client.configureCount, 2);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);
    const quint64 newGeneration = sessions.currentGeneration();
    client.reportConnected(newGeneration);
    client.reportSubscriptionSucceeded(newGeneration);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Connected);
}

void MqttSubscriberTest::staleGenerationCallbacksAreIgnored()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QVERIFY(sessions.establishSession(session()));
    const quint64 oldGeneration = sessions.currentGeneration();
    client.reportFailure(oldGeneration, MqttErrorCode::BadCredentials);
    QVERIFY(sessions.establishSession(session()));
    const quint64 newGeneration = sessions.currentGeneration();
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);

    client.reportConnected(oldGeneration);
    client.reportFailure(oldGeneration, MqttErrorCode::TlsCertificateRejected);
    client.reportSubscriptionSucceeded(oldGeneration);
    QCOMPARE(client.subscriptions.size(), 0);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);

    client.reportConnected(newGeneration);
    QCOMPARE(client.subscriptions.size(), 1);
    client.reportSubscriptionSucceeded(oldGeneration);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Reconnecting);
    client.reportSubscriptionSucceeded(newGeneration);
    QCOMPARE(subscriber.state(), MqttConnectivityState::Connected);
}

void MqttSubscriberTest::forwardsOnlyCurrentSubscribedTopic()
{
    SessionService sessions;
    MockMqttClient client;
    MqttRecognitionSubscriber subscriber(QStringLiteral("client-id"), false,
                                         &sessions, &client, 1ms);
    QSignalSpy events(&subscriber, &MqttRecognitionSubscriber::eventPayloadReceived);
    QVERIFY(sessions.establishSession(session()));
    const quint64 oldGeneration = sessions.currentGeneration();
    const QByteArray payload = QByteArrayLiteral("{\"schemaVersion\":1}");

    client.reportMessage(oldGeneration, QStringLiteral("plate/events"), payload);
    client.reportConnected(oldGeneration);
    client.reportMessage(oldGeneration, QStringLiteral("plate/events"), payload);
    QCOMPARE(events.count(), 0);

    client.reportSubscriptionSucceeded(oldGeneration);
    client.reportMessage(oldGeneration, QStringLiteral("plate/other"), payload);
    client.reportMessage(oldGeneration - 1, QStringLiteral("plate/events"), payload);
    QCOMPARE(events.count(), 0);
    client.reportMessage(oldGeneration, QStringLiteral("plate/events"), payload);
    QCOMPARE(events.count(), 1);
    QCOMPARE(events.constFirst().at(0).toULongLong(), oldGeneration);
    QCOMPARE(events.constFirst().at(1).toByteArray(), payload);

    QVERIFY(sessions.establishSession(session()));
    const quint64 newGeneration = sessions.currentGeneration();
    client.reportMessage(oldGeneration, QStringLiteral("plate/events"), payload);
    client.reportMessage(newGeneration, QStringLiteral("plate/events"), payload);
    QCOMPARE(events.count(), 1);
    client.reportConnected(newGeneration);
    client.reportSubscriptionSucceeded(newGeneration);
    client.reportMessage(newGeneration, QStringLiteral("plate/events"), payload);
    QCOMPARE(events.count(), 2);
}

QTEST_MAIN(MqttSubscriberTest)

#include "tst_mqtt_subscriber.moc"
