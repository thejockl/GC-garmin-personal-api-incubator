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
    QDate firstDate = lastDate.addDays(-30);
    const int page = 0;
    QLocale locale;
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

    qDebug() << "=== Credentials";
    qDebug() << "Access Token:" << garminPersonalAPI.getAccessToken();
    qDebug() << "Last Refresh:" << locale.toString(garminPersonalAPI.getTokenLastRefresh(), QLocale::NarrowFormat);
    qDebug() << "Token Expiry:" << locale.toString(garminPersonalAPI.getTokenExpiry(), QLocale::NarrowFormat);

    return 0;
}
