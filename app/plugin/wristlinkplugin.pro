TARGET = wristlinkplugin
TEMPLATE = lib
CONFIG += plugin c++11
QT = core dbus qml

VERSION = 0.7.1
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

INCLUDEPATH += ../src

SOURCES += \
    wristlinkplugin.cpp \
    ../src/controlclient.cpp

HEADERS += \
    ../src/controlclient.h \
    ../src/socketpath.h

qmldir.files = qmldir
target.path = $$[QT_INSTALL_QML]/org/wristlink
qmldir.path = $$target.path
INSTALLS += target qmldir
