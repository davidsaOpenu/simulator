#ifndef __FORM_1_H
#define __FORM_1_H

#include <QDialog>
#include <QTimer>
#include <QTcpSocket>

#include <qfile.h>
#include <qfiledialog.h>
#include <qdir.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#include "monitor_common.h"

#define REG_IS_WRITE 706


namespace Ui {
class Form1;
}


class Form1 : public QDialog
{
public:
    static Form1* Instance;
    static pthread_mutex_t InstanceLock;
    static pthread_cond_t  InstanceCondition;

    Q_OBJECT

public slots:
    void onTimer();
    void onReceive();

public:
    explicit Form1(QWidget *parent = 0);
    ~Form1();
    void init_var();
    void updateStats(SSDStatistics stats);
    void showEvent(QShowEvent*);
    bool event(QEvent* event);

private slots:
    void btnInit_clicked();
    void btnSave_clicked();

private:
    Ui::Form1 *ui;
    QTcpSocket *sock;
    QTimer *timer;

    /* variables */
    long long int TimeProg;
    int CELL_PROGRAM_DELAY;

    long int GC_NB;
    int GCStarted;
    int gcExchange, gcSector;
    int RandMergeCount, SeqMergeCount;
    long int overwriteCount;

    long int WriteCount, ReadCount;
    long int WriteSecCount, ReadSecCount;
    int TrimEffect, TrimCount;
    long int WriteAmpCount, WrittenCorrectCount;

    double ReadTime, WriteTime;
};


/**
 * Set the text of the object
 * @param obj the object to call setText on
 * @param str the string to use for the formatting
 * @param var the variable to pass the formatter
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT(obj, str, var, buf)    \
    do {                                \
        sprintf(buf, str, var);         \
        obj->setText(buf);              \
    } while(0)
/**
 * Set the text of the object to the integer given
 * @param obj the object to call setText on
 * @param {int} var the integer value
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT_I(obj, var, buf) SET_TEXT(obj, "%d", var, buf)
/**
 * Set the text of the object to the double given
 * @param obj the object to call setText on
 * @param {int} var the integer value
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT_F(obj, var, buf) SET_TEXT(obj, "%0.3f", var, buf)

#endif
