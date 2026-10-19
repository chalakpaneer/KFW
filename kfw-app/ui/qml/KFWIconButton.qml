// KFWIconButton.qml — compact icon-only action button
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Rectangle {
    id: root

    property string icon: ""
    property string tip: ""
    property bool accent: false
    property bool danger: false
    property bool solid: false
    property int iconSize: 18

    readonly property bool hoveredItem: hover.hovered

    signal clicked()

    width: 34
    height: 34
    radius: theme.rMd
    color: {
        if (solid && !hoveredItem)
            return theme.card
        if (solid && hoveredItem)
            return theme.cardHover
        if (danger && hoveredItem)
            return theme.badBg
        if (accent && hoveredItem)
            return theme.accentSoft
        if (hoveredItem)
            return theme.cardHover
        return "transparent"
    }
    border.width: solid || accent || danger ? 1 : 0
    border.color: danger ? theme.bad : accent ? theme.accentBorder : theme.borderMid

    Image {
        id: glyph
        source: root.icon
        width: root.iconSize
        height: root.iconSize
        anchors.centerIn: parent
        visible: false
        mipmap: true
    }

    MultiEffect {
        anchors.centerIn: parent
        width: glyph.width
        height: glyph.height
        source: glyph
        colorization: 1
        colorizationColor: root.danger  ? theme.bad
                         : root.accent ? theme.accent
                         : hover.hovered ? theme.textHi
                         : theme.textMid
    }

    HoverHandler { id: hover }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ToolTip {
        visible: hover.hovered && root.tip.length > 0
        text: root.tip
        delay: 350
    }
}
