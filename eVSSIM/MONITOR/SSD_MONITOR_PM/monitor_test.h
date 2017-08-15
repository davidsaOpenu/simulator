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

#ifndef __MONITOR_TEST_H__
#define __MONITOR_TEST_H__

#include <qevent.h>

#include "../../LOG_MGR/logging_rt_analyzer.h"


/**
 * Start the monitor in the current thread
 */
void* start_monitor(void*);

/**
 * Update the statistics displayed in the monitor
 * If no window is shown, busy wait for one to appear
 * @param stats the new statistics to display
 */
void update_stats(SSDStatistics stats);


/**
 * The event for statistics update
 */
class StatsUpdateEvent : public QEvent {
public:
    /**
     * Create a new event with the statistics given
     * @param stats the statistics to use in the update
     */
    StatsUpdateEvent(SSDStatistics stats);
    /**
     * The type of the event
     */
    static const QEvent::Type type = static_cast<QEvent::Type>(3000);
    /**
     * The statistics to use in the update
     */
    SSDStatistics stats;
};

#endif
