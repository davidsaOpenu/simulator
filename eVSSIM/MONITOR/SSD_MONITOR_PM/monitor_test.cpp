#include <qapplication.h>

#include "monitor_test.h"
#include "form1.h"

Form1* win;
pthread_mutex_t win_lock = PTHREAD_MUTEX_INITIALIZER;

void* start_monitor(void* args) {
    pthread_mutex_lock(&win_lock);
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
    pthread_mutex_lock(&win_lock);
    win->updateStats(stats);
    pthread_mutex_unlock(&win_lock);
}
