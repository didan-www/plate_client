#pragma once

#include "domain/RecognitionSnapshot.h"

#include <QByteArray>

#include <optional>

class TechnicalLogger;

class RecognitionEventCodec final
{
public:
    explicit RecognitionEventCodec(TechnicalLogger *logger);

    [[nodiscard]] std::optional<RecognitionSnapshot> decode(const QByteArray &payload) const;

private:
    TechnicalLogger *logger_;
};
