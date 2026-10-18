import QtQuick
import QtQuick.Controls

// Toolbar button whose icon is drawn, not typed.
//
// These controls used to be font glyphs, which only works as long as the image
// happens to ship a font that has them. The Liberation set on this image has no
// close cross (U+2715), no play triangle (U+25B6) and no reload symbol at all
// (U+21BB, U+27F3, U+2B6E), so the buttons came out as empty squares. The icons
// below are therefore drawn on a canvas, with coordinates that were rendered
// off-screen and checked before they went in.
Button {
    id: ctl

    property string tip: ""
    // "reload", "play", "stop" or "close".
    property string icon: "reload"

    width: 40
    height: 36
    hoverEnabled: true

    readonly property color iconColor: ctl.enabled
                                       ? (ctl.down ? "#FFFFFF" : "#E8EAF2")
                                       : "#4B5264"

    background: Rectangle {
        radius: 10
        color: ctl.down ? "#7C5CFC" : (ctl.hovered ? "#2A3142" : "#1C2231")
        border.color: ctl.hovered ? "#7C5CFC" : "#2A3142"
        border.width: 1
        scale: ctl.down ? 0.92 : (ctl.hovered ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: Canvas {
        id: iconCanvas
        width: 18
        height: 18
        anchors.centerIn: parent

        Connections {
            target: ctl
            function onIconColorChanged() { iconCanvas.requestPaint() }
            function onIconChanged() { iconCanvas.requestPaint() }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = ctl.iconColor
            ctx.fillStyle = ctl.iconColor
            ctx.lineWidth = 1.8
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            if (ctl.icon === "reload") {
                // A ring with a gap at the top right, and the arrow head on the
                // gap's lower end so that it points the way the ring runs.
                var cx = 9, cy = 9, r = 5.9
                var a0 = -Math.PI / 2 - 0.55
                var a1 = -Math.PI / 2 + 0.55
                ctx.beginPath()
                ctx.arc(cx, cy, r, a0, a1 + 2 * Math.PI, false)
                ctx.stroke()

                var tx = -Math.sin(a1), ty = Math.cos(a1)
                var nx = -ty, ny = tx
                var px = cx + r * Math.cos(a1), py = cy + r * Math.sin(a1)
                ctx.beginPath()
                ctx.moveTo(px + tx * 3.0, py + ty * 3.0)
                ctx.lineTo(px - tx * 0.4 + nx * 2.6, py - ty * 0.4 + ny * 2.6)
                ctx.lineTo(px - tx * 0.4 - nx * 2.6, py - ty * 0.4 - ny * 2.6)
                ctx.closePath()
                ctx.fill()
            } else if (ctl.icon === "play") {
                ctx.beginPath()
                ctx.moveTo(5.6, 4.0)
                ctx.lineTo(14.0, 9.0)
                ctx.lineTo(5.6, 14.0)
                ctx.closePath()
                ctx.fill()
            } else if (ctl.icon === "stop") {
                ctx.beginPath()
                ctx.rect(5.0, 5.0, 8.0, 8.0)
                ctx.fill()
            } else if (ctl.icon === "close") {
                ctx.beginPath()
                ctx.moveTo(5.2, 5.2)
                ctx.lineTo(12.8, 12.8)
                ctx.moveTo(12.8, 5.2)
                ctx.lineTo(5.2, 12.8)
                ctx.stroke()
            }
        }
    }

    ToolTip.visible: ctl.hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}
