import QtQuick
import Quickshell
import Quickshell.Wayland

ShellRoot {
    id: root

    // Match hyprtrizone-demo.lua. The panel geometry is configured here.
    readonly property real masterFactor: 0.5
    readonly property int headerHeight: 34
    readonly property bool split: Quickshell.env("QS_BAR_MODE") !== "top"

    Variants {
        model: Quickshell.screens

        delegate: Scope {
            required property var modelData

            PanelWindow {
                id: leftPanel
                screen: modelData
                anchors { top: true; left: true; right: !root.split }
                implicitWidth: Math.round(screen.width * (1 - root.masterFactor) / 2)
                implicitHeight: root.headerHeight
                exclusiveZone: root.split ? 0 : root.headerHeight
                color: "transparent"
                WlrLayershell.namespace: "hyprtrizone-demo-left"

                BarContents {
                    anchors.fill: parent
                    showLeft: true
                    showRight: !root.split
                }
            }

            PanelWindow {
                screen: modelData
                visible: root.split
                anchors { top: true; right: true }
                implicitWidth: Math.round(screen.width * (1 - root.masterFactor) / 2)
                implicitHeight: root.headerHeight
                exclusiveZone: 0
                color: "transparent"
                WlrLayershell.namespace: "hyprtrizone-demo-right"

                BarContents {
                    anchors.fill: parent
                    showLeft: false
                    showRight: true
                }
            }
        }
    }
}
