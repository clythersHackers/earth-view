import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EarthView

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    color: "#10141c"
    property var hoveredSatellite: null

    function formatSatellite(info) {
        if (!info || Object.keys(info).length === 0)
            return "Hover a satellite to see details"
        let parts = []
        const idVal = info.ID ?? info.id ?? info.Id
        if (idVal !== undefined)
            parts.push("ID " + idVal)
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

    EarthView {
        id: earth
        objectName: "earthView"
        anchors.fill: parent
        onSatelliteHovered: function(satelliteInfo) { hoveredSatellite = satelliteInfo }
    }

    Component.onCompleted: {
        // Auto-load bundled sample ground stations (application-level, not inside the widget)
        const url = Qt.resolvedUrl("assets/earth/ground_stations.json")
        const xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                try {
                    earth.groundStations = JSON.parse(xhr.responseText)
                } catch (e) {
                    console.log("Failed to parse ground stations", e)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }

    Rectangle {
        id: hoverBadge
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        radius: 6
        color: "#1c2331"
        opacity: 0.9
        border.color: "#263143"
        border.width: 1
        z: 2
        width: hoverText.implicitWidth + 16
        height: hoverText.implicitHeight + 12

        Text {
            id: hoverText
            anchors.fill: parent
            anchors.margins: 8
            color: "#dfe6f3"
            font.pixelSize: 13
            wrapMode: Text.NoWrap
            text: formatSatellite(hoveredSatellite)
        }
    }

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 4
        }
        height: 52
        radius: 6
        color: "#1c2331"
        opacity: 0.9

        RowLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 8

            Text {
                text: "Center"
                color: "#dfe6f3"
                font.pixelSize: 14
                width: 80
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignVCenter
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
                color: "#dfe6f3"
                font.pixelSize: 14
                Layout.preferredWidth: 70
                Layout.alignment: Qt.AlignVCenter
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
                Layout.alignment: Qt.AlignVCenter
            }

            CheckBox {
                id: rotatePortrait
                text: "Rotate"
                checked: earth.rotatePortrait
                onToggled: earth.rotatePortrait = checked
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
