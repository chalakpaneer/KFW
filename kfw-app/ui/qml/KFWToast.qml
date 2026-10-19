// KFWToast.qml — transient notification capsule
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Effects

Rectangle {
    id: root

    readonly property int success: 0
    readonly property int warnT: 1
    readonly property int errorT: 2
    readonly property int infoT: 3

    function show(message, tone) {
        const t = tone === undefined ? 3 : tone
        toastText.text = message
        toastIcon.source = t === errorT ? "icons/critical.svg"
                        : t === warnT ? "icons/warning.svg"
                        : t === success ? "icons/ok.svg"
                        : "icons/info.svg"
        toastTone = t
        animate.restart()
    }

    property int toastTone: infoT

    function toneColor() {
        return toastTone === errorT ? theme.bad
             : toastTone === warnT  ? theme.warn
             : toastTone === success ? theme.ok
             : theme.info
    }

    anchors {
        bottom: parent.bottom
        horizontalCenter: parent.horizontalCenter
        bottomMargin: 30
    }
    width: toastRow.implicitWidth + 44
    height: 42
    radius: 21
    color: theme.panel
    border.width: 1
    border.color: Qt.alpha(toneColor(), 0.55)
    opacity: 0
    z: 500

    SequentialAnimation {
        id: animate
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: 160 }
        PauseAnimation  { duration: 3200 }
        NumberAnimation { target: root; property: "opacity"; to: 0; duration: 380 }
    }

    Row {
        id: toastRow
        anchors.centerIn: parent
        spacing: 10

        Item {
            width: 18
            height: 18
            Image {
                id: toastIcon
                width: 18
                height: 18
                anchors.centerIn: parent
                source: "icons/info.svg"
                fillMode: Image.PreserveAspectFit
                mipmap: true
                visible: false
            }
            MultiEffect {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: toastIcon
                colorization: 1
                colorizationColor: root.toneColor()
            }
        }

        Text {
            id: toastText
            font.pixelSize: 12
            font.family: theme.fontFamily
            color: theme.textHi
            text: ""
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
