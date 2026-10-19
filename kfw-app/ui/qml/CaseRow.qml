// CaseRow.qml — one investigation case row in the dashboard list
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Rectangle {
    id: row

    // Model roles (bound by the ListView)
    required property string caseId
    required property string title
    required property string examiner
    required property string status
    required property string created
    required property string modified
    required property int    evidenceCount
    required property int    pendingVerify
    required property string notes

    // Filtering support from the dashboard search box
    property bool matches: true

    signal openCase(string caseId)
    signal statusChange(string caseId, string status)

    height: matches ? 52 : 0
    visible: matches
    width: parent ? parent.width : 400
    radius: theme.rMd
    color: hoverHover.hovered ? theme.cardHover : theme.card
    border.width: 1
    border.color: hoverHover.hovered ? theme.borderMid : theme.border

    Behavior on color { ColorAnimation { duration: theme.animFast } }

    Rectangle {
        width: 3
        height: 22
        radius: 1.5
        anchors {
            left: parent.left
            leftMargin: 0
            verticalCenter: parent.verticalCenter
        }
        color: theme.statusColor(row.status)
        visible: hoverHover.hovered
    }

    Row {
        anchors.fill: parent
        leftPadding: 16

        // Case ID
        Item {
            width: 130
            height: parent.height
            Text {
                text: row.caseId
                font { pixelSize: 12; family: theme.fontMono }
                color: theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Title
        Item {
            width: row.width - 130 - 130 - 130 - 150 - 90 - 90
            height: parent.height
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: row.title
                    elide: Text.ElideRight
                    width: parent.width - 16
                    font { pixelSize: 13; family: theme.fontFamily }
                    color: theme.textHi
                }
                Text {
                    visible: row.notes.length > 0
                    text: row.notes
                    elide: Text.ElideRight
                    width: parent.width - 16
                    font { pixelSize: 10; family: theme.fontFamily }
                    color: theme.textLow
                }
            }
        }

        // Examiner
        Item {
            width: 130
            height: parent.height
            Text {
                text: row.examiner
                elide: Text.ElideRight
                width: parent.width - 8
                font { pixelSize: 11; family: theme.fontFamily }
                color: theme.textMid
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Status chip
        Item {
            width: 130
            height: parent.height
            KFWStatusChip {
                status: row.status
                anchors.verticalCenter: parent.verticalCenter
                compact: true
            }
        }

        // Last modified
        Item {
            width: 150
            height: parent.height
            Text {
                text: row.modified
                font { pixelSize: 11; family: theme.fontFamily }
                color: theme.textMid
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Evidence count
        Item {
            width: 90
            height: parent.height
            Text {
                text: row.evidenceCount
                font { pixelSize: 13; bold: true; family: theme.fontFamily }
                color: theme.textHi
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Pending verify
        Item {
            width: 90
            height: parent.height
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Image {
                    id: pendingGlyph
                    source: "icons/warning.svg"
                    width: 14; height: 14
                    visible: false
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                MultiEffect {
                    width: 14; height: 14
                    source: pendingGlyph
                    colorization: 1
                    colorizationColor: theme.warn
                    visible: row.pendingVerify > 0
                }
                Text {
                    text: row.pendingVerify > 0 ? row.pendingVerify : "—"
                    font { pixelSize: 12; bold: row.pendingVerify > 0; family: theme.fontFamily }
                    color: row.pendingVerify > 0 ? theme.warn : theme.textLow
                }
            }
        }
    }

    // Right-edge quick actions on hover
    Row {
        visible: hoverHover.hovered && row.matches
        spacing: 6
        anchors {
            right: parent.right
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
        z: 5

        KFWIconButton {
            icon: row.status === "Closed" ? "icons/ok.svg" : "icons/close.svg"
            tip: row.status === "Closed" ? "Reopen case" : "Close case"
            onClicked: row.statusChange(
                row.caseId,
                row.status === "Closed" ? "Open" : "Closed")
        }
        KFWIconButton {
            icon: "icons/chevron.svg"
            tip: "Open case"
            onClicked: row.openCase(row.caseId)
        }
    }

    HoverHandler { id: hoverHover }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: row.openCase(row.caseId)
    }
}
