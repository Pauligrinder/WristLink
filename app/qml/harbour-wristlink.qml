import QtQuick 2.6
import Sailfish.Silica 1.0
import "pages"
import "cover" as Cover

ApplicationWindow {
    id: appWindow

    initialPage: Component { HomePage { } }
    cover: Cover.CoverPage { }
    allowedOrientations: Orientation.All

    Component {
        id: confirmationDialog

        Dialog {
            canAccept: true

            SilicaFlickable {
                anchors.fill: parent
                contentHeight: content.height

                Column {
                    id: content
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
