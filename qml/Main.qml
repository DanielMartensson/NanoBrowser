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

    // Hardware (VPU) playback of YouTube pages, implemented in src/main.cpp.
    property bool youtubeOffer: false
    property string hwErrorText: ""
    // The URL the page itself reports. webView.url follows the loads the app
    // asks for, but YouTube moves between videos with history.pushState, which
    // never loads a page - and a watch page opened any other way (a link, the
    // DevTools protocol) leaves webView.url pointing at the page before it. The
    // offer bar and the hardware player both need the page the user is looking
    // at, so they ask the page.
    property string pageUrl: ""
    readonly property bool youtubePage: window.isPlayablePage(window.pageUrl)
    readonly property bool hwCardVisible: hardwarePlayer.status.length > 0
                                           || hardwarePlayer.title.length > 0
                                           || window.hwErrorText.length > 0

    function refreshPageUrl() {
        webView.runJavaScript("location.href", function (href) {
            window.pageUrl = href ? href.toString() : ""
            window.youtubeOffer = window.isPlayablePage(window.pageUrl)
        })
    }

    function isPlayablePage(u) {
        var s = u.toString()
        return s.indexOf("youtube.com/watch") >= 0
            || s.indexOf("youtube.com/shorts/") >= 0
            || s.indexOf("youtube.com/live/") >= 0
            || s.indexOf("youtu.be/") >= 0
    }

    // Hands the page over to the VPU: the video is streamed straight from
    // YouTube and decoded by v4l2slh264dec (H.264), audio goes to PipeWire.
    // The page's own player is paused and muted so that there is only one
    // audio source and the CPU does not decode the video a second time.
    function playWithHardware() {
        window.youtubeOffer = false
        window.hwErrorText = ""
        webView.runJavaScript("document.querySelectorAll('video').forEach(function (v) { v.pause(); v.muted = true })")
        hardwarePlayer.play(window.pageUrl.length > 0 ? window.pageUrl : webView.url)
    }

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

    Shortcut {
        sequence: "F8"
        onActivated: {
            if (window.youtubePage)
                window.playWithHardware()
        }
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

            // The hardware player still has its F8 shortcut and the offer bar
            // that appears on a video page; the toolbar had a play button whose
            // triangle (U+25B6) is missing from the fonts on this image, so the
            // slot is used for a reload button instead.
            IconButton {
                icon: "reload"
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

    // Hardware (VPU) playback: shows what is being resolved/decoded and offers
    // a stop button.
    Rectangle {
        id: hardwareCard
        z: 60
        anchors {
            top: topBar.bottom
            horizontalCenter: parent.horizontalCenter
            topMargin: 10
        }
        width: Math.min(window.width - 24, 620)
        height: cardRow.implicitHeight + 20
        radius: 12
        color: "#1B2030"
        border.width: 1
        border.color: window.hwErrorText.length > 0 ? "#E74C3C"
                      : (hardwarePlayer.playing ? "#2ECC71" : "#7C5CFC")
        opacity: window.hwCardVisible ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        RowLayout {
            id: cardRow
            anchors {
                fill: parent
                leftMargin: 14
                rightMargin: 10
                topMargin: 10
                bottomMargin: 10
            }
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.bold: true
                    color: window.hwErrorText.length > 0 ? "#FF9A8B" : "#E8EAF2"
                    text: window.hwErrorText.length > 0
                          ? window.hwErrorText
                          : (hardwarePlayer.status.length > 0
                             ? hardwarePlayer.status
                             : "Hardware decoder: v4l2slh264dec (VPU)")
                }

                Text {
                    Layout.fillWidth: true
                    visible: hardwarePlayer.title.length > 0
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: "#98A1B3"
                    text: hardwarePlayer.title
                }
            }

            IconButton {
                icon: "stop"
                tip: "Stop hardware playback"
                onClicked: hardwarePlayer.stop()
            }
        }
    }

    // Offered whenever a YouTube video page is opened.
    Rectangle {
        id: youtubeBar
        z: 55
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 58
        color: "#241B4D"
        border.color: "#7C5CFC"
        border.width: 1
        opacity: (window.youtubeOffer && hardwarePlayer.available && !window.hwCardVisible) ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 14
                rightMargin: 12
                topMargin: 9
                bottomMargin: 9
            }
            spacing: 10

            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: "#E8EAF2"
                font.pixelSize: 13
                text: "YouTube video \u2014 play it with the hardware decoder (VPU, H.264)"
            }

            IconButton {
                icon: "play"
                tip: "Play with the VPU"
                onClicked: window.playWithHardware()
            }

            IconButton {
                icon: "close"
                tip: "Not now"
                onClicked: window.youtubeOffer = false
            }
        }
    }

    Timer {
        id: hwErrorTimer
        interval: 8000
        onTriggered: window.hwErrorText = ""
    }

    Connections {
        target: hardwarePlayer

        function onFailed(reason) {
            window.hwErrorText = reason
            hwErrorTimer.restart()
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
            window.pageUrl = url.toString()
            window.youtubeOffer = window.isPlayablePage(window.pageUrl)
        }
        onJavaScriptConsoleMessage: {}
    }

    // YouTube moves between videos without loading a page, so poll what the
    // page reports itself; a second costs nothing and keeps the offer bar and
    // the play button in step with what is on screen.
    Timer {
        interval: 1000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: window.refreshPageUrl()
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
