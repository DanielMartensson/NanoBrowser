import QtQuick
import QtQuick.Controls

Button {
    id: ctl
    property string tip: ""
    width: 40
    height: 36
    hoverEnabled: true
    font.pixelSize: 17

    contentItem: Text {
        text: ctl.text
        font: ctl.font
        color: ctl.enabled ? (ctl.down ? "#FFFFFF" : "#E8EAF2") : "#4B5264"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 10
        color: ctl.down ? "#7C5CFC" : (ctl.hovered ? "#2A3142" : "#1C2231")
        border.color: ctl.hovered ? "#7C5CFC" : "#2A3142"
        border.width: 1
        scale: ctl.down ? 0.92 : (ctl.hovered ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    ToolTip.visible: ctl.hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}