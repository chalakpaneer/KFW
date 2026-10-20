// KFWExaminationWorkspace.qml — Screen B: forensic video examination
// Copyright (C) 2026 Kishan G A
// KFW is an independent project and is not affiliated with or endorsed by VideoLAN.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import KFW.Video

Rectangle {
    id: root
    color: theme.bg

    // ── Playback state ────────────────────────────────────────────────────
    readonly property bool hasEvidence: kfwShell.activeEvidenceId.length > 0
    readonly property bool hasMedia:   kfwShell.activePlayerUrl.toString().length > 0
    readonly property int  mediaDuration: kfwShell.playerDurationMs > 0
        ? kfwShell.playerDurationMs
        : (kfwShell.activeMediaInfo.available
           ? Math.max(0, kfwShell.activeMediaInfo.durationMs || 0) : 0)

    focus: true

    // ── Marker dialog state ───────────────────────────────────────────────
    property bool markerPending: false

    function formatMs(ms) {
        const total = Math.max(0, ms)
        const h = Math.floor(total / 3600000)
        const m = Math.floor((total % 3600000) / 60000)
        const s = Math.floor((total % 60000) / 1000)
        const f = Math.floor(total % 1000)
        return String(h).padStart(2, "0") + ":" +
               String(m).padStart(2, "0") + ":" +
               String(s).padStart(2, "0") + "." +
               String(f).padStart(3, "0")
    }

    function togglePlay() {
        if (!hasMedia) return
        kfwShell.playToggle()
    }

    function stepMs(delta) {
        if (!hasMedia) return
        kfwShell.playerStepMs(delta)
    }

    function openMarkerDialog() {
        if (!hasEvidence) return
        markerDialog.noteAt = kfwShell.playerPositionMs
        markerDialog.labelInput = "Marker"
        markerDialog.open()
        markerInput.forceActiveFocus()
    }

    Keys.onPressed: function(event) {
        const isText = activeFocusItem
                       && (activeFocusItem.text !== undefined
                           || activeFocusItem instanceof TextArea)
        if (isText)
            return
        if (event.key === Qt.Key_Space)
            togglePlay()
        else if (event.key === Qt.Key_Left)
            stepMs(event.modifiers & Qt.ControlModifier ? -10000 : -40)
        else if (event.key === Qt.Key_Right)
            stepMs(event.modifiers & Qt.ControlModifier ? 10000 : 40)
        else if (event.key === Qt.Key_Home)
            stepMs(-kfwShell.playerPositionMs)
        else if (event.key === Qt.Key_End)
            kfwShell.playerSeekMs(
                kfwShell.playerDurationMs > 0
                ? kfwShell.playerDurationMs : mediaDuration)
        else if (event.key === Qt.Key_M)
            openMarkerDialog()
        else if (event.key === Qt.Key_F)
            kfwShell.exportFrame(kfwShell.activeEvidenceId,
                                 kfwShell.playerPositionMs)
        else if (event.key === Qt.Key_Escape)
            markerDialog.close()
        else
            return
        event.accepted = true
    }

    // ── Top bar ─────────────────────────────────────────────────────────
    Rectangle {
        id: workspaceTopBar
        width: parent.width
        height: 50
        color: theme.surface
        border.width: 1
        border.color: theme.border
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 16
            spacing: 10

            KFWIconButton {
                icon: "icons/back.svg"
                tip: "Back to dashboard"
                onClicked: {
                    kfwShell.playerPause()
                    kfwShell.showDashboard()
                }
            }

            Text {
                text: kfwShell.activeCaseId.length > 0
                      ? kfwShell.activeCaseId : "No case"
                font { pixelSize: 12; family: theme.fontMono }
                color: theme.textLow
            }

            Image {
                id: topChevron
                source: "icons/chevron.svg"
                width: 12; height: 12
                fillMode: Image.PreserveAspectFit
                mipmap: true
                visible: false
            }
            MultiEffect {
                width: 12; height: 12
                source: topChevron
                colorization: 1
                colorizationColor: theme.textLow
            }

            Text {
                text: hasEvidence
                      ? kfwShell.activeEvidenceName
                      : (kfwShell.activeCaseTitle.length > 0
                         ? kfwShell.activeCaseTitle : "Examination workspace")
                font { pixelSize: 13; bold: true; family: theme.fontFamily }
                color: theme.textHi
                elide: Text.ElideMiddle
                Layout.maximumWidth: 360
            }

            KFWStatusChip {
                visible: hasEvidence
                status: kfwShell.activeEvidenceStatus
                compact: true
            }

            Item { Layout.fillWidth: true }

            // Live analysis: auto-recorded indicator counter
            Rectangle {
                visible: kfwShell.liveIndicatorTotal > 0
                width: liveRow.implicitWidth + 22
                height: 30
                radius: theme.rSm
                color: theme.card
                border.width: 1
                border.color: theme.accentBorder

                Row {
                    id: liveRow
                    anchors.centerIn: parent
                    spacing: 7

                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.kindColor["Flicker"]
                               !== undefined ? theme.kindColor["Flicker"]
                                             : theme.accent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Indicators " + kfwShell.liveIndicatorTotal
                        font { pixelSize: 11; bold: true
                               family: theme.fontMono }
                        color: theme.textMid
                    }
                }
                ToolTip.visible: liveHov.hovered
                ToolTip.delay: 300
                ToolTip.text: {
                    const c = kfwShell.liveIndicators
                    let parts = []
                    if (c["BlackFrame"]) parts.push("Black frames ×" + c["BlackFrame"])
                    if (c["Freeze"])     parts.push("Freezes ×" + c["Freeze"])
                    if (c["Flicker"])    parts.push("Luminance jumps ×" + c["Flicker"])
                    if (c["RapidCuts"])  parts.push("Cut clusters ×" + c["RapidCuts"])
                    if (parts.length === 0)
                        parts.push("Auto-recorded while playback runs")
                    return "Live analysis — " + parts.join("\n")
                }
                HoverHandler { id: liveHov }
            }

            KFWButton {
                label: "Verify SHA-256"
                outline: true
                enabled_: hasEvidence
                onClicked: kfwShell.reverifyEvidence(
                    kfwShell.activeEvidenceId)
            }

            KFWButton {
                icon: ""
                label: hasMedia
                       ? (kfwShell.frameExportBusy ? "Exporting…" : "Export Frame")
                       : "No media"
                outline: true
                enabled_: hasMedia && !kfwShell.frameExportBusy
                onClicked: kfwShell.exportFrame(kfwShell.activeEvidenceId,
                                                 kfwShell.playerPositionMs)
            }

            KFWButton {
                label: "Report"
                onClicked: reportDialog.open()
                enabled_: kfwShell.activeCaseId.length > 0
            }
        }
    }

    // ── Body: browser | viewport | inspector ────────────────────────────────
    RowLayout {
        anchors {
            top: workspaceTopBar.bottom
            left: parent.left
            right: parent.right
            bottom: timelinePanel.top
        }
        spacing: 0

        // ── Evidence browser ─────────────────────────────────────────────
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: theme.surface
            border.width: 1
            border.color: theme.border

            Column {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 36
                    color: "transparent"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 8
                        spacing: 8

                        Text {
                            text: "EVIDENCE"
                            anchors.verticalCenter: parent.verticalCenter
                            font {
                                pixelSize: 9; bold: true
                                family: theme.fontFamily
                                letterSpacing: 1.3
                            }
                            color: theme.textLow
                        }

                        Item { Layout.fillWidth: true; height: 1 }

                        KFWIconButton {
                            width: 24; height: 24
                            icon: "icons/plus.svg"
                            tip: "Import evidence file…"
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: importDialog.open()
                        }
                    }
                }

                ScrollView {
                    width: parent.width
                    height: parent.height - 36 - importRow.height
                    clip: true
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: theme.border
                        }
                    }

                    Column {
                        width: parent.width

                        // Evidence entries
                        Repeater {
                            model: kfwEvidenceModel

                            Rectangle {
                                id: evRow
                                required property string evId
                                required property string filename
                                required property string fileType
                                required property string sizeText
                                required property string verifyStatus
                                required property bool   hasMedia
                                required property string duplicateOf

                                readonly property bool active:
                                    evId === kfwShell.activeEvidenceId

                                width: parent.width
                                height: 64
                                color: active ? theme.cardHover
                                              : evHover.hovered ? theme.card
                                              : "transparent"

                                Rectangle {
                                    width: 3
                                    height: parent.height
                                    color: theme.accent
                                    visible: evRow.active
                                }

                                Column {
                                    anchors {
                                        fill: parent
                                        topMargin: 9
                                        bottomMargin: 9
                                    }
                                    spacing: 4

                                    Row {
                                        leftPadding: evRow.active ? 14 : 11
                                        spacing: 6
                                        Text {
                                            text: evRow.filename
                                            width: evRow.width - 70
                                            elide: Text.ElideMiddle
                                            font {
                                                pixelSize: 12
                                                bold: evRow.active
                                                family: theme.fontFamily
                                            }
                                            color: evRow.active ? theme.textHi
                                                  : theme.textMid
                                            anchors.verticalCenter:
                                                parent.verticalCenter
                                        }
                                    }

                                    Row {
                                        leftPadding: evRow.active ? 14 : 11
                                        spacing: 8

                                        Text {
                                            text: evRow.evId
                                            font { pixelSize: 10; family: theme.fontMono }
                                            color: theme.accent
                                        }
                                        Text {
                                            text: evRow.sizeText
                                            font { pixelSize: 10; family: theme.fontFamily }
                                            color: theme.textLow
                                        }
                                        KFWStatusChip {
                                            width: 62
                                            status: evRow.verifyStatus
                                            compact: true
                                        }
                                        Rectangle {
                                            visible: evRow.duplicateOf.length > 0
                                            height: 16
                                            width: dupText.implicitWidth + 12
                                            radius: 8
                                            color: "transparent"
                                            border.width: 1
                                            border.color: theme.warn
                                            anchors.verticalCenter: parent.verticalCenter
                                            Text {
                                                id: dupText
                                                anchors.centerIn: parent
                                                text: "DUP of " + evRow.duplicateOf
                                                font { pixelSize: 9; family: theme.fontMono }
                                                color: theme.textMid
                                            }
                                            ToolTip.visible: dupHover.hovered
                                            ToolTip.text: "Byte-identical to " + evRow.duplicateOf
                                                          + " (same SHA-256)"
                                            HoverHandler { id: dupHover }
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Qt.alpha(theme.border, 0.6)
                                    visible: !evRow.active
                                }

                                HoverHandler { id: evHover }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: kfwShell.openEvidence(
                                        kfwShell.activeCaseId, evRow.evId)
                                }
                            }
                        }

                        // Empty state
                        Column {
                            visible: kfwEvidenceModel.count === 0
                            width: parent.width
                            topPadding: 30
                            spacing: 10

                            Image {
                                id: emptyMediaIcon
                                source: "icons/trailer.svg"
                                width: 30; height: 30
                                anchors.horizontalCenter: parent.horizontalCenter
                                fillMode: Image.PreserveAspectFit
                                mipmap: true
                                visible: false
                            }
                            MultiEffect {
                                width: 30; height: 30
                                anchors.horizontalCenter: parent.horizontalCenter
                                source: emptyMediaIcon
                                colorization: 1
                                colorizationColor: theme.textLow
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "No evidence imported"
                                font { pixelSize: 12; family: theme.fontFamily }
                                color: theme.textLow
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: "Import the original media file;\nthe copy stays untouched in the store."
                                font { pixelSize: 10; family: theme.fontFamily }
                                color: theme.textLow
                            }
                        }
                    }
                }

                // Import action strip
                Rectangle {
                    id: importRow
                    width: parent.width
                    height: 46
                    color: "transparent"

                    Rectangle {
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                        }
                        height: 1
                        color: theme.border
                    }

                    KFWButton {
                        anchors.centerIn: parent
                        label: "Import Evidence…"
                        outline: true
                        onClicked: importDialog.open()
                    }
                }
            }
        }

        // ── Center: video viewport ──────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.viewport

            Column {
                anchors.fill: parent

                // Screen
                Item {
                    width: parent.width
                    height: parent.height - controlsBar.height

                    KFWVideoScreen {
                        id: videoScreen
                        anchors.fill: parent
                        visible: hasMedia

                        Component.onCompleted:
                            kfwShell.attachVideoScreen(videoScreen)
                        Component.onDestruction:
                            kfwShell.detachVideoScreen(videoScreen)

                        // "Buffering…" hint until the first VLC frame lands
                        Text {
                            anchors.centerIn: parent
                            visible: !kfwShell.playerHasVideo && hasMedia
                            text: "Starting playback engine…"
                            font { pixelSize: 13; family: theme.fontFamily }
                            color: theme.textLow
                        }
                    }

                    // Placeholder
                    Column {
                        anchors.centerIn: parent
                        spacing: 14
                        visible: !hasMedia

                        Rectangle {
                            width: 64; height: 64; radius: 18
                            color: theme.panel
                            anchors.horizontalCenter: parent.horizontalCenter
                            Image {
                                id: playGlyph
                                source: "icons/play.svg"
                                width: 26; height: 26
                                anchors.centerIn: parent
                                fillMode: Image.PreserveAspectFit
                                mipmap: true
                                visible: false
                            }
                            MultiEffect {
                                anchors.centerIn: parent
                                width: 26; height: 26
                                source: playGlyph
                                colorization: 1
                                colorizationColor: theme.textLow
                            }
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: hasEvidence
                                  ? "The stored copy is missing — re-import from the original"
                                  : "Select an evidence item to begin examination"
                            font { pixelSize: 14; family: theme.fontFamily }
                            color: theme.textLow
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 380
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: hasEvidence
                                  ? ""
                                  : "Evidence imported from the browser on the left\npreserves the original file untouched in its store."
                            font { pixelSize: 11; family: theme.fontFamily }
                            color: theme.textLow
                        }
                    }

                    // Timestamp overlay
                    Rectangle {
                        anchors {
                            bottom: parent.bottom
                            right: parent.right
                            margins: 12
                        }
                        width: tsLabel.implicitWidth + 20
                        height: 30
                        radius: theme.rSm
                        color: Qt.alpha("#000000", 0.62)
                        visible: hasMedia
                        Text {
                            id: tsLabel
                            anchors.centerIn: parent
                            text: root.formatMs(kfwShell.playerPositionMs)
                            font {
                                pixelSize: 13; bold: true
                                family: theme.fontMono
                            }
                            color: theme.accent
                        }
                    }

                    // Missing-file overlay
                    Item {
                        anchors.fill: parent
                        visible: hasEvidence && !hasMedia
                                 && kfwShell.activePlayerUrl.toString().length > 0
                    }
                }

                // ── Controls ─────────────────────────────────────────────
                Rectangle {
                    id: controlsBar
                    width: parent.width
                    height: 172
                    color: theme.bg
                    border.width: 1
                    border.color: theme.border
                    clip: true

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7

                        // Timeline scrubber + clock
                        Row {
                            width: parent.width
                            spacing: 12

                            Text {
                                text: root.formatMs(kfwShell.playerPositionMs)
                                width: 96
                                font { pixelSize: 11; family: theme.fontMono }
                                color: theme.textMid
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Item {
                                height: 26
                                width: parent.width
                                        - 96 - 96 - 24

                                Slider {
                                    id: seekSlider
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    from: 0
                                    to: Math.max(1, mediaDuration)
                                    value: kfwShell.playerPositionMs
                                    enabled: hasMedia && mediaDuration > 0

                                    background: Rectangle {
                                        x: seekSlider.leftPadding
                                        y: seekSlider.topPadding
                                           + seekSlider.availableHeight / 2 - height / 2
                                        width: seekSlider.availableWidth
                                        height: 5
                                        radius: 2.5
                                        color: theme.card
                                        border.width: 1
                                        border.color: theme.border
                                        Rectangle {
                                            width: seekSlider.visualPosition
                                                   * parent.width
                                            height: parent.height
                                            radius: parent.radius
                                            color: theme.accent
                                            visible: seekSlider.enabled
                                        }
                                    }
                                    handle: Rectangle {
                                        x: seekSlider.leftPadding
                                           + seekSlider.visualPosition
                                           * (seekSlider.availableWidth - width)
                                        y: seekSlider.topPadding
                                           + seekSlider.availableHeight / 2 - height / 2
                                        width: 15; height: 15; radius: 7.5
                                        color: theme.accent
                                        border.width: 2
                                        border.color: theme.bg
                                        visible: seekSlider.enabled
                                    }
                                    onMoved: {
                                        if (hasMedia)
                                            kfwShell.playerSeekMs(value)
                                    }
                                }
                            }

                            Text {
                                text: root.formatMs(mediaDuration)
                                width: 96
                                font { pixelSize: 11; family: theme.fontMono }
                                color: theme.textLow
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // Transport row (primary playback controls)
                        Row {
                            spacing: 8
                            anchors.horizontalCenter: parent.horizontalCenter

                            KFWIconButton {
                                icon: "icons/skipback.svg"
                                tip: "Jump to start (Home)"
                                onClicked: kfwShell.playerSeekMs(0)
                                solid: true
                            }
                            KFWIconButton {
                                icon: "icons/back10.svg"
                                tip: "Back 10 seconds (Ctrl+←)"
                                onClicked: root.stepMs(-10000)
                                solid: true
                            }
                            KFWIconButton {
                                icon: "icons/back1.svg"
                                tip: "Step back one frame (~40 ms, ←)"
                                onClicked: root.stepMs(-40)
                                solid: true
                            }
                            Rectangle {
                                width: 46; height: 34; radius: theme.rMd
                                color: playMouse.containsMouse
                                       ? theme.accentSoft : theme.card
                                border.width: 1
                                border.color: theme.accentBorder

                                Image {
                                    id: playIcon
                                    width: 17; height: 17
                                    anchors.centerIn: parent
                                    source: kfwShell.playerPlaying
                                            ? "icons/pause.svg" : "icons/play.svg"
                                    fillMode: Image.PreserveAspectFit
                                    mipmap: true
                                }
                                HoverHandler { id: playMouse }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.togglePlay()
                                }
                                ToolTip.visible: playMouse.hovered
                                ToolTip.text:
                                    kfwShell.playerPlaying
                                    ? "Pause (Space)" : "Play (Space)"
                                ToolTip.delay: 300
                            }
                            KFWIconButton {
                                icon: "icons/fwd1.svg"
                                tip: "Step forward one frame (~40 ms, →)"
                                onClicked: root.stepMs(40)
                                solid: true
                            }
                            KFWIconButton {
                                icon: "icons/fwd10.svg"
                                tip: "Forward 10 seconds (Ctrl+→)"
                                onClicked: root.stepMs(10000)
                                solid: true
                            }
                            KFWIconButton {
                                icon: "icons/skipfwd.svg"
                                tip: "Jump to end (End)"
                                onClicked: kfwShell.playerSeekMs(
                                    kfwShell.playerDurationMs > 0
                                    ? kfwShell.playerDurationMs
                                    : mediaDuration)
                                solid: true
                            }
                        }

                        // Secondary row (speed + annotations)
                        Row {
                            spacing: 8
                            anchors.horizontalCenter: parent.horizontalCenter

                            Rectangle {
                                width: 1; height: 26
                                color: theme.border
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            // Live playback rate (real-player speed control)
                            Row {
                                spacing: 6
                                anchors.verticalCenter: parent.verticalCenter

                                Repeater {
                                    model: [0.25, 0.5, 1, 2, 4]

                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property double rateVal: modelData
                                        readonly property bool active:
                                            Math.abs(kfwShell.playerRate
                                                     - rateVal) < 0.01

                                        width: 34; height: 26; radius: theme.rSm
                                        color: active ? theme.accentSoft
                                                      : theme.card
                                        border.width: 1
                                        border.color: active
                                                      ? theme.accent
                                                      : theme.border
                                        Text {
                                            anchors.centerIn: parent
                                            text: parent.rateVal < 1
                                                  ? ("" + parent.rateVal)
                                                  : ("" + parent.rateVal)
                                            font {
                                                pixelSize: 10
                                                bold: parent.active
                                                family: theme.fontMono
                                            }
                                            color: parent.active
                                                   ? theme.accent
                                                   : theme.textMid
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked:
                                                kfwShell.playerSetRate(
                                                    parent.rateVal)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: 1; height: 26
                                color: theme.border
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            KFWButton {
                                height: 34
                                label: "Add Marker"
                                outline: true
                                enabled_: hasEvidence
                                onClicked: root.openMarkerDialog()
                            }
                            KFWButton {
                                height: 34
                                label: "Save Notes"
                                outline: true
                                enabled_: hasEvidence
                                && notesArea.length > 0
                                onClicked: kfwShell.saveEvidenceNotes(
                                    kfwShell.activeEvidenceId,
                                    notesArea.text)
                            }
                        }

                        // Audio language / subtitles / volume
                        Row {
                            spacing: 8
                            anchors.horizontalCenter: parent.horizontalCenter
                            enabled: hasMedia

                            Text {
                                text: "AUDIO"
                                anchors.verticalCenter: parent.verticalCenter
                                font { pixelSize: 9; bold: true; letterSpacing: 0.7
                                       family: theme.fontFamily }
                                color: theme.textLow
                            }
                            KFWSelect {
                                width: 230
                                anchors.verticalCenter: parent.verticalCenter
                                model: kfwShell.audioTracks
                                currentId: kfwShell.audioTrackId
                                emptyText: "No audio track"
                                enabled: kfwShell.audioTracks.length > 1
                                onPicked: (id) => kfwShell.setAudioTrack(id)
                            }

                            Text {
                                text: "SUBTITLES"
                                anchors.verticalCenter: parent.verticalCenter
                                font { pixelSize: 9; bold: true; letterSpacing: 0.7
                                       family: theme.fontFamily }
                                color: theme.textLow
                            }
                            KFWSelect {
                                width: 170
                                anchors.verticalCenter: parent.verticalCenter
                                model: kfwShell.subtitleTracks
                                currentId: kfwShell.subtitleTrackId
                                emptyText: "None in file"
                                enabled: kfwShell.subtitleTracks.length > 1
                                onPicked: (id) => kfwShell.setSubtitleTrack(id)
                            }

                            Rectangle {
                                width: 1; height: 22
                                color: theme.border
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            KFWButton {
                                height: 28
                                label: kfwShell.muted ? "Unmute" : "Mute"
                                outline: true
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: kfwShell.toggleMute()
                            }
                            Slider {
                                id: volumeSlider
                                width: 110
                                anchors.verticalCenter: parent.verticalCenter
                                from: 0
                                to: 200
                                stepSize: 5
                                value: kfwShell.volume
                                opacity: kfwShell.muted ? 0.4 : 1.0
                                onMoved: kfwShell.setVolume(Math.round(value))

                                background: Rectangle {
                                    x: volumeSlider.leftPadding
                                    y: volumeSlider.topPadding
                                       + volumeSlider.availableHeight / 2 - height / 2
                                    width: volumeSlider.availableWidth
                                    height: 4
                                    radius: 2
                                    color: theme.card
                                    border.width: 1
                                    border.color: theme.border
                                    Rectangle {
                                        width: volumeSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: parent.radius
                                        color: theme.accent
                                    }
                                }
                                handle: Rectangle {
                                    x: volumeSlider.leftPadding
                                       + volumeSlider.visualPosition
                                       * (volumeSlider.availableWidth - width)
                                    y: volumeSlider.topPadding
                                       + volumeSlider.availableHeight / 2 - height / 2
                                    width: 12; height: 12; radius: 6
                                    color: theme.accent
                                    border.width: 2
                                    border.color: theme.bg
                                }
                            }
                            Text {
                                text: kfwShell.volume + "%"
                                width: 36
                                anchors.verticalCenter: parent.verticalCenter
                                font { pixelSize: 10; family: theme.fontMono }
                                color: theme.textMid
                            }
                        }
                    }
                }
            }
        }

        // ── Evidence inspector ──────────────────────────────────────────────
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: theme.surface
            border.width: 1
            border.color: theme.border

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 3
                        color: theme.border
                    }
                }

                Column {
                    width: parent.width
                    spacing: 0

                    // Inspector header
                    Rectangle {
                        width: parent.width
                        height: 36
                        color: "transparent"
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            spacing: 8
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "INSPECTOR"
                                font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 1.3 }
                                color: theme.textLow
                            }
                        }
                    }

                    // ── Identity ────────────────────────────────────────
                    Column {
                        width: parent.width
                        spacing: 10
                        leftPadding: 14
                        rightPadding: 14

                        InfoRow {
                            label: "Evidence ID"
                            value: kfwShell.activeEvidenceId
                        }
                        InfoRow {
                            label: "Filename"
                            value: kfwShell.activeEvidenceName
                        }
                        InfoRow {
                            label: "Provenance"
                            value: kfwShell.activeEvidenceProvenance
                        }
                        InfoRow {
                            label: "File type"
                            value: kfwShell.activeMediaInfo.available
                                   ? (kfwShell.activeMediaInfo.container || "—")
                                   : "—"
                        }
                    }

                    Item { width: 1; height: 14 }

                    // ── Integrity ────────────────────────────────────────
                    Column {
                        width: parent.width
                        spacing: 8
                        leftPadding: 14
                        rightPadding: 14

                        Text {
                            text: "INTEGRITY"
                            font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 1.3 }
                            color: theme.textLow
                        }

                        Rectangle {
                            width: 272
                            height: 40
                            radius: theme.rSm
                            color: theme.inset
                            border.width: 1
                            border.color: theme.border

                            Text {
                                anchors.fill: parent
                                anchors.margins: 6
                                text: kfwShell.activeEvidenceSha.length > 0
                                      ? kfwShell.activeEvidenceSha : "—"
                                font { pixelSize: 9; family: theme.fontMono }
                                color: theme.accent
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        Row {
                            spacing: 8
                            KFWStatusChip {
                                status: kfwShell.activeEvidenceStatus
                            }
                            Text {
                                visible: kfwShell.activeEvidenceStatus.length > 0
                                text: "press ✓ to re-verify"
                                font { pixelSize: 10; family: theme.fontFamily }
                                color: theme.textLow
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    Item { width: 1; height: 14 }
                    Rectangle {
                        width: parent.width - 28
                        height: 1
                        color: theme.border
                        x: 14
                    }
                    Item { width: 1; height: 12 }

                    // ── Technical media info ─────────────────────────────
                    Column {
                        width: parent.width
                        spacing: 10
                        leftPadding: 14
                        rightPadding: 14

                        Text {
                            text: "TECHNICAL"
                            font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 1.3 }
                            color: theme.textLow
                        }

                        InfoRow {
                            label: "Duration"
                            value: root.formatMs(
                                kfwShell.activeMediaInfo.durationMs >= 0
                                ? kfwShell.activeMediaInfo.durationMs
                                : mediaDuration)
                        }
                        InfoRow {
                            label: "Resolution"
                            value: kfwShell.activeMediaInfo.available
                                   ? ((kfwShell.activeMediaInfo.width || 0) + " × "
                                      + (kfwShell.activeMediaInfo.height || 0))
                                   : "—"
                        }
                        InfoRow {
                            label: "Frame rate"
                            value: kfwShell.activeMediaInfo.available
                                   ? (kfwShell.activeMediaInfo.fps || "—")
                                   : "—"
                        }
                        InfoRow {
                            label: "Video codec"
                            value: kfwShell.activeMediaInfo.videoCodec || "—"
                        }
                        InfoRow {
                            label: "Audio codec"
                            value: kfwShell.activeMediaInfo.audioCodec
                                   || ((kfwShell.activeMediaInfo.available
                                        && !kfwShell.activeMediaInfo.audioCodec)
                                       ? "none" : "—")
                        }
                        InfoRow {
                            label: "Bitrate"
                            value: {
                                const b = kfwShell.activeMediaInfo.videoBitrate
                                return b > 0 ? (b / 1000).toFixed(0) + " kbit/s" : "—"
                            }
                        }
                        InfoRow {
                            label: "Parsed by"
                            value: kfwShell.activeMediaInfo.engine || "—"
                        }

                        // Track rows from the engine probe
                        Repeater {
                            model: kfwShell.activeMediaInfo.available
                                   ? (kfwShell.activeMediaInfo.tracks || []) : []

                            Rectangle {
                                required property var modelData
                                width: parent.width - 28
                                height: 34
                                radius: theme.rSm
                                color: theme.inset
                                border.width: 1
                                border.color: theme.border

                                Text {
                                    anchors {
                                        left: parent.left; leftMargin: 10
                                        verticalCenter: parent.verticalCenter
                                    }
                                    text: modelData.type + "  ·  " + modelData.codec
                                    font { pixelSize: 11; family: theme.fontFamily }
                                    color: theme.textMid
                                    elide: Text.ElideRight
                                    width: parent.width - 20
                                }
                            }
                        }
                    }

                    Item { width: 1; height: 14 }
                    Rectangle {
                        width: parent.width - 28
                        height: 1
                        color: theme.border
                        x: 14
                    }
                    Item { width: 1; height: 12 }

                    // ── Examiner notes ───────────────────────────────────
                    Column {
                        width: parent.width
                        spacing: 8
                        leftPadding: 14
                        rightPadding: 14

                        Text {
                            text: "EXAMINER NOTES"
                            font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 1.3 }
                            color: theme.textLow
                        }

                        Rectangle {
                            width: 272
                            height: 120
                            radius: theme.rSm
                            color: theme.inset
                            border.width: notesArea.activeFocus ? 2 : 1
                            border.color: notesArea.activeFocus
                                          ? theme.accent : theme.border

                            ScrollView {
                                anchors.fill: parent
                                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                    contentItem: Rectangle {
                                        implicitWidth: 6
                                        radius: 3
                                        color: theme.border
                                    }
                                }

                                TextArea {
                                    id: notesArea
                                    placeholderText: "Record observations for the timeline…"
                                    wrapMode: Text.Wrap
                                    background: null
                                    color: theme.textHi
                                    font {
                                        pixelSize: 12
                                        family: theme.fontFamily
                                    }
                                    leftPadding: 8
                                    rightPadding: 8
                                    topPadding: 8
                                    bottomPadding: 8
                                }
                            }
                        }
                    }

                    Item { height: 16 }
                }
            }
        }
    }

    // ── Timeline strip ───────────────────────────────────────────────────────
    Rectangle {
        id: timelinePanel
        width: parent.width
        height: 150
        anchors.bottom: parent.bottom
        color: theme.surface
        border.width: 1
        border.color: theme.border

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 30
                color: "transparent"
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 10

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "INVESTIGATION TIMELINE"
                        font { pixelSize: 9; bold: true; family: theme.fontFamily; letterSpacing: 1.3 }
                        color: theme.textLow
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: kfwTimelineModel.count + " event(s)"
                        font { pixelSize: 10; family: theme.fontFamily }
                        color: theme.textLow
                    }
                    Item { Layout.fillWidth: true; height: 1 }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "SPACE play · ← → step · Ctrl+← → 10s · M marker · F export frame"
                        font { pixelSize: 9; family: theme.fontMono }
                        color: theme.textLow
                    }
                }
            }

            ScrollView {
                width: parent.width
                height: 120
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    contentItem: Rectangle {
                        implicitHeight: 6
                        radius: 3
                        color: theme.border
                    }
                }

                Row {
                    leftPadding: 14
                    spacing: 10
                    height: 110
                    anchors.verticalCenter: parent.verticalCenter

                    // Marker cards
                    Repeater {
                        model: kfwTimelineModel

                        Rectangle {
                            id: markerCard
                            required property string eventId
                            required property int   tsMs
                            required property string tsText
                            required property string kind
                            required property string label
                            required property string body
                            required property string evidenceId

                            width: 190
                            height: 94
                            radius: theme.rMd
                            color: marHov.hovered ? theme.cardHover : theme.card
                            border.width: 1
                            border.color: Qt.alpha(theme.kindColor[kind]
                                                   || theme.neutral, 0.6)

                            Behavior on color { ColorAnimation { duration: 90 } }

                            Column {
                                anchors {
                                    fill: parent
                                    margins: 10
                                }
                                spacing: 4

                                Row {
                                    spacing: 6
                                    Rectangle {
                                        width: 8; height: 8; radius: 4
                                        color: theme.kindColor[markerCard.kind]
                                               || theme.neutral
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                    }
                                    Text {
                                        text: markerCard.tsText
                                        font {
                                            pixelSize: 11; bold: true
                                            family: theme.fontMono
                                        }
                                        color: theme.accent
                                    }
                                    Text {
                                        text: markerCard.kind
                                        font { pixelSize: 8; family: theme.fontFamily }
                                        color: theme.textLow
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                    }
                                }

                                Text {
                                    text: markerCard.label
                                    width: parent.width
                                    elide: Text.ElideRight
                                    font { pixelSize: 11; family: theme.fontFamily }
                                    color: theme.textHi
                                }
                                Text {
                                    text: markerCard.body
                                    width: parent.width
                                    elide: Text.ElideRight
                                    font { pixelSize: 10; family: theme.fontFamily }
                                    color: theme.textMid
                                    visible: markerCard.body.length > 0
                                }
                                Text {
                                    text: markerCard.evidenceId.length > 0
                                          ? markerCard.evidenceId : "case level"
                                    font { pixelSize: 9; family: theme.fontMono }
                                    color: theme.textLow
                                }
                            }

                            HoverHandler { id: marHov }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (hasMedia && markerCard.tsMs > 0) {
                                        kfwShell.playerSeekMs(markerCard.tsMs)
                                        kfwShell.playerPlay()
                                    }
                                }
                            }
                        }
                    }

                    // Empty state
                    Column {
                        visible: kfwTimelineModel.count === 0
                        leftPadding: 8
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            text: "No timeline events yet."
                            font { pixelSize: 11; family: theme.fontFamily }
                            color: theme.textLow
                        }
                        Text {
                            text: "Markers, frame exports, integrity checks and\nnotes are recorded here as you work."
                            font { pixelSize: 10; family: theme.fontFamily }
                            color: theme.textLow
                        }
                    }
                }
            }
        }
    }

    // ── Marker dialog ─────────────────────────────────────────────────────
    Dialog {
        id: markerDialog
        modal: true
        anchors.centerIn: parent
        width: 400
        property double noteAt: 0
        property string labelInput: ""

        background: Rectangle {
            color: theme.panel
            radius: theme.rLg
            border.width: 1
            border.color: theme.border
        }

        header: Text {
            text: "Add timeline marker"
            font { pixelSize: 14; bold: true; family: theme.fontFamily }
            color: theme.textHi
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Column {
            spacing: 12
            topPadding: 8

            Text {
                text: "Timestamp " + root.formatMs(markerDialog.noteAt)
                font { pixelSize: 13; family: theme.fontMono }
                color: theme.accent
            }

            Rectangle {
                width: parent.width
                height: theme.controlH
                radius: theme.rMd
                color: theme.inset
                border.width: markerInput.activeFocus ? 2 : 1
                border.color: markerInput.activeFocus
                              ? theme.accent : theme.border

                TextInput {
                    id: markerInput
                    anchors {
                        fill: parent
                        leftMargin: 12
                        rightMargin: 12
                    }
                    font { pixelSize: 13; family: theme.fontFamily }
                    color: theme.textHi
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    text: markerDialog.labelInput

                    Keys.onReturnPressed: markerDialog.accept()
                    Keys.onEnterPressed: markerDialog.accept()
                }
            }

            Row {
                spacing: 10
                anchors.right: parent.right

                KFWButton {
                    label: "Cancel"
                    outline: true
                    onClicked: markerDialog.close()
                }
                KFWButton {
                    label: "Add Marker"
                    accent: true
                    onClicked: {
                        kfwShell.addTimelineMarker(
                            kfwShell.activeCaseId,
                            kfwShell.activeEvidenceId,
                            markerDialog.noteAt,
                            markerInput.text.length > 0
                                ? markerInput.text : "Marker")
                        markerDialog.close()
                    }
                }
            }
        }
    }

    // ── Import evidence dialog ─────────────────────────────────────────────
    Dialog {
        id: importDialog
        modal: true
        anchors.centerIn: parent
        width: 430
        property string picked: ""

        background: Rectangle {
            color: theme.panel
            radius: theme.rLg
            border.width: 1
            border.color: theme.border
        }

        header: Text {
            text: "Import Evidence"
            font { pixelSize: 14; bold: true; family: theme.fontFamily }
            color: theme.textHi
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Column {
            spacing: 12
            topPadding: 8

            Text {
                text: "Choose the ORIGINAL source file."
                font { pixelSize: 11; family: theme.fontFamily }
                color: theme.textMid
                width: parent.width
                wrapMode: Text.Wrap
            }
            Text {
                text: "KFW copies the file into the evidence store, computes SHA-256 for both streams (source and copy) and records provenance. The original file is never modified."
                font { pixelSize: 10; family: theme.fontFamily }
                color: theme.textLow
                width: parent.width
                wrapMode: Text.Wrap
            }

            // File picker row
            Rectangle {
                width: parent.width
                height: theme.controlH
                radius: theme.rMd
                color: theme.inset
                border.width: 1
                border.color: theme.border

                Row {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    Text {
                        text: importDialog.picked.length > 0
                              ? importDialog.picked : "No file selected"
                        font { pixelSize: 12; family: theme.fontFamily }
                        color: importDialog.picked.length > 0
                               ? theme.textHi : theme.textLow
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                        width: parent.width - pickButton.width - 8
                    }

                    Item { Layout.fillWidth: true; height: 1 }

                    KFWButton {
                        id: pickButton
                        label: "Browse…"
                        outline: true
                        height: 26
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: evidenceFileDialog.open()
                    }
                }
            }

            Row {
                spacing: 10
                anchors.right: parent.right

                KFWButton {
                    label: "Cancel"
                    outline: true
                    onClicked: importDialog.close()
                }
                KFWButton {
                    label: "Import"
                    accent: true
                    enabled_: importDialog.picked.length > 0
                    onClicked: {
                        kfwShell.importEvidence(
                            kfwShell.activeCaseId, importDialog.picked)
                        importDialog.picked = ""
                        importDialog.close()
                    }
                }
            }
        }

        FileDialog {
            id: evidenceFileDialog
            title: "Select original media file"
            nameFilters: [
                "Media files (*.mp4 *.avi *.mkv *.mov *.mts *.m2ts *.ts *.mpg *.mpeg *.wmv *.flv *.wav *.mp3)",
                "All files (*)"
            ]
            onAccepted: importDialog.picked =
                evidenceFileDialog.selectedFile.toString().replace("file:///", "")
        }
    }

    // ── Report dialog ─────────────────────────────────────────────────────────
    Dialog {
        id: reportDialog
        modal: true
        anchors.centerIn: parent
        width: 470

        background: Rectangle {
            color: theme.panel
            radius: theme.rLg
            border.width: 1
            border.color: theme.border
        }

        header: Text {
            text: "Generate investigation report"
            font { pixelSize: 14; bold: true; family: theme.fontFamily }
            color: theme.textHi
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Column {
            spacing: 12
            topPadding: 8

            Text {
                text: "Includes a JSON manifest (SHA-256 signed fields for evidence and timeline) and an HTML investigation report."
                font { pixelSize: 11; family: theme.fontFamily }
                color: theme.textMid
                width: parent.width
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: parent.width
                height: 90
                radius: theme.rMd
                color: theme.inset
                border.width: reportArea.activeFocus ? 2 : 1
                border.color: reportArea.activeFocus
                              ? theme.accent : theme.border

                ScrollView {
                    anchors.fill: parent
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: theme.border
                        }
                    }

                    TextArea {
                        id: reportArea
                        placeholderText: "Optional examiner summary for the report…"
                        wrapMode: Text.Wrap
                        background: null
                        color: theme.textHi
                        font { pixelSize: 12; family: theme.fontFamily }
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 8
                        bottomPadding: 8
                    }
                }
            }

            Row {
                spacing: 10
                anchors.right: parent.right

                KFWButton {
                    label: "Cancel"
                    outline: true
                    onClicked: reportDialog.close()
                }
                KFWButton {
                    label: "Generate"
                    accent: true
                    onClicked: {
                        kfwShell.generateReport(
                            kfwShell.activeCaseId,
                            "RF-" + (Date.now() % 1000000),
                            reportArea.text)
                        reportArea.text = ""
                        reportDialog.close()
                    }
                }
            }
        }
    }

}