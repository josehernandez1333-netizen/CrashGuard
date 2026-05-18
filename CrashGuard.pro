QT += core
CONFIG += console c++11
CONFIG -= app_bundle

SOURCES += main.cpp \
           imu/imu.cpp \
           modem/modem.cpp \
           cloud/cloud_client.cpp

HEADERS += imu/imu.h \
           modem/modem.h \
           cloud/cloud_client.h

LIBS += -lcurl
