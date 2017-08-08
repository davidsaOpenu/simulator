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

#ifndef __LOGGING_PARSER_H__
#define __LOGGING_PARSER_H__


/*
 * Log the structure given (as a byte array)
 */
void log_struct(Logger* logger, unsigned char* struct_buffer, int length, int struct_type);


/*
 * The unique ids for the different structs available for parsing
 */
enum ParsableStructure {REG_WRITE_LOG_UID, REG_READ_LOG_UID, /*...*/ };

/*
 * A log of a register write
 */
typedef struct {
	// the register written to
	int reg;
} RegWriteLog;
/*
 * A log of a register read
 */
typedef struct {
	// the register read from
	int reg;
} RegReadLog;
/*...*/

/*
 * Log a register write
 * `logger` is a Logger*, and `reg` is an int
 */
#define LOG_REG_WRITE(logger, reg)													\
	do {																			\
		RegWriteLog log = {															\
				.reg = reg															\
		};																			\
		log_struct(logger, (unsigned char*) &log, sizeof(log), REG_WRITE_LOG_UID);	\
	} while (0)
/*
 * Log a register read
 * `logger` is a Logger*, and `reg` is an int
 */
#define LOG_REG_READ(logger, reg)													\
	do {																			\
		RegReadLog log = {															\
				.reg = reg															\
		};																			\
		log_struct(logger, (unsigned char*) &log, sizeof(log), REG_READ_LOG_UID);	\
	} while (0)
/*...*/


/*
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger);

/*
 * Write the next log in the logger to the buffer
 * It is up to the user to calculate `log_size` using the log type
 */
void next_log(Logger* logger, void* buffer, int log_size);


#endif
