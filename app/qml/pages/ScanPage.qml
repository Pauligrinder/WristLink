import QtQuick 2.6
import Sailfish.Silica 1.0
import org.wristlink 1.0

Page {
    id: page

    Component.onCompleted: bluez.startDiscovery()
    Component.onDestruction: {
        if (bluez.discovering)
            bluez.stopDiscovery()
    }

    SilicaListView {
        id: deviceList
        anchors.fill: parent
        model: bluez.devices

        header: Column {
            width: deviceList.width

            PageHeader {
                title: qsTr("Choose your watch")
                description: bluez.discovering ? qsTr("Scanning nearby devices")
                                               : qsTr("Scan paused")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                text: qsTr("On the watch, open Settings › Connections › Bluetooth and keep the pairing screen visible. If Sailfish asks you to confirm a code, accept it there. After pairing, connect the watch here and leave WristLink open on it.")
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }
        }

        delegate: BackgroundItem {
            id: deviceItem
            width: deviceList.width
            height: Math.max(Theme.itemSizeLarge, details.height + 2 * Theme.paddingMedium)
            enabled: !bluez.busy
            onClicked: {
                if (!modelData.paired)
                    bluez.pairDevice(modelData.path)
                else if (!modelData.connected)
                    bluez.connectDevice(modelData.path)
                else
                    bluez.disconnectDevice(modelData.path)
            }

            Rectangle {
                id: badge
                width: Theme.itemSizeMedium
                height: width
                radius: width / 2
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.rgba(modelData.wearable ? Theme.highlightColor
                                                     : Theme.secondaryColor, 0.16)

                Label {
                    anchors.centerIn: parent
                    text: modelData.wearable ? "⌚" : "B"
                    color: modelData.wearable ? Theme.highlightColor : Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeLarge
                }
            }

            Column {
                id: details
                anchors {
                    left: badge.right
                    leftMargin: Theme.paddingMedium
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    text: modelData.name
                    truncationMode: TruncationMode.Fade
                    color: deviceItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                Label {
                    width: parent.width
                    text: modelData.connected ? qsTr("Connected")
                                              : (modelData.paired ? qsTr("Paired — tap to connect")
                                                                  : (modelData.wearable ? qsTr("Galaxy Watch — tap to pair")
                                                                                        : modelData.address))
                    truncationMode: TruncationMode.Fade
                    color: modelData.wearable ? Theme.secondaryHighlightColor
                                              : Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        ViewPlaceholder {
            enabled: deviceList.count === 0
            text: bluez.bluetoothAvailable ? qsTr("Looking for watches…")
                                           : qsTr("Bluetooth is off")
            hintText: bluez.bluetoothAvailable
                      ? qsTr("Make sure the watch is in pairing mode")
                      : qsTr("Enable Bluetooth in Sailfish settings")
        }

        PullDownMenu {
            MenuItem {
                text: bluez.discovering ? qsTr("Stop scanning") : qsTr("Scan again")
                onClicked: bluez.discovering ? bluez.stopDiscovery() : bluez.startDiscovery()
            }
        }

        BusyIndicator {
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: Theme.paddingLarge
            }
            running: bluez.discovering || bluez.busy
            size: BusyIndicatorSize.Medium
        }

        VerticalScrollDecorator { }
    }
}
