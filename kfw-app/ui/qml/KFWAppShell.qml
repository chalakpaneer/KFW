// KFWAppShell.qml — application window: sidebar navigation + screen stack
// Copyright (C) 2026 Kishan G A
// KFW is an independent project and is not affiliated with or endorsed by VideoLAN.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

Window {
    id: appWindow
    title: "Kishan Forensic Workbench — KFW — Digital Media Evidence Examination"
    width: 1480
    height: 920
    minimumWidth: 1120
    minimumHeight: 720
    visible: true
    color: theme.bg

    // -- Screen navigation stack --------------------------------------------
    StackView {
        id: stack
        anchors {
            top: parent.top
            left: sidebarArea.right
            right: parent.right
            bottom: statusbar.top
        }
        initialItem: dashboardComponent

        pushEnter: Transition {
            PropertyAnimation { property: "x"; from: appWindow.width; to: 0; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation    { property: "opacity"; from: 0; to: 1; duration: 170 }
        }
        pushExit: Transition {
            PropertyAnimation { property: "x"; from: 0; to: -appWindow.width * 0.12; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation    { property: "opacity"; from: 1; to: 0; duration: 170 }
        }
        popEnter: Transition {
            PropertyAnimation { property: "x"; from: -appWindow.width * 0.12; to: 0; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation    { property: "opacity"; from: 0; to: 1; duration: 170 }
        }
        popExit: Transition {
            PropertyAnimation { property: "x"; from: 0; to: appWindow.width; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation    { property: "opacity"; from: 1; to: 0; duration: 170 }
        }
    }

    Component {
        id: dashboardComponent
        KFWDashboard {
            onNewCaseRequested: newCaseDialog.open()
        }
    }

    Component {
        id: workspaceComponent
        KFWExaminationWorkspace {
        }
    }

    // -- Sidebar ------------------------------------------------------------
    Rectangle {
        id: sidebarArea
        width: 224
        height: parent.height
        color: theme.surface
        border.width: 1
        border.color: theme.border
        z: 20

        Column {
            id: sidebarTop
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            spacing: 0

            // Brand block
            Rectangle {
                width: parent.width
                height: 64
                color: "transparent"
                Row {
                    anchors {
                        fill: parent
                        leftMargin: 16
                    }
                    spacing: 10

                    Image {
                        id: brandLogo
                        source: "icons/logo.svg"
                        width: 30; height: 30
                        anchors.verticalCenter: parent.verticalCenter
                        fillMode: Image.PreserveAspectFit
                        mipmap: true
                        visible: false
                    }
                    MultiEffect {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30
                        source: brandLogo
                        colorization: 1
                        colorizationColor: theme.textHi
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: "Forensic Workbench"
                            font { pixelSize: 13; bold: true; family: theme.fontFamily }
                            color: theme.textHi
                        }
                        Text {
                            text: "KFW · Evidence Examination"
                            font { pixelSize: 9; family: theme.fontFamily; letterSpacing: 0.6 }
                            color: theme.textLow
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: theme.border }

            Item { width: 1; height: 18 }

            // Navigation items
            Column {
                width: parent.width - 12
                x: 6
                spacing: 4

                Rectangle {
                    id: navDash
                    width: parent.width
                    height: 40
                    radius: theme.rMd
                    color: activeDashboard() ? theme.accentSoft : (navHoverD.hovered ? theme.card : "transparent")
                    border.width: activeDashboard() ? 1 : 0
                    border.color: theme.border
                    Behavior on color { ColorAnimation { duration: theme.animFast } }

                    function activeDashboard() {
                        return kfwShell.currentScreen === "Dashboard"
                    }

                    Row {
                        anchors.fill: parent
                        leftPadding: 12
                        spacing: 10

                        Image {
                            id: navDashIcon
                            source: "icons/dashboard.svg"
                            width: 17; height: 17
                            fillMode: Image.PreserveAspectFit
                            mipmap: true
                            anchors.verticalCenter: parent.verticalCenter
                            visible: false
                        }
                        MultiEffect {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 17; height: 17
                            source: navDashIcon
                            colorization: 1
                            colorizationColor: navDash.activeDashboard()
                                               ? theme.accent : theme.textMid
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Dashboard"
                            font {
                                pixelSize: 12
                                bold: navDash.activeDashboard()
                                family: theme.fontFamily
                            }
                            color: navDash.activeDashboard() ? theme.accent
                                  : navHoverD.hovered ? theme.textHi : theme.textMid
                        }
                    }

                    HoverHandler { id: navHoverD }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: kfwShell.showDashboard()
                    }
                }

                Rectangle {
                    id: navWorkspace
                    width: parent.width
                    height: 40
                    radius: theme.rMd
                    color: activeWorkspace() ? theme.accentSoft : (navHoverW.hovered ? theme.card : "transparent")
                    border.width: activeWorkspace() ? 1 : 0
                    border.color: theme.border
                    Behavior on color { ColorAnimation { duration: theme.animFast } }

                    function activeWorkspace() {
                        return kfwShell.currentScreen === "Workspace"
                    }

                    Row {
                        anchors.fill: parent
                        leftPadding: 12
                        spacing: 10

                        Image {
                            id: navWsIcon
                            source: "icons/trailer.svg"
                            width: 17; height: 17
                            fillMode: Image.PreserveAspectFit
                            mipmap: true
                            anchors.verticalCenter: parent.verticalCenter
                            visible: false
                        }
                        MultiEffect {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 17; height: 17
                            source: navWsIcon
                            colorization: 1
                            colorizationColor: navWorkspace.activeWorkspace()
                                               ? theme.accent : theme.textMid
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Examination"
                            font {
                                pixelSize: 12
                                bold: navWorkspace.activeWorkspace()
                                family: theme.fontFamily
                            }
                            color: navWorkspace.activeWorkspace() ? theme.accent
                                  : navHoverW.hovered ? theme.textHi : theme.textMid
                        }
                    }

                    HoverHandler { id: navHoverW }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (kfwShell.activeCaseId.length > 0)
                                kfwShell.openCase(kfwShell.activeCaseId)
                            else
                                toast.show("Open a case from the dashboard first", 1)
                        }
                    }
                }

                // Reports hint (v0.2 placeholder)
                Rectangle {
                    width: parent.width
                    height: 40
                    radius: theme.rMd
                    color: "transparent"
                    opacity: 0.45

                    Row {
                        anchors.fill: parent
                        leftPadding: 12
                        spacing: 10

                        Image {
                            id: navReportIcon
                            source: "icons/report.svg"
                            width: 17; height: 17
                            fillMode: Image.PreserveAspectFit
                            mipmap: true
                            anchors.verticalCenter: parent.verticalCenter
                            visible: false
                        }
                        MultiEffect {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 17; height: 17
                            source: navReportIcon
                            colorization: 1
                            colorizationColor: theme.textLow
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Reports            v0.2"
                            font { pixelSize: 12; family: theme.fontFamily }
                            color: theme.textLow
                        }
                    }

                    ToolTip.visible: navHoverR.hovered
                    ToolTip.text: "Report generation lives in the workstation toolbar"
                    ToolTip.delay: 350
                    HoverHandler { id: navHoverR }
                }
            }
        }

        // Bottom block: examiner card + about button, pinned to sidebar bottom
        Column {
            id: sidebarBottom
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: parent.right
            }
            spacing: 10

            // Examiner card
            Rectangle {
                width: parent.width - 16
                height: 58
                x: 8
                radius: theme.rLg
                color: theme.card
                border.width: 1
                border.color: theme.border

                Row {
                    anchors {
                        fill: parent
                        leftMargin: 10
                    }
                    spacing: 9

                    KFWAvatarBadge {
                        name: kfwShell.examinerName
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            text: kfwShell.examinerName
                            font { pixelSize: 11; bold: true; family: theme.fontFamily }
                            color: theme.textHi
                            width: 150
                            elide: Text.ElideRight
                        }
                        Text {
                            text: "Lead Examiner"
                            font { pixelSize: 9; family: theme.fontFamily }
                            color: theme.textLow
                        }
                    }
                }
            }

            Item { width: 1; height: 10 }

            // Theme switch — Light / Dark (persisted per device)
            Rectangle {
                width: parent.width - 16
                height: 34
                x: 8
                radius: theme.rMd
                color: theme.inset
                border.width: 1
                border.color: theme.border

                Row {
                    anchors {
                        fill: parent
                        topMargin: 3
                        bottomMargin: 3
                        leftMargin: 3
                        rightMargin: 3
                    }
                    spacing: 0

                    Rectangle {
                        width: parent.width / 2 - 2
                        height: parent.height
                        radius: theme.rSm
                        color: theme.mode === "light" ? theme.card : "transparent"
                        Behavior on color { ColorAnimation { duration: theme.animFast } }

                        Text {
                            anchors.centerIn: parent
                            text: "Light"
                            font {
                                pixelSize: 11
                                bold: theme.mode === "light"
                                family: theme.fontFamily
                            }
                            color: theme.mode === "light"
                                   ? theme.textHi : theme.textLow
                        }

                        HoverHandler { id: lightHov }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: theme.setMode("light")
                        }
                        ToolTip.visible: lightHov.hovered
                        ToolTip.text: "Professional light theme"
                        ToolTip.delay: 350
                    }

                    Rectangle {
                        width: parent.width / 2 - 2
                        height: parent.height
                        radius: theme.rSm
                        color: theme.mode === "dark" ? theme.card : "transparent"
                        Behavior on color { ColorAnimation { duration: theme.animFast } }

                        Text {
                            anchors.centerIn: parent
                            text: "Dark"
                            font {
                                pixelSize: 11
                                bold: theme.mode === "dark"
                                family: theme.fontFamily
                            }
                            color: theme.mode === "dark"
                                   ? theme.textHi : theme.textLow
                        }

                        HoverHandler { id: darkHov }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: theme.setMode("dark")
                        }
                        ToolTip.visible: darkHov.hovered
                        ToolTip.text: "Professional dark theme"
                        ToolTip.delay: 350
                    }
                }

                Text {
                    visible: false   // accessibility label (screen readers)
                    text: "Switch between the light and dark professional theme"
                }
            }

            Item { width: 1; height: 6 }

            // About button
            Rectangle {
                width: parent.width - 16
                height: 34
                x: 8
                radius: theme.rMd
                color: aboutHov.hovered ? theme.card : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Image {
                        id: aboutIcon
                        source: "icons/info.svg"
                        width: 15; height: 15
                        fillMode: Image.PreserveAspectFit
                        mipmap: true
                        anchors.verticalCenter: parent.verticalCenter
                        visible: false
                    }
                    MultiEffect {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15; height: 15
                        source: aboutIcon
                        colorization: 1
                        colorizationColor: aboutHov.hovered ? theme.textHi : theme.textLow
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "About KFW"
                        font { pixelSize: 11; family: theme.fontFamily }
                        color: aboutHov.hovered ? theme.textHi : theme.textLow
                    }
                }

                HoverHandler { id: aboutHov }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: aboutDialog.open()
                }
            }

            Item { width: 1; height: 14 }
        }
    }

    // -- Status bar -----------------------------------------------------------
    Rectangle {
        id: statusbar
        width: parent.width
        height: 26
        anchors.bottom: parent.bottom
        color: theme.surface
        border.width: 1
        border.color: theme.border
        z: 20

        Row {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 14

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Database: " + (kfwShell.databasePath.length > 56
                     ? "—" + kfwShell.databasePath.slice(-56) : kfwShell.databasePath)
                font { pixelSize: 9; family: theme.fontMono }
                color: theme.textLow
                elide: Text.ElideRight
                width: parent.width - 340
            }

            Item { height: 1; width: 1 }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (kfwShell.auditChainOk ? "Audit chain: OK" : "Audit chain: BROKEN")
                      + "   ·   " + kfwShell.engineVersion
                      + "   ·   v" + kfwShell.appVersion
                font { pixelSize: 9; family: theme.fontMono }
                color: kfwShell.auditChainOk ? theme.textLow : theme.bad
            }
        }
    }

    // -- Toast ----------------------------------------------------------------
    KFWToast { id: toast }

    Connections {
        target: kfwShell

        function onCurrentScreenChanged() {
            const scr = kfwShell.currentScreen
            if (scr === "Workspace" && stack.depth === 1)
                stack.push(workspaceComponent)
            else if (scr === "Dashboard" && stack.depth > 1)
                stack.pop()
        }

        function onNotice(msg)    { toast.show(msg, toast.infoT) }
        function onErrorOccurred(msg) { toast.show(msg, toast.errorT) }

        function onEvidenceImported(evId, status, sha) {
            toast.show("Evidence " + evId + " imported — SHA-256 " + status,
                       status === "Verified" ? toast.success : toast.warnT)
        }

        function onEvidenceVerified(evId, status) {
            toast.show(evId + " — SHA-256 " + status,
                       status === "Verified" ? toast.success
                       : status === "Mismatch" ? toast.errorT : toast.infoT)
        }

        function onFrameExported(evId, path, ts) {
            toast.show("Frame exported at " + ts + " ms", toast.success)
        }

        function onReportGenerated(jsonPath, htmlPath, sha) {
            toast.show("Report saved — opening the folder—", toast.success)
            kfwShell.revealPath(jsonPath)
        }
    }

    // -- New Case dialog -----------------------------------------------------
    Dialog {
        id: newCaseDialog
        modal: true
        anchors.centerIn: parent
        width: 470
        visible: false

        property bool submitted: false

        background: Rectangle {
            color: theme.panel
            radius: theme.rLg
            border.width: 1
            border.color: theme.border
        }

        header: Text {
            text: "New Investigation Case"
            font { pixelSize: 14; bold: true; family: theme.fontFamily }
            color: theme.textHi
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Column {
            spacing: 14
            topPadding: 8

            KFWField {
                id: caseTitleField
                labelText: "CASE TITLE"
                placeholder: "e.g. CCTV Footage Analysis — Market St Incident"
                hintMessage: text.trim().length === 0 && newCaseDialog.submitted
                             ? "Give the case a brief title." : ""
                isError: text.trim().length === 0 && newCaseDialog.submitted
            }

            KFWField {
                id: examinerField
                labelText: "EXAMINER"
                placeholder: "Lead examiner name"
                text: kfwShell.examinerName
            }

            Text {
                visible: caseTitleField.text.trim().length > 0
                text: "A case ID (KFW-YYYY-NNN) will be assigned automatically."
                font { pixelSize: 10; family: theme.fontFamily }
                color: theme.textLow
            }

            Row {
                spacing: 10
                anchors.right: parent.right

                KFWButton {
                    label: "Cancel"
                    outline: true
                    onClicked: {
                        newCaseDialog.submitted = false
                        caseTitleField.text = ""
                        newCaseDialog.close()
                    }
                }
                KFWButton {
                    label: "Create Case"
                    accent: true
                    onClicked: {
                        newCaseDialog.submitted = true
                        if (caseTitleField.text.trim().length === 0)
                            return
                        kfwShell.createCase(caseTitleField.text.trim(),
                                            examinerField.text.trim())
                        caseTitleField.text = ""
                        newCaseDialog.submitted = false
                        newCaseDialog.close()
                    }
                }
            }
        }

        onAboutToHide: newCaseDialog.submitted = false
    }

    // -- About dialog -------------------------------------------------------
    Dialog {
        id: aboutDialog
        modal: true
        anchors.centerIn: parent
        width: 500

        background: Rectangle {
            color: theme.panel
            radius: theme.rLg
            border.width: 1
            border.color: theme.border
        }

        header: Text {
            text: "About Kishan Forensic Workbench"
            font { pixelSize: 14; bold: true; family: theme.fontFamily }
            color: theme.textHi
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Column {
            spacing: 10
            topPadding: 8

            Row {
                spacing: 12
                Image {
                    id: aboutLogo
                    source: "icons/logo.svg"
                    width: 38; height: 38
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    visible: false
                }
                MultiEffect {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38; height: 38
                    source: aboutLogo
                    colorization: 1
                    colorizationColor: theme.textHi
                }
                Column {
                    spacing: 2
                    Text {
                        text: "Kishan Forensic Workbench"
                        font { pixelSize: 14; bold: true; family: theme.fontFamily }
                        color: theme.textHi
                    }
                    Text {
                        text: "v" + kfwShell.appVersion
                              + "  —  engine " + kfwShell.engineVersion
                        font { pixelSize: 10; family: theme.fontMono }
                        color: theme.textLow
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font { pixelSize: 11; family: theme.fontFamily }
                color: theme.textMid
                text: "Independent, open-source examination workbench for digital media evidence — case management, SHA-256 integrity verification, forensic review, timeline analysis, and investigation reporting."
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font { pixelSize: 10; family: theme.fontFamily }
                color: theme.textLow
                text: "Created by Kishan G A · Creator & Lead Developer.\n\nKFW is an independent project and is not affiliated with or endorsed by VideoLAN. The VLC engine and third-party components remain subject to their own licenses and notices."

            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font { pixelSize: 10; family: theme.fontFamily }
                color: theme.warn
                text: "Disclaimer: KFW is a developing forensic tool. Findings must be independently validated before use in formal investigations."
            }

            Row {
                anchors.right: parent.right
                spacing: 10

                KFWButton {
                    label: "License"
                    outline: true
                    onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/gpl-2.0.html")
                }
                KFWButton {
                    label: "Close"
                    accent: true
                    onClicked: aboutDialog.close()
                }
            }
        }
    }
}