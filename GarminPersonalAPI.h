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

#ifndef GARMINPERSONALAPI_H
#define GARMINPERSONALAPI_H

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

/*
 * Synchronous client for the Garmin Connect Personal API
 *
 * Implements https://developer.garmin.com/gc-developer-program/personal/swagger-docs/
 */
class GarminPersonalAPI : public QObject
{
    Q_OBJECT
public:
    enum class Failure {
        None,
        Busy,            // another request in progress
        InvalidArgument, // bad date range, bad activity id
        Transport,       // network failure or timeout (see transportError)
        Ssl,             // TLS handshake or certificate verification failed (see errorText)
        Http,            // server answered with a non-2xx status (see httpCode)
        Auth,            // 401/403: authentication or authorization denied
        InvalidReply     // 2xx, but unusable: bad JSON, not a ZIP, token not understood
    };

    struct BasicReply {
        Failure failure = Failure::None;
        QNetworkReply::NetworkError transportError = QNetworkReply::NoError;
        int httpCode = 0;
        QString errorText;

        void fail(Failure category, const QString &text) {
            failure = category;
            errorText = text;
        }

        bool hasError() const {
            return failure != Failure::None;
        }

        bool isAuthError() const {
            return failure == Failure::Auth;
        }

        bool isRetryable() const {
            return    failure == Failure::Transport
                   || (   failure == Failure::Http
                       && (   httpCode == 429
                           || httpCode >= 500));
        }
    };

    struct ActivityListingEntry {
        QString activityId;
        QString name;
        QDateTime startTime;      // UTC
        QDateTime startTimeLocal; // local Time
        double duration = 0.0;    // seconds
        double distance = 0.0;    // km
    };

    struct ActivityListing : public BasicReply {
        QList<ActivityListingEntry> activities;
        int page = 0;
        bool hasMore = false;
    };

    struct ActivityDownload : public BasicReply {
        QString activityId;
        QByteArray data;    // ZIP-data
    };

    struct HrvEntry {
        int lastNightAvg = 0;
    };

    struct HrvListing : public BasicReply {
        QMap<QDate, HrvEntry> hrvEntries;
    };

    struct UserSummaryEntry {
        // Heartrate
        int hrMinValue = 0;
        int hrMaxValue = 0;
        int hrRestingValue = 0;

        // Calories
        int calBurnedResting = 0;
        int calBurnedActive = 0;
        int calBurnedTotal = 0;

        // Bodybattery
        int bbMinValue = 0;
        int bbMaxValue = 0;
        int bbChargedValue = 0;
        int bbDrainedValue = 0;
        int bbEndOfDayValue = 0;

        // Intensity
        int intModerate = 0;
        int intVigorous = 0;

        // Stress
        int strAvgLevel = 0;
        int strMaxLevel = 0;
        double strTotalDuration = 0.0;          // seconds
        double strRestDuration = 0.0;           // seconds
        double strActivityDuration = 0.0;       // seconds
        double strUncategorizedDuration = 0.0;  // seconds
        double strLowStressDuration = 0.0;      // seconds
        double strMediumStressDuration = 0.0;   // seconds
        double strHighStressDuration = 0.0;     // seconds

        // Movement
        int movStepsGoal = 0;
        int movSteps = 0;
        double movStepsDist = 0.0;      // meters
        int movPushesGoal = 0;
        int movPushes = 0;
        double movPushesDist = 0.0;     // meters
        int movFloorsAscGoal = 0;
        int movFloorsAsc = 0;
        double movFloorsAscDist = 0.0;  // meters
        int movFloorsDescGoal = 0;
        int movFloorsDesc = 0;
        double movFloorsDescDist = 0.0; // meters
    };

    struct UserSummaryListing : public BasicReply {
        QMap<QDate, UserSummaryEntry> userSummaryEntries;
    };

    enum Metric {
        NoData = 0x0,
        Hrv = 0x1,
        UserSummary = 0x2,
    };
    Q_DECLARE_FLAGS(Metrics, Metric)

    struct AthleteMetrics {
        HrvEntry hrv;
        UserSummaryEntry userSummary;
    };

    struct AthleteMetricsListing : public BasicReply {
        Metrics availableMetrics;
        QMap<QDate, AthleteMetrics> metrics;
    };


    explicit GarminPersonalAPI(const QString &key, const QString &token = QString(), const QDateTime &tokenLastRefresh = QDateTime(), QObject *parent = nullptr);

    // Services
    ActivityListing activityListService(const QDate &startDate, const QDate &endDate, int page = 0);
    ActivityDownload downloadService(const QString &activityId);
    HrvListing hrvService(const QDate &startDate, const QDate &endDate);
    UserSummaryListing userSummaryService(const QDate &startDate, const QDate &endDate);

    // Combined athlete metrics
    AthleteMetricsListing athleteMetrics(const QDate &startDate, const QDate &endDate, Metrics metrics);

    QString getAccessToken() const;
    QDateTime getTokenLastRefresh() const;
    QDateTime getTokenExpiry() const;

private:
    struct HttpReply : public BasicReply {
        QString contentType;
        QByteArray data;
    };

    HttpReply getService(const QString &path, const QMap<QString, QString> &query, int timeoutMS, const QString &accept);

    bool tokenIsUsable(const QDateTime &nowUtc) const;
    bool ensureAccessToken(bool forceRefresh, BasicReply &tokenFailure);

    HttpReply getSync(const QString &path, const QMap<QString, QString> &headers, const QMap<QString, QString> &query, int timeoutMS) const;
    HttpReply postSync(const QString &path, const QMap<QString, QString> &headers, int timeoutMS) const;
    HttpReply execute(const QNetworkRequest &request, bool post, int timeoutMS) const;

    void enrichRequest(QNetworkRequest &request, const QString &path, const QMap<QString, QString> &query = QMap<QString, QString>()) const;
    void enrichRequestHeader(QNetworkRequest &request, const QMap<QString, QString> &headers) const;

    static QString describeHttpStatus(int httpCode, QNetworkReply const *reply);

    QNetworkAccessManager *manager;

    const QUrl baseUrl = QUrl("https://personal.connectapi.garmin.com");
    const QString userAgent = "GoldenCheetah";
    const QString garminCreateUrl = "https://connect.garmin.com/app/settings/personalApiToken";
    static constexpr int accessTTL = 86400;
    static constexpr int refreshMarginSecs = 3600;
    static constexpr int listLimit = 99;
    static constexpr int tokenTimeoutMS = 15000;
    static constexpr int listTimeoutMS = 15000;
    static constexpr int downloadTimeoutMS = 60000;
    static constexpr int authFailureBlockSecs = 600;    // after the API key was rejected
    static constexpr int otherFailureBlockSecs = 30;    // after network / server errors

    QString apiKey;
    QString accessToken;
    QDateTime lastAccessRefresh;
    QDateTime tokenExpiry;
    QDateTime tokenRetryNotBefore;
    BasicReply lastTokenFailure;

    bool requestInProgress = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(GarminPersonalAPI::Metrics)

#endif
