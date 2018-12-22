QT       += core gui network widgets

TEMPLATE	= app
LANGUAGE	= C++

QMAKE_LFLAGS += ssd_log_monitor.c

INCLUDEPATH	+= .

HEADERS	+= form1.h

SOURCES	+= main.cpp \
           form1.cpp \
           monitor_test.cpp

FORMS	 = form1.ui

