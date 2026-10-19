// KFWAvatarBadge.qml — initials badge shown beside the examiner name
// Copyright (C) 2026 Kishan G A

import QtQuick

Rectangle {
    id: root

    property string name: ""

    width: 30
    height: 30
    radius: height / 2
    color: theme.accentSoft
    border.width: 1
    border.color: theme.accentBorder

    Text {
        anchors.centerIn: parent
        text: {
            const parts = root.name.trim().split(/\s+/)
            let s = ""
            for (let i = 0; i < parts.length && s.length < 2; ++i)
                if (parts[i].length > 0)
                    s += parts[i][0]
            return s.toUpperCase()
        }
        font { pixelSize: 11; bold: true; family: theme.fontFamily }
        color: theme.accent
    }
}
