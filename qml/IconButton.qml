import QtQuick
import QtQuick.Controls

// Toolbar button whose icon is drawn, not typed.
//
// These controls used to be font glyphs, which only works as long as the image
// happens to ship a font that has them. The Liberation set on this image has no
// reload symbol at all (U+21BB, U+27F3, U+2B6E), so the button came out as an
// empty square. The icon is therefore drawn on a canvas, with coordinates that
// were rendered off-screen and checked before they went in.
Button {
    id: ctl

    property string tip: ""
    // Called glyph and not icon on purpose: Button already has a FINAL icon
    // property in Qt Quick Controls, and a property of that name here makes the
    // whole file fail to load ("Cannot override FINAL property"), which takes
    // the entire window down with it.
    property string glyph: "reload"

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
        antialiasing: true

        Connections {
            target: ctl
            function onIconColorChanged() { iconCanvas.requestPaint() }
            function onGlyphChanged() { iconCanvas.requestPaint() }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = ctl.iconColor
            ctx.fillStyle = ctl.iconColor
            ctx.lineWidth = 2.0
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            // The circular arrow browsers use for reload: a ring with a gap at
            // the top right and an arrow head at the top of the ring, pointing
            // the way the ring runs (clockwise). The ring is 14 px across, the
            // same as the fullscreen bracket on the button next to it, and the
            // 2 px stroke takes it from y=1 to y=17 on the 18 px canvas.
            var cx = 9, cy = 9, r = 7.0
            var aEnd = -1.257          // -72 degrees: just right of the top
            var aStart = aEnd - 5.03   // 288 degrees of ring
            ctx.beginPath()
            ctx.arc(cx, cy, r, aStart, aEnd, false)
            ctx.stroke()

            // Head as a filled triangle sitting on the ring's end, along the
            // tangent, so it reads as an arrow rather than a blob.
            var tx = -Math.sin(aEnd), ty = Math.cos(aEnd)
            var nx = -ty, ny = tx
            var px = cx + r * Math.cos(aEnd), py = cy + r * Math.sin(aEnd)
            ctx.beginPath()
            ctx.moveTo(px + tx * 3.2, py + ty * 3.2)
            ctx.lineTo(px - tx * 0.5 + nx * 2.0, py - ty * 0.5 + ny * 2.0)
            ctx.lineTo(px - tx * 0.5 - nx * 2.0, py - ty * 0.5 - ny * 2.0)
            ctx.closePath()
            ctx.fill()
        }
    }

    ToolTip.visible: ctl.hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}
