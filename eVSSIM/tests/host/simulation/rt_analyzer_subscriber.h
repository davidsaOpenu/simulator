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

#ifndef __RT_ANALYZER_SUBSCRIBER_H__
#define __RT_ANALYZER_SUBSCRIBER_H__

extern "C" {

#include "logging_rt_analyzer.h"
}


/**
 * The namespace of the real time log analyzer subscriber, used for testing
 */
namespace rt_subscriber {
    /**
     * Write the different logs to the logger previously subscribed to
     */
    void write();
    /**
     * Read the logs from the logger using rt_log_analyzer_loop
     */
    void read();
    /**
     * Subscribe to the analyzer given, and save it for future use
     * @param analyzer the analyzer to use in the subscriber
     */
    void subscribe(RTLogAnalyzer* analyzer);
}


#endif
