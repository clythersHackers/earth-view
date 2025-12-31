Earth Overview Renderer (Qt Quick)
Purpose

This component renders a flat Earth overview for satellite monitoring:

satellites (points)

ground tracks (past/future lines)

coverage / visibility shapes

ground stations

optional day/night terminator

It is an operator overview, not a GIS/map application.

The design prioritises:

correctness

performance on phones and WASM

deterministic behaviour

minimal UI clutter

High-level design
Core principles

Single renderer

One custom C++ QQuickItem

Draws both the Earth background and all overlays

GPU scenegraph rendering

No Canvas

No runtime SVG rendering

No per-satellite QML Items

Flat equirectangular projection

Latitude → Y (linear)

Longitude → X (linear)

Distortion near poles is acceptable and expected

Viewer owns projection

NATS data is WGS84 / ECEF

Projection, centring, seam handling are UI concerns

Earth background

A single bundled PNG texture (RGBA)

Equirectangular (2:1 aspect)

Desaturated / low contrast

Land in mid-grey, partially transparent

Ocean more transparent

No labels, no borders

The texture is:

embedded via .qrc

loaded once

reused for the lifetime of the view

Longitude wrapping

The background texture is wrapped, not split:

The same texture is drawn multiple times horizontally

Horizontal offset is derived from centerLongitude

At least two copies are drawn to guarantee coverage

Foreground geometry is seam-split; background is seam-wrapped

Foreground geometry

Rendered in the same QQuickItem using QSGGeometryNodes:

Satellites → points or small quads

Tracks → line strips

Coverage / visibility → polygon outlines or filled fans

Ground stations → points + optional footprint outlines

All geometry:

is built in C++

uses the same projection logic

shares the same seam logic

Seam handling (dateline)

Because longitude wraps at ±180°:

Any polyline or polygon crossing the seam must be split

Rule of thumb:

if abs(lon[i] - lon[i-1]) > 180° → seam crossing

Resulting segments are drawn separately

This applies to:

satellite tracks

coverage polygons

terminator line

Data model (NATS)
Satellites (pub/sub)

Messages contain truth data, not viewer semantics:

time reference

lat / lon / alt (or ECEF)

optional sampled past/future track points

optional coverage parameters

status / health

Altitude:

does not affect map position

does affect coverage, visibility, lighting

Ground stations (KV + pub/sub)

KV: static definitions

lat / lon / alt

masks

optionally precomputed footprint polygons (e.g. 64 points)

pub/sub: dynamic status

availability

contact state

alarms

Geometry published in NATS must always be WGS84 lat/lon, never screen-space.

View state (local only)

Per-user / per-device state, not shared:

centerLongitude

zoom (if supported)

aspect-ratio dependent behaviour

selection state

declutter level

Changing centerLongitude:

shifts background texture

rebuilds foreground geometry

does not change NATS data

Layout & responsiveness

Landscape is the primary mode

tablets and laptops assumed landscape

full Earth overview

Portrait is a constrained mode

Earth shown as a horizontal band (letterboxed)

fewer overlays

optional horizontal scrollbar for longitude recentering

Aspect ratio drives behaviour, not device type.

What this component is NOT

Not a map engine

Not Qt Location

Not Cesium

Not SVG-driven

Not Canvas-based

Not free-pan / free-zoom GIS

Implementation constraints (important)

Codex / contributors must not:

introduce QML Canvas

draw geometry in QML

use Repeater for satellites

animate hundreds of QML items

rasterise SVGs at runtime

couple rendering logic to NATS semantics

All heavy rendering stays in one C++ QQuickItem.

Goal state

At the end, we want:

One reusable EarthOverviewItem

Deterministic rendering

Correct seam behaviour

Smooth performance on desktop, phone, and WASM

A subdued Earth background that never competes with foreground data

This README is the contract.
Any code generation or assistance must adhere to it.
