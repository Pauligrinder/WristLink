import QtQuick 2.6
import Sailfish.Silica 1.0

CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "⌚"
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeHuge
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("WristLink")
            color: Theme.primaryColor
            font.pixelSize: Theme.fontSizeLarge
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: watchHealth.stepsValid ? Number(watchHealth.steps).toLocaleString(Qt.locale(), "f", 0)
                                         : (bluez.discovering ? qsTr("Scanning")
                                                              : (bluez.busy ? qsTr("Working") : qsTr("Watch companion")))
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
