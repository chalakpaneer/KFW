// KFWButton.qml — text button with coordinated visual variants
// Copyright (C) 2026 Kishan G A

import QtQuick

Rectangle {
    id: root

    property string label: ""
    property string icon: ""
    property bool   accent: false     // primary action
    property bool   danger: false
    property bool   outline: false    // bordered subtle action
    property bool   enabled_: true

    signal clicked()

    implicitHeight: theme.btnH
    radius: theme.rMd
    enabled: enabled_
    opacity: enabled_ ? 1 : 0.45

    color: {
        if (accent)
            return pressedMO.pressed ? theme.accentDeep
                 : containsMO ? theme.accentBright : theme.accent
        if (danger)
            return pressedMO.pressed ? theme.badBg
                 : containsMO ? theme.badBg : "transparent"
        if (outline)
            return pressedMO.pressed ? theme.cardHover
                 : containsMO ? theme.cardHover : theme.card
        return pressedMO.pressed ? theme.cardHover
             : containsMO ? theme.cardHover : "transparent"
    }
    border.width: accent ? 0 : 1
    border.color: danger ? theme.bad : theme.borderMid

    Behavior on color { ColorAnimation { duration: theme.animFast } }

    implicitWidth: Math.max(96, contentRow.implicitWidth + (accent ? 28 : 24))

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 7

        Item {
            visible: root.icon.length > 0 && root.label.length > 0
            width: 15
            height: 1
        }

        Text {
            text: root.label
            font.pixelSize: 13
            font.family: theme.fontFamily
            font.bold: accent
            color: root.accent ? theme.accentText
                 : root.danger ? theme.bad
                 : containsMO ? theme.textHi : theme.textMid
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Leading glyph (optional textual mark such as “+ N”). Icons live in
    // KFWIconButton; this keeps the text button dependency-free.
    Text {
        visible: root.icon.length > 0 && root.label.length === 0
        text: root.icon
        anchors.centerIn: parent
        color: root.accent ? theme.accentText : theme.textMid
        font.pixelSize: 13
    }

    HoverHandler { id: containsMO }
    MouseArea {
        id: pressedMO
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (root.enabled_) root.clicked()
    }
}
