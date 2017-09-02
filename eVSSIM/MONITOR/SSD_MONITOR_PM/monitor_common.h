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

#ifndef __MONITOR_COMMON_H__
#define __MONITOR_COMMON_H__

#include <qevent.h>

// The monitor is built separately from the rest of the simulator, and thus the includes need to be
// relative
#include "../../LOG_MGR/logging_rt_analyzer.h"


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
     * The number needs to be between 1001 and 65534, and should be unique
     * thus, 3000 was randomly chosen
     */
    static const QEvent::Type type = static_cast<QEvent::Type>(3000);
    /**
     * The statistics to use in the update
     */
    SSDStatistics stats;
};


#endif
