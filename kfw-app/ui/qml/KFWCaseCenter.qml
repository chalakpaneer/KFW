// KFWCaseCenter.qml — case details, integrity, handover packages and search
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Rectangle {
    id: root

    visible: false
    color: Qt.rgba(0, 0, 0, 0.55)
    focus: visible

    property string caseId: ""
    property var    searchResults: []
    property string packagePath: ""
    property string packageSha: ""
    property string packageSummary: ""
    property bool   packageWarn: false
    property string verifyText: ""
    property bool   verifyOk: false

    function openFor(id) {
        caseId = id
        packagePath = ""
        packageSummary = ""
        verifyText = ""
        searchField.text = ""
        searchResults = []
        load()
        visible = true
        forceActiveFocus()
    }
    function close() { visible = false }

    function load() {
        var d = kfwShell.caseDetails(caseId)
        titleText.text = (d.caseId || "") + "  ·  " + (d.title || "")
        metaText.text = "Created " + (d.created || "") + "  ·  Examiner " + (d.examiner || "")
        numField.text = d.caseNumber || ""
        agencyField.text = d.agency || ""
        requesterField.text = d.requester || ""
        incidentField.text = d.incidentDate || ""
        locationField.text = d.location || ""
        descField.text = d.description || ""
    }

    Keys.onEscapePressed: root.close()

    // Click outside the panel closes the centre.
    MouseArea { anchors.fill: parent; onClicked: root.close() }

    Connections {
        target: kfwShell
        function onPackageCreated(path, sha, summary, hasWarnings) {
            root.packagePath = path
            root.packageSha = sha
            root.packageSummary = summary
            root.packageWarn = hasWarnings
        }
        function onPackageVerified(r) {
            if (!r.readable) {
                root.verifyOk = false
                root.verifyText = "Not a KFW handover package (manifest.json not found)."
                return
            }
            root.verifyOk = r.ok
            var t = r.ok ? "Package verifies: " + r.okCount + " of " + r.files + " files match."
                         : "Package does NOT verify. " + r.okCount + " of " + r.files + " files match"
                           + (r.mismatch > 0 ? ", " + r.mismatch + " altered" : "")
                           + (r.missing > 0 ? ", " + r.missing + " missing" : "")
                           + (r.extra > 0 ? ", " + r.extra + " unlisted" : "")
                           + (r.manifestDigest === 0 ? ", manifest edited" : "") + "."
            if (r.firstProblem.length > 0)
                t += "\nFirst problem: " + r.firstProblem
            t += "\nManifest SHA-256: " + r.manifestSha256
            root.verifyText = t
        }
    }

    FolderDialog {
        id: verifyFolderDialog
        title: "Choose a handover package folder"
        onAccepted: kfwShell.verifyHandoverPackage(selectedFolder.toString())
    }

    component SectionTitle: Text {
        font { pixelSize: 10; bold: true; letterSpacing: 0.9; family: theme.fontFamily }
        color: theme.textLow
    }

    // ---- Panel ---------------------------------------------------------------
    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(900, parent.width - 80)
        height: parent.height - 80
        radius: theme.rLg
        color: theme.panel
        border.width: 1
        border.color: theme.border

        MouseArea { anchors.fill: parent }   // swallow clicks inside the panel

        Item {
            id: head
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 64

            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                spacing: 4
                Text {
                    id: titleText
                    font { pixelSize: 15; bold: true; family: theme.fontFamily }
                    color: theme.textHi
                }
                Text {
                    id: metaText
                    font { pixelSize: 11; family: theme.fontFamily }
                    color: theme.textLow
                }
            }
            KFWButton {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                label: "Close"
                outline: true
                onClicked: root.close()
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }
        }

        Flickable {
            id: flick
            anchors { top: head.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.margins: 22
            contentWidth: width
            contentHeight: body.height + 20
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: body
                width: flick.width - 12
                spacing: 14

                // ---- Case details ----------------------------------------------
                SectionTitle { text: "CASE DETAILS" }

                Row {
                    width: parent.width
                    spacing: 14
                    Column {
                        width: (parent.width - 14) / 2
                        spacing: 12
                        KFWField { id: numField; labelText: "CASE / FIR NUMBER"; placeholder: "Agency reference" }
                        KFWField { id: requesterField; labelText: "REQUESTED BY"; placeholder: "Requesting officer or client" }
                        KFWField { id: incidentField; labelText: "INCIDENT DATE"; placeholder: "e.g. 2026-08-07" }
                    }
                    Column {
                        width: (parent.width - 14) / 2
                        spacing: 12
                        KFWField { id: agencyField; labelText: "AGENCY / ORGANISATION"; placeholder: "Unit or organisation" }
                        KFWField { id: locationField; labelText: "LOCATION"; placeholder: "Where the footage was recorded" }
                        KFWField { id: descField; labelText: "DESCRIPTION"; placeholder: "Short factual description" }
                    }
                }
                KFWButton {
                    label: "Save case details"
                    accent: true
                    onClicked: kfwShell.saveCaseDetails(root.caseId, {
                        "caseNumber": numField.text,
                        "agency": agencyField.text,
                        "requester": requesterField.text,
                        "incidentDate": incidentField.text,
                        "location": locationField.text,
                        "description": descField.text
                    })
                }

                Rectangle { width: parent.width; height: 1; color: theme.border }

                // ---- Integrity and report ----------------------------------------
                SectionTitle { text: "INTEGRITY AND REPORT" }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Re-hash every stored evidence file and compare it with the SHA-256 recorded at import. "
                          + "A match shows the bytes are unchanged since import; it does not show the footage is genuine."
                    font { pixelSize: 11; family: theme.fontFamily }
                    color: theme.textMid
                }
                Row {
                    spacing: 10
                    KFWButton {
                        label: "Verify all evidence"
                        outline: true
                        onClicked: kfwShell.verifyAllEvidence(root.caseId)
                    }
                    KFWButton {
                        label: "Generate report"
                        outline: true
                        onClicked: kfwShell.generateReport(root.caseId,
                                                           "RF-" + (Date.now() % 1000000), "")
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.border }

                // ---- Handover package -----------------------------------------------
                SectionTitle { text: "HANDOVER PACKAGE" }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "A folder with the evidence, a fresh report, the chain of custody, the timeline and a "
                          + "manifest of SHA-256 hashes, so the recipient can verify nothing was altered in transit. "
                          + "Send the manifest SHA-256 to the recipient by a second channel."
                    font { pixelSize: 11; family: theme.fontFamily }
                    color: theme.textMid
                }
                CheckBox {
                    id: includeEvidence
                    checked: true
                    text: "Include evidence files (uncheck for a metadata-only package)"
                    contentItem: Text {
                        leftPadding: includeEvidence.indicator.width + 8
                        text: includeEvidence.text
                        font { pixelSize: 11; family: theme.fontFamily }
                        color: theme.textMid
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Row {
                    spacing: 10
                    KFWButton {
                        label: "Export package"
                        accent: true
                        onClicked: kfwShell.exportHandoverPackage(root.caseId, includeEvidence.checked)
                    }
                    KFWButton {
                        label: "Verify a package…"
                        outline: true
                        onClicked: verifyFolderDialog.open()
                    }
                    KFWButton {
                        label: "Show packages folder"
                        outline: true
                        onClicked: kfwShell.revealPath(kfwShell.packagesDir())
                    }
                }
                Rectangle {
                    visible: root.packageSummary.length > 0
                    width: parent.width
                    height: pkgCol.height + 20
                    radius: theme.rMd
                    color: theme.inset
                    border.width: 1
                    border.color: root.packageWarn ? theme.warn : theme.border
                    Column {
                        id: pkgCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        Text {
                            width: parent.width
                            wrapMode: Text.WrapAnywhere
                            text: root.packagePath
                            font { pixelSize: 11; family: theme.fontMono }
                            color: theme.textHi
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: root.packageSummary
                            font { pixelSize: 11; family: theme.fontFamily }
                            color: root.packageWarn ? theme.warn : theme.textMid
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WrapAnywhere
                            text: "Manifest SHA-256: " + root.packageSha
                            font { pixelSize: 10; family: theme.fontMono }
                            color: theme.accent
                        }
                        KFWButton {
                            label: "Show in folder"
                            outline: true
                            onClicked: kfwShell.revealPath(root.packagePath)
                        }
                    }
                }
                Rectangle {
                    visible: root.verifyText.length > 0
                    width: parent.width
                    height: vText.implicitHeight + 20
                    radius: theme.rMd
                    color: theme.inset
                    border.width: 1
                    border.color: root.verifyOk ? theme.ok : theme.bad
                    Text {
                        id: vText
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        wrapMode: Text.WrapAnywhere
                        text: root.verifyText
                        font { pixelSize: 11; family: theme.fontFamily }
                        color: root.verifyOk ? theme.ok : theme.bad
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.border }

                // ---- Search ------------------------------------------------------------
                SectionTitle { text: "SEARCH THIS CASE" }
                KFWField {
                    id: searchField
                    labelText: ""
                    placeholder: "File names, hashes, timeline labels, notes, tags…"
                    onTextChanged: root.searchResults = kfwShell.search(root.caseId, text)
                }
                Text {
                    visible: searchField.text.trim().length > 0
                    text: root.searchResults.length === 0 ? "No matches."
                          : root.searchResults.length + " match(es)"
                    font { pixelSize: 11; family: theme.fontFamily }
                    color: theme.textLow
                }
                Repeater {
                    model: root.searchResults
                    delegate: Rectangle {
                        id: hit
                        required property var modelData
                        width: body.width
                        height: 46
                        radius: theme.rMd
                        color: hitHover.hovered ? theme.cardHover : theme.card
                        border.width: 1
                        border.color: theme.border

                        Row {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                            spacing: 12
                            Text {
                                width: 62
                                anchors.verticalCenter: parent.verticalCenter
                                text: hit.modelData.kind.toUpperCase()
                                font { pixelSize: 9; bold: true; letterSpacing: 0.8; family: theme.fontFamily }
                                color: theme.accent
                            }
                            Column {
                                width: parent.width - 62 - 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: hit.modelData.title
                                          + (hit.modelData.tsMs > 0
                                             ? "   @ " + Math.floor(hit.modelData.tsMs / 1000) + " s" : "")
                                    font { pixelSize: 12; family: theme.fontFamily }
                                    color: theme.textHi
                                }
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: hit.modelData.snippet
                                    font { pixelSize: 10; family: theme.fontFamily }
                                    color: theme.textLow
                                }
                            }
                        }
                        HoverHandler { id: hitHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (hit.modelData.evidenceId.length > 0) {
                                    kfwShell.openEvidence(root.caseId, hit.modelData.evidenceId)
                                    root.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
