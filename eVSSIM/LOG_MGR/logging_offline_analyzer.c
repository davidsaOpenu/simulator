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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#include "logging_offline_analyzer.h"
#include "logging_backend.h"

elk_logger_writer elk_logger_writer_obj;
int lines_read_in_json = 0;
int auto_delete = TRUE;
static int elk_logger_writer_initialized = FALSE;
static int elk_logger_writer_ref_count = 0;

#define OFFLINE_ANALYZER_BATCH_BYTES (1 << 20)

#define ACTIVE_SNAPSHOT_NAME_MAX_SIZE 256

OfflineLogAnalyzer* offline_log_analyzer_init(Logger_Pool* logger_pool, uint8_t device_index) {
    OfflineLogAnalyzer* analyzer = (OfflineLogAnalyzer*) malloc(sizeof(OfflineLogAnalyzer));
    if (analyzer == NULL)
        return NULL;

    analyzer->logger_pool = logger_pool;
    analyzer->device_index = device_index;
    analyzer->exit_loop_flag = 0;

    return analyzer;
}

void* offline_log_analyzer_run(void* analyzer) {
    offline_log_analyzer_loop((OfflineLogAnalyzer*) analyzer);
    return NULL;
}

void offline_log_analyzer_loop(OfflineLogAnalyzer* analyzer) {
    char* json_buf = NULL;

    size_t batch_len = 0;
    char* batch = (char*) malloc(OFFLINE_ANALYZER_BATCH_BYTES);
    if (NULL == batch)
        fprintf(stderr, "WARNING: failed to allocate the offline analyzer batch buffer!\n");

    while ( 1 )
    {
        int stopping = __atomic_load_n(&analyzer->exit_loop_flag, __ATOMIC_ACQUIRE);

        while ( 1 ) {
            int log_type;
            int bytes_read = 0;

            bytes_read = logger_read(analyzer->logger_pool, ((Byte*)&log_type), sizeof(log_type), OFFLINE_ANALYZER);

            if (0 == bytes_read || -1 == bytes_read) {
                break;
            }

            // if the log type is invalid, continue
            if (log_type < 0 || log_type >= LOG_UID_COUNT) {
                fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
                fprintf(stderr, "WARNING: the log may be corrupted and unusable!\n");
                continue;
            }

            // update the statistics according to the log
            switch (log_type) {
                case PHYSICAL_CELL_READ_LOG_UID:
                {
                    PhysicalCellReadLog res;
                    NEXT_PHYSICAL_CELL_READ_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_PHYSICAL_CELL_READ(&res, &json_buf);
                    break;
                }
                case PHYSICAL_CELL_PROGRAM_LOG_UID:
                {
                    PhysicalCellProgramLog res;
                    NEXT_PHYSICAL_CELL_PROGRAM_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_PHYSICAL_CELL_PROGRAM(&res, &json_buf);
                    break;
                }
                case PHYSICAL_CELL_PROGRAM_COMPATIBLE_LOG_UID:
                {
                    PhysicalCellProgramCompatibleLog res;
                    NEXT_PHYSICAL_CELL_PROGRAM_COMPATIBLE_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_PHYSICAL_CELL_PROGRAM_COMPATIBLE(&res, &json_buf);
                    break;
                }
                case LOGICAL_CELL_PROGRAM_LOG_UID:
                {
                    LogicalCellProgramLog res;
                    NEXT_LOGICAL_CELL_PROGRAM_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_LOGICAL_CELL_PROGRAM(&res, &json_buf);
                    break;
                }
                case GARBAGE_COLLECTION_LOG_UID:
                {
                    GarbageCollectionLog res;
                    NEXT_GARBAGE_COLLECTION_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_GARBAGE_COLLECTION(&res, &json_buf);
                    break;
                }
                case REGISTER_READ_LOG_UID:
                {
                    RegisterReadLog res;
                    NEXT_REGISTER_READ_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_REGISTER_READ(&res, &json_buf);
                    break;
                }
                case REGISTER_WRITE_LOG_UID:
                {
                    RegisterWriteLog res;
                    NEXT_REGISTER_WRITE_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_REGISTER_WRITE(&res, &json_buf);
                    break;
                }
                case BLOCK_ERASE_LOG_UID:
                {
                    BlockEraseLog res;
                    NEXT_BLOCK_ERASE_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_BLOCK_ERASE(&res, &json_buf);
                    break;
                }
                case PAGE_COPYBACK_LOG_UID:
                {
                    PageCopyBackLog res;
                    NEXT_PAGE_COPYBACK_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_PAGE_COPYBACK(&res, &json_buf);
                    break;
                }
                case CHANNEL_SWITCH_TO_READ_LOG_UID:
                {
                    ChannelSwitchToReadLog res;
                    NEXT_CHANNEL_SWITCH_TO_READ_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_CHANNEL_SWITCH_TO_READ(&res, &json_buf);
                    break;
                }
                case CHANNEL_SWITCH_TO_WRITE_LOG_UID:
                {
                    ChannelSwitchToWriteLog res;
                    NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_CHANNEL_SWITCH_TO_WRITE(&res, &json_buf);
                    break;
                }
                case OBJECT_ADD_PAGE_LOG_UID:
                {
                    ObjectAddPageLog res;
                    NEXT_OBJECT_ADD_PAGE_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_OBJECT_ADD_PAGE(&res, &json_buf);
                    break;
                }
                case OBJECT_COPYBACK_LOG_UID:
                {
                    ObjectCopyback res;
                    NEXT_OBJECT_COPYBACK_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_OBJECT_COPYBACK(&res, &json_buf);
                    break;
                }
                case LOG_SYNC_LOG_UID:
                {
                    LoggeingServerSync res;
                    NEXT_LOG_SYNC_LOG(analyzer->logger_pool, &res, OFFLINE_ANALYZER);
                    JSON_LOG_SYNC(&res, &json_buf);
                    break;
                }
                case SSD_UTILIZATION_LOG_UID:
                {
                    SsdUtilizationLog log;
                    NEXT_SSD_UTILIZATION_LOG(analyzer->logger_pool, &log, OFFLINE_ANALYZER);
                    JSON_SSD_UTILIZATION(&log, &json_buf);
                    break;
                }
                default:
                    fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
                    fprintf(stderr, "WARNING: rt_log_analyzer_loop may not be up to date!\n");
                    break;
            }

            // Inject device_index into the JSON log entry
            char* enriched_buf = json_inject_device_index(json_buf, analyzer->device_index);
            free(json_buf);
            json_buf = NULL;

            if (enriched_buf != NULL) {
                size_t len = strlen(enriched_buf);
                if (batch != NULL && len < OFFLINE_ANALYZER_BATCH_BYTES) {
                    if (batch_len + len > OFFLINE_ANALYZER_BATCH_BYTES) {
                        elk_logger_writer_save_log_to_file((Byte*)batch, (int)batch_len);
                        batch_len = 0;
                    }
                    memcpy(batch + batch_len, enriched_buf, len);
                    batch_len += len;
                } else {
                    elk_logger_writer_save_log_to_file((Byte*)enriched_buf, (int)len);
                }
                free(enriched_buf);
            }
        }

        if (batch_len > 0) {
            elk_logger_writer_save_log_to_file((Byte*)batch, (int)batch_len);
            batch_len = 0;
        }

        logger_reduce_size(analyzer->logger_pool);
        logger_clean(analyzer->logger_pool);

        if (stopping)
            break;

        // go into penalty timeoff after each iteration of the analyzer loop
        // in order to let the rt analyzer complete it's operation on more logs
        (void)usleep(OFFLINE_ANALYZER_LOOP_TIMEOUT_US);
    }

    free(batch);

    analyzer->exit_loop_flag = 0;

    free(json_buf);
}

/**
 * frees the given analyzer
 */
void offline_log_analyzer_free(OfflineLogAnalyzer* analyzer) {
    free((void*) analyzer);
}

static void elk_logger_writer_get_time_string(char *buf)
{
    time_t timer;
    struct tm* tm_info;

    timer = time(NULL);
    tm_info = gmtime(&timer);

    strftime(buf, TIME_STAMP_LEN, LOG_NAME_PATTERN, tm_info);
}

/**
 * Tells whether a log file was created by this process.
 * @param name the log file name (no path)
 * @return TRUE if this process created the file
 */
static int is_log_file_of_current_process(const char *name){
    const char *seq_sep = strrchr(name, '-');
    const char *pid_sep = NULL;
    const char *p;

    if (seq_sep == NULL)
        return FALSE;

    for (p = name; p < seq_sep; p++){
        if (*p == '-')
            pid_sep = p;
    }

    if (pid_sep == NULL)
        return FALSE;

    return (pid_t)strtol(pid_sep + 1, NULL, 10) == getpid();
}

/**
 * returns the number of log files
 * @param path the path to the directory that contain all of the files
 */
static int get_log_num(const char *path){
    DIR * d;
    struct dirent *dir;
    int count = 0;

    d = opendir(path);

    if(d) {
        while((dir = readdir(d))!=NULL){
            if(!strcmp(strrchr(dir->d_name, '\0')-4,".log") && is_log_file_of_current_process(dir->d_name)){
                count++;
            }
        }
    }

    closedir(d);

    return count;
}
/**
 * saves all the log file names in files
 * @param path the path to the directory that contain all of the files
 * @param files the list of file names
 * @param count the number of log files
 */
static void get_log_files(const char *path, char **files){
    DIR * d;
    struct dirent *dir;
    d = opendir(path);

    int i=0;
    while((dir = readdir(d))!=NULL){
        if(!strcmp(strrchr(dir->d_name, '\0')-4,".log") && is_log_file_of_current_process(dir->d_name)){
            files[i] = malloc(strlen(dir->d_name)+1);
            strcpy(files[i], dir->d_name);
            i++;
        }
    }

    closedir(d);
}

/** extracts the latest bit that was sent by filebeat and updates the size
 * @param line the line read from filebeat's log
 * @param size array of file sizes
 * @param arrsize the size of the arrays
 * @param files the list of file names
 */
static void extract_log_size(char* line,int* size,char **files, int arrsize){
    int offset;
    char address[LOG_FILE_NAME_SIZE+strlen(".log")];

    struct json_object *parsed;
    struct json_object *state;
    struct json_object *json_offset;
    struct json_object *json_address;

    if(strlen(line)<2){
        return;
    }

    parsed = json_tokener_parse(line);

    //skip the snapshot array brackets and any line that is not an object
    if(parsed == NULL || json_object_get_type(parsed) != json_type_object){
        if(parsed != NULL){
            json_object_put(parsed);
        }
        return;
    }

    //log.json nests the state under "v", snapshot lines are the state itself
    if(!json_object_object_get_ex(parsed, "v", &state)){
        state = parsed;
    }

    json_object_object_get_ex(state, "source", &json_address);
    json_object_object_get_ex(state, "offset", &json_offset);

    if(json_address != NULL){
        strcpy(address,json_object_get_string(json_address));
        memmove(address,address+6,strlen(address));
        offset = json_object_get_int(json_offset);

        int i = 0;

        int stop = FALSE;

        while((i < arrsize) && stop == FALSE){
            if(strcmp(files[i],address)==0){
                size[i]=offset;
                stop = TRUE;
            }
            i++;
        }
    }
    json_object_put(parsed);
}



/** checks for each file if it has been fully shipped and deletes it
 * @param size array of file sizes
 * @param arrsize the size of the arrays
 * @param files the list of file names
 * @param path the path to the directory that contain all of the files
 */
static int delete_fully_shipped(int *size, int arrsize, char** files, const char* path){
    struct stat st;
    int i=0;
    int amount = arrsize;

    for(i=0;i<arrsize;i++){
        char fulladdress [strlen(path)+strlen(files[i])+7];
        strcat(strcpy(fulladdress, path),files[i]);
        if(stat(fulladdress, &st)!=0){
            continue;
        }
        if(st.st_size == 0 || (size[i] != 0 && size[i] == st.st_size)){
            amount--;
            remove(fulladdress);
        }
    }
    return amount;
}

/**
 * reads the name of the active filebeat registry snapshot from active.dat.
 * @param name buffer to receive the snapshot file name
 * @param name_size the size of name
 * @return 0 on success, -1 if active.dat is unavailable or empty
 */
static int get_active_snapshot_name(char *name, size_t name_size){
    FILE * fp = OPEN_FROM_REGISTRY("active.dat", "r");
    char path[ACTIVE_SNAPSHOT_NAME_MAX_SIZE];
    const char *base;
    size_t len;

    if(fp == NULL){
        return -1;
    }

    if(fgets(path, sizeof(path), fp) == NULL){
        fclose(fp);
        return -1;
    }
    fclose(fp);

    base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;

    len = strlen(base);
    while(len > 0 && (base[len-1] == '\n' || base[len-1] == '\r')){
        len--;
    }

    if(len == 0 || len >= name_size){
        return -1;
    }

    memcpy(name, base, len);
    name[len] = '\0';

    return 0;
}

/**
 * finds and deletes all logs shipped by filebeat.
 * reads the active snapshot (<N>.json) first and log.json on top, so offsets
 * from before a registry checkpoint are not lost.
 */
static int delete_shipped(void){
    FILE * fp;
    char * line =NULL;
    size_t len = 0;
    ssize_t read;
    int read_state = FALSE;
    int amount = 0;
    int i = 0;

    //gets the name and amount of all the log files
    amount = get_log_num(ELK_LOGGER_WRITER_LOGS_PATH);

    if(!amount){
        return amount;
    }

    char *filelist[amount];

    get_log_files(ELK_LOGGER_WRITER_LOGS_PATH, filelist);

    const int arrsize = amount;

    int* size = (int *)calloc(arrsize, sizeof(int));

    //read the checkpoint snapshot first: it holds every state as of the last checkpoint
    char snapshot_name[ACTIVE_SNAPSHOT_NAME_MAX_SIZE];

    if(get_active_snapshot_name(snapshot_name, sizeof(snapshot_name)) == 0){
        char snapshot_path[ACTIVE_SNAPSHOT_NAME_MAX_SIZE + sizeof(FILEBEAT_REGISTRY_PATH)];
        FILE * fps;

        snprintf(snapshot_path, sizeof(snapshot_path), "%s%s",
                 FILEBEAT_REGISTRY_PATH, snapshot_name);
        fps = fopen(snapshot_path, "r");

        if(fps != NULL){
            read_state = TRUE;
            while((read = getline(&line, &len, fps)) != -1){
                //snapshot lines are array elements terminated by commas
                size_t l = strlen(line);
                while(l > 0 && (line[l-1] == '\n' || line[l-1] == '\r' || line[l-1] == ',')){
                    line[--l] = '\0';
                }
                extract_log_size(line, size, filelist, arrsize);
            }
            fclose(fps);
        }
    }

    //then apply the post-checkpoint updates, which override the snapshot
    fp = OPEN_FROM_REGISTRY("log.json", "r");

    if (fp != NULL){
        read_state = TRUE;
        while((read = getline(&line, &len, fp)) != -1){
            extract_log_size(line, size, filelist, arrsize);
        }
        fclose(fp);
    }

    if(!read_state){
        if(line){
            free(line);
        }
        free(size);
        for(i = 0; i<arrsize; i++){
            free(filelist[i]);
        }
        return amount;
    }

    //checks for each file if it has been fully shipped
    amount = delete_fully_shipped(size,arrsize,filelist,ELK_LOGGER_WRITER_LOGS_PATH);

    if (line){
        free(line);
    }

    for(i = 0; i<arrsize; i++){
        free(filelist[i]);
    }

    free(size);

    return amount;
}

/**
 * wait until filebeat read all the logs.
 */
int elk_logger_wait_until_all_shipped(uint64_t timeout_ms){
    uint64_t waited = 0;

    if (get_log_num(ELK_LOGGER_WRITER_LOGS_PATH) == 0){
        return 0;
    }

    while(TRUE){
        FILE * probe = OPEN_FROM_REGISTRY("log.json", "r");

        if(probe == NULL){
            return -2;
        }
        fclose(probe);

        delete_shipped();

        if(get_log_num(ELK_LOGGER_WRITER_LOGS_PATH) == 0){
            return 0;
        }

        if (waited >= timeout_ms){
            return -1;
        }

        usleep(500 * 1000);
        waited += 500;
    }
}

/**
 * creates and opens a new log file and checks for shipped files to delete
 */
static int elk_logger_writer_open_file_for_write(void) {

    if(auto_delete){
        //checks for files fully shipped by filebeat and deletes them
        int unshipped = delete_shipped();

        if (unshipped > MAX_UNSHIPPED_LOGS)
            printf("WARNING: there are %d unshipped logs (might need to increase filebeat workers)\n",unshipped);
    }

    static unsigned int file_seq = 0;
    char buf[TIME_STAMP_LEN];
    char log_name[TIME_STAMP_LEN+60];

    elk_logger_writer_get_time_string(buf);
    sprintf(log_name, ELK_LOGGER_WRITER_LOGS_PATH "elk_log_file-%s-%d-%u.log", buf, getpid(),
            __atomic_fetch_add(&file_seq, 1, __ATOMIC_RELAXED));
    elk_logger_writer_obj.log_file = open(log_name, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (0 >= elk_logger_writer_obj.log_file) {
        return -1;
    }

    return 0;
}

/**
 * closes the log file in use
 */
static void elk_logger_writer_close_file(void) {
    if (elk_logger_writer_obj.log_file > 0)
        close(elk_logger_writer_obj.log_file);
}

void elk_logger_writer_init(void) {
    // Only initialize once across all devices; ref-count additional callers
    if (elk_logger_writer_initialized) {
        elk_logger_writer_ref_count++;
        return;
    }

    pthread_mutex_init(&elk_logger_writer_obj.lock, NULL);
    elk_logger_writer_obj.log_file_size = 10 *1024 * 1024; // 10 MB
    elk_logger_writer_obj.curr_size = 0;

    int retval = system("mkdir -p " ELK_LOGGER_WRITER_LOGS_PATH);

    if(!auto_delete){
        retval = system(LOG_FILE_REMOVAL_COMMAND);
    }

    retval = elk_logger_writer_open_file_for_write();

    if (retval == -1) {
        pthread_mutex_destroy(&elk_logger_writer_obj.lock);
        return;
    }

    elk_logger_writer_initialized = TRUE;
    elk_logger_writer_ref_count = 1;
}

/**
 * frees the writer
 */
void elk_logger_writer_free(void) {
    if (!elk_logger_writer_initialized) {
        return;
    }
    elk_logger_writer_ref_count--;
    if (elk_logger_writer_ref_count > 0) {
        return;
    }
    pthread_mutex_lock(&elk_logger_writer_obj.lock);
    elk_logger_writer_close_file();
    if (auto_delete) {
        delete_shipped();
    }
    pthread_mutex_unlock(&elk_logger_writer_obj.lock);
    pthread_mutex_destroy(&elk_logger_writer_obj.lock);
    elk_logger_writer_initialized = FALSE;
}

/**
 * writes a buffer to the current log file
 * @param buffer the buffer to be written to the current log file
 * @param length the length of the buffer
 */
void elk_logger_writer_save_log_to_file(Byte *buffer, int length) {
    int res;
    if (NULL != buffer) {
        // I lock here in order to prevent collisions between the different rt_analyzer threads while the touch
        //  writer_obj related members.
        pthread_mutex_lock(&elk_logger_writer_obj.lock);
        // Check if there is enought space in the current log file to write log
        if (length + elk_logger_writer_obj.curr_size > elk_logger_writer_obj.log_file_size) {
            elk_logger_writer_obj.curr_size = 0;
            //logger_writer_obj.curr_log_file = (logger_writer_obj.curr_log_file + 1) % NUM_LOG_FILES;
            elk_logger_writer_close_file();
            elk_logger_writer_open_file_for_write();
        }
        // Increase the size of current log_file by the buffer's size
        elk_logger_writer_obj.curr_size += length;
        res = write(elk_logger_writer_obj.log_file, buffer, length);
        (void) res;

        pthread_mutex_unlock(&elk_logger_writer_obj.lock);
    }
}
