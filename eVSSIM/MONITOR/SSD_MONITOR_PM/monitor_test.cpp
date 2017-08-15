#include <qapplication.h>

#include "monitor_test.h"
#include "form1.h"

Form1* win;

void* start_monitor(void* args) {
    int argc = 0;
    char **argv = NULL;
    QApplication a( argc, argv );
    win = new Form1;
    win->show();
    a.connect( &a, SIGNAL( lastWindowClosed() ), &a, SLOT( quit() ) );
    a.exec();
    return NULL;
}


void update_stats(SSDStatistics stats) {
    win->updateStats(stats);
}
