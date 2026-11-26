TARGET = harbour-wristlink

# sailfishapp.prf enables link_pkgconfig and appends sailfishapp to PKGCONFIG;
# setting link_pkgconfig here would load it before that append and drop -lsailfishapp.
CONFIG += sailfishapp
CONFIG += c++11
QT += dbus

VERSION = 0.7.2
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

SOURCES += \
    src/harbour-wristlink.cpp \
    src/controlclient.cpp

HEADERS += \
    src/controlclient.h \
    src/socketpath.h

DISTFILES += \
    rpm/harbour-wristlink.spec \
    harbour-wristlink.desktop \
    harbour-wristlink.service \
    qml/harbour-wristlink.qml \
    qml/cover/CoverPage.qml \
    qml/pages/HomePage.qml \
    qml/pages/ScanPage.qml \
    qml/pages/AboutPage.qml \
    qml/pages/NotificationsPage.qml \
    settings/harbour-wristlink.json \
    plugin/qmldir \
    plugin/wristlinkplugin.cpp \
    plugin/wristlinkplugin.pro

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

appicon.files = icons/harbour-wristlink.svg
appicon.path = /usr/share/icons/hicolor/scalable/apps

settingsentry.files = settings/harbour-wristlink.json
settingsentry.path = /usr/share/jolla-settings/entries

INSTALLS += appicon settingsentry
