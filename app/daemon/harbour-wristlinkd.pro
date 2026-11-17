TARGET = harbour-wristlinkd
TEMPLATE = app
QT = core dbus
CONFIG += c++11 console link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += dbus-1

SOURCES += \
    ../src/daemonmain.cpp \
    ../src/controlserver.cpp \
    ../src/bluezmanager.cpp \
    ../src/watchhealth.cpp \
    ../src/notificationbridge.cpp \
    ../src/gattserver.cpp \
    ../src/callmonitor.cpp

HEADERS += \
    ../src/controlserver.h \
    ../src/bluezmanager.h \
    ../src/watchhealth.h \
    ../src/notificationbridge.h \
    ../src/gattserver.h \
    ../src/callmonitor.h \
    ../src/socketpath.h

target.path = /usr/bin
systemd.files = ../harbour-wristlink.service
systemd.path = /usr/lib/systemd/user
INSTALLS += target systemd
