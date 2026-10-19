// InfoRow.qml — key/value field pair used across inspector panels
// Copyright (C) 2026 Kishan G A

import QtQuick

Column {
    id: row

    required property string label
    required property string value

    width: parent ? parent.width - 28 : 200
    spacing: 3

    Text {
        text: row.label.toUpperCase()
        font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 0.7 }
        color: theme.textLow
    }

    Text {
        text: row.value.length > 0 ? row.value : "—"
        width: row.width
        font {
            pixelSize: 12
            family: row.value.length === 64 ? theme.fontMono : theme.fontFamily
        }
        color: theme.textHi
        elide: Text.ElideRight
        wrapMode: row.value.length > 40 ? Text.WrapAnywhere : Text.NoWrap
        maximumLineCount: 3
    }
}
