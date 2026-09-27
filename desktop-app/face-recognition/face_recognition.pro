#-------------------------------------------------
#
# Project created by QtCreator 2021-09-08T20:37:18
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = face_recognition
TEMPLATE = app

#INCLUDEPATH += /usr/local/include/ \
#                                /usr/local/include/opencv2/ \
#                                /usr/local/include/opencv2/opencv4 \
#                                /usr/local/include/opencv2/opencv4/core \
#                                 /usr/local/include/opencv2/core
#LIBS += /usr/local/lib/libopencv_*


INCLUDEPATH += /opt/opencv/opencv-3.4.5/install/include/opencv \
/opt/opencv/opencv-3.4.5/install/include/opencv2 \
/opt/opencv/opencv-3.4.5/install/include \
/opt/opencv/opencv-3.4.5/install/include/opencv2/core \
/opt/opencv/opencv_contrib-3.4.5/modules/face/include/opencv2/
LIBS += /opt/opencv/opencv-3.4.5/install/lib/libopencv_*


# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


SOURCES += \
        main.cpp \
        widget.cpp \
    setting.cpp \
    future_hotel_api.cpp \
    jx_uart_send.cpp \
    lldk.cpp

HEADERS += \
        widget.h \
    setting.h \
    lldk.h \
    future_hotel_api.h \
    jx_uart_send.h

FORMS += \
        widget.ui \
    setting.ui \
    lldk.ui

RESOURCES += \
    source.qrc
