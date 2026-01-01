#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>

#include "EarthView.h"
#include "OrbitFeed.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<EarthView>("EarthView", 1, 0, "EarthView");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("EarthView", "Main");

    // Wire NATS orbit feed to EarthView in the sample app (EarthView itself stays NATS-agnostic).
    if (!engine.rootObjects().isEmpty()) {
        QObject *root = engine.rootObjects().first();
        if (auto *earth = root->findChild<EarthView *>(QStringLiteral("earthView"))) {
            auto *feed = new OrbitFeed(&app);
            QObject::connect(feed, &OrbitFeed::satellitesUpdated, earth, [earth](const QVariantList &sats) {
                earth->setSatellites(sats);
            });
            QObject::connect(feed, &OrbitFeed::groundStationsUpdated, earth, [earth](const QVariantList &stations) {
                earth->setGroundStations(stations);
            });
            QObject::connect(feed, &OrbitFeed::statusMessage, [](const QString &msg) {
                qInfo().noquote() << msg;
            });
            feed->setSubject(QStringLiteral("m.orbit.*"));
            feed->start();
        }
    }

    return app.exec();
}
