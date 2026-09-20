import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland

Item {
    id: bar
    property bool showLeft: true
    property bool showRight: true

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 4
        spacing: 10

        Rectangle {
            visible: bar.showLeft
            Layout.fillWidth: true
            Layout.maximumWidth: 580
            Layout.fillHeight: true
            radius: 8
            color: "#aa585c66"
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 10
                spacing: 4

                Repeater {
                    model: 4

                    Rectangle {
                        required property int index
                        readonly property int workspaceId: index + 1
                        Layout.preferredWidth: 28
                        Layout.fillHeight: true
                        radius: 11
                        color: Hyprland.focusedWorkspace?.id === workspaceId
                            ? "#dd7c8eb0" : mouse.containsMouse ? "#667f8897" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: parent.workspaceId
                            color: "#f5f7fb"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Hyprland.dispatch(Hyprland.usingLua
                                ? "hl.dsp.focus({ workspace = " + parent.workspaceId + " })"
                                : "workspace " + parent.workspaceId)
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: Hyprland.activeToplevel?.title ?? "No active window"
                    elide: Text.ElideRight
                    color: "#c3c9d3"
                    font.pixelSize: 12
                }
            }
        }

        Item { Layout.fillWidth: true; visible: bar.showRight }

        Rectangle {
            visible: bar.showRight
            Layout.preferredWidth: clockText.implicitWidth + 24
            Layout.fillHeight: true
            radius: 8
            color: "#aa585c66"

            Text {
                id: clockText
                anchors.centerIn: parent
                text: Qt.formatDateTime(clock.date, "ddd dd MMM  HH:mm")
                color: "#89b4fa"
                font.pixelSize: 12
            }
        }
    }
}
