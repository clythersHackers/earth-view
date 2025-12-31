#pragma once

#include <QObject>
#include <QVariantList>

extern "C" {
#include "nats.h"
}

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
    void statusMessage(const QString &msg);

private:
    static void onMessage(natsConnection *, natsSubscription *, natsMsg *msg, void *closure);
    void handleMessage(natsMsg *msg);
    void disconnect();

    QString m_subject;
    natsConnection *m_conn {nullptr};
    natsSubscription *m_sub {nullptr};
};
