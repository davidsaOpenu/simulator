/********************************************************************************
** Form generated from reading UI file 'form1.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_FORM1_H
#define UI_FORM1_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTextEdit>

#include <QDialog>
#include <QTimer>
#include <QTcpSocket>


#include <pthread.h>

namespace Ui {
class Form1;
}

class Form1 : public QDialog
{
    Q_OBJECT

public slots:
    void onTimer();
    void onReceive();
    static Form1* Instance;
    static pthread_mutex_t InstanceLock;
    static pthread_cond_t InstanceCondition;

public:
    QPushButton *btnInit;
    QGroupBox *groupBox1;
    QLabel *textLabel1;
    QLineEdit *txtWriteCount;
    QLabel *textLabel1_3_3_2;
    QLabel *textLabel1_3_3;
    QLineEdit *txtWriteSpeed;
    QLineEdit *txtReadSpeed;
    QLineEdit *txtWriteSectorCount;
    QLineEdit *txtReadSectorCount;
    QLabel *textLabel1_3;
    QLabel *textLabel1_2;
    QLineEdit *txtReadCount;
    QGroupBox *groupBox2;
    QLabel *textLabel1_2_2;
    QLineEdit *txtGarbageCollectionNB;
    QLineEdit *txtGarbageCollection;
    QLineEdit *txtGCStart;
    QLabel *textLabel1_4;
    QLabel *textLabel2;
    QLabel *textLabel3;
    QGroupBox *groupBox3;
    QLabel *textLabel1_2_2_3_2;
    QLabel *textLabel4;
    QLineEdit *txtTrimCount;
    QLineEdit *txtTrimEffect;
    QLabel *textLabel5;
    QLabel *textLabel1_2_2_3_2_2_2_2_2_2;
    QLineEdit *txtWriteAmpCount;
    QLineEdit *txtSsdUtil;
    QLabel *textLabel1_2_3;
    QLabel *textLabel1_2_2_3_2_2_2_2_2_2_2;
    QLineEdit *txtWrittenBlockCount;
    QLabel *textLabel1_2_2_3_2_2_2_2_2;
    QLineEdit *txtWrittenBlockCount1;
    QTextEdit *txtDebug;
    QLabel *textLabel1_5;
    QLineEdit *txtTimeProg;
    QLabel *textLabel1_2_2_3_2_2_2_2_2_2_2_2;
    QPushButton *btnSave;

    void setupUi(QDialog *Form1)
    {
        if (Form1->objectName().isEmpty())
            Form1->setObjectName(QStringLiteral("Form1"));
        Form1->resize(600, 500);
        btnInit = new QPushButton(Form1);
        btnInit->setObjectName(QStringLiteral("btnInit"));
        btnInit->setGeometry(QRect(490, 10, 100, 50));
        groupBox1 = new QGroupBox(Form1);
        groupBox1->setObjectName(QStringLiteral("groupBox1"));
        groupBox1->setGeometry(QRect(11, 10, 460, 91));
        textLabel1 = new QLabel(groupBox1);
        textLabel1->setObjectName(QStringLiteral("textLabel1"));
        textLabel1->setGeometry(QRect(20, 30, 56, 20));
        textLabel1->setWordWrap(false);
        txtWriteCount = new QLineEdit(groupBox1);
        txtWriteCount->setObjectName(QStringLiteral("txtWriteCount"));
        txtWriteCount->setGeometry(QRect(120, 30, 89, 22));
        txtWriteCount->setAlignment(Qt::AlignRight);
        textLabel1_3_3_2 = new QLabel(groupBox1);
        textLabel1_3_3_2->setObjectName(QStringLiteral("textLabel1_3_3_2"));
        textLabel1_3_3_2->setGeometry(QRect(140, 10, 60, 20));
        textLabel1_3_3_2->setWordWrap(false);
        textLabel1_3_3 = new QLabel(groupBox1);
        textLabel1_3_3->setObjectName(QStringLiteral("textLabel1_3_3"));
        textLabel1_3_3->setGeometry(QRect(235, 10, 75, 20));
        textLabel1_3_3->setWordWrap(false);
        txtWriteSpeed = new QLineEdit(groupBox1);
        txtWriteSpeed->setObjectName(QStringLiteral("txtWriteSpeed"));
        txtWriteSpeed->setGeometry(QRect(230, 30, 89, 22));
        txtWriteSpeed->setAlignment(Qt::AlignRight);
        txtReadSpeed = new QLineEdit(groupBox1);
        txtReadSpeed->setObjectName(QStringLiteral("txtReadSpeed"));
        txtReadSpeed->setGeometry(QRect(230, 60, 89, 22));
        txtReadSpeed->setAlignment(Qt::AlignRight);
        txtWriteSectorCount = new QLineEdit(groupBox1);
        txtWriteSectorCount->setObjectName(QStringLiteral("txtWriteSectorCount"));
        txtWriteSectorCount->setGeometry(QRect(355, 30, 89, 22));
        txtWriteSectorCount->setAlignment(Qt::AlignRight);
        txtReadSectorCount = new QLineEdit(groupBox1);
        txtReadSectorCount->setObjectName(QStringLiteral("txtReadSectorCount"));
        txtReadSectorCount->setGeometry(QRect(355, 60, 89, 22));
        txtReadSectorCount->setAlignment(Qt::AlignRight);
        textLabel1_3 = new QLabel(groupBox1);
        textLabel1_3->setObjectName(QStringLiteral("textLabel1_3"));
        textLabel1_3->setGeometry(QRect(350, 10, 100, 20));
        textLabel1_3->setWordWrap(false);
        textLabel1_2 = new QLabel(groupBox1);
        textLabel1_2->setObjectName(QStringLiteral("textLabel1_2"));
        textLabel1_2->setGeometry(QRect(20, 60, 56, 20));
        textLabel1_2->setWordWrap(false);
        txtReadCount = new QLineEdit(groupBox1);
        txtReadCount->setObjectName(QStringLiteral("txtReadCount"));
        txtReadCount->setGeometry(QRect(120, 60, 89, 22));
        txtReadCount->setAlignment(Qt::AlignRight);
        groupBox2 = new QGroupBox(Form1);
        groupBox2->setObjectName(QStringLiteral("groupBox2"));
        groupBox2->setGeometry(QRect(10, 120, 460, 80));
        textLabel1_2_2 = new QLabel(groupBox2);
        textLabel1_2_2->setObjectName(QStringLiteral("textLabel1_2_2"));
        textLabel1_2_2->setGeometry(QRect(10, 20, 99, 20));
        textLabel1_2_2->setWordWrap(false);
        txtGarbageCollectionNB = new QLineEdit(groupBox2);
        txtGarbageCollectionNB->setObjectName(QStringLiteral("txtGarbageCollectionNB"));
        txtGarbageCollectionNB->setGeometry(QRect(120, 40, 89, 22));
        txtGarbageCollectionNB->setAlignment(Qt::AlignRight);
        txtGarbageCollection = new QLineEdit(groupBox2);
        txtGarbageCollection->setObjectName(QStringLiteral("txtGarbageCollection"));
        txtGarbageCollection->setGeometry(QRect(230, 40, 89, 22));
        txtGarbageCollection->setAlignment(Qt::AlignRight);
        txtGCStart = new QLineEdit(groupBox2);
        txtGCStart->setObjectName(QStringLiteral("txtGCStart"));
        txtGCStart->setGeometry(QRect(355, 40, 89, 22));
        txtGCStart->setAlignment(Qt::AlignRight);
        textLabel1_4 = new QLabel(groupBox2);
        textLabel1_4->setObjectName(QStringLiteral("textLabel1_4"));
        textLabel1_4->setGeometry(QRect(140, 20, 66, 20));
        textLabel1_4->setWordWrap(false);
        textLabel2 = new QLabel(groupBox2);
        textLabel2->setObjectName(QStringLiteral("textLabel2"));
        textLabel2->setGeometry(QRect(238, 20, 75, 20));
        textLabel2->setWordWrap(false);
        textLabel3 = new QLabel(groupBox2);
        textLabel3->setObjectName(QStringLiteral("textLabel3"));
        textLabel3->setGeometry(QRect(380, 20, 77, 20));
        textLabel3->setWordWrap(false);
        groupBox3 = new QGroupBox(Form1);
        groupBox3->setObjectName(QStringLiteral("groupBox3"));
        groupBox3->setGeometry(QRect(10, 210, 270, 70));
        textLabel1_2_2_3_2 = new QLabel(groupBox3);
        textLabel1_2_2_3_2->setObjectName(QStringLiteral("textLabel1_2_2_3_2"));
        textLabel1_2_2_3_2->setGeometry(QRect(10, 20, 70, 20));
        textLabel1_2_2_3_2->setWordWrap(false);
        textLabel4 = new QLabel(groupBox3);
        textLabel4->setObjectName(QStringLiteral("textLabel4"));
        textLabel4->setGeometry(QRect(70, 20, 56, 20));
        textLabel4->setWordWrap(false);
        txtTrimCount = new QLineEdit(groupBox3);
        txtTrimCount->setObjectName(QStringLiteral("txtTrimCount"));
        txtTrimCount->setGeometry(QRect(50, 40, 89, 22));
        txtTrimCount->setAlignment(Qt::AlignRight);
        txtTrimEffect = new QLineEdit(groupBox3);
        txtTrimEffect->setObjectName(QStringLiteral("txtTrimEffect"));
        txtTrimEffect->setGeometry(QRect(160, 40, 89, 22));
        txtTrimEffect->setAlignment(Qt::AlignRight);
        textLabel5 = new QLabel(groupBox3);
        textLabel5->setObjectName(QStringLiteral("textLabel5"));
        textLabel5->setGeometry(QRect(182, 20, 56, 20));
        textLabel5->setWordWrap(false);
        textLabel1_2_2_3_2_2_2_2_2_2 = new QLabel(Form1);
        textLabel1_2_2_3_2_2_2_2_2_2->setObjectName(QStringLiteral("textLabel1_2_2_3_2_2_2_2_2_2"));
        textLabel1_2_2_3_2_2_2_2_2_2->setGeometry(QRect(250, 300, 150, 20));
        textLabel1_2_2_3_2_2_2_2_2_2->setWordWrap(false);
        txtWriteAmpCount = new QLineEdit(Form1);
        txtWriteAmpCount->setObjectName(QStringLiteral("txtWriteAmpCount"));
        txtWriteAmpCount->setGeometry(QRect(370, 300, 89, 22));
        txtWriteAmpCount->setAlignment(Qt::AlignRight);
        txtSsdUtil = new QLineEdit(Form1);
        txtSsdUtil->setObjectName(QStringLiteral("txtSsdUtil"));
        txtSsdUtil->setGeometry(QRect(370, 330, 89, 22));
        txtSsdUtil->setAlignment(Qt::AlignRight);
        textLabel1_2_3 = new QLabel(Form1);
        textLabel1_2_3->setObjectName(QStringLiteral("textLabel1_2_3"));
        textLabel1_2_3->setGeometry(QRect(250, 330, 56, 20));
        textLabel1_2_3->setWordWrap(false);
        textLabel1_2_2_3_2_2_2_2_2_2_2 = new QLabel(Form1);
        textLabel1_2_2_3_2_2_2_2_2_2_2->setObjectName(QStringLiteral("textLabel1_2_2_3_2_2_2_2_2_2_2"));
        textLabel1_2_2_3_2_2_2_2_2_2_2->setGeometry(QRect(30, 300, 100, 20));
        textLabel1_2_2_3_2_2_2_2_2_2_2->setWordWrap(false);
        txtWrittenBlockCount = new QLineEdit(Form1);
        txtWrittenBlockCount->setObjectName(QStringLiteral("txtWrittenBlockCount"));
        txtWrittenBlockCount->setGeometry(QRect(130, 300, 89, 22));
        txtWrittenBlockCount->setAlignment(Qt::AlignRight);
        textLabel1_2_2_3_2_2_2_2_2 = new QLabel(Form1);
        textLabel1_2_2_3_2_2_2_2_2->setObjectName(QStringLiteral("textLabel1_2_2_3_2_2_2_2_2"));
        textLabel1_2_2_3_2_2_2_2_2->setGeometry(QRect(280, 640, 110, 20));
        textLabel1_2_2_3_2_2_2_2_2->setWordWrap(false);
        txtWrittenBlockCount1 = new QLineEdit(Form1);
        txtWrittenBlockCount1->setObjectName(QStringLiteral("txtWrittenBlockCount1"));
        txtWrittenBlockCount1->setGeometry(QRect(470, 630, 89, 22));
        txtWrittenBlockCount1->setAlignment(Qt::AlignRight);
        txtDebug = new QTextEdit(Form1);
        txtDebug->setObjectName(QStringLiteral("txtDebug"));
        txtDebug->setGeometry(QRect(10, 400, 460, 80));
        txtDebug->setMouseTracking(false);
        textLabel1_5 = new QLabel(Form1);
        textLabel1_5->setObjectName(QStringLiteral("textLabel1_5"));
        textLabel1_5->setGeometry(QRect(10, 370, 120, 20));
        textLabel1_5->setWordWrap(false);
        txtTimeProg = new QLineEdit(Form1);
        txtTimeProg->setObjectName(QStringLiteral("txtTimeProg"));
        txtTimeProg->setGeometry(QRect(130, 330, 89, 22));
        txtTimeProg->setAlignment(Qt::AlignRight);
        textLabel1_2_2_3_2_2_2_2_2_2_2_2 = new QLabel(Form1);
        textLabel1_2_2_3_2_2_2_2_2_2_2_2->setObjectName(QStringLiteral("textLabel1_2_2_3_2_2_2_2_2_2_2_2"));
        textLabel1_2_2_3_2_2_2_2_2_2_2_2->setGeometry(QRect(40, 330, 50, 20));
        textLabel1_2_2_3_2_2_2_2_2_2_2_2->setWordWrap(false);
        btnSave = new QPushButton(Form1);
        btnSave->setObjectName(QStringLiteral("btnSave"));
        btnSave->setGeometry(QRect(490, 70, 100, 50));

        retranslateUi(Form1);
        QObject::connect(btnInit, SIGNAL(clicked()), Form1, SLOT(btnInit_clicked()));
        QObject::connect(btnSave, SIGNAL(clicked()), Form1, SLOT(btnSave_clicked()));

        QMetaObject::connectSlotsByName(Form1);
    } // setupUi

    void retranslateUi(QDialog *Form1)
    {
        Form1->setWindowTitle(QApplication::translate("Form1", "SSD Monitor", 0));
        btnInit->setText(QApplication::translate("Form1", "Initialize ", 0));
        groupBox1->setTitle(QString());
        textLabel1->setText(QApplication::translate("Form1", "Write:", 0));
        txtWriteCount->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_3_3_2->setText(QApplication::translate("Form1", "Count", 0));
        textLabel1_3_3->setText(QApplication::translate("Form1", "Speed [MB/s]", 0));
        txtWriteSpeed->setText(QApplication::translate("Form1", "0", 0));
        txtReadSpeed->setText(QApplication::translate("Form1", "0", 0));
        txtWriteSectorCount->setText(QApplication::translate("Form1", "0", 0));
        txtReadSectorCount->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_3->setText(QApplication::translate("Form1", "Sector Count", 0));
        textLabel1_2->setText(QApplication::translate("Form1", "Read:", 0));
        txtReadCount->setText(QApplication::translate("Form1", "0", 0));
        groupBox2->setTitle(QApplication::translate("Form1", "Garbage Collection", 0));
        textLabel1_2_2->setText(QString());
        txtGarbageCollectionNB->setText(QApplication::translate("Form1", "0", 0));
        txtGarbageCollection->setText(QApplication::translate("Form1", "0", 0));
        txtGCStart->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_4->setText(QApplication::translate("Form1", "Count", 0));
        textLabel2->setText(QApplication::translate("Form1", "Exchange", 0));
        textLabel3->setText(QApplication::translate("Form1", "Sector", 0));
        groupBox3->setTitle(QApplication::translate("Form1", "TRIM", 0));
        textLabel1_2_2_3_2->setText(QString());
        textLabel4->setText(QApplication::translate("Form1", "Count", 0));
        txtTrimCount->setText(QApplication::translate("Form1", "0", 0));
        txtTrimEffect->setText(QApplication::translate("Form1", "0", 0));
        textLabel5->setText(QApplication::translate("Form1", "Effect", 0));
        textLabel1_2_2_3_2_2_2_2_2_2->setText(QApplication::translate("Form1", "Write Amplification", 0));
        txtWriteAmpCount->setText(QApplication::translate("Form1", "0", 0));
        txtSsdUtil->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_2_3->setText(QApplication::translate("Form1", "SSD Util", 0));
        textLabel1_2_2_3_2_2_2_2_2_2_2->setText(QApplication::translate("Form1", "Written Page", 0));
        txtWrittenBlockCount->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_2_2_3_2_2_2_2_2->setText(QApplication::translate("Form1", "Written Block Count", 0));
        txtWrittenBlockCount1->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_5->setText(QApplication::translate("Form1", "Debug Status", 0));
        txtTimeProg->setText(QApplication::translate("Form1", "0", 0));
        textLabel1_2_2_3_2_2_2_2_2_2_2_2->setText(QApplication::translate("Form1", "Time", 0));
        btnSave->setText(QApplication::translate("Form1", "Save", 0));
    } // retranslateUi

};


#endif // UI_FORM1_H
