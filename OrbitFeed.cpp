#include "OrbitFeed.h"

#include <QCborValue>
#include <QCborMap>
#include <QCborArray>
#include <QCborParserError>
#include <QByteArray>
#include <QMetaObject>
#include <QMutexLocker>
#include <cmath>
#include <limits>

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

    startGroundStationWatcher();

    emit statusMessage(QStringLiteral("Subscribed to %1").arg(QString::fromUtf8(subjUtf8)));
}

void OrbitFeed::stop()
{
    disconnect();
}

void OrbitFeed::disconnect()
{
    stopGroundStationWatcher();
    if (m_sub) {
        natsSubscription_Destroy(m_sub);
        m_sub = nullptr;
    }
    if (m_js) {
        jsCtx_Destroy(m_js);
        m_js = nullptr;
    }
    if (m_kv) {
        kvStore_Destroy(m_kv);
        m_kv = nullptr;
    }
    // Watcher is destroyed by stopGroundStationWatcher.
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

void OrbitFeed::startGroundStationWatcher()
{
    if (!m_conn || m_kvWatcher)
        return;

    jsOptions jsOpts;
    jsOptions_Init(&jsOpts);
    natsStatus s = natsConnection_JetStream(&m_js, m_conn, &jsOpts);
    if (s != NATS_OK) {
        emit statusMessage(QStringLiteral("JetStream init failed: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
        if (m_js) {
            jsCtx_Destroy(m_js);
            m_js = nullptr;
        }
        return;
    }

    s = js_KeyValue(&m_kv, m_js, "mgs");
    if (s != NATS_OK) {
        emit statusMessage(QStringLiteral("KV bind failed: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
        if (m_kv) {
            kvStore_Destroy(m_kv);
            m_kv = nullptr;
        }
        if (m_js) {
            jsCtx_Destroy(m_js);
            m_js = nullptr;
        }
        return;
    }

    kvWatchOptions opts;
    kvWatchOptions_Init(&opts);
    opts.IgnoreDeletes = false;
    opts.UpdatesOnly = false;

    s = kvStore_Watch(&m_kvWatcher, m_kv, "m.gs.*.mask", &opts);
    if (s != NATS_OK) {
        emit statusMessage(QStringLiteral("KV watch failed: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
        if (m_kvWatcher) {
            kvWatcher_Destroy(m_kvWatcher);
            m_kvWatcher = nullptr;
        }
        if (m_kv) {
            kvStore_Destroy(m_kv);
            m_kv = nullptr;
        }
        if (m_js) {
            jsCtx_Destroy(m_js);
            m_js = nullptr;
        }
        return;
    }

    m_kvThreadRunning = true;
    m_kvThread = std::thread([this]() { watchGroundStations(); });
}

void OrbitFeed::stopGroundStationWatcher()
{
    m_kvThreadRunning = false;
    if (m_kvWatcher) {
        kvWatcher_Stop(m_kvWatcher);
    }
    if (m_kvThread.joinable()) {
        m_kvThread.join();
    }
    if (m_kvWatcher) {
        kvWatcher_Destroy(m_kvWatcher);
        m_kvWatcher = nullptr;
    }
    if (m_kv) {
        kvStore_Destroy(m_kv);
        m_kv = nullptr;
    }
    if (m_js) {
        jsCtx_Destroy(m_js);
        m_js = nullptr;
    }
    {
        QMutexLocker locker(&m_groundStationMutex);
        m_groundStations.clear();
    }
    publishGroundStations();
}

void OrbitFeed::watchGroundStations()
{
    while (m_kvThreadRunning && m_kvWatcher) {
        kvEntry *entry = nullptr;
        natsStatus s = kvWatcher_Next(&entry, m_kvWatcher, 500);
        if (!m_kvThreadRunning) {
            if (entry) {
                kvEntry_Destroy(entry);
                entry = nullptr;
            }
            break;
        }
        if (s == NATS_TIMEOUT) {
            continue;
        }
        if (s != NATS_OK) {
            if (entry) {
                kvEntry_Destroy(entry);
                entry = nullptr;
            }
            emit statusMessage(QStringLiteral("KV watch stopped: %1").arg(QString::fromLatin1(natsStatus_GetText(s))));
            m_kvThreadRunning = false;
            break;
        }
        if (!entry) {
            continue; // initial snapshot marker
        }
        handleGroundStationEntry(entry);
        kvEntry_Destroy(entry);
    }
}

void OrbitFeed::handleGroundStationEntry(kvEntry *entry)
{
    if (!entry)
        return;

    const char *keyPtr = kvEntry_Key(entry);
    if (!keyPtr)
        return;
    const QString key = QString::fromLatin1(keyPtr);
    const QString prefix = QStringLiteral("m.gs.");
    const QString suffix = QStringLiteral(".mask");
    if (!key.startsWith(prefix) || !key.endsWith(suffix))
        return;
    const QString id = key.mid(prefix.size(), key.size() - prefix.size() - suffix.size());
    if (id.isEmpty())
        return;

    const kvOperation op = kvEntry_Operation(entry);
    if (op == kvOp_Delete || op == kvOp_Purge) {
        {
            QMutexLocker locker(&m_groundStationMutex);
            m_groundStations.remove(id);
        }
        publishGroundStations();
        return;
    }

    const void *valPtr = kvEntry_Value(entry);
    const int len = kvEntry_ValueLen(entry);
    if (!valPtr || len <= 0)
        return;

    const QByteArray payload(static_cast<const char *>(valPtr), len);
    QVariantMap station = parseGroundStationPayload(payload);
    if (station.isEmpty())
        return;
    station.insert(QStringLiteral("id"), id);

    {
        QMutexLocker locker(&m_groundStationMutex);
        m_groundStations.insert(id, station);
    }
    publishGroundStations();
}

QVariantMap OrbitFeed::parseGroundStationPayload(const QByteArray &payload) const
{
    QCborParserError err;
    QCborValue val = QCborValue::fromCbor(payload, &err);
    if (err.error != QCborError::NoError)
        return {};

    auto readNumber = [](const QCborMap &m, const std::initializer_list<QCborValue> &keys, double &out) -> bool {
        for (const auto &k : keys) {
            auto it = m.constFind(k);
            if (it != m.constEnd()) {
                const double v = it->toDouble(std::numeric_limits<double>::quiet_NaN());
                if (std::isfinite(v)) {
                    out = v;
                    return true;
                }
            }
        }
        return false;
    };

    auto parsePoint = [&](const QCborValue &v, double &lat, double &lon) -> bool {
        if (v.isMap()) {
            const QCborMap m = v.toMap();
            return readNumber(m, {QCborValue(QStringLiteral("Lat")), QCborValue(QStringLiteral("lat"))}, lat)
                && readNumber(m, {QCborValue(QStringLiteral("Lon")), QCborValue(QStringLiteral("lon"))}, lon);
        }
        if (v.isArray()) {
            const QCborArray arr = v.toArray();
            if (arr.size() >= 2) {
                const double la = arr.at(0).toDouble(std::numeric_limits<double>::quiet_NaN());
                const double lo = arr.at(1).toDouble(std::numeric_limits<double>::quiet_NaN());
                if (std::isfinite(la) && std::isfinite(lo)) {
                    lat = la;
                    lon = lo;
                    return true;
                }
            }
        }
        return false;
    };

    auto parsePointsArray = [&](const QCborArray &arr) -> QVariantList {
        QVariantList pts;
        for (const QCborValue &item : arr) {
            double lat = std::numeric_limits<double>::quiet_NaN();
            double lon = std::numeric_limits<double>::quiet_NaN();
            if (parsePoint(item, lat, lon)) {
                QVariantMap p;
                p.insert(QStringLiteral("lat"), lat);
                p.insert(QStringLiteral("lon"), lon);
                pts.append(p);
            }
        }
        return pts;
    };

    QVariantList mask;
    double lat = std::numeric_limits<double>::quiet_NaN();
    double lon = std::numeric_limits<double>::quiet_NaN();
    double radiusKm = std::numeric_limits<double>::quiet_NaN();

    if (val.isMap()) {
        const QCborMap m = val.toMap();
        readNumber(m, {QCborValue(QStringLiteral("Lat")), QCborValue(QStringLiteral("lat"))}, lat);
        readNumber(m, {QCborValue(QStringLiteral("Lon")), QCborValue(QStringLiteral("lon"))}, lon);
        readNumber(m, {QCborValue(QStringLiteral("radius_km")), QCborValue(QStringLiteral("RadiusKm")), QCborValue(QStringLiteral("radiusKm")), QCborValue(QStringLiteral("radius"))}, radiusKm);

        auto pick = [&](const std::initializer_list<QCborValue> &keys) -> QCborValue {
            for (const auto &k : keys) {
                auto it = m.constFind(k);
                if (it != m.constEnd())
                    return it.value();
            }
            return QCborValue();
        };

        auto tryParseMask = [&](const QCborValue &candidate) {
            if (!candidate.isArray())
                return;
            QVariantList pts = parsePointsArray(candidate.toArray());
            if (!pts.isEmpty())
                mask = pts;
        };

        tryParseMask(pick({QCborValue(QStringLiteral("mask")), QCborValue(QStringLiteral("Mask"))}));
        if (mask.isEmpty())
            tryParseMask(pick({QCborValue(QStringLiteral("boundary")), QCborValue(QStringLiteral("Boundary"))}));
        if (mask.isEmpty())
            tryParseMask(pick({QCborValue(QStringLiteral("footprint")), QCborValue(QStringLiteral("Footprint"))}));
        if (mask.isEmpty())
            tryParseMask(pick({QCborValue(QStringLiteral("points")), QCborValue(QStringLiteral("Points"))}));
        if (mask.isEmpty() && val.isArray())
            mask = parsePointsArray(val.toArray());
    } else if (val.isArray()) {
        mask = parsePointsArray(val.toArray());
    }

    if ((std::isnan(lat) || std::isnan(lon)) && !mask.isEmpty()) {
        double sumLat = 0.0;
        double sumLon = 0.0;
        for (const auto &pVar : mask) {
            const QVariantMap p = pVar.toMap();
            sumLat += p.value(QStringLiteral("lat")).toDouble();
            sumLon += p.value(QStringLiteral("lon")).toDouble();
        }
        lat = sumLat / mask.size();
        lon = sumLon / mask.size();
    }

    QVariantMap out;
    if (std::isfinite(lat) && std::isfinite(lon)) {
        out.insert(QStringLiteral("lat"), lat);
        out.insert(QStringLiteral("lon"), lon);
    }
    if (!mask.isEmpty())
        out.insert(QStringLiteral("mask"), mask);
    if (std::isfinite(radiusKm))
        out.insert(QStringLiteral("radius_km"), radiusKm);
    return out;
}

void OrbitFeed::publishGroundStations()
{
    QVariantList stations;
    {
        QMutexLocker locker(&m_groundStationMutex);
        stations.reserve(m_groundStations.size());
        for (auto it = m_groundStations.constBegin(); it != m_groundStations.constEnd(); ++it)
            stations.append(it.value());
    }

    QMetaObject::invokeMethod(
        this,
        [this, stations]() { emit groundStationsUpdated(stations); },
        Qt::QueuedConnection);
}
