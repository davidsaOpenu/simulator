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

OfflineLogAnalyzer* offline_log_analyzer_init(Logger_Pool* logger_pool) {
    OfflineLogAnalyzer* analyzer = (OfflineLogAnalyzer*) malloc(sizeof(OfflineLogAnalyzer));
    if (analyzer == NULL)
        return NULL;

    analyzer->logger_pool = logger_pool;
    analyzer->exit_loop_flag = 0;

    return analyzer;
}

void* offline_log_analyzer_run(void* analyzer) {
    offline_log_analyzer_loop((OfflineLogAnalyzer*) analyzer);
    return NULL;
}

void offline_log_analyzer_loop(OfflineLogAnalyzer* analyzer) {
    char* json_buf = NULL;
    // loop as long as exit_loop_flag is not set
    while( ! ( analyzer->exit_loop_flag ) )
    {
        while ( ! ( analyzer->exit_loop_flag )) {
            // read the log type, while listening to analyzer->exit_loop_flag
            int log_type;
            int bytes_read = 0;

            bytes_read = offline_logger_read(analyzer->logger_pool, ((Byte*)&log_type),sizeof(log_type));

            // exit if needed
            if (analyzer->exit_loop_flag || 0 == bytes_read) {
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
                    NEXT_PHYSICAL_CELL_READ_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_PHYSICAL_CELL_READ(&res, &json_buf);
                    break;
                }
                case PHYSICAL_CELL_PROGRAM_LOG_UID:
                {
                    PhysicalCellProgramLog res;
                    NEXT_PHYSICAL_CELL_PROGRAM_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_PHYSICAL_CELL_PROGRAM(&res, &json_buf);
                    break;
                }
                case LOGICAL_CELL_PROGRAM_LOG_UID:
                {
                    LogicalCellProgramLog res;
                    NEXT_LOGICAL_CELL_PROGRAM_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_LOGICAL_CELL_PROGRAM(&res, &json_buf);
                    break;
                }
                case GARBAGE_COLLECTION_LOG_UID:
                {
                    GarbageCollectionLog res;
                    NEXT_GARBAGE_COLLECTION_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_GARBAGE_COLLECTION(&res, &json_buf);
                    break;
                }
                case REGISTER_READ_LOG_UID:
                {
                    RegisterReadLog res;
                    NEXT_REGISTER_READ_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_REGISTER_READ(&res, &json_buf);
                    break;
                }
                case REGISTER_WRITE_LOG_UID:
                {
                    RegisterWriteLog res;
                    NEXT_REGISTER_WRITE_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_REGISTER_WRITE(&res, &json_buf);
                    break;
                }
                case BLOCK_ERASE_LOG_UID:
                {
                    BlockEraseLog res;
                    NEXT_BLOCK_ERASE_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_BLOCK_ERASE(&res, &json_buf);
                    break;
                }
                case CHANNEL_SWITCH_TO_READ_LOG_UID:
                {
                    ChannelSwitchToReadLog res;
                    NEXT_CHANNEL_SWITCH_TO_READ_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_CHANNEL_SWITCH_TO_READ(&res, &json_buf);
                    break;
                }
                case CHANNEL_SWITCH_TO_WRITE_LOG_UID:
                {
                    ChannelSwitchToWriteLog res;
                    NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_CHANNEL_SWITCH_TO_WRITE(&res, &json_buf);
                    break;
                }
                case OBJECT_ADD_PAGE_LOG_UID:
                {
                    ObjectAddPageLog res;
                    NEXT_OBJECT_ADD_PAGE_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_OBJECT_ADD_PAGE(&res, &json_buf);
                    break;
                }
                case OBJECT_COPYBACK_LOG_UID:
                {
                    ObjectCopyback res;
                    NEXT_OBJECT_COPYBACK_LOG(analyzer->logger_pool, &res, LOGGER_TYPE_OFFLINE);
                    JSON_OBJECT_COPYBACK(&res, &json_buf);
                    break;
                }
                default:
                    fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
                    fprintf(stderr, "WARNING: rt_log_analyzer_loop may not be up to date!\n");
                    break;
            }

            elk_logger_writer_save_log_to_file((Byte *)json_buf, strlen(json_buf));
            
            free(json_buf);
            json_buf = NULL;
        }

        logger_reduce_size(analyzer->logger_pool);
        logger_clean(analyzer->logger_pool);

        // go into penalty timeoff after each iteration of the analyzer loop
        // in order to let the rt analyzer complete it's operation on more logs
        (void)usleep(OFFLINE_ANALYZER_LOOP_TIMEOUT_US);
    }

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
    tm_info = localtime(&timer);

    strftime(buf, TIME_STAMP_LEN, "%Y-%m-%d_%H:%M:%S", tm_info);
}

/**
 * saves all the log file names in files and the number in count
 * @param path the path to the directory that contain all of the files
 * @param files the list of file names
 * @param count the number of log files
 */
static void get_log_files(const char *path, char ***files, int* count){
    DIR * d;
    struct dirent *dir;
    *count = 0;
    
    d = opendir(path);
    
    if(d) {
        while((dir = readdir(d))!=NULL){
            if(!strcmp(strrchr(dir->d_name, '\0')-4,".log")){
                (*count)++;
            }
        }
       }
       
       rewinddir(d);
       
       int i=0;
       *files = malloc(*count * sizeof(char *));
       
    while((dir = readdir(d))!=NULL){
        if(!strcmp(strrchr(dir->d_name, '\0')-4,".log")){    
            (*files)[i] = malloc(strlen(dir->d_name)+1);
            strcpy((*files)[i], dir->d_name);
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
    struct json_object *jsonv;
    struct json_object *json_offset; 
    struct json_object *json_address; 
    
    if(strlen(line)<2){
        return;
    }
    
    if (line[2]=='k'){
        parsed = json_tokener_parse(line);
        json_object_object_get_ex(parsed, "v", &jsonv);
        json_object_object_get_ex(jsonv, "source", &json_address);
        json_object_object_get_ex(jsonv, "offset", &json_offset);
                
        json_object_put(parsed);
        json_object_put(jsonv);
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
    }
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
        stat(fulladdress, &st); 
        if(size[i] == st.st_size){
            amount--;
            remove(fulladdress);
        }    
    }
    return amount;
}

/*
 ** finds and deletes all logs shipped by filebeat
*/
static int delete_shipped(void){
    struct stat st;
    FILE * fp;
    FILE * fplr;
    char * line =NULL;
    size_t len = 0;
    ssize_t read;
    char **dirlist;
    int canRead = 1;
    int amount = 0;
    int i = 0;
      
      get_log_files(ELK_LOGGER_WRITER_LOGS_PATH, &dirlist, &amount);
       
       if(!amount){
           free(dirlist);
        return amount;
    }
       
       const int arrsize = amount;
    
    
       int *size = malloc(amount* sizeof(int));
       
       for(i=0;i<arrsize;i++){
           size[i]=0;
       }

    fplr = OPEN_FROM_LOGS("lastread.txt", "r");
    
    long start = 0;    
    
    if(fplr != NULL){
        if(fscanf (fplr, "%ld", &start)==0){
            start = 0;
        }
        else{
            fclose(fplr);
        }
    }
    
    fp = OPEN_FROM_LOGS("log.json", "r");
    
    if (fp == NULL){
        free(size);
        for(i = 0; i<arrsize; i++){
            free(dirlist[i]);
           }
        free(dirlist);
    
        printf("ERROR: fp %s\n", strerror(errno));
        return amount;
    }
        
    if(start != 0){
        if(fseek(fp, start, SEEK_SET)==0){
            printf("ERROR: fseek %s\n", strerror(errno));
            canRead = 0;
        }    
    }
    
    if(canRead){
        //reads the log file created by filebeat
        while((read = getline(&line, &len, fp)) != -1){
            extract_log_size(line, size, dirlist, arrsize);
        }
        fclose(fp);
    }

    
    fplr = OPEN_FROM_LOGS("lastread.txt", "w");
    
    if(fplr!=NULL){
        if(stat(ELK_LOGGER_WRITER_LOGS_PATH "log.json", &st)==0){
            fprintf(fplr,"%zd",st.st_size);
        }
        fclose(fplr);
       }

    //checks for each file if it has been fully shipped 
    amount = delete_fully_shipped(size,arrsize,dirlist,ELK_LOGGER_WRITER_LOGS_PATH);
    
    if (line){
        free(line);
    }
    
    for(i = 0; i<arrsize; i++){
        free(dirlist[i]);
    }
    
    free(dirlist);
    
    free(size);
    
    return 1;
}

/**
 * creates and opens a new log file
 */
static int elk_logger_writer_open_file_for_write(void) {

    if(auto_delete){
        int unshipped = delete_shipped();
        
        if (unshipped > MAX_UNSHIPPED_LOGS)
            printf("WARNING: there are %d unshipped logs (might need to increase filebeat workers)\n",unshipped);
    }

    char buf[TIME_STAMP_LEN];
    char log_name[TIME_STAMP_LEN+30];

    elk_logger_writer_get_time_string(buf);
    sprintf(log_name, ELK_LOGGER_WRITER_LOGS_PATH "elk_log_file-%s.log", buf);
    elk_logger_writer_obj.log_file = open(log_name, O_WRONLY | O_CREAT, 0644);

    if (0 >= elk_logger_writer_obj.log_file) {
        return -1;
    }

    return 0;
}

/**
 * closes the log file in use
 */
static void elk_logger_writer_close_file(void) {
    if (0 >= elk_logger_writer_obj.log_file)
        close(elk_logger_writer_obj.log_file);
}

void elk_logger_writer_init(void) {
    elk_logger_writer_obj.log_file_size = 10 *1024 * 1024; // 10 MB
    elk_logger_writer_obj.curr_size = 0;

    int retval = system("mkdir -p " ELK_LOGGER_WRITER_LOGS_PATH);
    
    if(!auto_delete){
        retval = system("rm -rf " ELK_LOGGER_WRITER_LOGS_PATH "*.log");
    }
    
    retval = elk_logger_writer_open_file_for_write();

    if (retval == -1) {
        elk_logger_writer_free();
        return;
    }
}

/**
 * frees the writer
 */
void elk_logger_writer_free(void) {
    elk_logger_writer_close_file();
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
