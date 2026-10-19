// KFWStatusChip.qml — colored pill for statuses (verify state, case state)
// Copyright (C) 2026 Kishan G A

import QtQuick

Rectangle {
    id: root

    property string status: ""
    property bool compact: false

    width: label.implicitWidth + (compact ? 14 : 20)
    height: compact ? 17 : 22
    radius: height / 2
    color: theme.statusBg(status)
    border.width: 1
    border.color: {
        const c = theme.statusColor(status)
        return c === theme.neutral ? theme.borderMid : c
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.status.length > 0 ? root.status : "Not checked"
        font.pixelSize: compact ? 8 : 10
        font.family: theme.fontFamily
        font.bold: true
        font.letterSpacing: 0.3
        color: theme.statusColor(root.status)
    }
}
