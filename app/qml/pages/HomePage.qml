import QtQuick 2.6
import Sailfish.Silica 1.0
import org.wristlink 1.0

Page {
    id: page

    function pairedWatch() {
        for (var i = 0; i < bluez.devices.length; ++i) {
            if (bluez.devices[i].paired)
                return bluez.devices[i]
        }
        return null
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("About WristLink")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Mirrored notifications")
                onClicked: pageStack.push(Qt.resolvedUrl("NotificationsPage.qml"))
            }
            MenuItem {
                text: qsTr("Find a watch")
                onClicked: pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
            }
        }

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("WristLink")
                description: wristlink.connected ? qsTr("Wear OS companion")
                                                 : wristlink.status
            }

            Item {
                width: parent.width
                height: Theme.itemSizeHuge * 2.2

                Rectangle {
                    id: watchBody
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.5, Theme.itemSizeHuge * 1.7)
                    height: width
                    radius: width / 2
                    color: Theme.rgba(Theme.highlightColor, 0.13)
                    border.width: Theme.paddingSmall
                    border.color: Theme.highlightColor

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.paddingMedium
                        radius: width / 2
                        color: Theme.rgba(Theme.primaryColor, 0.06)

                        Label {
                            anchors.centerIn: parent
                            text: gattServer.watchLinked ? "✓" : "⌚"
                            color: Theme.highlightColor
                            font.pixelSize: Theme.fontSizeHuge * 1.25
                        }
                    }
                }
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                text: pairedWatch() ? pairedWatch().name : qsTr("No watch paired")
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.primaryColor
                wrapMode: Text.Wrap
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                text: gattServer.watchLinked
                      ? qsTr("Connected")
                      : gattServer.status
                color: gattServer.watchLinked ? Theme.highlightColor : Theme.secondaryColor
                wrapMode: Text.Wrap
            }

            Label {
                visible: gattServer.detail.length > 0
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                text: gattServer.detail
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: pairedWatch()
                      ? (pairedWatch().connected ? qsTr("Disconnect") : qsTr("Connect"))
                      : qsTr("Start")
                enabled: !bluez.busy
                onClicked: {
                    var watch = pairedWatch()
                    if (!watch) {
                        pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
                    } else if (watch.connected) {
                        bluez.disconnectDevice(watch.path)
                    } else {
                        bluez.connectDevice(watch.path)
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthSmall
                text: qsTr("Restart link")
                onClicked: wristlink.restartLink()
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: bluez.busy
                size: BusyIndicatorSize.Small
            }

            Label {
                visible: bluez.statusMessage.length > 0
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                text: bluez.statusMessage
                color: Theme.secondaryHighlightColor
                wrapMode: Text.Wrap
            }

            SectionHeader { text: qsTr("Today") }

            Label {
                visible: watchHealth.status.length > 0
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: watchHealth.status
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
            }

            Column {
                width: parent.width

                DetailItem {
                    label: qsTr("Steps")
                    value: watchHealth.stepsValid ? Number(watchHealth.steps).toLocaleString(Qt.locale(), "f", 0)
                                                  : "—"
                }
                DetailItem {
                    label: qsTr("Heart rate")
                    value: watchHealth.heartRateValid ? qsTr("%1 bpm").arg(watchHealth.heartRate)
                                                      : "—"
                }
                DetailItem {
                    label: qsTr("Calories")
                    value: watchHealth.caloriesValid ? qsTr("%1 kcal").arg(watchHealth.calories)
                                                     : "—"
                }
                DetailItem {
                    label: qsTr("Distance")
                    value: watchHealth.distanceValid
                           ? (watchHealth.distanceMeters >= 1000
                              ? qsTr("%1 km").arg((watchHealth.distanceMeters / 1000.0).toFixed(2))
                              : qsTr("%1 m").arg(watchHealth.distanceMeters))
                           : "—"
                }
                DetailItem {
                    label: qsTr("Floors")
                    value: watchHealth.floorsValid ? String(watchHealth.floors) : "—"
                }
                DetailItem {
                    label: qsTr("Watch battery")
                    value: watchHealth.batteryValid ? qsTr("%1%").arg(watchHealth.batteryPercent)
                                                    : "—"
                }
                DetailItem {
                    label: qsTr("Updated")
                    value: watchHealth.lastUpdated.length > 0 ? watchHealth.lastUpdated : "—"
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Refresh stats")
                enabled: pairedWatch() && !watchHealth.syncing
                onClicked: {
                    if (pairedWatch())
                        watchHealth.sync(pairedWatch().path)
                }
            }

            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("NotificationsPage.qml"))

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Choose mirrored apps")
                    color: Theme.primaryColor
                }
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: watchHealth.syncing
                size: BusyIndicatorSize.Small
            }

            SectionHeader { text: qsTr("Diagnostics") }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: bluez.agentStatus.length > 0 ? bluez.agentStatus
                                                   : qsTr("Pairing agent not registered yet")
                color: bluez.agentReady ? Theme.secondaryColor : Theme.errorColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: gattServer.status
                color: gattServer.ready ? Theme.secondaryColor : Theme.errorColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: callMonitor.status
                color: callMonitor.ringing ? Theme.highlightColor : Theme.secondaryColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: notificationBridge.status
                color: notificationBridge.ready ? Theme.secondaryColor : Theme.errorColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Label {
                visible: watchHealth.lastError.length > 0
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: watchHealth.lastError
                color: Theme.errorColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            SectionHeader { text: qsTr("Settings") }

            TextSwitch {
                width: parent.width
                text: qsTr("Service enabled")
                description: qsTr("When off, WristLink does not run in the background")
                checked: wristlink.serviceEnabled
                onClicked: wristlink.serviceEnabled = checked
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Group notifications by app")
                description: qsTr("Show one notification per app, with the latest text and how many others are waiting")
                checked: notificationBridge.groupByApp
                onClicked: notificationBridge.groupByApp = checked
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Only while locked")
                description: qsTr("Do not send notifications to the watch while the phone is unlocked")
                checked: notificationBridge.skipWhenUnlocked
                onClicked: notificationBridge.skipWhenUnlocked = checked
            }

            SectionHeader { text: qsTr("Watch") }

            BackgroundItem {
                width: parent.width
                enabled: pairedWatch() !== null
                onClicked: {
                    var watch = pairedWatch()
                    if (watch)
                        bluez.removeDevice(watch.path)
                }

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Remove paired watch")
                    color: parent.enabled ? Theme.primaryColor : Theme.secondaryColor
                }
            }

            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(uninstallDialog)

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Uninstall WristLink")
                    color: Theme.primaryColor
                }
            }
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: uninstallDialog

        Dialog {
            canAccept: true

            SilicaFlickable {
                anchors.fill: parent
                contentHeight: uninstallContent.height

                Column {
                    id: uninstallContent
                    width: parent.width
                    spacing: Theme.paddingLarge

                    DialogHeader {
                        acceptText: qsTr("Uninstall")
                        cancelText: qsTr("Cancel")
                    }

                    Label {
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        x: Theme.horizontalPageMargin
                        text: qsTr("Are you sure you want to uninstall the service? Your configuration will remain in storage")
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeLarge
                        wrapMode: Text.Wrap
                    }
                }
            }

            onAccepted: wristlink.uninstall()
        }
    }

    Component {
        id: confirmationDialog

        Dialog {
            canAccept: true

            SilicaFlickable {
                anchors.fill: parent
                contentHeight: dialogContent.height

                Column {
                    id: dialogContent
                    width: parent.width
                    spacing: Theme.paddingLarge

                    DialogHeader {
                        acceptText: qsTr("Match")
                        cancelText: qsTr("Cancel")
                    }

                    Label {
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        x: Theme.horizontalPageMargin
                        text: qsTr("Does this number match your watch?")
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeLarge
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: bluez.confirmationCode
                        font.pixelSize: Theme.fontSizeHuge
                        font.bold: true
                        color: Theme.primaryColor
                    }

                    Label {
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        x: Theme.horizontalPageMargin
                        horizontalAlignment: Text.AlignHCenter
                        text: bluez.confirmationDevice
                        color: Theme.secondaryColor
                    }
                }
            }

            onAccepted: bluez.acceptPairing()
            onRejected: bluez.rejectPairing()
        }
    }

    Connections {
        target: bluez
        onConfirmationRequested: pageStack.push(confirmationDialog)
    }
}
