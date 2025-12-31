#include "OrbitFeed.h"

#include "nats.h"

#include <QCborValue>
#include <QCborMap>
#include <QCborArray>
#include <QByteArray>
#include <QMetaObject>

OrbitFeed::OrbitFeed(QObject *parent)
    : QObject(parent)
{
}

OrbitFeed::~OrbitFeed()
{
    stop();
}

void OrbitFeed::setSubject(const QString &subject)
{
    m_subject = subject;
}

void OrbitFeed::start()
{
    if (m_conn)
        return;

    const QByteArray subjUtf8 = m_subject.isEmpty() ? QByteArrayLiteral("m.orbit.*") : m_subject.toUtf8();

    natsStatus s = natsConnection_ConnectTo(&m_conn, "nats://127.0.0.1:4222");
    if (s != NATS_OK) {
        emit statusMessage(QStringLiteral("NATS connect failed: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
        disconnect();
        return;
    }

    s = natsConnection_Subscribe(&m_sub, m_conn, subjUtf8.constData(), &OrbitFeed::onMessage, this);
    if (s != NATS_OK) {
        emit statusMessage(QStringLiteral("NATS subscribe failed: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
        disconnect();
        return;
    }

    emit statusMessage(QStringLiteral("Subscribed to %1").arg(QString::fromUtf8(subjUtf8)));
}

void OrbitFeed::stop()
{
    disconnect();
}

void OrbitFeed::disconnect()
{
    if (m_sub) {
        natsSubscription_Destroy(m_sub);
        m_sub = nullptr;
    }
    if (m_conn) {
        natsConnection_Destroy(m_conn);
        m_conn = nullptr;
    }
}

// static
void OrbitFeed::onMessage(natsConnection *, natsSubscription *, natsMsg *msg, void *closure)
{
    auto *self = static_cast<OrbitFeed *>(closure);
    if (!self)
        return;
    self->handleMessage(msg);
}

void OrbitFeed::handleMessage(natsMsg *msg)
{
    if (!msg)
        return;

    QByteArray payload(reinterpret_cast<const char *>(natsMsg_GetData(msg)), natsMsg_GetDataLength(msg));
    QCborParserError err;
    QCborValue val = QCborValue::fromCbor(payload, &err);
    if (err.error != QCborError::NoError || !val.isMap()) {
        natsMsg_Destroy(msg);
        return;
    }
    const QCborMap map = val.toMap();

    auto pick = [&](const QCborMap &m, const std::initializer_list<QCborValue> &keys) -> QCborValue {
        for (const auto &k : keys) {
            auto it = m.constFind(k);
            if (it != m.constEnd())
                return it.value();
        }
        return QCborValue();
    };

    QCborValue statesVal = pick(map, {QCborValue(1), QCborValue(QStringLiteral("1")), QCborValue(QStringLiteral("States"))});
    if (!statesVal.isArray()) {
        natsMsg_Destroy(msg);
        return;
    }

    QVariantList sats;
    for (const QCborValue &entry : statesVal.toArray()) {
        if (!entry.isMap())
            continue;
        const QCborMap m = entry.toMap();
        auto get = [&](const std::initializer_list<QCborValue> &keys) -> QCborValue {
            return pick(m, keys);
        };
        const QCborValue idVal = get({QCborValue(QStringLiteral("ID")), QCborValue(QStringLiteral("id"))});
        const double lat = get({QCborValue(QStringLiteral("Lat")), QCborValue(QStringLiteral("lat"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double lon = get({QCborValue(QStringLiteral("Lon")), QCborValue(QStringLiteral("lon"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double alt = get({QCborValue(QStringLiteral("Alt")), QCborValue(QStringLiteral("alt"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double latPast = get({QCborValue(QStringLiteral("LatPast"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double lonPast = get({QCborValue(QStringLiteral("LonPast"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double latFuture = get({QCborValue(QStringLiteral("LatFuture"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double lonFuture = get({QCborValue(QStringLiteral("LonFuture"))}).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(lat) || !std::isfinite(lon))
            continue;
        QVariantMap sat;
        if (!idVal.isUndefined() && !idVal.isNull()) {
            QVariant idVar = idVal.toVariant();
            if (idVar.isValid())
                sat.insert(QStringLiteral("ID"), idVar);
        }
        sat.insert(QStringLiteral("Lat"), lat);
        sat.insert(QStringLiteral("Lon"), lon);
        if (std::isfinite(alt))
            sat.insert(QStringLiteral("Alt"), alt);
        if (std::isfinite(latPast) && std::isfinite(lonPast)) {
            sat.insert(QStringLiteral("LatPast"), latPast);
            sat.insert(QStringLiteral("LonPast"), lonPast);
        }
        if (std::isfinite(latFuture) && std::isfinite(lonFuture)) {
            sat.insert(QStringLiteral("LatFuture"), latFuture);
            sat.insert(QStringLiteral("LonFuture"), lonFuture);
        }
        sats.append(sat);
    }

    if (!sats.isEmpty()) {
        QMetaObject::invokeMethod(
            this,
            [this, sats]() { emit satellitesUpdated(sats); },
            Qt::QueuedConnection);
    }

    natsMsg_Destroy(msg);
}
