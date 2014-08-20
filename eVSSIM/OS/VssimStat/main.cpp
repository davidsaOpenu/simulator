#include "statcontroller.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    StatController w;
    w.show();

    return a.exec();
}
