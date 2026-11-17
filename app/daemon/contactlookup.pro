TARGET = harbour-wristlink-contact
TEMPLATE = app
QT = core
CONFIG += c++11 console link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += sqlite3

SOURCES += contactlookup.cpp

target.path = /usr/libexec
INSTALLS += target
