# Earth Overview Renderer (Qt Quick)

## Purpose
- Flat Earth overview for satellite monitoring: satellites, ground tracks (past/future), coverage/visibility shapes, ground stations, optional day/night terminator.
- Operator overview, not a GIS/map app; aims for correctness, deterministic behaviour, and smooth performance on phones and WASM with minimal UI.
- Reusable `EarthOverviewItem` with correct seam handling, subdued background, and smooth performance on desktop/phone/WASM.
- The bundled Qt/QML application is for testing & development only; not a reference UI.

## High-Level Design

### Core Principles
- Single renderer: one custom C++ `QQuickItem` draws background and overlays via the GPU scenegraph (no Canvas/runtime SVG, no per-satellite QML items).
- Flat equirectangular projection: latitude → Y, longitude → X; polar distortion is acceptable.
- Viewer owns projection; NATS data is provided in WGS84/ECEF. Projection, centring, and seam handling live in the view.

### Earth Background
- Single bundled RGBA PNG, equirectangular 2:1, desaturated/low contrast; land mid-grey and partially transparent, ocean more transparent, no labels/borders.
- Embedded via `.qrc`, loaded once, reused for the lifetime of the view.

### Longitude Wrapping
- Background texture is wrapped (not split): same texture drawn multiple times horizontally.
- Horizontal offset derived from `centerLongitude`; at least two copies drawn for coverage.
- Foreground geometry is seam-split; background is seam-wrapped.

### Foreground Geometry
- Same `QQuickItem` using `QSGGeometryNode`s:
  - Satellites → points or small quads
  - Tracks → line strips
  - Coverage/visibility → polygon outlines or filled fans
  - Ground stations → points + optional footprint outlines
- Geometry is built in C++ with shared projection/seam logic.

### Seam Handling (Dateline)
- Longitude wraps at ±180°; any polyline or polygon crossing the seam must be split.
- Rule: if `abs(lon[i] - lon[i-1]) > 180°`, treat as a seam crossing and draw resulting segments separately.
- Applies to satellite tracks, coverage polygons, and the terminator line.

## Data Model

### Schema (inputs to EarthView)

EarthView consumes two payload types (regardless of transport):

- **Satellites**: list of maps
  - Required: `Lat`, `Lon` (degrees; lat in [-90, 90], lon in [-180, 180]); optionally `ID`/`id`.
  - Optional: `Alt`/`alt` (km), `LatPast`/`LonPast`, `LatFuture`/`LonFuture` (degrees) for short past/future track segments.
  - Field names are case-tolerant (`lat`/`Lat`, `lon`/`Lon`, etc.); entries with non-finite coords are ignored.
  - Payload is handed directly to `EarthView::setSatellites(const QVariantList &)`; extra fields are preserved in the hover signal.

- **Ground stations**: list of maps
  - Position: `lat`/`Lat`, `lon`/`Lon` (degrees). If absent but a mask is present, the centroid of the mask is used.
  - Footprint/mask (optional): array under `mask`/`Mask`, `boundary`/`footprint`/`points`; each point is `[lat, lon]` or `{lat, lon}`.
  - Optional radius: `radius_km`/`RadiusKm`/`radiusKm`/`radius` (km).
  - Optional ID: `id`/`ID` (otherwise empty).
  - Payload is handed to `EarthView::setGroundStations(const QVariantList &)`.

All geometry is expected in WGS84 lat/lon; EarthView handles projection, seam-splitting, and rendering. Invalid or out-of-range entries are skipped.

### Satellites (NATS pub/sub)
- Messages carry truth data only: time reference; lat/lon/alt (or ECEF); optional sampled past/future track points; optional coverage parameters; status/health.
- Altitude does not affect map position but does affect coverage & visibility.

### Ground Stations (NATS KV + pub/sub)
- KV stores static definitions: lat/lon/alt, masks, optionally precomputed footprint polygons (e.g., 72 points = 5deg).
- Pub/sub carries dynamic status: availability, contact state, alarms.
- Geometry published in NATS is WGS84 lat/lon, never screen-space.

### View State (Local Only)
- Per-user/device, not shared: `centerLongitude`, zoom (if supported), aspect-ratio dependent behaviour, selection state, declutter level.
- Changing `centerLongitude` shifts the background texture and rebuilds foreground geometry; it does not change NATS data.

## Layout & Responsiveness
- Landscape is primary: tablets/laptops assumed landscape, full Earth overview.
- Portrait is constrained: Earth shown as a horizontal band (letterboxed), fewer overlays, optional horizontal scrollbar for longitude recentering.
- Aspect ratio drives behaviour, not device type.

## Out of Scope
- Not a map engine/Qt Location/Cesium, not SVG- or Canvas-based, not free-pan/zoom GIS.

## Implementation Constraints
- Goal is very lightweight rendering & smooth performance on low-end devices e.g. in WASM.
- Coupling to NATS messages/KV is at application level
- All heavy rendering stays in one C++ `QQuickItem`.


## License

This project is distributed under the Mozilla Public License 2.0. See `LICENSE.txt` for details.