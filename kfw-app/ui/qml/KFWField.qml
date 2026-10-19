// KFWField.qml — labeled input field used by dialogs
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Controls

Column {
    id: root

    property string labelText: ""
    property string placeholder: ""
    property alias  text: focusField.text
    property int     longest: 0
    property bool    isError: false
    property string  hintMessage: ""

    signal textEdited(string value)

    width: parent.width
    spacing: 7

    Text {
        text: root.labelText
        font { pixelSize: 10; bold: true; family: theme.fontFamily; letterSpacing: 0.8 }
        color: root.isError ? theme.bad : theme.textLow
    }

    Rectangle {
        width: parent.width
        height: theme.controlH
        radius: theme.rMd
        color: theme.inset
        border.color: focusField.activeFocus ? theme.accent
                     : root.isError ? theme.bad : theme.border
        border.width: focusField.activeFocus ? 2 : 1

        TextInput {
            id: focusField
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
                topMargin: 8
                bottomMargin: 8
            }
            font.pixelSize: 13
            font.family: theme.fontFamily
            color: theme.textHi
            cursorVisible: activeFocus
            clip: true
            selectByMouse: true
            selectionColor: Qt.alpha(theme.accent, 0.35)
            verticalAlignment: TextInput.AlignVCenter

            Text {
                anchors.fill: parent
                visible: focusField.text.length === 0 && !focusField.activeFocus
                text: root.placeholder
                font.pixelSize: 13
                font.family: theme.fontFamily
                color: theme.textLow
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            onTextChanged: root.textEdited(focusField.text)
            Keys.onEnterPressed: root.done()
            Keys.onReturnPressed: root.done()
        }
    }

    Text {
        visible: root.hintMessage.length > 0
        text: root.hintMessage
        font.pixelSize: 10
        font.family: theme.fontFamily
        color: root.isError ? theme.bad : theme.textLow
    }

    function done() { /* dialogs connect this via FocusScope or accept */ root.textEdited(root.text) }
}
