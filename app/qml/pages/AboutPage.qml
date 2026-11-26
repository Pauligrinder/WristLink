import QtQuick 2.6
import Sailfish.Silica 1.0
import org.wristlink 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: qsTr("About WristLink") }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: qsTr("WristLink is an independent Sailfish OS companion for discovering, pairing, and reading health stats from Bluetooth watches.")
                wrapMode: Text.Wrap
                color: Theme.primaryColor
            }

            SectionHeader { text: qsTr("Steps and health data") }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: qsTr("WristLink keeps the paired Bluetooth connection and uses it only when something happens. Notifications and calls go to the watch as they arrive. Steps and the other health numbers are read at the same time, and otherwise about every half hour. Leave WristLink open on the watch; it waits for this phone instead of searching for it.")
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: qsTr("Bluetooth pairing uses the standard BlueZ service. If WristLink cannot register its own pairing agent, confirm the code in the Sailfish Bluetooth dialog. Vendor watch apps, accounts, and health sync (Samsung, Google, OnePlus, and others) are not provided by this app.")
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
            }

            DetailItem {
                label: qsTr("Version")
                value: appVersion
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: qsTr("Not affiliated with Samsung Electronics, Google, or Jolla.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
            }
        }

        VerticalScrollDecorator { }
    }
}
