#include "statcontroller.h"
#include "ui_statcontroller.h"
#include <QString>
#include "/home/ori/VSSIM/VSSIM_v1.1/FTL_SOURCE/COMMON/common.h"

StatController::StatController(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::StatController)
{
    ui->setupUi(this);
}

StatController::~StatController()
{
    delete ui;
}

void StatController::on_btnGather_clicked()
{
    QString str1 = "Start Gathering Statistics";
    QString str2 = "Stop Gathering Statistics";

    if (ui->btnGather->text() == str1){
        ui->btnGather->setText(str2);
    }else{
        ui->btnGather->setText(str1);
    }
}
