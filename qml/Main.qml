import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import NanoBrowser

ApplicationWindow {
    id: window
    width: 1000
    height: 640
    visible: true
    title: webView.title.length > 0 ? "NanoBrowser - " + webView.title : "NanoBrowser"

    palette {
        window: "#0F1117"
        windowText: "#E8EAF2"
        base: "#171B26"
        alternateBase: "#131722"
        text: "#E8EAF2"
        button: "#1B2030"
        buttonText: "#E8EAF2"
        highlight: "#7C5CFC"
        highlightedText: "#FFFFFF"
        placeholderText: "#6B7280"
    }

    readonly property bool isFullscreen: window.visibility === Window.FullScreen
    readonly property bool isMaximized: window.visibility === Window.Maximized
    property bool edgeHover: false
    property int barHeight: 62

    function loadUrl() {
        var text = addressBar.text.trim()
        if (text.length === 0)
            return
        if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(text))
            text = "https://" + text
        webView.url = text
    }

    function toggleFullscreen() {
        window.visibility = window.isFullscreen ? Window.Windowed : Window.FullScreen
    }

    // Maximize is xdg_toplevel.set_maximized, which Weston's desktop shell
    // implements itself (desktop-shell/shell.c: set_maximized_position() and the
    // maximize_binding on Super+Shift+M). It is not fullscreen: the compositor
    // keeps the panel and the other windows in view.
    function toggleMaximized() {
        if (window.isMaximized)
            window.showNormal()
        else
            window.showMaximized()
    }

    onVisibilityChanged: {
        if (window.visibility !== Window.FullScreen)
            window.edgeHover = false
    }

    Shortcut {
        sequence: "F11"
        onActivated: window.toggleFullscreen()
    }

    // Weston's desktop shell on this image has no xdg-decoration support, and Qt 6
    // cannot ask for server-side decorations either, so the window draws its own
    // slim title bar. Without it the window could not be moved, minimized or
    // closed at all: dragging the bar asks the compositor for an interactive move
    // (xdg_toplevel.move), which is what CSD clients do everywhere.
    //
    // Minimize is xdg_toplevel.set_minimized, which Weston's desktop shell
    // implements by moving the view into its minimized layer - a hidden window,
    // not a stopped one. It comes back with the compositor's switcher, which the
    // shell binds to Super+Tab (binding_modifier defaults to Super, see
    // desktop-shell/shell.c: switcher_next() puts the minimized views in the list
    // and switcher_destroy() restores the ones that were not picked).
    //
    // The three buttons draw their icons instead of using font glyphs: the
    // Liberation fonts on this image have no U+2715 (the close cross came out as
    // an empty square) and no U+25A1 for a maximize frame.
    Rectangle {
        id: titleBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: window.isFullscreen && !window.edgeHover ? 0 : 30
        visible: height > 1
        opacity: height === 0 ? 0 : 1
        clip: true
        color: "#101521"

        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }
        Behavior on opacity { NumberAnimation { duration: 180 } }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: window.startSystemMove()
        }

        Text {
            anchors {
                left: parent.left
                leftMargin: 12
                right: minimizeButton.left
                rightMargin: 10
                verticalCenter: parent.verticalCenter
            }
            text: webView.title.length > 0 ? webView.title : "NanoBrowser"
            color: "#98A1B3"
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        WindowButton {
            id: minimizeButton
            anchors {
                right: maximizeButton.left
                rightMargin: 4
                verticalCenter: parent.verticalCenter
            }
            kind: "minimize"
            tip: "Minimize NanoBrowser (Super+Tab brings it back)"
            onClicked: window.showMinimized()
        }

        WindowButton {
            id: maximizeButton
            anchors {
                right: closeButton.left
                rightMargin: 4
                verticalCenter: parent.verticalCenter
            }
            kind: window.isMaximized ? "restore" : "maximize"
            tip: window.isMaximized ? "Restore NanoBrowser (Super+Shift+M)"
                                   : "Maximize NanoBrowser (Super+Shift+M)"
            onClicked: window.toggleMaximized()
        }

        WindowButton {
            id: closeButton
            anchors {
                right: parent.right
                rightMargin: 6
                verticalCenter: parent.verticalCenter
            }
            kind: "close"
            tip: "Close NanoBrowser"
            onClicked: window.close()
        }
    }

    Rectangle {
        id: topBar
        anchors { top: titleBar.bottom; left: parent.left; right: parent.right }
        height: window.isFullscreen && !window.edgeHover ? 0 : window.barHeight
        visible: height > 1
        opacity: height === 0 ? 0 : 1
        clip: true
        color: "#151A26"

        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Rectangle {
            width: parent.width
            height: 1
            color: "#7C5CFC"
            opacity: 0.4
            anchors.bottom: parent.bottom
        }

        Rectangle {
            width: parent.width
            height: 2
            anchors.bottom: parent.bottom
            clip: true
            visible: webView.loading

            Rectangle {
                width: parent.width * (webView.loadProgress / 100)
                height: parent.height
                radius: 1
                color: "#7C5CFC"

                Rectangle {
                    width: parent.width * 0.45
                    height: parent.height
                    radius: 1
                    color: "#B9A8FF"
                    opacity: 0.7
                    anchors.right: parent.right
                }
            }
        }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
                topMargin: 10
                bottomMargin: 12
            }
            spacing: 8

            NavButton {
                text: "\u2190"
                tip: "Back"
                enabled: webView.canGoBack
                onClicked: webView.goBack()
            }

            NavButton {
                text: "\u2192"
                tip: "Forward"
                enabled: webView.canGoForward
                onClicked: webView.goForward()
            }

            FullscreenButton {
                active: window.isFullscreen
                tip: window.isFullscreen ? "Exit fullscreen (F11)" : "Enter fullscreen (F11)"
                onClicked: window.toggleFullscreen()
            }

            // The toolbar had a play button whose triangle (U+25B6) is missing
            // from the fonts on this image, so the slot is used for a reload
            // button instead. Playing video on the VPU is YtGst's job.
            IconButton {
                glyph: "reload"
                tip: "Reload this page"
                onClicked: webView.reload()
            }

            Rectangle {
                id: addressField
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: 18
                color: "#1C2231"
                border.color: (addressBar.activeFocus || addressBar.hovered) ? "#7C5CFC" : "#2A3142"
                border.width: 1

                Behavior on border.color { ColorAnimation { duration: 120 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: addressBar.text.toString().startsWith("https://") ? "#2ECC71" : "#E74C3C"
                    }

                    TextField {
                        id: addressBar
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        background: null
                        color: "#E8EAF2"
                        placeholderText: "Search or enter address"
                        font.pixelSize: 13
                        selectByMouse: true
                        verticalAlignment: Text.AlignVCenter
                        onAccepted: window.loadUrl()
                    }

                    Text {
                        id: progressText
                        text: webView.loading ? webView.loadProgress + "%" : ""
                        color: "#7C5CFC"
                        font.pixelSize: 11
                        font.bold: true
                        width: 34
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }
    }

    // There is nothing to announce when Qt Quick composites on Vulkan - that is
    // what this board is supposed to do, and which API was picked is written to
    // graphics.log on every start. The banner is only left for the case that
    // matters: Vulkan was not available and Qt fell back to OpenGL, which is
    // worth a warning because everything then runs through a driver that is not
    // the intended one.
    Rectangle {
        id: graphicsBanner
        anchors {
            top: topBar.bottom
            left: parent.left
            right: parent.right
        }
        height: 44
        z: 50
        clip: true
        visible: graphicsApi !== "Vulkan"
        opacity: 0
        color: "#8A4A00"

        Behavior on opacity { NumberAnimation { duration: 250 } }

        Component.onCompleted: graphicsBanner.opacity = 1

        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            color: "#FFFFFF"
            font.pixelSize: 13
            font.bold: true
            text: "OpenGL fallback \u2014 " + graphicsReason
        }
    }

    WebEngineProfile {
        id: browserProfile
        storageName: "nanobrowser"
        offTheRecord: false
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
    }

    WebEngineView {
        id: webView
        profile: browserProfile
        anchors {
            top: topBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        settings.webGLEnabled: true
        url: "https://duckduckgo.com/"
        onUrlChanged: {
            addressBar.text = url.toString()
        }
        onJavaScriptConsoleMessage: {}
    }

    MouseArea {
        z: 100
        width: parent.width
        height: 90
        hoverEnabled: true
        visible: window.isFullscreen

        onPositionChanged: window.edgeHover = mouseY < 45
        onExited: window.edgeHover = false
    }

    // There is no compositor-side frame either, so the window also resizes
    // itself: pressing an edge asks for an interactive resize
    // (xdg_toplevel.resize).
    MouseArea {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: 6
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        visible: !window.isFullscreen
        onPressed: window.startSystemResize(Qt.LeftEdge)
    }

    MouseArea {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 6
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        visible: !window.isFullscreen
        onPressed: window.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 6
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        visible: !window.isFullscreen
        onPressed: window.startSystemResize(Qt.BottomEdge)
    }

    MouseArea {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 6
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        visible: !window.isFullscreen
        onPressed: window.startSystemResize(Qt.TopEdge)
    }
}
