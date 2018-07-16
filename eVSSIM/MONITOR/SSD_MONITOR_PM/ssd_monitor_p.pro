QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ssd_monitor_p



TEMPLATE	= app
LANGUAGE	= C++

#INCLUDEPATH	+= .

HEADERS	+= form1.ui.h

SOURCES	+= main.cpp form1.cpp\
	monitor_test.cpp

FORMS	= form1.ui


