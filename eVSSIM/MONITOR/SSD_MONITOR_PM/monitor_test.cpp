/*
 * Copyright 2017 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <qapplication.h>
#include <unistd.h>

#include "monitor_test.h"
#include "form1.h"


Form1* Form1::Instance = NULL;


void* start_monitor(void*) {
    int argc = 0;
    char **argv = NULL;
    QApplication app( argc, argv );
    Form1 window;
    window.show();
    app.connect( &app, SIGNAL( lastWindowClosed() ), &app, SLOT( quit() ) );
    app.exec();
    return NULL;
}


void update_stats(SSDStatistics stats) {
    while (Form1::Instance == NULL) {
        usleep(1);
    }
    Form1::Instance->updateStats(stats);
}
