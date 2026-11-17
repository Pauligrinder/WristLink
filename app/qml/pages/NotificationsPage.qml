import QtQuick 2.6
import Sailfish.Silica 1.0
import org.wristlink 1.0

Page {
    id: page

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: notificationBridge.apps

        header: Column {
            width: listView.width

            PageHeader {
                title: qsTr("Mirrored apps")
                description: qsTr("Choose which phone apps appear on the watch")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                text: notificationBridge.apps.length === 0
                      ? qsTr("Apps appear here after they post a notification on the phone.")
                      : qsTr("New apps are mirrored by default. Turn one off to stop sending it to the watch.")
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                bottomPadding: Theme.paddingLarge
            }
        }

        delegate: TextSwitch {
            width: listView.width
            text: modelData.name
            checked: modelData.enabled
            onClicked: notificationBridge.setAppEnabled(modelData.name, checked)
        }

        ViewPlaceholder {
            enabled: notificationBridge.apps.length === 0
            text: qsTr("No apps yet")
            hintText: qsTr("Trigger a notification on the phone, then return here")
        }

        VerticalScrollDecorator { }
    }

    Connections {
        target: notificationBridge
        onAppsChanged: listView.model = notificationBridge.apps
    }
}
