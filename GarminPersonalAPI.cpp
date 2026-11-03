/*
 * Copyright (c) 2026 Joachim Kohlhammer (joachim.kohlhammer@gmx.de)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "GarminPersonalAPI.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslError>
#include <QSslSocket>
#include <QScopedPointer>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QStringList>

#include <algorithm>


namespace {

    const char * const tokenServicePath = "personalapitoken-service/personal-api/personal-api-key/token";
    const char * const listServicePath = "activitylist-service/activities/search/activities";
    const char * const downloadServicePath = "download-service/files/activity/";

    class BusyGuard
    {
    public:
        explicit BusyGuard(bool &flag) : flag_(flag), acquired_(! flag) {
            if (acquired_) {
                flag_ = true;
            }
        }

        ~BusyGuard() {
            if (acquired_) {
                flag_ = false;
            }
        }

        BusyGuard(const BusyGuard &) = delete;

        BusyGuard &operator=(const BusyGuard &) = delete;

        bool acquired() const {
            return acquired_;
        }
    private:
        bool &flag_;
        bool acquired_;
    };


    QDateTime
    utcFromMSecs
    (qint64 ms)
    {
        return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc());
    }


    bool
    splitDateTime
    (const QString &text, QDate &date, QTime &time)
    {
        date = QDate::fromString(text.left(10), "yyyy-MM-dd");
        time = QTime::fromString(text.mid(11), "HH:mm:ss");
        return date.isValid() && time.isValid();
    }


    QDateTime
    utcFromString
    (const QString &text)
    {
        QDate date;
        QTime time;
        if (! splitDateTime(text, date, time)) {
            return QDateTime();
        }
        return QDateTime(date, time, QTimeZone::utc());
    }


    QDateTime
    localFromString
    (const QString &text)
    {
        QDate date;
        QTime time;
        if (! splitDateTime(text, date, time)) {
            return QDateTime();
        }
        return QDateTime(date, time);
    }


    bool
    contentTypeIs
    (const QString &actual, const QString &expected)
    {
        const QString mime = actual.section(';', 0, 0).trimmed();
        return mime.compare(expected, Qt::CaseInsensitive) == 0;
    }


    QDateTime
    jwtExpiry
    (const QString &token)
    {
        const QList<QByteArray> parts = token.toLatin1().split('.');
        if (parts.size() != 3) {
            return QDateTime();
        }
        const QByteArray payload = QByteArray::fromBase64(parts[1], QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        QJsonParseError jsonError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || ! doc.isObject()) {
            return QDateTime();
        }
        const QJsonValue exp = doc.object().value("exp");
        if (! exp.isDouble() || exp.toDouble() <= 0) {
            return QDateTime();
        }
        return utcFromMSecs(static_cast<qint64>(exp.toDouble()) * 1000);
    }


    QString
    extractToken
    (const QByteArray &body)
    {
        QByteArray text = body.trimmed();
        if (text.startsWith('{')) {
            const QJsonObject obj = QJsonDocument::fromJson(text).object();
            for (char const *key : {"access_token", "accessToken", "token"}) {
                const QString value = obj.value(QLatin1String(key)).toString().trimmed();
                if (! value.isEmpty()) {
                    return value;
                }
            }
            return QString();
        }
        if (text.size() >= 2 && text.startsWith('"') && text.endsWith('"')) {
            text = text.mid(1, text.size() - 2).trimmed();
        }
        const QString token = QString::fromLatin1(text);
        for (const QChar c : token) {
            if (c.isSpace()) {
                return QString();
            }
        }
        return token;
    }

}


GarminPersonalAPI::GarminPersonalAPI
(const QString &key, const QString &token, const QDateTime &tokenLastRefresh, QObject *parent)
: QObject(parent), manager(new QNetworkAccessManager(this)), apiKey(key), accessToken(token.trimmed()), lastAccessRefresh(tokenLastRefresh)
{
    if (! accessToken.isEmpty()) {
        tokenExpiry = jwtExpiry(accessToken);
        if (! tokenExpiry.isValid() && lastAccessRefresh.isValid()) {
            // Not a JWT: fall back to accessTTL
            tokenExpiry = lastAccessRefresh.addSecs(accessTTL);
        }
    }
}


GarminPersonalAPI::ActivityListing
GarminPersonalAPI::activityListService
(const QDate &startDate, const QDate &endDate, int page)
{
    ActivityListing ret;
    ret.page = std::max(0, page);
    ret.hasMore = false;

    BusyGuard guard(requestInProgress);
    if (! guard.acquired()) {
        ret.fail(Failure::Busy, tr("Another Garmin request is already in progress"));
        return ret;
    }

    if (   ! startDate.isValid()
        || ! endDate.isValid()
        || startDate > endDate) {
        ret.fail(Failure::InvalidArgument, tr("Invalid date range"));
        return ret;
    }

    QMap<QString, QString> query;
    query.insert("limit", QString::number(listLimit));
    query.insert("start", QString::number(static_cast<qint64>(ret.page) * listLimit));
    query.insert("startDate", startDate.toString("yyyy-MM-dd"));
    query.insert("endDate", endDate.toString("yyyy-MM-dd"));
    query.insert("sortOrder", "asc");

    const QString expectedContentType = "application/json";
    const HttpReply reply = getService(listServicePath, query, listTimeoutMS, expectedContentType);
    static_cast<BasicReply &>(ret) = reply;
    if (ret.hasError()) {
        return ret;
    }

    if (! contentTypeIs(reply.contentType, expectedContentType)) {
        ret.fail(Failure::InvalidReply, tr("Unexpected content type in reply: %1").arg(reply.contentType));
        return ret;
    }

    QJsonParseError jsonError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(reply.data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        ret.fail(Failure::InvalidReply, tr("Failed to parse JSON: %1").arg(jsonError.errorString()));
        return ret;
    }
    if (! jsonDoc.isArray()) {
        ret.fail(Failure::InvalidReply, tr("Invalid JSON: expected an array of activities"));
        return ret;
    }

    const QJsonArray jsonArr = jsonDoc.array();
    for (const QJsonValue &value : jsonArr) {
        const QJsonObject activityObj = value.toObject();

        const qint64 id = activityObj.value("activityId").toInteger();
        if (id <= 0) {
            qWarning() << "Garmin activity listing: skipping entry without activityId";
            continue;
        }

        ActivityListingEntry entry;
        entry.activityId = QString::number(id);
        entry.name = activityObj.value("activityName").toString();

        const double beginTimestamp = activityObj.value("beginTimestamp").toDouble();
        if (beginTimestamp > 0) {
            entry.startTime = utcFromMSecs(static_cast<qint64>(beginTimestamp));
        } else {
            entry.startTime = utcFromString(activityObj.value("startTimeGMT").toString());
        }
        entry.startTimeLocal = localFromString(activityObj.value("startTimeLocal").toString());
        entry.duration = activityObj.value("duration").toDouble();
        entry.distance = activityObj.value("distance").toDouble() / 1000.0;

        ret.activities << entry;
    }
    ret.hasMore = (jsonArr.size() == listLimit);

    return ret;
}


GarminPersonalAPI::ActivityDownload
GarminPersonalAPI::downloadService
(const QString &activityId)
{
    ActivityDownload ret;
    ret.activityId = activityId;

    BusyGuard guard(requestInProgress);
    if (! guard.acquired()) {
        ret.fail(Failure::Busy, tr("Another Garmin request is already in progress"));
        return ret;
    }

    static const QRegularExpression idPattern("^[0-9]{5,15}$");
    if (! idPattern.match(activityId).hasMatch()) {
        ret.fail(Failure::InvalidArgument, tr("Invalid activity id"));
        return ret;
    }

    const HttpReply reply = getService(QString(downloadServicePath) + activityId, QMap<QString, QString>(), downloadTimeoutMS, "application/x-zip-compressed");
    static_cast<BasicReply &>(ret) = reply;
    if (ret.hasError()) {
        return ret;
    }

    if (reply.data.isEmpty()) {
        ret.fail(Failure::InvalidReply, tr("Empty download reply"));
    } else if (! reply.data.startsWith(QByteArrayLiteral("PK\x03\x04"))) {
        ret.fail(Failure::InvalidReply, tr("The reply is not a ZIP archive (content type: %1)").arg(reply.contentType));
    } else {
        ret.data = reply.data;
    }

    return ret;
}


QString
GarminPersonalAPI::getAccessToken
() const
{
    return accessToken;
}


QDateTime
GarminPersonalAPI::getTokenLastRefresh
() const
{
    return lastAccessRefresh;
}


QDateTime
GarminPersonalAPI::getTokenExpiry
() const
{
    return tokenExpiry;
}


GarminPersonalAPI::HttpReply
GarminPersonalAPI::getService
(const QString &path, const QMap<QString, QString> &query, int timeoutMS, const QString &accept)
{
    HttpReply ret;
    BasicReply tokenFailure;

    if (! ensureAccessToken(false, tokenFailure)) {
        static_cast<BasicReply &>(ret) = tokenFailure;
        return ret;
    }

    auto attempt = [&]() {
        QMap<QString, QString> headers;
        headers.insert("Authorization", QString("Bearer %1").arg(accessToken));
        headers.insert("Accept", accept);
        return getSync(path, headers, query, timeoutMS);
    };

    ret = attempt();
    if (ret.httpCode == 401) {
        // The token was rejected before its expiry: get a new one and retry once
        if (! ensureAccessToken(true, tokenFailure)) {
            static_cast<BasicReply &>(ret) = tokenFailure;
            return ret;
        }
        ret = attempt();
    }
    return ret;
}


bool
GarminPersonalAPI::tokenIsUsable
(const QDateTime &nowUtc) const
{
    if (accessToken.isEmpty() || ! tokenExpiry.isValid()) {
        return false;
    }
    const qint64 secsLeft = nowUtc.secsTo(tokenExpiry);
    if (secsLeft > refreshMarginSecs) {
        return true;
    }
    // Near the end of its validity: keep using a token that was fetched a moment
    // ago instead of requesting a new one before every call
    return    secsLeft > 30
           && lastAccessRefresh.isValid()
           && lastAccessRefresh.secsTo(nowUtc) < 300;
}


bool
GarminPersonalAPI::ensureAccessToken
(bool forceRefresh, BasicReply &tokenFailure)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (! forceRefresh && tokenIsUsable(now)) {
        return true;
    }
    const bool stillValid =    ! forceRefresh
                            && ! accessToken.isEmpty()
                            && tokenExpiry.isValid()
                            && now < tokenExpiry;
    if (tokenRetryNotBefore.isValid() && now < tokenRetryNotBefore) {
        if (stillValid) {
            return true;
        }
        tokenFailure = lastTokenFailure;
        tokenFailure.errorText = tr("%1 (not retrying before %2)").arg(lastTokenFailure.errorText, tokenRetryNotBefore.toLocalTime().toString("HH:mm:ss"));
        return false;
    }

    QMap<QString, QString> headers;
    headers.insert("Authorization", QString("ApiKey %1").arg(apiKey));
    const HttpReply res = postSync(tokenServicePath, headers, tokenTimeoutMS);

    QString token;
    QDateTime expiry;
    if (! res.hasError()) {
        token = extractToken(res.data);
        if (! token.isEmpty()) {
            expiry = jwtExpiry(token);
            if (! expiry.isValid()) {
                expiry = now.addSecs(accessTTL);
            }
        }
    }

    if (! token.isEmpty() && expiry > now) {
        accessToken = token;
        tokenExpiry = expiry;
        lastAccessRefresh = QDateTime::currentDateTime();
        lastTokenFailure = BasicReply();
        tokenRetryNotBefore = QDateTime();
        return true;
    }

    BasicReply f = res;
    int blockSecs = otherFailureBlockSecs;
    if (res.isAuthError()) {
        f.errorText = tr("Garmin rejected the API key (HTTP %1). It is invalid, expired or deleted; create a new one in Garmin Connect (%2)")
                        .arg(res.httpCode)
                        .arg(garminCreateUrl);
        blockSecs = authFailureBlockSecs;
    } else if (res.hasError()) {
        f.errorText = tr("Could not obtain an access token: %1").arg(res.errorText);
    } else if (token.isEmpty()) {
        f.fail(Failure::InvalidReply, tr("Could not obtain an access token: the reply could not be understood"));
    } else {
        f.fail(Failure::InvalidReply, tr("Could not obtain an access token: the received token is already expired"));
    }

    qDebug() << "Token request failed:" << f.httpCode << f.errorText;

    if (! stillValid) {
        accessToken.clear();
        tokenExpiry = QDateTime();
    }
    lastTokenFailure = f;
    tokenRetryNotBefore = now.addSecs(blockSecs);
    tokenFailure = f;
    return stillValid;
}


GarminPersonalAPI::HttpReply
GarminPersonalAPI::getSync
(const QString &path, const QMap<QString, QString> &headers, const QMap<QString, QString> &query, int timeoutMS) const
{
    QNetworkRequest request;
    enrichRequest(request, path, query);
    enrichRequestHeader(request, headers);
    return execute(request, false, timeoutMS);
}


GarminPersonalAPI::HttpReply
GarminPersonalAPI::postSync
(const QString &path, const QMap<QString, QString> &headers, int timeoutMS) const
{
    QNetworkRequest request;
    enrichRequest(request, path);
    enrichRequestHeader(request, headers);
    return execute(request, true, timeoutMS);
}


GarminPersonalAPI::HttpReply
GarminPersonalAPI::execute
(const QNetworkRequest &request, bool post, int timeoutMS) const
{
    using ReplyPtr = QScopedPointer<QNetworkReply, QScopedPointerDeleteLater>;

    if (! QSslSocket::supportsSsl()) {
        HttpReply ret;
        ret.fail(Failure::Ssl, tr("This build of Qt has no usable TLS support (available backends: %1)").arg(QSslSocket::availableBackends().join(", ")));
        return ret;
    }

    QNetworkRequest req(request);
    req.setTransferTimeout(timeoutMS);

    ReplyPtr reply(post ? manager->post(req, QByteArray()) : manager->get(req));

    QList<QSslError> sslErrors;
    connect(reply.get(), &QNetworkReply::sslErrors, reply.get(), [&sslErrors](const QList<QSslError> &errors) { sslErrors = errors; });

    if (! reply->isFinished()) {
        QEventLoop loop;
        connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }

    HttpReply ret;
    ret.transportError = reply->error();
    ret.httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    ret.data = reply->readAll();
    ret.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    if (   ret.transportError == QNetworkReply::OperationCanceledError
        && ret.httpCode == 0) {
        ret.transportError = QNetworkReply::TimeoutError;
    }

    const bool httpSuccess = ret.httpCode >= 200 && ret.httpCode <= 299;
    const bool httpAnswered = ret.httpCode != 0;
    const bool sslFailure =    ! httpAnswered
                            && (   ret.transportError == QNetworkReply::SslHandshakeFailedError
                                || ! sslErrors.isEmpty());
    if (sslFailure) {
        QStringList details;
        for (const QSslError &sslError : std::as_const(sslErrors)) {
            details << sslError.errorString();
        }
        if (details.isEmpty()) {
            details << reply->errorString();
        }
        details.removeDuplicates();

        ret.data.clear();
        ret.fail(Failure::Ssl, tr("Could not establish a secure connection to Garmin (%1). Check that the system date and time are correct "
                                  "and whether a proxy, VPN or security software intercepts HTTPS traffic").arg(details.join("; ")));
        qDebug() << "TLS failure:" << details
                 << "backend:" << QSslSocket::activeBackend()
                 << QSslSocket::sslLibraryVersionString();
    } else if (   ! httpAnswered
               || (   httpSuccess
                   && ret.transportError != QNetworkReply::NoError)) {
        ret.data.clear();
        ret.fail(Failure::Transport, (ret.transportError == QNetworkReply::TimeoutError)
                                     ? tr("Request timed out (no data for %1 s)").arg(timeoutMS / 1000)
                                     : tr("Network error: %1").arg(reply->errorString()));
    } else if (! httpSuccess) {
        ret.failure = (ret.httpCode == 401 || ret.httpCode == 403) ? Failure::Auth : Failure::Http;
        ret.errorText = describeHttpStatus(ret.httpCode, reply.get());
        if (   ret.contentType.contains("json", Qt::CaseInsensitive)
            || ret.contentType.startsWith("text/", Qt::CaseInsensitive)) {
            const QString snippet = QString::fromUtf8(ret.data.left(200)).simplified();
            if (! snippet.isEmpty()) {
                ret.errorText += " - " + snippet;
            }
        }
    }

    return ret;
}


QString
GarminPersonalAPI::describeHttpStatus
(int httpCode, const QNetworkReply *reply)
{
    switch (httpCode) {
    case 400:
        return tr("Invalid request (HTTP 400)");
    case 401:
        return tr("Authorization failed (HTTP 401): the token or API key is invalid, expired or revoked");
    case 403:
        return tr("No access to this resource (HTTP 403)");
    case 404:
        return tr("Resource not found (HTTP 404)");
    case 429: {
        QString msg = tr("Too many requests (HTTP 429)");
        if (reply->hasRawHeader("Retry-After")) {
            msg += tr(", retry after %1").arg(QString::fromLatin1(reply->rawHeader("Retry-After")));
        }
        return msg;
    }
    default:
        if (httpCode >= 500) {
            return tr("Garmin server error (HTTP %1), try again later").arg(httpCode);
        }
        return tr("Unexpected reply (HTTP %1): %2").arg(httpCode).arg(reply->errorString());
    }
}


void
GarminPersonalAPI::enrichRequest
(QNetworkRequest &request, const QString &path, const QMap<QString, QString> &query) const
{
    QUrl url(baseUrl);
    url.setPath(baseUrl.path() + "/" + path);
    QUrlQuery urlQuery;
    for (auto [key, value] : query.asKeyValueRange()) {
        urlQuery.addQueryItem(key, value);
    }
    url.setQuery(urlQuery);
    request.setUrl(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
}


void
GarminPersonalAPI::enrichRequestHeader
(QNetworkRequest &request, const QMap<QString, QString> &headers) const
{
    for (auto [key, value] : headers.asKeyValueRange()) {
        request.setRawHeader(key.toLatin1(), value.toLatin1());
    }
}
