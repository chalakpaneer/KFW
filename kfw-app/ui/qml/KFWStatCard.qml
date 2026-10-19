// KFWStatCard.qml — dashboard metric tile with an icon glyph
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Effects

Rectangle {
    id: root

    property string label: ""
    property int    value: 0
    property color  tone: theme.accent
    property string icon_

    height: 92
    radius: theme.rLg
    color: theme.card
    border.color: theme.border

    Row {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Rectangle {
            width: 40
            height: 40
            radius: theme.rMd
            color: Qt.alpha(root.tone, 0.12)
            anchors.verticalCenter: parent.verticalCenter

            Image {
                id: iconGlyph
                source: root.icon_
                width: 20; height: 20
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                mipmap: true
                visible: false
            }

            MultiEffect {
                anchors.centerIn: parent
                width: iconGlyph.width
                height: iconGlyph.height
                source: iconGlyph
                colorization: 1
                colorizationColor: root.tone
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                text: root.value
                font { pixelSize: 26; bold: true; family: theme.fontFamily }
                color: root.tone
            }
            Text {
                text: root.label
                font.pixelSize: 11
                font.family: theme.fontFamily
                color: theme.textMid
                font.letterSpacing: 0.2
            }
        }
    }
}
