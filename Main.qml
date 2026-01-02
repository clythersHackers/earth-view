import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import EarthView

ApplicationWindow {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    // Follow desktop theme using Material style
    Material.theme: Material.System
    Material.accent: Material.DeepPurple
    color: Material.background
    readonly property color textColor: Material.foreground
    readonly property color surfaceColor: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.95)
    readonly property color borderColor: Material.dividerColor
    property var hoveredSatellite: null
    property var hoveredGroundStation: null

    function formatSatellite(info) {
        if (!info || Object.keys(info).length === 0)
            return ""
        let parts = []
        const idVal = info.ID ?? info.id ?? info.Id
        if (idVal !== undefined)
            parts.push("SAT " + idVal)
        if (info.Lat !== undefined && info.Lon !== undefined)
            parts.push("lat " + Number(info.Lat).toFixed(3) + " lon " + Number(info.Lon).toFixed(3))
        if (info.Alt !== undefined)
            parts.push("alt " + Number(info.Alt).toFixed(1) + " km")
        if (info.LatPast !== undefined && info.LonPast !== undefined)
            parts.push("past " + Number(info.LatPast).toFixed(2) + ", " + Number(info.LonPast).toFixed(2))
        if (info.LatFuture !== undefined && info.LonFuture !== undefined)
            parts.push("future " + Number(info.LatFuture).toFixed(2) + ", " + Number(info.LonFuture).toFixed(2))
        return parts.join("  •  ")
    }

    function formatGroundStation(info) {
        if (!info || Object.keys(info).length === 0)
            return ""
        let parts = []
        const idVal = info.ID ?? info.id ?? info.Id
        if (idVal !== undefined)
            parts.push("GS " + idVal)
        else
            parts.push("Ground Station")
        if (info.Lat !== undefined && info.Lon !== undefined)
            parts.push("lat " + Number(info.Lat).toFixed(3) + " lon " + Number(info.Lon).toFixed(3))
        const radius = info.RadiusKm ?? info.radius_km ?? info.radiusKm ?? info.radius
        if (radius !== undefined)
            parts.push("radius " + Number(radius).toFixed(1) + " km")
        return parts.join("  •  ")
    }

    function formatBadge() {
        const satText = formatSatellite(hoveredSatellite)
        if (satText.length > 0)
            return satText
        const gsText = formatGroundStation(hoveredGroundStation)
        if (gsText.length > 0)
            return gsText
        return "Hover a satellite or ground station to see details"
    }

    EarthView {
        id: earth
        objectName: "earthView"
        anchors.fill: parent
        onSatelliteHovered: function(satelliteInfo) { hoveredSatellite = satelliteInfo }
        onGroundStationHovered: function(groundStationInfo) { hoveredGroundStation = groundStationInfo }
    }

    Rectangle {
        id: hoverBadge
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        radius: 4
        color: surfaceColor
        opacity: 0.95
        border.color: borderColor
        border.width: 1
        z: 2
        width: hoverText.implicitWidth + 12
        height: hoverText.implicitHeight + 10

        Label {
            id: hoverText
            anchors.fill: parent
            anchors.margins: 6
            wrapMode: Text.NoWrap
            text: formatBadge()
        }
    }

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 2
        }
        height: 44
        radius: 4
        color: surfaceColor
        opacity: 0.95

        RowLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2

            Label {
                text: "Center"
            }

            Slider {
                id: lonSlider
                from: -180
                to: 180
                value: earth.centerLongitude
                onValueChanged: earth.centerLongitude = value
                Layout.fillWidth: true
            }

            TextField {
                id: lonField
                text: earth.centerLongitude.toFixed(1)
                Layout.preferredWidth: 68
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onEditingFinished: {
                    const v = parseFloat(text)
                    if (!isNaN(v)) {
                        earth.centerLongitude = v
                        lonSlider.value = v
                    }
                }
            }

            CheckBox {
                id: fitWorld
                text: "Fit"
                checked: earth.fitWorld
                onToggled: earth.fitWorld = checked
            }

            CheckBox {
                id: rotatePortrait
                text: "Rotate"
                checked: earth.rotatePortrait
                onToggled: earth.rotatePortrait = checked
            }
        }
    }
}
