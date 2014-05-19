/****************************************************************************
** Form interface generated from reading ui file 'form1.ui'
**
** Created: Thu Dec 26 07:08:57 2013
**
** WARNING! All changes made in this file will be lost!
****************************************************************************/

#ifndef FORM1_H
#define FORM1_H

#include <qvariant.h>
#include <qdialog.h>
#include "qsocket.h"
#include "qtimer.h"
#include "qfile.h"

class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QSpacerItem;
class QPushButton;
class QGroupBox;
class QLabel;
class QLineEdit;
class QTextEdit;

class Form1 : public QDialog
{
    Q_OBJECT

public:
    Form1( QWidget* parent = 0, const char* name = 0, bool modal = FALSE, WFlags fl = 0 );
    ~Form1();

    QPushButton* btnInit;
    QGroupBox* groupBox1;
    QLabel* textLabel1;
    QLineEdit* txtWriteCount;
    QLabel* textLabel1_3_3_2;
    QLabel* textLabel1_3_3;
    QLineEdit* txtWriteSpeed;
    QLineEdit* txtReadSpeed;
    QLineEdit* txtWriteSectorCount;
    QLineEdit* txtReadSectorCount;
    QLabel* textLabel1_3;
    QLabel* textLabel1_2;
    QLineEdit* txtReadCount;
    QGroupBox* groupBox2;
    QLabel* textLabel1_2_2;
    QLineEdit* txtGarbageCollectionNB;
    QLineEdit* txtGarbageCollection;
    QLineEdit* txtGCStart;
    QLabel* textLabel1_4;
    QLabel* textLabel2;
    QLabel* textLabel3;
    QGroupBox* groupBox3;
    QLabel* textLabel1_2_2_3_2;
    QLabel* textLabel4;
    QLineEdit* txtTrimCount;
    QLineEdit* txtTrimEffect;
    QLabel* textLabel5;
    QLabel* textLabel1_2_2_3_2_2_2_2_2_2;
    QLineEdit* txtWriteAmpCount;
    QLineEdit* txtSsdUtil;
    QLabel* textLabel1_2_3;
    QLabel* textLabel1_2_2_3_2_2_2_2_2_2_2;
    QLineEdit* txtWrittenBlockCount;
    QLabel* textLabel1_2_2_3_2_2_2_2_2;
    QLineEdit* txtWrittenBlockCount_2;
    QTextEdit* txtDebug;
    QLabel* textLabel1_5;
    QLineEdit* txtTimeProg;
    QLabel* textLabel1_2_2_3_2_2_2_2_2_2_2_2;
    QPushButton* btnSave;

    virtual void init();

public slots:
    virtual void init_var();
    virtual void btnConnect_clicked();
    virtual void onReceive();
    virtual void btnInit_clicked();
    virtual void btnSave_clicked();

protected:

protected slots:
    virtual void languageChange();

private:
    long long int *access_time_reg_mon;
    int *access_type_reg_mon;
    int CELL_PROGRAM_DELAY;
    long int GC_NB;
    int GCStarted;
    long int ReadSectorCount;
    long int WriteAmpCount;
    long int WrittenCorrectCount;
    long int WriteSectorCount;
    int TrimEffect;
    int TrimCount;
    int WriteCount;
    int PowCount;
    QSocket *sock;
    int ReadCount;
    int RandMergeCount;
    int SeqMergeCount;
    int ExchangeCount;
    long int OverwriteCount;
    QTimer* timer;
    long long int TimeProg;
    double WriteTime;
    double ReadTime;
    unsigned int WriteSecCount;
    unsigned int ReadSecCount;

private slots:
    virtual void onTimer();

};

#endif // FORM1_H
