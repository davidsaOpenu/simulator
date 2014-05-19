/********************************************************************************
** Form generated from reading UI file 'statcontroller.ui'
**
** Created by: Qt User Interface Compiler version 5.1.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_STATCONTROLLER_H
#define UI_STATCONTROLLER_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_StatController
{
public:
    QWidget *centralWidget;
    QPushButton *btnGather;
    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *StatController)
    {
        if (StatController->objectName().isEmpty())
            StatController->setObjectName(QStringLiteral("StatController"));
        StatController->resize(234, 117);
        centralWidget = new QWidget(StatController);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        btnGather = new QPushButton(centralWidget);
        btnGather->setObjectName(QStringLiteral("btnGather"));
        btnGather->setGeometry(QRect(20, 10, 191, 41));
        StatController->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(StatController);
        menuBar->setObjectName(QStringLiteral("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 234, 20));
        StatController->setMenuBar(menuBar);
        mainToolBar = new QToolBar(StatController);
        mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
        StatController->addToolBar(Qt::TopToolBarArea, mainToolBar);
        statusBar = new QStatusBar(StatController);
        statusBar->setObjectName(QStringLiteral("statusBar"));
        StatController->setStatusBar(statusBar);

        retranslateUi(StatController);

        QMetaObject::connectSlotsByName(StatController);
    } // setupUi

    void retranslateUi(QMainWindow *StatController)
    {
        StatController->setWindowTitle(QApplication::translate("StatController", "Stat Controller", 0));
        btnGather->setText(QApplication::translate("StatController", "Start Gathering Statistics", 0));
    } // retranslateUi

};

namespace Ui {
    class StatController: public Ui_StatController {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_STATCONTROLLER_H
