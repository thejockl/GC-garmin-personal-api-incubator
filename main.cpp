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

#include <QCoreApplication>
#include <QDebug>

#include "GarminPersonalAPI.h"

extern int
main
(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    qSetMessagePattern("[%{time h:mm:ss.zzz}] %{type} %{function}: %{message}");

    const QString apiKey = qEnvironmentVariable("GARMIN_API_KEY", "");
    if (apiKey.isEmpty()) {
        qCritical() << "The required environment variable GARMIN_API_KEY is unset";
        return 1;
    }
    GarminPersonalAPI garminPersonalAPI(apiKey);

    QDate lastDate = QDate::currentDate();
    QDate firstDate = lastDate.addDays(-10);
    QLocale locale;

#if 0
    const int page = 0;
    GarminPersonalAPI::ActivityListing listing = garminPersonalAPI.activityListService(firstDate, lastDate, page);
    qDebug() << "=== ACTIVITY LISTING"
             << locale.toString(firstDate, QLocale::NarrowFormat)
             << "-"
             << locale.toString(lastDate, QLocale::NarrowFormat)
             << " Page" << page;
    qDebug() << "Failure   :" << static_cast<int>(listing.failure);
    qDebug() << "Transport :" << listing.transportError << "(" << listing.httpCode << ")";
    qDebug() << "Error Text:" << listing.errorText;
    qDebug() << "Page      :" << listing.page;
    qDebug() << "Has more  :" << listing.hasMore;
    qDebug() << "Entries   :" << listing.activities.count();
    for (const GarminPersonalAPI::ActivityListingEntry &entry : listing.activities) {
        qDebug() << "--- Activity" << entry.activityId;
        qDebug() << "Name      :" << entry.name;
        qDebug() << "Start Time:" << locale.toString(entry.startTime, QLocale::NarrowFormat);
        qDebug() << "Duration  :" << entry.duration;
        qDebug() << "Distance  :" << entry.distance;
    }

    if (listing.activities.count() > 0) {
        const GarminPersonalAPI::ActivityListingEntry entry = listing.activities.last();

        GarminPersonalAPI::ActivityDownload download = garminPersonalAPI.downloadService(entry.activityId);
        qDebug() << "=== DOWNLOAD" << download.activityId;
        qDebug() << "Failure   :" << static_cast<int>(download.failure);
        qDebug() << "Transport :" << download.transportError << "(" << download.httpCode << ")";
        qDebug() << "Error Text:" << download.errorText;
        qDebug() << "data      :" << download.data.size();
    }
#endif

    GarminPersonalAPI::AthleteMetricsListing athleteMetrics = garminPersonalAPI.athleteMetrics(firstDate, lastDate, GarminPersonalAPI::Sleep | GarminPersonalAPI::Hrv | GarminPersonalAPI::UserSummary | GarminPersonalAPI::Weight);
    bool hasHrv = athleteMetrics.availableMetrics.testFlag(GarminPersonalAPI::Hrv);
    bool hasUserSummary = athleteMetrics.availableMetrics.testFlag(GarminPersonalAPI::UserSummary);
    bool hasSleep = athleteMetrics.availableMetrics.testFlag(GarminPersonalAPI::Sleep);
    bool hasWeight = athleteMetrics.availableMetrics.testFlag(GarminPersonalAPI::Weight);
    qDebug() << "=== Athlete Metrics"
             << locale.toString(firstDate, QLocale::NarrowFormat)
             << "-"
             << locale.toString(lastDate, QLocale::NarrowFormat);
    qDebug() << "Failure               :" << static_cast<int>(athleteMetrics.failure);
    qDebug() << "Transport             :" << athleteMetrics.transportError << "(" << athleteMetrics.httpCode << ")";
    qDebug() << "Error Text            :" << athleteMetrics.errorText;
    qDebug() << "HRV available         :" << hasHrv;
    qDebug() << "User summary available:" << hasUserSummary;
    qDebug() << "Sleep available       :" << hasSleep;
    qDebug() << "Weight available      :" << hasWeight;
    for (auto [day, entry] : athleteMetrics.metrics.asKeyValueRange()) {
        qDebug() << "---" << day;
        if (hasHrv) {
            qDebug() << "HRV Last Night Avg   :" << entry.hrv.lastNightAvg;
        }
        if (hasUserSummary) {
            qDebug() << "HR Min Value         :" << entry.userSummary.hrMinValue;
            qDebug() << "HR Max Value         :" << entry.userSummary.hrMaxValue;
            qDebug() << "HR Resting Value     :" << entry.userSummary.hrRestingValue;
            qDebug() << "Cal Burned Resting   :" << entry.userSummary.calBurnedResting;
            qDebug() << "Cal Burned Active    :" << entry.userSummary.calBurnedActive;
            qDebug() << "Cal Burned Total     :" << entry.userSummary.calBurnedTotal;
            qDebug() << "BB Min Value         :" << entry.userSummary.bbMinValue;
            qDebug() << "BB Max Value         :" << entry.userSummary.bbMaxValue;
            qDebug() << "BB Charged Value     :" << entry.userSummary.bbChargedValue;
            qDebug() << "BB Drained Value     :" << entry.userSummary.bbDrainedValue;
            qDebug() << "BB EOD Value         :" << entry.userSummary.bbEndOfDayValue;
            qDebug() << "Int Moderate         :" << entry.userSummary.intModerate;
            qDebug() << "Int Vigorous         :" << entry.userSummary.intVigorous;
            qDebug() << "Str Avg Level        :" << entry.userSummary.strAvgLevel;
            qDebug() << "Str Max Level        :" << entry.userSummary.strMaxLevel;
            qDebug() << "Str Total secs       :" << entry.userSummary.strTotalDuration;
            qDebug() << "Str Rest secs        :" << entry.userSummary.strRestDuration;
            qDebug() << "Str Act secs         :" << entry.userSummary.strActivityDuration;
            qDebug() << "Str uncat secs       :" << entry.userSummary.strUncategorizedDuration;
            qDebug() << "Str low secs         :" << entry.userSummary.strLowStressDuration;
            qDebug() << "Str med secs         :" << entry.userSummary.strMediumStressDuration;
            qDebug() << "Str high secs        :" << entry.userSummary.strHighStressDuration;
            qDebug() << "Mov steps goal       :" << entry.userSummary.movStepsGoal;
            qDebug() << "Mov steps            :" << entry.userSummary.movSteps;
            qDebug() << "Mov steps dist       :" << entry.userSummary.movStepsDist;
            qDebug() << "Mov pushes goal      :" << entry.userSummary.movPushesGoal;
            qDebug() << "Mov pushes           :" << entry.userSummary.movPushes;
            qDebug() << "Mov pushes dist      :" << entry.userSummary.movPushesDist;
            qDebug() << "Mov floors asc goal  :" << entry.userSummary.movFloorsAscGoal;
            qDebug() << "Mov floors asc       :" << entry.userSummary.movFloorsAsc;
            qDebug() << "Mov floors asc dist  :" << entry.userSummary.movFloorsAscDist;
            qDebug() << "Mov floors desc goal :" << entry.userSummary.movFloorsDescGoal;
            qDebug() << "Mov floors desc      :" << entry.userSummary.movFloorsDesc;
            qDebug() << "Mov floors desc dist :" << entry.userSummary.movFloorsDescDist;
        }
        if (hasSleep) {
            qDebug() << "Sleep sleep time     :" << entry.sleep.sleepTime;
            qDebug() << "Sleep nap time       :" << entry.sleep.napTime;
            qDebug() << "Sleep unmeasurable   :" << entry.sleep.unmeasurableSleep;
            qDebug() << "Sleep deep           :" << entry.sleep.deepSleep;
            qDebug() << "Sleep light          :" << entry.sleep.lightSleep;
            qDebug() << "Sleep REM            :" << entry.sleep.remSleep;
            qDebug() << "Sleep awake          :" << entry.sleep.awakeSleep;
            qDebug() << "Sleep avg resp       :" << entry.sleep.avgRespiration;
            qDebug() << "Sleep lowest resp    :" << entry.sleep.lowestRespiration;
            qDebug() << "Sleep highest resp   :" << entry.sleep.highestRespiration;
            qDebug() << "Sleep awake count    :" << entry.sleep.awakeCount;
            qDebug() << "Sleep avg stress     :" << entry.sleep.avgSleepStress;
            qDebug() << "Sleep avg hr         :" << entry.sleep.avgHR;
            qDebug() << "Sleep Score          :" << entry.sleep.scoreOverall;
        }
        if (hasWeight) {
            qDebug() << "Wght time UTC        :" << locale.toString(entry.weight.startTime, QLocale::NarrowFormat);
            qDebug() << "Wght time Local      :" << locale.toString(entry.weight.startTimeLocal, QLocale::NarrowFormat);
            qDebug() << "Wght weight          :" << entry.weight.weight;
            qDebug() << "Wght BMI             :" << entry.weight.bmi;
            qDebug() << "Wght body fat        :" << entry.weight.bodyFat;
            qDebug() << "Wght body water      :" << entry.weight.bodyWater;
            qDebug() << "Wght bone mass       :" << entry.weight.boneMass;
            qDebug() << "Wght muscle mass     :" << entry.weight.muscleMass;
            qDebug() << "Wght physique rating :" << entry.weight.physiqueRating;
            qDebug() << "Wght visceral fat    :" << entry.weight.visceralFat;
            qDebug() << "Wght metabolic age   :" << entry.weight.metabolicAge;
            qDebug() << "Wght weight delta    :" << entry.weight.weightDelta;
        }
    }

    qDebug() << "=== Credentials";
    qDebug() << "Access Token:" << garminPersonalAPI.getAccessToken();
    qDebug() << "Last Refresh:" << locale.toString(garminPersonalAPI.getTokenLastRefresh(), QLocale::NarrowFormat);
    qDebug() << "Token Expiry:" << locale.toString(garminPersonalAPI.getTokenExpiry(), QLocale::NarrowFormat);

    return 0;
}
