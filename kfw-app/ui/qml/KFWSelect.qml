// KFWSelect.qml — themed drop-down used for audio / subtitle track selection
// Copyright (C) 2026 Kishan G A

import QtQuick
import QtQuick.Controls

ComboBox {
    id: box

    /** Engine id of the currently active entry (not the list index). */
    property int currentId: -1
    /** Text shown when the list has no entries. */
    property string emptyText: "—"

    /** Emitted only when the examiner picks an entry (never from the engine). */
    signal picked(int trackId)

    textRole: "name"
    valueRole: "id"
    implicitHeight: 28
    font { pixelSize: 11; family: theme.fontFamily }
    displayText: box.count === 0 ? box.emptyText : box.currentText

    // Follow the engine without re-emitting `picked`.
    currentIndex: { box.count; return box.indexOfValue(box.currentId) }
    onActivated: (index) => box.picked(box.valueAt(index))

    background: Rectangle {
        radius: theme.rSm
        color: box.hovered ? theme.cardHover : theme.card
        border.width: 1
        border.color: (box.visualFocus || box.popup.visible)
                      ? theme.accent : theme.border
        opacity: box.enabled ? 1.0 : 0.5
    }

    contentItem: Text {
        leftPadding: 9
        rightPadding: 24
        text: box.displayText
        font: box.font
        color: box.enabled ? theme.textHi : theme.textLow
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: box.width - width - 9
        y: (box.height - height) / 2
        text: "▾"
        font.pixelSize: 11
        color: theme.textMid
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : box.width
        height: 28
        highlighted: box.highlightedIndex === index

        contentItem: Text {
            text: item.modelData.name
            font: box.font
            color: item.highlighted ? theme.accent : theme.textHi
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: item.highlighted ? theme.accentSoft : "transparent"
            radius: theme.rSm
        }
    }

    popup: Popup {
        // The control sits at the bottom of the window: open upwards.
        y: -height - 2
        width: Math.max(box.width, 260)
        implicitHeight: Math.min(list.contentHeight + 2, 240)
        padding: 1

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: theme.panel
            radius: theme.rMd
            border.width: 1
            border.color: theme.border
        }
    }
}
