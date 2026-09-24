// Captions and title cards for the demo video. The tour script drives it over Quickshell IPC:
//
//   qs ipc -p overlay.qml call tour caption "<title>" "<detail>" "<keys, e.g. Super+Shift+→>"
//   qs ipc -p overlay.qml call tour hideCaption
//   qs ipc -p overlay.qml call tour card "<title>" "<subtitle>" "<footer>"
//   qs ipc -p overlay.qml call tour hideCard
//
// Both layers sit above the windows and pass all input through.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

ShellRoot {
    id: root

    readonly property color base: "#11111b"
    readonly property color surface: "#1e1e2e"
    readonly property color outline: "#45475a"
    readonly property color text: "#cdd6f4"
    readonly property color subtext: "#a6adc8"
    readonly property color accent: "#89b4fa"
    readonly property string font: "Noto Sans"

    // Caption text is swapped while it is faded out, so a new caption crossfades from the old one.
    property var pending: null
    property var current: ({ title: "", detail: "", keys: [] })
    property bool captionShown: false

    property var card: ({ title: "", subtitle: "", footer: "" })
    property bool cardShown: false

    function showCaption(next) {
        if (captionShown && captionLevel.value > 0) {
            pending = next;
            captionShown = false;
        } else {
            current = next;
            captionShown = true;
        }
    }

    QtObject {
        id: captionLevel
        property real value: root.captionShown ? 1 : 0
        Behavior on value { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        onValueChanged: {
            if (value === 0 && root.pending !== null) {
                root.current = root.pending;
                root.pending = null;
                root.captionShown = true;
            }
        }
    }

    QtObject {
        id: cardLevel
        property real value: root.cardShown ? 1 : 0
        Behavior on value { NumberAnimation { duration: 700; easing.type: Easing.InOutCubic } }
    }

    IpcHandler {
        target: "tour"

        function caption(title: string, detail: string, keys: string): void {
            root.showCaption({ title: title, detail: detail, keys: keys.length > 0 ? keys.split("+") : [] });
        }

        function hideCaption(): void {
            root.pending = null;
            root.captionShown = false;
        }

        function card(title: string, subtitle: string, footer: string): void {
            root.card = { title: title, subtitle: subtitle, footer: footer };
            root.cardShown = true;
        }

        function hideCard(): void {
            root.cardShown = false;
        }
    }

    component KeyCap: Rectangle {
        property alias label: keyText.text
        implicitWidth: Math.max(keyText.implicitWidth + 28, 46)
        implicitHeight: 42
        radius: 9
        color: "#313244"
        border.color: "#585b70"
        border.width: 1

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 4
            radius: 8
            color: "#26263a"
        }

        Text {
            id: keyText
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -1
            color: root.text
            font { family: root.font; pixelSize: 20; weight: Font.DemiBold }
        }
    }

    // Lower third, centered over the main column.
    PanelWindow {
        anchors.bottom: true
        margins.bottom: 64
        implicitWidth: 1880
        implicitHeight: 150
        exclusiveZone: 0
        color: "transparent"
        mask: Region {}
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "hyprtrizone-tour-caption"

        Rectangle {
            id: pill
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: (1 - captionLevel.value) * -18
            opacity: captionLevel.value
            visible: opacity > 0
            width: row.implicitWidth + 64
            height: row.implicitHeight + 40
            radius: 22
            color: Qt.rgba(0.07, 0.07, 0.11, 0.92)
            border.color: root.outline
            border.width: 1

            RowLayout {
                id: row
                anchors.centerIn: parent
                spacing: 34

                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.fillHeight: true
                    radius: 3
                    color: root.accent
                }

                ColumnLayout {
                    spacing: 6

                    Text {
                        text: root.current.title
                        color: root.text
                        font { family: root.font; pixelSize: 34; weight: Font.Bold }
                    }

                    Text {
                        visible: text.length > 0
                        text: root.current.detail
                        color: root.subtext
                        font { family: root.font; pixelSize: 23 }
                    }
                }

                RowLayout {
                    visible: root.current.keys.length > 0
                    spacing: 8

                    Repeater {
                        model: root.current.keys

                        RowLayout {
                            required property string modelData
                            required property int index
                            spacing: 8

                            Text {
                                visible: index > 0
                                text: "+"
                                color: root.subtext
                                font { family: root.font; pixelSize: 22 }
                            }

                            KeyCap { label: modelData }
                        }
                    }
                }
            }
        }
    }

    // Full-screen intro and outro card.
    PanelWindow {
        visible: cardLevel.value > 0
        anchors { top: true; bottom: true; left: true; right: true }
        exclusiveZone: 0
        color: "transparent"
        mask: Region {}
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "hyprtrizone-tour-card"

        Rectangle {
            anchors.fill: parent
            color: root.base
            opacity: cardLevel.value

            ColumnLayout {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: (1 - cardLevel.value) * 24
                spacing: 0

                // The three zones: a quarter, a half, a quarter.
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 56
                    spacing: 10

                    Column {
                        spacing: 10
                        Repeater {
                            model: 2
                            Rectangle { width: 120; height: 55; radius: 8; color: "transparent"; border.color: "#cba6f7"; border.width: 3 }
                        }
                    }
                    Rectangle { width: 250; height: 120; radius: 8; color: "transparent"; border.color: root.accent; border.width: 3 }
                    Column {
                        spacing: 10
                        Row {
                            spacing: 10
                            Repeater {
                                model: 2
                                Rectangle { width: 55; height: 55; radius: 8; color: "transparent"; border.color: "#fab387"; border.width: 3 }
                            }
                        }
                        Rectangle { width: 120; height: 55; radius: 8; color: "transparent"; border.color: "#a6e3a1"; border.width: 3 }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.card.title
                    color: root.text
                    font { family: root.font; pixelSize: 96; weight: Font.Bold; letterSpacing: 2 }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 12
                    text: root.card.subtitle
                    color: root.subtext
                    font { family: root.font; pixelSize: 34 }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 56
                    visible: text.length > 0
                    text: root.card.footer
                    color: root.accent
                    font { family: "monospace"; pixelSize: 24 }
                }
            }
        }
    }
}
