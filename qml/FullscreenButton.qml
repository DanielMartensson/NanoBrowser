import QtQuick
import QtQuick.Controls

Button {
    id: ctl
    property bool active: false
    property string tip: ""
    width: 40
    height: 36
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: 18
        implicitHeight: 18
        anchors.centerIn: parent

        Canvas {
            id: icon
            anchors.fill: parent
            antialiasing: true
            readonly property color iconColor: ctl.active || ctl.down
                ? "#FFFFFF"
                : (ctl.hovered ? "#C9BEFA" : "#98A1B3")
            onIconColorChanged: requestPaint()
            Component.onCompleted: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = icon.iconColor
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                const s = width
                const m = 2
                const l = 7
                ctx.beginPath()
                ctx.moveTo(m, m + l); ctx.lineTo(m, m); ctx.lineTo(m + l, m)
                ctx.moveTo(s - m - l, m); ctx.lineTo(s - m, m); ctx.lineTo(s - m, m + l)
                ctx.moveTo(s - m, s - m - l); ctx.lineTo(s - m, s - m); ctx.lineTo(s - m - l, s - m)
                ctx.moveTo(m + l, s - m); ctx.lineTo(m, s - m); ctx.lineTo(m, s - m - l)
                ctx.stroke()
            }
        }
    }

    background: Rectangle {
        radius: 10
        color: ctl.down ? "#6C4CF0" : (ctl.active ? "#7C5CFC" : (ctl.hovered ? "#2A3142" : "#1C2231"))
        border.color: ctl.active || ctl.hovered ? "#7C5CFC" : "#2A3142"
        border.width: 1
        scale: ctl.down ? 0.92 : (ctl.hovered ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    ToolTip.visible: ctl.hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}