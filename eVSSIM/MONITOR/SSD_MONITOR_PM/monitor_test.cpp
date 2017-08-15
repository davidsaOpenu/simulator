#include <qapplication.h>
#include <unistd.h>

#include "monitor_test.h"
#include "form1.h"


Form1* Form1::Instance = NULL;


void* start_monitor(void*) {
    int argc = 0;
    char **argv = NULL;
    QApplication a( argc, argv );
    Form1 w;
    w.show();
    a.connect( &a, SIGNAL( lastWindowClosed() ), &a, SLOT( quit() ) );
    a.exec();
    return NULL;
}


void update_stats(SSDStatistics stats) {
    while (Form1::Instance == NULL) {
        usleep(1);
    }
    Form1::Instance->updateStats(stats);
}
