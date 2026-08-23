#include "services/AccessListService.h"

#include "infrastructure/http/AccessListApiClient.h"
#include "services/SessionService.h"

#include <QChar>
#include <QList>

#include <utility>

namespace {
QString listTypeText(AccessListType listType)
{
    switch (listType) {
    case AccessListType::White:
        return QStringLiteral("WHITE");
    case AccessListType::Black:
        return QStringLiteral("BLACK");
    }
    return {};
}

bool isUnicodeWhitespace(char32_t codePoint)
{
    if ((codePoint >= 0x0009U && codePoint <= 0x000DU) || codePoint == 0x0085U) {
        return true;
    }
    const QChar::Category category = QChar::category(codePoint);
    return category == QChar::Separator_Space || category == QChar::Separator_Line
           || category == QChar::Separator_Paragraph;
}

bool isWellFormedUtf16(const QString &value)
{
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar current = value.at(index);
        if (current.isHighSurrogate()) {
            if (index + 1 >= value.size() || !value.at(index + 1).isLowSurrogate()) {
                return false;
            }
            ++index;
        } else if (current.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

std::optional<QString> normalizePlateLike(const QString &value, bool allowEmpty)
{
    if (!isWellFormedUtf16(value)) {
        return std::nullopt;
    }
    const QList<uint> sourceCodePoints = value.toUcs4();
    QList<char32_t> codePoints;
    codePoints.reserve(sourceCodePoints.size());
    for (const uint codePoint : sourceCodePoints) {
        codePoints.append(static_cast<char32_t>(codePoint));
    }
    qsizetype first = 0;
    while (first < codePoints.size() && isUnicodeWhitespace(codePoints.at(first))) {
        ++first;
    }
    qsizetype last = codePoints.size();
    while (last > first && isUnicodeWhitespace(codePoints.at(last - 1))) {
        --last;
    }
    if ((!allowEmpty && first == last) || last - first > 16) {
        return std::nullopt;
    }

    QList<char32_t> normalized;
    normalized.reserve(last - first);
    for (qsizetype index = first; index < last; ++index) {
        char32_t codePoint = codePoints.at(index);
        if (isUnicodeWhitespace(codePoint)) {
            return std::nullopt;
        }
        if (codePoint >= U'a' && codePoint <= U'z') {
            codePoint -= U'a' - U'A';
        }
        normalized.append(codePoint);
    }
    return QString::fromUcs4(normalized.constData(), normalized.size());
}

const QList<AccessListRecord> &emptyItems()
{
    static const QList<AccessListRecord> empty;
    return empty;
}

void advanceGeneration(quint64 &generation)
{
    ++generation;
    if (generation == 0) {
        ++generation;
    }
}

AccessListServiceErrorCode serviceErrorCode(AccessListApiErrorCode code)
{
    switch (code) {
    case AccessListApiErrorCode::InvalidRequest:
        return AccessListServiceErrorCode::InvalidInput;
    case AccessListApiErrorCode::Unauthorized:
        return AccessListServiceErrorCode::Unauthorized;
    case AccessListApiErrorCode::Forbidden:
        return AccessListServiceErrorCode::Forbidden;
    case AccessListApiErrorCode::NotFound:
        return AccessListServiceErrorCode::NotFound;
    case AccessListApiErrorCode::ConflictWhite:
        return AccessListServiceErrorCode::ConflictWhite;
    case AccessListApiErrorCode::ConflictBlack:
        return AccessListServiceErrorCode::ConflictBlack;
    case AccessListApiErrorCode::ClientRejected:
        return AccessListServiceErrorCode::ClientRejected;
    case AccessListApiErrorCode::ServerError:
        return AccessListServiceErrorCode::ServerError;
    case AccessListApiErrorCode::NetworkError:
        return AccessListServiceErrorCode::NetworkError;
    case AccessListApiErrorCode::TlsError:
        return AccessListServiceErrorCode::TlsError;
    case AccessListApiErrorCode::Timeout:
        return AccessListServiceErrorCode::Timeout;
    case AccessListApiErrorCode::ResponseTooLarge:
        return AccessListServiceErrorCode::ResponseTooLarge;
    case AccessListApiErrorCode::Cancelled:
    case AccessListApiErrorCode::StaleResponse:
    case AccessListApiErrorCode::ProtocolError:
        return AccessListServiceErrorCode::ProtocolError;
    }
    return AccessListServiceErrorCode::ProtocolError;
}

AccessListServiceError serviceError(const AccessListApiError &error)
{
    return AccessListServiceError(serviceErrorCode(error.code()),
                                  error.message(), error.serverCode(), error.requestId());
}

AccessListServiceError invalidInput(const QString &message)
{
    return AccessListServiceError(AccessListServiceErrorCode::InvalidInput, message);
}
} // namespace

AccessListFilter::AccessListFilter(AccessListType listType, QString keyword, int page)
    : listType_(listType)
    , keyword_(std::move(keyword))
    , page_(page)
{
}

AccessListType AccessListFilter::listType() const noexcept
{
    return listType_;
}

const QString &AccessListFilter::keyword() const noexcept
{
    return keyword_;
}

int AccessListFilter::page() const noexcept
{
    return page_;
}

QVariantMap AccessListFilter::snapshot() const
{
    return QVariantMap {{QStringLiteral("listType"), listTypeText(listType_)},
                        {QStringLiteral("keyword"), keyword_},
                        {QStringLiteral("page"), page_},
                        {QStringLiteral("pageSize"), AccessListService::pageSize}};
}

bool operator==(const AccessListFilter &left, const AccessListFilter &right) noexcept
{
    return left.listType_ == right.listType_ && left.keyword_ == right.keyword_
           && left.page_ == right.page_;
}

QVariantMap AccessListService::WriteCommand::snapshot() const
{
    QVariantMap result {{QStringLiteral("operation"),
                         operation == AccessListWriteOperation::Create
                             ? QStringLiteral("create")
                             : QStringLiteral("delete")},
                        {QStringLiteral("listType"), listTypeText(listType)},
                        {QStringLiteral("plateNumber"), plateNumber}};
    if (operation == AccessListWriteOperation::Create) {
        result.insert(QStringLiteral("remark"), remark);
    }
    if (id) {
        result.insert(QStringLiteral("id"), *id);
    }
    return result;
}

bool AccessListService::WriteCommand::operator==(const WriteCommand &other) const noexcept
{
    return operation == other.operation && listType == other.listType
           && plateNumber == other.plateNumber && remark == other.remark
           && id == other.id;
}

AccessListService::AccessListService(AccessListApiClient *accessListApiClient,
                                     SessionService *sessionService,
                                     QObject *parent)
    : QObject(parent)
    , accessListApiClient_(accessListApiClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(accessListApiClient_);
    Q_ASSERT(sessionService_);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &AccessListService::activate);
    connect(sessionService_, &SessionService::sessionInvalidated,
            this, &AccessListService::invalidate);
    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

AccessListService::~AccessListService()
{
    QPointer<AccessListPageRequestHandle> query = activeQuery_;
    QPointer<AccessListCreateRequestHandle> createRequest = activeCreate_;
    QPointer<AccessListDeleteRequestHandle> deleteRequest = activeDelete_;
    QPointer<AccessListLookupRequestHandle> verification = activeVerification_;
    activeQuery_.clear();
    activeCreate_.clear();
    activeDelete_.clear();
    activeVerification_.clear();
    writeCommand_.reset();
    if (query) query->cancel();
    if (createRequest) createRequest->cancel();
    if (deleteRequest) deleteRequest->cancel();
    if (verification) verification->cancel();
}

std::optional<QString> AccessListService::normalizePlateNumber(const QString &value)
{
    return normalizePlateLike(value, false);
}

std::optional<QString> AccessListService::normalizeKeyword(const QString &value)
{
    return normalizePlateLike(value, true);
}

bool AccessListService::isValidRemark(const QString &value)
{
    return isWellFormedUtf16(value) && value.toUcs4().size() <= 200;
}

const std::optional<AccessListFilter> &
AccessListService::lastSubmittedFilter() const noexcept
{
    return lastSubmittedFilter_;
}

const std::optional<PageResult<AccessListRecord>> &
AccessListService::lastSuccessfulPage() const noexcept
{
    return lastSuccessfulPage_;
}

const QList<AccessListRecord> &AccessListService::items() const noexcept
{
    return lastSuccessfulPage_ ? lastSuccessfulPage_->items() : emptyItems();
}

bool AccessListService::isQueryLoading() const noexcept
{
    return queryLoading_;
}

bool AccessListService::isWriteBusy() const noexcept
{
    return writeBusy_;
}

bool AccessListService::hasActiveQuery() const noexcept
{
    return !activeQuery_.isNull();
}

bool AccessListService::canGoPrevious() const noexcept
{
    return lastSuccessfulPage_ && lastSuccessfulPage_->page() > 1;
}

bool AccessListService::canGoNext() const noexcept
{
    return lastSuccessfulPage_
           && static_cast<qint64>(lastSuccessfulPage_->page()) * pageSize
                  < lastSuccessfulPage_->total();
}

bool AccessListService::search(AccessListType listType, const QString &keyword)
{
    const auto normalized = normalizeKeyword(keyword);
    if (listTypeText(listType).isEmpty() || !normalized) {
        emit queryFailed(invalidInput(QStringLiteral("名单查询条件无效。")));
        return false;
    }
    submitQuery(AccessListFilter(listType, *normalized, 1));
    return true;
}

void AccessListService::refresh()
{
    if (lastSubmittedFilter_) {
        submitQuery(*lastSubmittedFilter_);
    }
}

bool AccessListService::previousPage()
{
    if (!canGoPrevious() || !lastSubmittedFilter_) {
        return false;
    }
    submitQuery(AccessListFilter(lastSubmittedFilter_->listType(),
                                 lastSubmittedFilter_->keyword(),
                                 lastSuccessfulPage_->page() - 1));
    return true;
}

bool AccessListService::nextPage()
{
    if (!canGoNext() || !lastSubmittedFilter_) {
        return false;
    }
    submitQuery(AccessListFilter(lastSubmittedFilter_->listType(),
                                 lastSubmittedFilter_->keyword(),
                                 lastSuccessfulPage_->page() + 1));
    return true;
}

bool AccessListService::create(AccessListType listType,
                               const QString &plateNumber,
                               const QString &remark)
{
    if (writeBusy_) {
        emit writeFailed(AccessListServiceError(AccessListServiceErrorCode::Busy,
                                                QStringLiteral("已有名单写操作正在进行。")));
        return false;
    }
    const auto normalizedPlate = normalizePlateNumber(plateNumber);
    if (listTypeText(listType).isEmpty() || !normalizedPlate || !isValidRemark(remark)) {
        emit writeFailed(invalidInput(QStringLiteral("名单新增输入无效。")));
        return false;
    }

    WriteCommand command {AccessListWriteOperation::Create,
                          listType,
                          *normalizedPlate,
                          remark,
                          std::nullopt};
    advanceGeneration(writeGeneration_);
    const quint64 generation = writeGeneration_;
    writeCommand_ = command;
    QPointer<AccessListService> guarded(this);
    setWriteBusy(true);
    if (!guarded) {
        return false;
    }

    AccessListCreateRequestHandle *request = accessListApiClient_->create(
        listType, command.plateNumber, remark, sessionGeneration_, generation);
    activeCreate_ = request;
    connect(request, &AccessListCreateRequestHandle::succeeded, this,
            [this, request, generation, command] {
                if (activeCreate_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               generation,
                                               command,
                                               command.snapshot())
                    || !request->record()) {
                    return;
                }
                activeCreate_.clear();
                finishWrite(AccessListWriteResult(command.operation,
                                                  AccessListWriteDisposition::Applied,
                                                  command.listType,
                                                  command.plateNumber,
                                                  command.id,
                                                  *request->record()),
                            generation,
                            command);
            });
    connect(request, &AccessListCreateRequestHandle::failed, this,
            [this, request, generation, command](const AccessListApiError &error) {
                if (activeCreate_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               generation,
                                               command,
                                               command.snapshot())) {
                    return;
                }
                activeCreate_.clear();
                if (error.code() == AccessListApiErrorCode::Timeout) {
                    beginVerification(generation, command);
                } else if (error.code() == AccessListApiErrorCode::Cancelled
                           || error.code() == AccessListApiErrorCode::StaleResponse) {
                    cancelWriteInternal();
                } else {
                    failWrite(serviceError(error), generation, command);
                }
            });
    return true;
}

bool AccessListService::remove(const AccessListRecord &record)
{
    if (writeBusy_) {
        emit writeFailed(AccessListServiceError(AccessListServiceErrorCode::Busy,
                                                QStringLiteral("已有名单写操作正在进行。")));
        return false;
    }
    const auto normalizedPlate = normalizePlateNumber(record.plateNumber());
    if (record.id() <= 0 || listTypeText(record.listType()).isEmpty()
        || !normalizedPlate || *normalizedPlate != record.plateNumber()) {
        emit writeFailed(invalidInput(QStringLiteral("名单删除目标无效。")));
        return false;
    }

    WriteCommand command {AccessListWriteOperation::Delete,
                          record.listType(),
                          *normalizedPlate,
                          {},
                          record.id()};
    advanceGeneration(writeGeneration_);
    const quint64 generation = writeGeneration_;
    writeCommand_ = command;
    QPointer<AccessListService> guarded(this);
    setWriteBusy(true);
    if (!guarded) {
        return false;
    }

    AccessListDeleteRequestHandle *request = accessListApiClient_->remove(
        record.id(), record.listType(), command.plateNumber,
        sessionGeneration_, generation);
    activeDelete_ = request;
    connect(request, &AccessListDeleteRequestHandle::succeeded, this,
            [this, request, generation, command] {
                if (activeDelete_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               generation,
                                               command,
                                               command.snapshot())) {
                    return;
                }
                activeDelete_.clear();
                finishWrite(AccessListWriteResult(command.operation,
                                                  AccessListWriteDisposition::Applied,
                                                  command.listType,
                                                  command.plateNumber,
                                                  command.id),
                            generation,
                            command);
            });
    connect(request, &AccessListDeleteRequestHandle::failed, this,
            [this, request, generation, command](const AccessListApiError &error) {
                if (activeDelete_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               generation,
                                               command,
                                               command.snapshot())) {
                    return;
                }
                activeDelete_.clear();
                if (error.code() == AccessListApiErrorCode::NotFound) {
                    finishWrite(AccessListWriteResult(
                                    command.operation,
                                    AccessListWriteDisposition::AlreadyAbsent,
                                    command.listType,
                                    command.plateNumber,
                                    command.id),
                                generation,
                                command);
                } else if (error.code() == AccessListApiErrorCode::Timeout) {
                    beginVerification(generation, command);
                } else if (error.code() == AccessListApiErrorCode::Cancelled
                           || error.code() == AccessListApiErrorCode::StaleResponse) {
                    cancelWriteInternal();
                } else {
                    failWrite(serviceError(error), generation, command);
                }
            });
    return true;
}

void AccessListService::cancelQuery()
{
    cancelQueryInternal(false);
}

void AccessListService::cancelWrite()
{
    cancelWriteInternal();
}

void AccessListService::activate(const SessionSummary &summary)
{
    QPointer<AccessListService> guarded(this);
    cancelQueryInternal(true);
    if (!guarded) {
        return;
    }
    cancelWriteInternal();
    if (!guarded) {
        return;
    }
    sessionGeneration_ = summary.generation();
    lastSubmittedFilter_.reset();
}

void AccessListService::invalidate(SessionInvalidationReason reason, quint64 generation)
{
    Q_UNUSED(reason)
    if (generation != sessionGeneration_) {
        return;
    }
    QPointer<AccessListService> guarded(this);
    cancelQueryInternal(true);
    if (!guarded) {
        return;
    }
    cancelWriteInternal();
    if (!guarded) {
        return;
    }
    sessionGeneration_ = 0;
    lastSubmittedFilter_.reset();
}

void AccessListService::submitQuery(AccessListFilter filter)
{
    QPointer<AccessListPageRequestHandle> previous = activeQuery_;
    activeQuery_.clear();
    advanceGeneration(queryGeneration_);
    const quint64 generation = queryGeneration_;
    if (previous) {
        QPointer<AccessListService> guarded(this);
        previous->cancel();
        if (!guarded) {
            return;
        }
    }

    lastSubmittedFilter_ = filter;
    AccessListPageRequestHandle *request = accessListApiClient_->getPage(
        filter.listType(), filter.keyword(), filter.page(),
        sessionGeneration_, generation);
    activeQuery_ = request;
    connect(request, &AccessListPageRequestHandle::succeeded, this,
            [this, request, generation, filter] {
                if (!queryResponseIsCurrent(request, generation, filter)
                    || !request->page()) {
                    return;
                }
                activeQuery_.clear();
                lastSuccessfulPage_ = *request->page();
                QPointer<AccessListService> guarded(this);
                setQueryLoading(false);
                if (guarded) {
                    emit guarded->resultsChanged();
                }
            });
    connect(request, &AccessListPageRequestHandle::failed, this,
            [this, request, generation, filter](const AccessListApiError &error) {
                if (!queryResponseIsCurrent(request, generation, filter)) {
                    return;
                }
                activeQuery_.clear();
                QPointer<AccessListService> guarded(this);
                setQueryLoading(false);
                if (guarded && error.code() != AccessListApiErrorCode::Cancelled
                    && error.code() != AccessListApiErrorCode::StaleResponse) {
                    emit guarded->queryFailed(serviceError(error));
                }
            });
    setQueryLoading(true);
}

void AccessListService::beginVerification(quint64 writeGeneration,
                                          const WriteCommand &command)
{
    if (!writeContextIsCurrent(writeGeneration, command)) {
        return;
    }
    AccessListLookupRequestHandle *request = accessListApiClient_->lookup(
        command.plateNumber, sessionGeneration_, writeGeneration);
    activeVerification_ = request;
    connect(request, &AccessListLookupRequestHandle::succeeded, this,
            [this, request, writeGeneration, command] {
                if (activeVerification_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               writeGeneration,
                                               command,
                                               QVariantMap {
                                                   {QStringLiteral("plateNumber"),
                                                    command.plateNumber}})
                    || !request->record()) {
                    return;
                }
                activeVerification_.clear();
                const AccessListRecord record = *request->record();
                AccessListWriteDisposition disposition;
                if (command.operation == AccessListWriteOperation::Create) {
                    disposition = record.listType() == command.listType
                                      ? AccessListWriteDisposition::Applied
                                      : AccessListWriteDisposition::NotApplied;
                } else {
                    disposition = command.id && record.id() == *command.id
                                      ? AccessListWriteDisposition::NotApplied
                                      : AccessListWriteDisposition::Applied;
                }
                finishWrite(AccessListWriteResult(command.operation,
                                                  disposition,
                                                  command.listType,
                                                  command.plateNumber,
                                                  command.id,
                                                  record),
                            writeGeneration,
                            command);
            });
    connect(request, &AccessListLookupRequestHandle::failed, this,
            [this, request, writeGeneration, command](const AccessListApiError &error) {
                if (activeVerification_ != request
                    || !writeResponseIsCurrent(request->context(),
                                               writeGeneration,
                                               command,
                                               QVariantMap {
                                                   {QStringLiteral("plateNumber"),
                                                    command.plateNumber}})) {
                    return;
                }
                activeVerification_.clear();
                if (error.code() == AccessListApiErrorCode::Cancelled
                    || error.code() == AccessListApiErrorCode::StaleResponse) {
                    cancelWriteInternal();
                    return;
                }
                if (error.code() == AccessListApiErrorCode::NotFound) {
                    finishWrite(AccessListWriteResult(
                                    command.operation,
                                    command.operation == AccessListWriteOperation::Create
                                        ? AccessListWriteDisposition::NotApplied
                                        : AccessListWriteDisposition::Applied,
                                    command.listType,
                                    command.plateNumber,
                                    command.id),
                                writeGeneration,
                                command);
                    return;
                }
                finishWrite(AccessListWriteResult(command.operation,
                                                  AccessListWriteDisposition::Unknown,
                                                  command.listType,
                                                  command.plateNumber,
                                                  command.id),
                            writeGeneration,
                            command);
            });
}

void AccessListService::finishWrite(AccessListWriteResult result,
                                    quint64 writeGeneration,
                                    const WriteCommand &command)
{
    if (!writeContextIsCurrent(writeGeneration, command)) {
        return;
    }
    activeCreate_.clear();
    activeDelete_.clear();
    activeVerification_.clear();
    writeCommand_.reset();
    const bool shouldRefresh = result.disposition() == AccessListWriteDisposition::Applied
                               || result.disposition()
                                      == AccessListWriteDisposition::AlreadyAbsent;
    QPointer<AccessListService> guarded(this);
    setWriteBusy(false);
    if (!guarded) {
        return;
    }
    if (shouldRefresh && lastSubmittedFilter_) {
        const AccessListFilter filter = *lastSubmittedFilter_;
        submitQuery(filter);
        if (!guarded) {
            return;
        }
    }
    emit guarded->writeCompleted(result);
}

void AccessListService::failWrite(AccessListServiceError error,
                                  quint64 writeGeneration,
                                  const WriteCommand &command)
{
    if (!writeContextIsCurrent(writeGeneration, command)) {
        return;
    }
    activeCreate_.clear();
    activeDelete_.clear();
    activeVerification_.clear();
    writeCommand_.reset();
    QPointer<AccessListService> guarded(this);
    setWriteBusy(false);
    if (guarded) {
        emit guarded->writeFailed(error);
    }
}

void AccessListService::cancelQueryInternal(bool clearStoredResults)
{
    QPointer<AccessListService> guarded(this);
    QPointer<AccessListPageRequestHandle> request = activeQuery_;
    activeQuery_.clear();
    advanceGeneration(queryGeneration_);
    const bool resultsWerePresent = clearStoredResults && lastSuccessfulPage_.has_value();
    if (clearStoredResults) {
        lastSuccessfulPage_.reset();
    }
    const bool loadingChanged = queryLoading_;
    queryLoading_ = false;
    if (request) {
        request->cancel();
        if (!guarded) {
            return;
        }
    }
    if (resultsWerePresent) {
        emit guarded->resultsChanged();
        if (!guarded) {
            return;
        }
    }
    if (loadingChanged) {
        emit guarded->queryLoadingChanged(false);
    }
}

void AccessListService::cancelWriteInternal()
{
    QPointer<AccessListService> guarded(this);
    QPointer<AccessListCreateRequestHandle> createRequest = activeCreate_;
    QPointer<AccessListDeleteRequestHandle> deleteRequest = activeDelete_;
    QPointer<AccessListLookupRequestHandle> verification = activeVerification_;
    activeCreate_.clear();
    activeDelete_.clear();
    activeVerification_.clear();
    advanceGeneration(writeGeneration_);
    writeCommand_.reset();
    const bool busyChanged = writeBusy_;
    writeBusy_ = false;
    if (createRequest) {
        createRequest->cancel();
        if (!guarded) {
            return;
        }
    }
    if (deleteRequest) {
        deleteRequest->cancel();
        if (!guarded) {
            return;
        }
    }
    if (verification) {
        verification->cancel();
        if (!guarded) {
            return;
        }
    }
    if (busyChanged) {
        emit guarded->writeBusyChanged(false);
    }
}

void AccessListService::setQueryLoading(bool loading)
{
    if (queryLoading_ == loading) {
        return;
    }
    queryLoading_ = loading;
    emit queryLoadingChanged(queryLoading_);
}

void AccessListService::setWriteBusy(bool busy)
{
    if (writeBusy_ == busy) {
        return;
    }
    writeBusy_ = busy;
    emit writeBusyChanged(writeBusy_);
}

bool AccessListService::queryResponseIsCurrent(AccessListPageRequestHandle *request,
                                               quint64 queryGeneration,
                                               const AccessListFilter &filter) const
{
    return request && activeQuery_ == request && queryGeneration == queryGeneration_
           && request->context().sessionGeneration() == sessionGeneration_
           && request->context().requestGeneration() == queryGeneration
           && request->context().filterSnapshot() == filter.snapshot()
           && lastSubmittedFilter_ && *lastSubmittedFilter_ == filter
           && sessionService_->isCurrent(request->context());
}

bool AccessListService::writeContextIsCurrent(quint64 writeGeneration,
                                               const WriteCommand &command) const
{
    return writeBusy_ && writeGeneration == writeGeneration_
           && writeCommand_ && *writeCommand_ == command
           && sessionGeneration_ != 0;
}

bool AccessListService::writeResponseIsCurrent(const RequestContext &context,
                                               quint64 writeGeneration,
                                               const WriteCommand &command,
                                               const QVariantMap &expectedSnapshot) const
{
    return writeContextIsCurrent(writeGeneration, command)
           && context.sessionGeneration() == sessionGeneration_
           && context.requestGeneration() == writeGeneration
           && context.filterSnapshot() == expectedSnapshot
           && sessionService_->isCurrent(context);
}
