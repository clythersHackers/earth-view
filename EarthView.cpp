#include "EarthView.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QVariantMap>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <cmath>
#include <algorithm>

EarthView::EarthView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
    // The resource is bundled by the QML module under /EarthView/.
    m_backgroundImage = QImage(QStringLiteral(":/EarthView/assets/earth/earth-landmask-2048.png"));
}

void EarthView::ensureTexture()
{
    if (!window()) {
        return;
    }

    if (m_lastWindow != window()) {
        // Window changed; drop the old texture to avoid using it with another scene graph.
        if (m_texture) {
            m_texture->deleteLater();
        }
        m_texture = nullptr;
        m_lastWindow = window();
    }

    if (!m_texture && !m_backgroundImage.isNull()) {
        m_texture = window()->createTextureFromImage(m_backgroundImage);
    }
}

void EarthView::setCenterLongitude(double lon)
{
    // Wrap to [-180, 180)
    lon = std::fmod(lon + 180.0, 360.0);
    if (lon < 0)
        lon += 360.0;
    lon -= 180.0;

    if (qFuzzyCompare(lon, m_centerLongitude)) {
        return;
    }

    m_centerLongitude = lon;
    emit centerLongitudeChanged();
    update();
}

void EarthView::setFitWorld(bool fit)
{
    if (m_fitWorld == fit) {
        return;
    }
    m_fitWorld = fit;
    emit fitWorldChanged();
    update();
}

void EarthView::setRotatePortrait(bool rotate)
{
    if (m_rotatePortrait == rotate) {
        return;
    }
    m_rotatePortrait = rotate;
    emit rotatePortraitChanged();
    update();
}

void EarthView::setGroundStations(const QVariantList &stations)
{
    m_groundStations = stations;
    m_groundStationData.clear();

    for (const auto &v : stations) {
        const QVariantMap m = v.toMap();
        const double lat = m.value(QStringLiteral("lat")).toDouble();
        const double lon = m.value(QStringLiteral("lon")).toDouble();
        const double radius = m.value(QStringLiteral("radius_km")).toDouble();
        if (!std::isfinite(lat) || !std::isfinite(lon))
            continue;
        if (lat < -90.0 || lat > 90.0)
            continue;
        GroundStation gs;
        gs.lat = lat;
        gs.lon = lon;
        if (std::isfinite(radius) && radius > 0.0)
            gs.radiusKm = radius;
        m_groundStationData.push_back(gs);
    }

    emit groundStationsChanged();
    update();
}

void EarthView::setSatellites(const QVariantList &sats)
{
    m_satellites = sats;
    m_satelliteData.clear();

    auto readField = [](const QVariantMap &m, const char *key, double &out) -> bool {
        const QVariant v = m.value(QString::fromLatin1(key));
        bool ok = false;
        double val = v.toDouble(&ok);
        if (ok && std::isfinite(val)) {
            out = val;
            return true;
        }
        return false;
    };

    for (const auto &v : sats) {
        QVariantMap m = v.toMap();
        const QVariant idField = m.value(QStringLiteral("ID"), m.value(QStringLiteral("id")));
        const double lat = m.value(QStringLiteral("Lat")).toDouble();
        const double lon = m.value(QStringLiteral("Lon")).toDouble();
        if (!std::isfinite(lat) || !std::isfinite(lon))
            continue;
        if (lat < -90.0 || lat > 90.0)
            continue;
        Satellite s;
        s.lat = lat;
        s.lon = lon;
        readField(m, "LatPast", s.latPast);
        readField(m, "LonPast", s.lonPast);
        readField(m, "LatFuture", s.latFuture);
        readField(m, "LonFuture", s.lonFuture);
        if (idField.isValid())
            s.id = idField.toString();
        s.raw = m;
        if (idField.isValid())
            s.raw.insert(QStringLiteral("ID"), idField);
        m_satelliteData.push_back(s);
    }

    emit satellitesChanged();
    update();
}

QRectF EarthView::viewRect(bool &rotated) const
{
    const QRectF bounds = boundingRect();
    constexpr qreal targetAspect = 2.0; // 360x180 degrees -> 2:1
    rotated = m_rotatePortrait && bounds.height() > bounds.width();

    const qreal availW = rotated ? bounds.height() : bounds.width();
    const qreal availH = rotated ? bounds.width() : bounds.height();
    const qreal availAspect = availH > 0 ? availW / availH : targetAspect;

    QRectF rect;
    if (m_fitWorld) {
        if (availAspect >= targetAspect) {
            const qreal h = availH;
            const qreal w = h * targetAspect;
            rect = QRectF(0, 0, w, h);
        } else {
            const qreal w = availW;
            const qreal h = w / targetAspect;
            rect = QRectF(0, 0, w, h);
        }
    } else {
        const qreal h = availH;
        const qreal w = h * targetAspect;
        rect = QRectF(0, 0, w, h);
    }
    rect.moveCenter(bounds.center());
    return rect;
}

QSGNode *EarthView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    ensureTexture();

    // Root -> transform -> clip -> content nodes
    QSGNode *root = oldNode;
    QSGTransformNode *transformNode = nullptr;
    QSGClipNode *clipNode = nullptr;
    QSGNode *contentRoot = nullptr;
    QVector<QSGSimpleTextureNode *> textureNodes;
    QSGGeometryNode *gsFootNode = nullptr;
    QSGGeometryNode *gsDotNode = nullptr;
    QSGGeometryNode *satNode = nullptr;
    QSGGeometryNode *satPastNode = nullptr;
    QSGGeometryNode *satFutureNode = nullptr;

    if (!root) {
        root = new QSGNode();
    } else {
        // Walk known structure but be defensive
        for (QSGNode *child = root->firstChild(); child; child = child->nextSibling()) {
            if (auto *t = dynamic_cast<QSGTransformNode *>(child)) {
                transformNode = t;
                break;
            }
        }
        if (transformNode) {
            for (QSGNode *child = transformNode->firstChild(); child; child = child->nextSibling()) {
                if (auto *c = dynamic_cast<QSGClipNode *>(child)) {
                    clipNode = c;
                    break;
                }
            }
        }
        if (clipNode) {
            for (QSGNode *child = clipNode->firstChild(); child; child = child->nextSibling()) {
                if (!contentRoot && dynamic_cast<QSGGeometryNode *>(child) == nullptr) {
                    contentRoot = child;
                }
            }
        }
    }

    if (m_texture) {
        bool doRotate = false;
        const QRectF bounds = boundingRect();
        const QRectF rect = viewRect(doRotate);

        // Set/update transform for optional portrait rotation
        if (!transformNode) {
            transformNode = new QSGTransformNode();
            root->appendChildNode(transformNode);
        }
        if (doRotate) {
            QMatrix4x4 mat;
            // rotate around center of bounds
            const QPointF c = bounds.center();
            mat.translate(c.x(), c.y());
            mat.rotate(-90, 0, 0, 1);
            mat.translate(-c.x(), -c.y());
            transformNode->setMatrix(mat);
        } else {
            transformNode->setMatrix(QMatrix4x4());
        }

        // Set/update clip
        if (!clipNode) {
            clipNode = new QSGClipNode();
            clipNode->setIsRectangular(true);
            transformNode->appendChildNode(clipNode);
        }
        clipNode->setClipRect(rect);

        // Ensure content root
        if (!contentRoot) {
            contentRoot = new QSGNode();
            clipNode->appendChildNode(contentRoot);
        }

        // Collect existing children for reuse
        for (QSGNode *child = contentRoot->firstChild(); child; child = child->nextSibling()) {
            if (auto *tex = dynamic_cast<QSGSimpleTextureNode *>(child)) {
                textureNodes.append(tex);
                continue;
            }
            if (auto *geom = dynamic_cast<QSGGeometryNode *>(child)) {
                if (auto *mat = dynamic_cast<QSGFlatColorMaterial *>(geom->material())) {
                    const QColor c = mat->color();
                    if (!gsFootNode && c == QColor(200, 200, 200, 90)) {
                        gsFootNode = geom;
                        continue;
                    }
                    if (!gsDotNode && c == QColor(180, 180, 180, 220)) {
                        gsDotNode = geom;
                        continue;
                    }
                    if (!satNode && c == QColor(220, 240, 255, 240)) {
                        satNode = geom;
                        continue;
                    }
                    if (!satPastNode && c == QColor(180, 200, 220, 140)) {
                        satPastNode = geom;
                        continue;
                    }
                    if (!satFutureNode && c == QColor(120, 190, 255, 220)) {
                        satFutureNode = geom;
                        continue;
                    }
                }
            }
        }

        // Offset in [0, width)
        qreal offset = std::fmod((m_centerLongitude / 360.0) * rect.width(), rect.width());
        if (offset < 0)
            offset += rect.width();
        const qreal baseX = rect.x() - offset;

        // Ensure two texture nodes
        while (textureNodes.size() < 2) {
            auto *n = new QSGSimpleTextureNode();
            n->setOwnsTexture(false);
            contentRoot->appendChildNode(n);
            textureNodes.append(n);
        }
        while (textureNodes.size() > 2) {
            QSGSimpleTextureNode *extra = textureNodes.takeLast();
            contentRoot->removeChildNode(extra);
            delete extra;
        }
        for (int i = 0; i < textureNodes.size(); ++i) {
            QSGSimpleTextureNode *n = textureNodes[i];
            n->setTexture(m_texture);
            const qreal x = baseX + i * rect.width();
            n->setRect(QRectF(x, rect.y(), rect.width(), rect.height()));
        }

        // Terminator removed for now.

        auto project = [&](double latDeg, double lonDeg) -> QPointF {
            qreal x = rect.x() + ((lonDeg + 180.0) / 360.0) * rect.width();
            qreal y = rect.y() + ((90.0 - latDeg) / 180.0) * rect.height();
            // apply wrap based on center longitude
            qreal localOffset = (m_centerLongitude / 360.0) * rect.width();
            x -= localOffset;
            while (x < rect.x()) x += rect.width();
            while (x > rect.x() + rect.width()) x -= rect.width();
            return QPointF(x, y);
        };

        // Ground station footprints
        if (m_groundStationData.isEmpty()) {
            if (gsFootNode) {
                contentRoot->removeChildNode(gsFootNode);
                delete gsFootNode;
                gsFootNode = nullptr;
            }
            if (gsDotNode) {
                contentRoot->removeChildNode(gsDotNode);
                delete gsDotNode;
                gsDotNode = nullptr;
            }
        } else {
            // Footprint node
            if (!gsFootNode) {
                gsFootNode = new QSGGeometryNode();
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                geom->setDrawingMode(QSGGeometry::DrawTriangles);
                gsFootNode->setGeometry(geom);
                gsFootNode->setFlag(QSGNode::OwnsGeometry);

                auto *mat = new QSGFlatColorMaterial();
                mat->setColor(QColor(200, 200, 200, 90)); // subtle, similar to background
                gsFootNode->setMaterial(mat);
                gsFootNode->setFlag(QSGNode::OwnsMaterial);
                contentRoot->appendChildNode(gsFootNode);
            }
            if (!gsDotNode) {
                gsDotNode = new QSGGeometryNode();
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                geom->setDrawingMode(QSGGeometry::DrawTriangles);
                gsDotNode->setGeometry(geom);
                gsDotNode->setFlag(QSGNode::OwnsGeometry);

                auto *mat = new QSGFlatColorMaterial();
                mat->setColor(QColor(180, 180, 180, 220)); // slightly brighter dot
                gsDotNode->setMaterial(mat);
                gsDotNode->setFlag(QSGNode::OwnsMaterial);
                contentRoot->appendChildNode(gsDotNode);
            }

            const int footSegments = 36;
            const int dotSegments = 10;
            const qreal dotPxRadius = 4.0;

            // Footprints: triangle lists per station, duplicating across seam
            {
                const int vertsPerFoot = footSegments * 3; // triangles per ellipse
                QVector<QPointF> centers;
                QVector<QPointF> radii;
                centers.reserve(m_groundStationData.size() * 2);
                radii.reserve(m_groundStationData.size() * 2);

                for (const auto &gs : m_groundStationData) {
                    const QPointF c = project(gs.lat, gs.lon);
                    const double latRad = gs.lat * M_PI / 180.0;
                    const double effectiveKm = (gs.radiusKm > 0.0 ? gs.radiusKm : 200.0) * 1.5;
                    const double latDegSpan = effectiveKm / 111.0;
                    const double lonDegSpan = effectiveKm / (111.0 * std::max(0.2, std::cos(latRad)));
                    const qreal rx = (lonDegSpan / 360.0) * rect.width();
                    const qreal ry = (latDegSpan / 180.0) * rect.height();

                    auto addCopy = [&](const QPointF &pt) {
                        centers.append(pt);
                        radii.append(QPointF(rx, ry));
                    };

                    addCopy(c);
                    if (c.x() - rx < rect.x())
                        addCopy(QPointF(c.x() + rect.width(), c.y()));
                    if (c.x() + rx > rect.x() + rect.width())
                        addCopy(QPointF(c.x() - rect.width(), c.y()));
                }

                QSGGeometry *geom = gsFootNode->geometry();
                geom->allocate(centers.size() * vertsPerFoot);
                QSGGeometry::Point2D *v = geom->vertexDataAsPoint2D();
                int idx = 0;
                for (int i = 0; i < centers.size(); ++i) {
                    const QPointF c = centers[i];
                    const QPointF r = radii[i];
                    for (int s = 0; s < footSegments; ++s) {
                        const qreal a0 = (2 * M_PI * s) / footSegments;
                        const qreal a1 = (2 * M_PI * (s + 1)) / footSegments;
                        const QPointF p0 = c + QPointF(std::cos(a0) * r.x(), std::sin(a0) * r.y());
                        const QPointF p1 = c + QPointF(std::cos(a1) * r.x(), std::sin(a1) * r.y());
                        v[idx++].set(c.x(), c.y());
                        v[idx++].set(p0.x(), p0.y());
                        v[idx++].set(p1.x(), p1.y());
                    }
                }
                gsFootNode->markDirty(QSGNode::DirtyGeometry);
            }

            // Dots: small circles in px space, duplicating across seam if needed
            {
                const int vertsPerCircle = dotSegments * 3;
                QVector<QPointF> centers;
                centers.reserve(m_groundStationData.size() * 2);
                for (const auto &gs : m_groundStationData) {
                    const QPointF c = project(gs.lat, gs.lon);
                    centers.append(c);
                    if (c.x() < rect.x() + dotPxRadius)
                        centers.append(QPointF(c.x() + rect.width(), c.y()));
                    if (c.x() > rect.x() + rect.width() - dotPxRadius)
                        centers.append(QPointF(c.x() - rect.width(), c.y()));
                }

                QSGGeometry *geom = gsDotNode->geometry();
                geom->allocate(centers.size() * vertsPerCircle);
                geom->setDrawingMode(QSGGeometry::DrawTriangles);
                QSGGeometry::Point2D *v = geom->vertexDataAsPoint2D();
                int idx = 0;
                for (const QPointF &c : centers) {
                    for (int s = 0; s < dotSegments; ++s) {
                        const qreal a0 = (2 * M_PI * s) / dotSegments;
                        const qreal a1 = (2 * M_PI * (s + 1)) / dotSegments;
                        const QPointF p0 = c + QPointF(std::cos(a0), std::sin(a0)) * dotPxRadius;
                        const QPointF p1 = c + QPointF(std::cos(a1), std::sin(a1)) * dotPxRadius;
                        v[idx++].set(c.x(), c.y());
                        v[idx++].set(p0.x(), p0.y());
                        v[idx++].set(p1.x(), p1.y());
                    }
                }
                gsDotNode->markDirty(QSGNode::DirtyGeometry);
            }
        }

    auto sampleArc = [&](double latA, double lonA, double latB, double lonB, int segments) -> QVector<QPointF> {
        QVector<QPointF> pts;
        if (segments < 2)
            return pts;

        const double aLat = latA * M_PI / 180.0;
            const double aLon = lonA * M_PI / 180.0;
            const double bLat = latB * M_PI / 180.0;
            const double bLon = lonB * M_PI / 180.0;

            auto toVec = [](double lat, double lon) {
                return QVector3D(std::cos(lat) * std::cos(lon),
                                 std::cos(lat) * std::sin(lon),
                                 std::sin(lat));
        };
        QVector3D A = toVec(aLat, aLon).normalized();
        QVector3D B = toVec(bLat, bLon).normalized();

        const float dot = QVector3D::dotProduct(A, B);
        double omega = std::acos(std::clamp(static_cast<double>(dot), -1.0, 1.0));
        if (omega < 1e-6)
            return pts;

            for (int i = 0; i < segments; ++i) {
                const double t = static_cast<double>(i) / (segments - 1);
                const double sinOmega = std::sin(omega);
                const double wA = std::sin((1 - t) * omega) / sinOmega;
                const double wB = std::sin(t * omega) / sinOmega;
                QVector3D P = A * wA + B * wB;
                P.normalize();
                const double lat = std::asin(std::clamp(static_cast<double>(P.z()), -1.0, 1.0)) * 180.0 / M_PI;
                const double lon = std::atan2(P.y(), P.x()) * 180.0 / M_PI;
                pts.append(project(lat, lon));
            }
        return pts;
    };

    auto findClosestSatellite = [&](const QPointF &pt) -> QVariantMap {
        const qreal maxDistPx = 12.0;
        QVariantMap best;
        qreal bestDist2 = maxDistPx * maxDistPx;
        for (const auto &sat : m_satelliteData) {
            const QPointF c = project(sat.lat, sat.lon);
            const qreal dx = c.x() - pt.x();
            const qreal dy = c.y() - pt.y();
            const qreal d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                best = sat.raw;
            }
        }
        return best;
    };

        // Satellites (small dots) and direction lines
        if (m_satelliteData.isEmpty()) {
            if (satNode) {
                contentRoot->removeChildNode(satNode);
                delete satNode;
                satNode = nullptr;
            }
            if (satPastNode) {
                contentRoot->removeChildNode(satPastNode);
                delete satPastNode;
                satPastNode = nullptr;
            }
            if (satFutureNode) {
                contentRoot->removeChildNode(satFutureNode);
                delete satFutureNode;
                satFutureNode = nullptr;
            }
        } else {
            if (!satPastNode) {
                satPastNode = new QSGGeometryNode();
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                geom->setDrawingMode(QSGGeometry::DrawLines);
                satPastNode->setGeometry(geom);
                satPastNode->setFlag(QSGNode::OwnsGeometry);
                auto *mat = new QSGFlatColorMaterial();
                mat->setColor(QColor(180, 200, 220, 140));
                satPastNode->setMaterial(mat);
                satPastNode->setFlag(QSGNode::OwnsMaterial);
                contentRoot->appendChildNode(satPastNode);
            }
            if (!satFutureNode) {
                satFutureNode = new QSGGeometryNode();
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                geom->setDrawingMode(QSGGeometry::DrawLines);
                satFutureNode->setGeometry(geom);
                satFutureNode->setFlag(QSGNode::OwnsGeometry);
                auto *mat = new QSGFlatColorMaterial();
                mat->setColor(QColor(120, 190, 255, 220));
                satFutureNode->setMaterial(mat);
                satFutureNode->setFlag(QSGNode::OwnsMaterial);
                contentRoot->appendChildNode(satFutureNode);
            }
            if (!satNode) {
                satNode = new QSGGeometryNode();
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                geom->setDrawingMode(QSGGeometry::DrawTriangles);
                satNode->setGeometry(geom);
                satNode->setFlag(QSGNode::OwnsGeometry);

                auto *mat = new QSGFlatColorMaterial();
                mat->setColor(QColor(220, 240, 255, 240));
                satNode->setMaterial(mat);
                satNode->setFlag(QSGNode::OwnsMaterial);
                contentRoot->appendChildNode(satNode);
            }

            // Lines (past -> future only if both exist)
            {
                QVector<QPointF> segmentsPast;
                QVector<QPointF> segmentsFuture;
                segmentsPast.reserve(m_satelliteData.size() * 4);
                segmentsFuture.reserve(m_satelliteData.size() * 4);
                const int arcSamples = 4;
                const qreal w = rect.width();

                for (const auto &sat : m_satelliteData) {
                    if (std::isfinite(sat.latPast) && std::isfinite(sat.lonPast)) {
                        auto pts = sampleArc(sat.latPast, sat.lonPast, sat.lat, sat.lon, arcSamples);
                        for (int i = 0; i + 1 < pts.size(); ++i) {
                            const QPointF a = pts[i];
                            QPointF b = pts[i + 1];
                            qreal dx = b.x() - a.x();
                            if (dx > w / 2) b.rx() -= w;
                            else if (dx < -w / 2) b.rx() += w;
                            if (std::abs(b.x() - a.x()) > w)
                                continue;
                            segmentsPast.append(a);
                            segmentsPast.append(b);
                        }
                    }
                    if (std::isfinite(sat.latFuture) && std::isfinite(sat.lonFuture)) {
                        auto pts = sampleArc(sat.lat, sat.lon, sat.latFuture, sat.lonFuture, arcSamples);
                        for (int i = 0; i + 1 < pts.size(); ++i) {
                            const QPointF a = pts[i];
                            QPointF b = pts[i + 1];
                            qreal dx = b.x() - a.x();
                            if (dx > w / 2) b.rx() -= w;
                            else if (dx < -w / 2) b.rx() += w;
                            if (std::abs(b.x() - a.x()) > w)
                                continue;
                            segmentsFuture.append(a);
                            segmentsFuture.append(b);
                        }
                    }
                }
                // Past segments
                QSGGeometry *geomPast = satPastNode->geometry();
                geomPast->allocate(segmentsPast.size());
                geomPast->setDrawingMode(QSGGeometry::DrawLines);
                QSGGeometry::Point2D *vPast = geomPast->vertexDataAsPoint2D();
                for (int i = 0; i < segmentsPast.size(); ++i) {
                    const QPointF a = segmentsPast[i];
                    vPast[i].set(a.x(), a.y());
                }
                satPastNode->markDirty(QSGNode::DirtyGeometry);

                // Future segments
                QSGGeometry *geomFuture = satFutureNode->geometry();
                geomFuture->allocate(segmentsFuture.size());
                geomFuture->setDrawingMode(QSGGeometry::DrawLines);
                QSGGeometry::Point2D *vFuture = geomFuture->vertexDataAsPoint2D();
                for (int i = 0; i < segmentsFuture.size(); ++i) {
                    vFuture[i].set(segmentsFuture[i].x(), segmentsFuture[i].y());
                }
                satFutureNode->markDirty(QSGNode::DirtyGeometry);
            }

            // Dots
            {
                const int dotSegments = 8;
                const qreal dotPxRadius = 3.0;
                const int vertsPerCircle = dotSegments * 3;
                QVector<QPointF> centers;
                centers.reserve(m_satelliteData.size() * 2);
                for (const auto &sat : m_satelliteData) {
                    const QPointF c = project(sat.lat, sat.lon);
                    centers.append(c);
                    if (c.x() < rect.x() + dotPxRadius)
                        centers.append(QPointF(c.x() + rect.width(), c.y()));
                    if (c.x() > rect.x() + rect.width() - dotPxRadius)
                        centers.append(QPointF(c.x() - rect.width(), c.y()));
                }

                QSGGeometry *geom = satNode->geometry();
                geom->allocate(centers.size() * vertsPerCircle);
                geom->setDrawingMode(QSGGeometry::DrawTriangles);
                QSGGeometry::Point2D *v = geom->vertexDataAsPoint2D();
                int idx = 0;
                for (const QPointF &c : centers) {
                    for (int s = 0; s < dotSegments; ++s) {
                        const qreal a0 = (2 * M_PI * s) / dotSegments;
                        const qreal a1 = (2 * M_PI * (s + 1)) / dotSegments;
                        const QPointF p0 = c + QPointF(std::cos(a0), std::sin(a0)) * dotPxRadius;
                        const QPointF p1 = c + QPointF(std::cos(a1), std::sin(a1)) * dotPxRadius;
                        v[idx++].set(c.x(), c.y());
                        v[idx++].set(p0.x(), p0.y());
                        v[idx++].set(p1.x(), p1.y());
                    }
                }
                satNode->markDirty(QSGNode::DirtyGeometry);
            }
        }
    } else {
        // No texture yet; clear children
        if (transformNode) {
            root->removeAllChildNodes();
            delete transformNode;
        }
    }

    return root;
}

void EarthView::timerEvent(QTimerEvent *event)
{
    QQuickItem::timerEvent(event);
}

void EarthView::releaseResources()
{
    if (m_texture) {
        m_texture->deleteLater();
        m_texture = nullptr;
    }
}

void EarthView::hoverMoveEvent(QHoverEvent *event)
{
    const QVariantMap sat = satelliteAt(event->position());
    const bool hasSat = !sat.isEmpty();
    if (hasSat) {
        emit satelliteHovered(sat);
        m_lastHoverHadSat = true;
    } else if (m_lastHoverHadSat) {
        emit satelliteHovered(QVariantMap());
        m_lastHoverHadSat = false;
    }
    event->accept();
}

void EarthView::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event);
    if (m_lastHoverHadSat) {
        emit satelliteHovered(QVariantMap());
        m_lastHoverHadSat = false;
    }
}

void EarthView::mouseMoveEvent(QMouseEvent *event)
{
    const QVariantMap sat = satelliteAt(event->position());
    const bool hasSat = !sat.isEmpty();
    if (hasSat) {
        emit satelliteHovered(sat);
        m_lastHoverHadSat = true;
    } else if (m_lastHoverHadSat) {
        emit satelliteHovered(QVariantMap());
        m_lastHoverHadSat = false;
    }
    QQuickItem::mouseMoveEvent(event);
}

void EarthView::mousePressEvent(QMouseEvent *event)
{
    const QVariantMap sat = satelliteAt(event->position());
    const bool hasSat = !sat.isEmpty();
    if (hasSat) {
        emit satelliteHovered(sat);
        m_lastHoverHadSat = true;
    } else if (m_lastHoverHadSat) {
        emit satelliteHovered(QVariantMap());
        m_lastHoverHadSat = false;
    }
    QQuickItem::mousePressEvent(event);
}

void EarthView::touchEvent(QTouchEvent *event)
{
    if (event->points().isEmpty()) {
        event->ignore();
        return;
    }

    const QPointF pt = event->points().first().position();
    const QVariantMap sat = satelliteAt(pt);
    const bool hasSat = !sat.isEmpty();
    if (hasSat) {
        emit satelliteHovered(sat);
        m_lastHoverHadSat = true;
    } else if (m_lastHoverHadSat) {
        emit satelliteHovered(QVariantMap());
        m_lastHoverHadSat = false;
    }
    event->accept();
}

QVariantMap EarthView::satelliteAt(const QPointF &pt) const
{
    const qreal maxDistPx = 12.0;
    QVariantMap best;
    qreal bestDist2 = maxDistPx * maxDistPx;

    bool rotated = false;
    const QRectF rect = viewRect(rotated);
    const QRectF bounds = boundingRect();

    auto project = [&](double latDeg, double lonDeg) -> QPointF {
        qreal x = rect.x() + ((lonDeg + 180.0) / 360.0) * rect.width();
        qreal y = rect.y() + ((90.0 - latDeg) / 180.0) * rect.height();
        qreal localOffset = (m_centerLongitude / 360.0) * rect.width();
        x -= localOffset;
        while (x < rect.x()) x += rect.width();
        while (x > rect.x() + rect.width()) x -= rect.width();
        return QPointF(x, y);
    };

    auto inverseRotateIfNeeded = [&](const QPointF &p) -> QPointF {
        if (!rotated)
            return p;
        const QPointF c = bounds.center();
        const qreal dx = p.x() - c.x();
        const qreal dy = p.y() - c.y();
        // inverse of the -90 deg rotation used for rendering is +90
        return QPointF(c.x() - dy, c.y() + dx);
    };
    const QPointF queryPt = inverseRotateIfNeeded(pt);

    for (const auto &sat : m_satelliteData) {
        const QPointF c = project(sat.lat, sat.lon);
        const qreal dx = c.x() - queryPt.x();
        const qreal dy = c.y() - queryPt.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best = sat.raw;
            if (!sat.id.isEmpty())
                best.insert(QStringLiteral("ID"), sat.id);
        }
    }
    return best;
}
