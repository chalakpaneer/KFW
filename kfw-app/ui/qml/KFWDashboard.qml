// KFWDashboard.qml — Investigation overview: cases, integrity status, actions
// Copyright (C) 2026 Kishan G A
// KFW is an independent project and is not affiliated with or endorsed by VideoLAN.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

Rectangle {
    id: root
    color: theme.bg

    signal openCaseRequested(string caseId)
    signal newCaseRequested()

    // ── Content frame ─────────────────────────────────────────────────────
    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.height + contentCol.y * 2
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: theme.border
            }
        }

        Column {
            id: contentCol
            width: Math.max(scroll.width - 96, 720)
            x: 48
            y: 36
            bottomPadding: 28
            spacing: 26

            // ── Page header ───────────────────────────────────────────────
            RowLayout {
                width: parent.width

                Column {
                    spacing: 5
                    Text {
                        text: "Investigation Dashboard"
                        font { pixelSize: 22; bold: true; family: theme.fontFamily }
                        color: theme.textHi
                    }
                    Row {
                        spacing: 10
                        Text {
                            text: Qt.formatDateTime(new Date(),
                                    "dddd, d MMMM yyyy  ·  hh:mm AP")
                            font.pixelSize: 12
                            font.family: theme.fontFamily
                            color: theme.textLow
                        }
                        Rectangle {
                            width: 1; height: 12
                            color: theme.borderMid
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Row {
                            spacing: 6
                            height: 16
                            Rectangle {
                                width: 8; height: 8
                                radius: 4
                                color: kfwShell.auditChainOk ? theme.ok : theme.bad
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: kfwShell.auditChainOk
                                      ? "Audit chain intact" : "Audit chain BROKEN"
                                font { pixelSize: 11; family: theme.fontFamily }
                                color: kfwShell.auditChainOk ? theme.textMid : theme.bad
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true; height: 1 }

                KFWButton {
                    label: "New Case"
                    accent: true
                    onClicked: root.newCaseRequested()
                }
            }

            // ── Stat cards ─────────────────────────────────────────────────
            Row {
                width: parent.width
                spacing: 14

                KFWStatCard {
                    width: (parent.width - 42) / 4
                    label: "Total Cases"
                    value: kfwCaseModel.totalCases
                    tone: theme.accent
                    icon_: "icons/folder.svg"
                }
                KFWStatCard {
                    width: (parent.width - 42) / 4
                    label: "Open Cases"
                    value: kfwCaseModel.openCases
                    tone: theme.ok
                    icon_: "icons/examine.svg"
                }
                KFWStatCard {
                    width: (parent.width - 42) / 4
                    label: "Pending Verify"
                    value: kfwCaseModel.pendingTotal
                    tone: theme.warn
                    icon_: "icons/shield.svg"
                }
                KFWStatCard {
                    width: (parent.width - 42) / 4
                    label: "Evidence Items"
                    value: kfwCaseModel.evidenceTotal
                    tone: theme.info
                    icon_: "icons/trailer.svg"
                }
            }

            // ── Pending verification alert ────────────────────────────────
            Rectangle {
                width: parent.width
                height: 62
                radius: theme.rMd
                color: theme.warnBg
                border.width: 1
                border.color: Qt.alpha(theme.warn, 0.4)
                visible: kfwCaseModel.pendingTotal > 0

                Row {
                    anchors {
                        left: parent.left; leftMargin: 18
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: 14

                    Image {
                        id: alertIcon
                        source: "icons/warning.svg"
                        width: 22; height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        fillMode: Image.PreserveAspectFit
                        mipmap: true
                        visible: false
                    }
                    MultiEffect {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22
                        source: alertIcon
                        colorization: 1
                        colorizationColor: theme.warn
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: kfwCaseModel.pendingTotal
                                  + " evidence item(s) awaiting SHA-256 verification"
                            font { pixelSize: 13; bold: true; family: theme.fontFamily }
                            color: theme.warn
                        }
                        Text {
                            text: "Open each item and run integrity verification before relying on it."
                            font { pixelSize: 11; family: theme.fontFamily }
                            color: theme.textMid
                        }
                    }
                }
            }

            // ── Case list ───────────────────────────────────────────────────
            Column {
                width: parent.width
                spacing: 0

                Row {
                    width: parent.width
                    spacing: 12

                    Text {
                        text: "Investigation Cases"
                        font { pixelSize: 15; bold: true; family: theme.fontFamily }
                        color: theme.textHi
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: kfwCaseModel.totalCases + " total"
                        font.pixelSize: 11
                        font.family: theme.fontFamily
                        color: theme.textLow
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Rectangle {
                        color: theme.inset
                        radius: theme.rSm
                        border.width: 1
                        border.color: searchBox.activeFocus ? theme.accent : theme.border
                        width: 220
                        height: 30
                        anchors.verticalCenter: parent.verticalCenter

                        TextInput {
                            id: searchBox
                            anchors {
                                fill: parent
                                leftMargin: 10
                                rightMargin: 10
                            }
                            font.pixelSize: 12
                            font.family: theme.fontFamily
                            color: theme.textHi
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter

                            Text {
                                visible: searchBox.text.length === 0
                                         && !searchBox.activeFocus
                                text: "Filter by ID, title, examiner…"
                                font.pixelSize: 12
                                font.family: theme.fontFamily
                                color: theme.textLow
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                Item { width: 1; height: 12 }

                // Column headers
                Rectangle {
                    width: parent.width
                    height: 34
                    color: theme.panel
                    border.width: 1
                    border.color: theme.border
                    radius: theme.rSm

                    Row {
                        anchors.fill: parent
                        leftPadding: 16

                        Repeater {
                            model: [
                                { t: "CASE ID",        w: 130 },
                                { t: "TITLE",          w: 0   },
                                { t: "EXAMINER",       w: 130 },
                                { t: "STATUS",         w: 130 },
                                { t: "LAST MODIFIED",  w: 150 },
                                { t: "EVIDENCE",       w: 90  },
                                { t: "PENDING",        w: 90  },
                            ]
                            delegate: Item {
                                required property var modelData
                                width: modelData.w > 0
                                       ? modelData.w
                                       : caseList.width - 752
                                height: 34

                                Text {
                                    text: parent.width > 0 ? parent.modelData.t : ""
                                    font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 0.9 }
                                    color: theme.textLow
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }

                // Rows
                ListView {
                    id: caseList
                    width: parent.width
                    height: contentHeight
                    interactive: false
                    clip: true
                    spacing: 2

                    model: kfwCaseModel

                    delegate: CaseRow {
                        width: caseList.width - 4
                        matches: searchBox.text.length === 0
                                 || caseId.toLowerCase().indexOf(searchBox.text.toLowerCase()) >= 0
                                 || title.toLowerCase().indexOf(searchBox.text.toLowerCase()) >= 0
                                 || examiner.toLowerCase().indexOf(searchBox.text.toLowerCase()) >= 0
                                 || status.toLowerCase().indexOf(searchBox.text.toLowerCase()) >= 0

                        onOpenCase: function(cid) { root.openCaseRequested(cid) }
                        onStatusChange: function(cid, st) {
                            kfwShell.setCaseStatus(cid, st)
                        }
                    }
                }

                // Empty state
                Column {
                    width: parent.width
                    visible: kfwCaseModel.count === 0
                    spacing: 14

                    Rectangle {
                        width: parent.width
                        height: 210
                        radius: theme.rLg
                        color: theme.panel
                        border.width: 1
                        border.color: theme.border

                        Column {
                            anchors.centerIn: parent
                            spacing: 12

                            Rectangle {
                                width: 56; height: 56; radius: 16
                                color: theme.accentSoft
                                anchors.horizontalCenter: parent.horizontalCenter
                                Image {
                                    id: folderGlyph
                                    source: "icons/folder.svg"
                                    width: 26; height: 26
                                    anchors.centerIn: parent
                                    fillMode: Image.PreserveAspectFit
                                    mipmap: true
                                    visible: false
                                }
                                MultiEffect {
                                    anchors.centerIn: parent
                                    width: 26; height: 26
                                    source: folderGlyph
                                    colorization: 1
                                    colorizationColor: theme.accent
                                }
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "No investigation cases yet"
                                font { pixelSize: 14; bold: true; family: theme.fontFamily }
                                color: theme.textMid
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Create your first case to start importing and examining evidence."
                                font { pixelSize: 12; family: theme.fontFamily }
                                color: theme.textLow
                            }

                            KFWButton {
                                id: emptyNew
                                label: "New Case"
                                icon: "+"
                                accent: true
                                anchors.horizontalCenter: parent.horizontalCenter
                                onClicked: root.newCaseRequested()
                            }
                        }
                    }
                }
            }

            // ── Footer ────────────────────────────────────────────────────
            Text {
                text: "Kishan Forensic Workbench v" + kfwShell.appVersion
                      + "   ·   engine: " + kfwShell.engineVersion
                      + "   ·   Inspect. Correlate. Verify. Report."
                font { pixelSize: 10; family: theme.fontFamily }
                color: theme.textLow
            }
        }
    }
}