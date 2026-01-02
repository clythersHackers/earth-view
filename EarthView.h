#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QSGTexture>
#include <QVariantList>
#include <QVector>
#include <QString>
#include <QColor>
#include <limits>

#include <QtQml/qqmlregistration.h>

class EarthView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    Q_PROPERTY(double centerLongitude READ centerLongitude WRITE setCenterLongitude NOTIFY centerLongitudeChanged)
    Q_PROPERTY(bool fitWorld READ fitWorld WRITE setFitWorld NOTIFY fitWorldChanged)
    Q_PROPERTY(bool rotatePortrait READ rotatePortrait WRITE setRotatePortrait NOTIFY rotatePortraitChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(QVariantList groundStations READ groundStations WRITE setGroundStations NOTIFY groundStationsChanged)
    Q_PROPERTY(QVariantList satellites READ satellites WRITE setSatellites NOTIFY satellitesChanged)

    explicit EarthView(QQuickItem *parent = nullptr);

    double centerLongitude() const { return m_centerLongitude; }
    void setCenterLongitude(double lon);

    bool fitWorld() const { return m_fitWorld; }
    void setFitWorld(bool fit);

    bool rotatePortrait() const { return m_rotatePortrait; }
    void setRotatePortrait(bool rotate);

    QColor accentColor() const { return m_accentColor; }
    void setAccentColor(const QColor &color);

    QVariantList groundStations() const { return m_groundStations; }
    void setGroundStations(const QVariantList &stations);

    QVariantList satellites() const { return m_satellites; }
    void setSatellites(const QVariantList &sats);



protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void timerEvent(QTimerEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void touchEvent(QTouchEvent *event) override;

signals:
    void centerLongitudeChanged();
    void fitWorldChanged();
    void rotatePortraitChanged();
    void accentColorChanged();
    void groundStationsChanged();
    void satellitesChanged();
    void satelliteHovered(const QVariantMap &satelliteInfo);
    void groundStationHovered(const QVariantMap &groundStationInfo);

private:
    void ensureTexture();
    QVariantMap satelliteAt(const QPointF &pt) const;
    QVariantMap groundStationAt(const QPointF &pt) const;
    QRectF viewRect(bool &rotated) const;

    QImage m_backgroundImage;
    QPointer<QSGTexture> m_texture;
    QPointer<QQuickWindow> m_lastWindow;
    double m_centerLongitude {0.0};
    bool m_fitWorld {true};
    bool m_rotatePortrait {false};
    QColor m_accentColor {QColor(90, 210, 255)}; // default pale/electric blue
    QVariantList m_groundStations;
    QVariantList m_satellites;

    struct GeoPoint {
        double lat {0.0};
        double lon {0.0};
    };

    struct GroundStation {
        double lat {0.0};
        double lon {0.0};
        double radiusKm {0.0};
        QString id;
        QVector<GeoPoint> mask;
        QVariantMap raw;
    };
    QVector<GroundStation> m_groundStationData;

    struct Satellite {
        double lat {0.0};
        double lon {0.0};
        double latPast {std::numeric_limits<double>::quiet_NaN()};
        double lonPast {std::numeric_limits<double>::quiet_NaN()};
        double latFuture {std::numeric_limits<double>::quiet_NaN()};
        double lonFuture {std::numeric_limits<double>::quiet_NaN()};
        QString id;
        QVariantMap raw;
    };
    QVector<Satellite> m_satelliteData;
    bool m_lastHoverHadSat {false};
    bool m_lastHoverHadGroundStation {false};

};
