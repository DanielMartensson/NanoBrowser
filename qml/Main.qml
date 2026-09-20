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

    onVisibilityChanged: {
        if (window.visibility !== Window.FullScreen)
            window.edgeHover = false
    }

    Shortcut {
        sequence: "F11"
        onActivated: window.toggleFullscreen()
    }

    Rectangle {
        id: topBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
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
        onUrlChanged: addressBar.text = url.toString()
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
}