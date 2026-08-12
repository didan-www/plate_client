#pragma once

#include "domain/AccessListRecord.h"
#include "domain/Errors.h"
#include "domain/HttpEnvelope.h"
#include "domain/PageResult.h"
#include "domain/RecognitionSnapshot.h"
#include "domain/SessionContext.h"

#include <QByteArray>
#include <QString>
#include <QUuid>

class JsonCodecs final
{
public:
    [[nodiscard]] static ProtocolResult<QByteArray> encodeLoginRequest(
        const QString &username, const QString &password, const QUuid &clientId);
    [[nodiscard]] static ProtocolResult<QByteArray> encodeAccessListCreateRequest(
        AccessListType listType, const QString &plateNumber, const QString &remark);

    [[nodiscard]] static ProtocolResult<RecognitionSnapshot> decodeRecognitionSnapshot(
        const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<SessionContext>> decodeLoginEnvelope(
        const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<RecognitionSnapshot>>
    decodeRecognitionEnvelope(const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<PageResult<RecognitionSnapshot>>>
    decodeRecognitionPageEnvelope(const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<AccessListRecord>>
    decodeAccessListEnvelope(const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<PageResult<AccessListRecord>>>
    decodeAccessListPageEnvelope(const QByteArray &json);
    [[nodiscard]] static ProtocolResult<HttpEnvelope<EmptyResponse>> decodeEmptyEnvelope(
        const QByteArray &json);

private:
    JsonCodecs() = delete;
};
