#pragma once

#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"

#include <QList>

class MockMqttClient final : public MqttClientInterface
{
    Q_OBJECT

public:
    struct Subscription final
    {
        QString topic;
        quint8 qos = 0;
    };

    explicit MockMqttClient(QObject *parent = nullptr)
        : MqttClientInterface(parent)
    {
    }

    void configure(quint64 sessionGeneration,
                   const MqttClientConfiguration &configuration) override
    {
        ++configureCount;
        configuredGeneration = sessionGeneration;
        lastConfiguration = configuration;
    }

    void connectToHost() override { ++connectCount; }
    void disconnectFromHost() override { ++disconnectCount; }

    bool subscribe(const QString &topic, quint8 qos) override
    {
        subscriptions.append({topic, qos});
        return acceptSubscriptionRequest;
    }

    void clearCredentials() override
    {
        ++clearCredentialsCount;
        lastConfiguration.username.fill(QChar::Null);
        lastConfiguration.username.clear();
        lastConfiguration.password.fill(QChar::Null);
        lastConfiguration.password.clear();
    }

    void reportConnected(quint64 generation) { emit connected(generation); }
    void reportDisconnected(quint64 generation) { emit disconnected(generation); }
    void reportFailure(quint64 generation, MqttErrorCode code)
    {
        emit failed(generation, code);
    }
    void reportSubscriptionSucceeded(quint64 generation)
    {
        emit subscriptionSucceeded(generation);
    }
    void reportSubscriptionFailed(quint64 generation)
    {
        emit subscriptionFailed(generation);
    }
    void reportMessage(quint64 generation, const QString &topic, const QByteArray &payload)
    {
        emit messageReceived(generation, topic, payload);
    }

    int configureCount = 0;
    int connectCount = 0;
    int disconnectCount = 0;
    int clearCredentialsCount = 0;
    quint64 configuredGeneration = 0;
    bool acceptSubscriptionRequest = true;
    MqttClientConfiguration lastConfiguration;
    QList<Subscription> subscriptions;
};
