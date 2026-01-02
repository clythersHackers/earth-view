#pragma once

#include <QObject>
#include <QVariantList>
#include <QMutex>
#include <QHash>
#include <atomic>
#include <thread>

extern "C" {
#include "nats.h"
}

// Copyright (c) 2026 Andy Armitage
// This source is distributed under the Mozilla Public License 2.0; see LICENSE.txt.

class OrbitFeed : public QObject
{
    Q_OBJECT
public:
    explicit OrbitFeed(QObject *parent = nullptr);
    ~OrbitFeed();

    void setSubject(const QString &subject);
    void start();
    void stop();

signals:
    void satellitesUpdated(const QVariantList &satellites);
    void groundStationsUpdated(const QVariantList &groundStations);
    void statusMessage(const QString &msg);

private:
    static void onMessage(natsConnection *, natsSubscription *, natsMsg *msg, void *closure);
    void handleMessage(natsMsg *msg);
    void startGroundStationWatcher();
    void stopGroundStationWatcher();
    void watchGroundStations();
    void handleGroundStationEntry(kvEntry *entry);
    QVariantMap parseGroundStationPayload(const QByteArray &payload) const;
    void publishGroundStations();
    void disconnect();

    QString m_subject;
    natsConnection *m_conn {nullptr};
    natsSubscription *m_sub {nullptr};
    jsCtx *m_js {nullptr};
    kvStore *m_kv {nullptr};
    kvWatcher *m_kvWatcher {nullptr};
    std::thread m_kvThread;
    std::atomic<bool> m_kvThreadRunning {false};
    mutable QMutex m_groundStationMutex;
    QHash<QString, QVariantMap> m_groundStations;
};
