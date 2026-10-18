import QtQuick
import QtQuick.Controls

// Small window control for the client-side title bar: minimize, maximize,
// restore and close.
//
// The icons are drawn from rectangles instead of font glyphs. The fonts on the
// target image do not carry the symbols this used to ask for - the close cross
// (U+2715) came out as an empty square - and a window control that depends on
// glyph coverage breaks again on the next image update.
Button {
    id: ctl

    // "minimize", "maximize", "restore" or "close".
    property string kind: "close"
    property string tip: ""

    width: 30
    height: 22
    hoverEnabled: true

    readonly property color iconColor: ctl.enabled
                                       ? (ctl.down ? "#FFFFFF" : "#E8EAF2")
                                       : "#4B5264"

    background: Rectangle {
        radius: 6
        color: ctl.down ? "#7C5CFC" : (ctl.hovered ? "#2A3142" : "transparent")
        border.width: 1
        border.color: ctl.hovered ? "#7C5CFC" : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: Item {
        // Minimize: one calm bar, a little below the middle.
        Rectangle {
            visible: ctl.kind === "minimize"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 3
            width: 11
            height: 1.6
            radius: 0.8
            color: ctl.iconColor
        }

        // Maximize: an empty frame.
        Rectangle {
            visible: ctl.kind === "maximize"
            anchors.centerIn: parent
            width: 11
            height: 9
            radius: 1.5
            color: "transparent"
            border.width: 1.4
            border.color: ctl.iconColor
        }

        // Restore: the maximized frame, with the corner of the smaller window
        // behind it. Only the two lines that do not cross the front frame are
        // drawn, so nothing has to be masked out.
        Item {
            visible: ctl.kind === "restore"
            anchors.centerIn: parent
            width: 14
            height: 12

            Rectangle {
                x: 0; y: 0
                width: 8; height: 1.4
                radius: 0.7
                color: ctl.iconColor
            }
            Rectangle {
                x: 0; y: 0
                width: 1.4; height: 7
                radius: 0.7
                color: ctl.iconColor
            }
            Rectangle {
                x: 3; y: 3
                width: 11; height: 9
                radius: 1.5
                color: "transparent"
                border.width: 1.4
                border.color: ctl.iconColor
            }
        }

        // Close: a cross.
        Item {
            visible: ctl.kind === "close"
            anchors.centerIn: parent
            width: 12
            height: 12

            Rectangle {
                anchors.centerIn: parent
                width: 12; height: 1.6
                radius: 0.8
                color: ctl.iconColor
                rotation: 45
            }
            Rectangle {
                anchors.centerIn: parent
                width: 12; height: 1.6
                radius: 0.8
                color: ctl.iconColor
                rotation: -45
            }
        }
    }

    ToolTip.visible: ctl.hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}
