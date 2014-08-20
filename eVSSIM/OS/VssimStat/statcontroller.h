#ifndef STATCONTROLLER_H
#define STATCONTROLLER_H

#include <QMainWindow>

namespace Ui {
class StatController;
}

class StatController : public QMainWindow
{
    Q_OBJECT

public:
    explicit StatController(QWidget *parent = 0);
    ~StatController();

private slots:
    void on_btnGather_clicked();

private:
    Ui::StatController *ui;
};

#endif // STATCONTROLLER_H
