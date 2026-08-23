#pragma once

#include "domain/RecognitionSnapshot.h"

#include <QList>
#include <QObject>

#include <optional>

class SessionService;
class SessionSummary;

class RealtimeRecognitionStore final : public QObject
{
    Q_OBJECT

public:
    explicit RealtimeRecognitionStore(SessionService *sessionService,
                                      QObject *parent = nullptr);

    [[nodiscard]] bool apply(quint64 sessionGeneration, RecognitionSnapshot snapshot);
    [[nodiscard]] const QList<RecognitionSnapshot> &items() const noexcept;
    [[nodiscard]] const std::optional<RecognitionSnapshot> &current() const noexcept;
    [[nodiscard]] quint64 activeSessionGeneration() const noexcept;

signals:
    void itemsChanged();
    void currentChanged();

private:
    void activate(const SessionSummary &summary);
    void reset(quint64 sessionGeneration);

    SessionService *sessionService_;
    QList<RecognitionSnapshot> items_;
    std::optional<RecognitionSnapshot> current_;
    quint64 activeSessionGeneration_ = 0;
};
