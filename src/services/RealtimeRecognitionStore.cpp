#include "services/RealtimeRecognitionStore.h"

#include "services/SessionService.h"

#include <utility>

namespace {
constexpr qsizetype maximumRealtimeItems = 100;

bool isFinal(RecognitionStatus status)
{
    return status == RecognitionStatus::Succeeded || status == RecognitionStatus::Failed;
}
} // namespace

RealtimeRecognitionStore::RealtimeRecognitionStore(SessionService *sessionService,
                                                   QObject *parent)
    : QObject(parent)
    , sessionService_(sessionService)
{
    Q_ASSERT(sessionService_);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &RealtimeRecognitionStore::activate);
    connect(sessionService_, &SessionService::sessionInvalidated, this,
            [this](SessionInvalidationReason, quint64 generation) {
                if (generation == activeSessionGeneration_) {
                    reset(0);
                }
            });

    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

bool RealtimeRecognitionStore::apply(quint64 sessionGeneration,
                                     RecognitionSnapshot snapshot)
{
    if (sessionGeneration == 0 || sessionGeneration != activeSessionGeneration_) {
        return false;
    }

    const QString recognitionId = snapshot.recognitionId();
    qsizetype existingIndex = -1;
    for (qsizetype index = 0; index < items_.size(); ++index) {
        if (items_.at(index).recognitionId() == recognitionId) {
            existingIndex = index;
            break;
        }
    }

    bool changesCurrent = false;
    if (existingIndex >= 0) {
        const RecognitionSnapshot &existing = items_.at(existingIndex);
        if (snapshot.revision() <= existing.revision()
            || (isFinal(existing.status())
                && snapshot.status() == RecognitionStatus::Processing)) {
            return false;
        }

        items_[existingIndex] = std::move(snapshot);
        const RecognitionSnapshot &updated = items_.at(existingIndex);
        if (updated.status() == RecognitionStatus::Processing) {
            current_ = updated;
            changesCurrent = true;
        } else if (current_ && current_->recognitionId() == recognitionId) {
            current_ = updated;
            changesCurrent = true;
        }
    } else {
        items_.prepend(std::move(snapshot));
        if (items_.constFirst().status() == RecognitionStatus::Processing) {
            current_ = items_.constFirst();
            changesCurrent = true;
        }
        if (items_.size() > maximumRealtimeItems) {
            const QString evictedId = items_.constLast().recognitionId();
            items_.removeLast();
            if (current_ && current_->recognitionId() == evictedId) {
                current_.reset();
                changesCurrent = true;
            }
        }
    }

    emit itemsChanged();
    if (changesCurrent) {
        emit currentChanged();
    }
    return true;
}

const QList<RecognitionSnapshot> &RealtimeRecognitionStore::items() const noexcept
{
    return items_;
}

const std::optional<RecognitionSnapshot> &RealtimeRecognitionStore::current() const noexcept
{
    return current_;
}

quint64 RealtimeRecognitionStore::activeSessionGeneration() const noexcept
{
    return activeSessionGeneration_;
}

void RealtimeRecognitionStore::activate(const SessionSummary &summary)
{
    reset(summary.generation());
}

void RealtimeRecognitionStore::reset(quint64 sessionGeneration)
{
    const bool hadItems = !items_.isEmpty();
    const bool hadCurrent = current_.has_value();
    items_.clear();
    current_.reset();
    activeSessionGeneration_ = sessionGeneration;
    if (hadItems) {
        emit itemsChanged();
    }
    if (hadCurrent) {
        emit currentChanged();
    }
}
