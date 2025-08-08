// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

/* Devices Configuration */
ssd_config_t devices[MAX_DEVICES];
uint8_t device_count = 0;

bool parse_header(char* str, ssd_config_t* device);
void calculate_derived_values(ssd_config_t *device);
bool parse_config_line(const char* key, FILE* file, ssd_config_t* device);
void update_globals(void);

void INIT_SSD_CONFIG(void)
{
    FILE *pfData = fopen("./data/ssd.conf", "r");

    if (pfData == NULL){
        PERR("Can't open file: ./data/ssd.conf\n");
        exit(1);
    }

    memset(devices, 0x00, sizeof(ssd_config_t) * MAX_DEVICES);

    char key[64];
    int i;
    uint8_t device_index = 0;
    ssd_config_t *current_device = NULL;

    while (fscanf(pfData, "%63s", key) != EOF) {

        // Skip empty lines
        if (!strlen(key))
            continue;

        if (key[0] == '[') {
            if (device_index > MAX_DEVICES) {
                RERR(, "Maximum number of devices reached\n");
            }

            // Finalize previous device
            if (current_device != NULL) {
                calculate_derived_values(current_device);
            }
            current_device = &devices[device_index];

            // Create data directory for the device
            char* dirname = GET_DATA_FILENAME("", device_index);
            if (dirname == NULL)
                RERR(, "GET_DATA_FILENAME failed!\n");
        
            mkdir(dirname, 0777);
            free(dirname);

            ++device_index;
            ++device_count;
            if (parse_header(key, current_device)) {
                RERR(, "Invalid nvme device format: %s\n", key);
            }
            continue;
        }
        if (current_device == NULL) {
            RERR(, "Configuration parameter found before device header: %s\n", key);
        }
        if (strcmp(key, "STAT_PATH") == 0){
            if (fgets(current_device->stat_path, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read STAT_PATH\n");
            continue;
        }

        if (strcmp(key, "OSD_PATH") == 0){
            if (fgets(current_device->osd_path, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read OSD_PATH\n");
            continue;
        }

        if (sscanf(key, "NS%d", &i) == 1){
            if (i > MAX_NUMBER_OF_NAMESPACES || i <= 0)
                RERR(, "Invalid namespaces index\n");

            if (fscanf(pfData, "%" SCNu64, &current_device->namespaces_size[i-1]) == EOF){
                RERR(, "Can't read %s\n", key);
            }

            continue;
        }

        if (parse_config_line(key, pfData, current_device)) {
            RERR(, "Unknown configuration option: %s\n", key);
        }
    }

    // Finalize the last device
    if (current_device != NULL) {
        calculate_derived_values(current_device);
    }

    fclose(pfData);
}

bool parse_config_line(const char* key, FILE* file, ssd_config_t* device) {

    if (strcmp(key, "FILE_NAME") == 0) {
        return fscanf(file, "%s", device->file_name) == 1;
    }
    if (strcmp(key, "PAGE_SIZE") == 0) {
        return fscanf(file, "%" SCNu32, &device->page_size) == 1;
    }
    if (strcmp(key, "PAGE_NB") == 0) {
        return fscanf(file, "%" SCNu64, &device->page_nb) == 1;
    }
    if (strcmp(key, "SECTOR_SIZE") == 0) {
        return fscanf(file, "%" SCNu32, &device->sector_size) == 1;
    }
    if (strcmp(key, "FLASH_NB") == 0) {
        return fscanf(file, "%" SCNu32, &device->flash_nb) == 1;
    }
    if (strcmp(key, "BLOCK_NB") == 0) {
        return fscanf(file, "%" SCNu64, &device->block_nb) == 1;
    }
    if (strcmp(key, "PLANES_PER_FLASH") == 0) {
        return fscanf(file, "%" SCNu32, &device->planes_per_flash) == 1;
    }
    if (strcmp(key, "REG_WRITE_DELAY") == 0) {
        return fscanf(file, "%d", &device->reg_write_delay) == 1;
    }
    if (strcmp(key, "CELL_PROGRAM_DELAY") == 0) {
        return fscanf(file, "%d", &device->cell_program_delay) == 1;
    }
    if (strcmp(key, "REG_READ_DELAY") == 0) {
        return fscanf(file, "%d", &device->reg_read_delay) == 1;
    }
    if (strcmp(key, "CELL_READ_DELAY") == 0) {
        return fscanf(file, "%d", &device->cell_read_delay) == 1;
    }
    if (strcmp(key, "BLOCK_ERASE_DELAY") == 0) {
        return fscanf(file, "%d", &device->block_erase_delay) == 1;
    }
    if (strcmp(key, "CHANNEL_SWITCH_DELAY_R") == 0) {
        return fscanf(file, "%d", &device->channel_switch_delay_r) == 1;
    }
    if (strcmp(key, "CHANNEL_SWITCH_DELAY_W") == 0) {
        return fscanf(file, "%d", &device->channel_switch_delay_w) == 1;
    }
    if (strcmp(key, "DSM_TRIM_ENABLE") == 0) {
        return fscanf(file, "%d", &device->dsm_trim_enable) == 1;
    }
    if (strcmp(key, "IO_PARALLELISM") == 0) {
        return fscanf(file, "%d", &device->io_parallelism) == 1;
    }
    if (strcmp(key, "CHANNEL_NB") == 0) {
        return fscanf(file, "%" SCNu32, &device->channel_nb) == 1;
    }
    if (strcmp(key, "STAT_TYPE") == 0) {
        return fscanf(file, "%d", &device->stat_type) == 1;
    }
    if (strcmp(key, "STAT_SCOPE") == 0) {
        return fscanf(file, "%d", &device->stat_scope) == 1;
    }
    if (strcmp(key, "STORAGE_STRATEGY") == 0) {
        return fscanf(file, "%d", &device->storage_strategy) == 1;
    }
    if (strcmp(key, "GC_LOW_THR") == 0) {
        return fscanf(file, "%d", &device->gc_low_thr) == 1;
    }
    if (strcmp(key, "GC_HI_THR") == 0) {
        return fscanf(file, "%d", &device->gc_hi_thr) == 1;
    }

#if defined FTL_MAP_CACHE
    if (strcmp(key, "CACHE_IDX_SIZE") == 0) {
        return fscanf(file, "%d", &CACHE_IDX_SIZE) == 1;
    }
#endif
#ifdef SSD_WRITE_BUFFER
    if (strcmp(key, "WRITE_BUFFER_SIZE") == 0) {
        return fscanf(file, "%u", &WRITE_BUFFER_SIZE) == 1;
    }
#endif
#if defined FAST_FTL || defined LAST_FTL
    if (strcmp(key, "LOG_RAND_BLOCK_NB") == 0) {
        return fscanf(file, "%d", &LOG_RAND_BLOCK_NB) == 1;
    }
    if (strcmp(key, "LOG_SEQ_BLOCK_NB") == 0) {
        return fscanf(file, "%d", &LOG_SEQ_BLOCK_NB) == 1;
    }
#endif

    return false; // Unknown key
}


char* GET_FILE_NAME(uint8_t device_index){
	return devices[device_index].file_name;
}

uint32_t GET_SECTOR_SIZE(uint8_t device_index){
    return devices[device_index].sector_size;
}

uint32_t GET_PAGE_SIZE(uint8_t device_index){
    return devices[device_index].page_size;
}

ssd_config_t* GET_DEVICES(void){
    return devices;
}

bool parse_header(char *str, ssd_config_t *device) {
    if (str == NULL || device == NULL) {
        return false;
    }

    int disk_num, offset;

    // Match string of format "[nvmeXX]"
    if (sscanf(str, "[nvme%2d]%n", &disk_num, &offset) != 1) {
        return false;
    }

    // Check if disk_num is valid and offset matches the expected length
    if (disk_num == 0 || strlen(str) != (unsigned int) offset) {
        return false;
    }

    // Start after the opening bracket '[' and copy 6 characters: "nvmeXX"
    strncpy(device->device_name, str + 1, 6);
    device->device_name[6] = '\0';

    return true;
}

void calculate_derived_values(ssd_config_t* device) {
    // Exception Handler
    if (device->flash_nb < device->channel_nb)
        RERR(, "Wrong CHANNEL_NB %d\n", device->channel_nb);
    if (device->planes_per_flash != 1 && device->planes_per_flash % 2 != 0)
        RERR(, "Wrong PLANES_PER_FLASH %d\n", device->planes_per_flash);

    // Calculate derived values
    device->sectors_per_page = device->page_size / device->sector_size;
    device->pages_per_flash = device->page_nb * device->block_nb;
    device->sector_nb = (uint64_t)device->sectors_per_page * device->page_nb *
                        device->block_nb * device->flash_nb;

    device->block_mapping_entry_nb = (uint64_t)device->block_nb * device->flash_nb;
    device->pages_in_ssd = device->page_nb * device->block_nb * device->flash_nb;

    // Validate namespace sizes
    uint64_t total_namespace_size = 0;
    int i;
    for (i = 0; i < MAX_NUMBER_OF_NAMESPACES; i++) {
        if (UINT64_MAX - total_namespace_size < device->namespaces_size[i]) {
            RERR(, "ERROR, overflow detected at total size of namespaces\n");
        }
        total_namespace_size += device->namespaces_size[i];
    }

    if (device->block_nb * device->flash_nb < total_namespace_size) {
        RERR(, "ERROR, The total sum sizes of all namespaces is larger than the SSD size\n");
    }

#ifdef PAGE_MAP
    device->page_mapping_entry_nb = device->page_nb * device->block_nb * device->flash_nb;
    device->each_empty_table_entry_nb = device->block_nb / device->planes_per_flash;
    device->empty_table_entry_nb = device->flash_nb * device->planes_per_flash;
    device->victim_table_entry_nb = device->flash_nb * device->planes_per_flash;
    device->data_block_nb = device->block_nb;

    device->gc_threshold = 0.2;
    device->gc_threshold_block_nb = (int)((1-device->gc_threshold) * (double)device->block_mapping_entry_nb);
    device->gc_threshold_block_nb_each = (int)((1-device->gc_threshold) * (double)device->each_empty_table_entry_nb);
    device->gc_victim_nb = device->gc_threshold_block_nb * 0.5;

    double gc_l2_threshold = 0.1;
    device->gc_l2_threshold_block_nb = (int)((1-gc_l2_threshold) * (double)device->block_mapping_entry_nb);
#endif
}


uint64_t GET_NAMESPACE_TOTAL_SIZE(uint8_t device_index) {
    int i;
    uint64_t NAMESPACE_TOTAL_SIZE = 0;

    for (i = 0; i < MAX_NUMBER_OF_NAMESPACES; i++){
        // Check for overflow in the sum of namespaces size.
        if (UINT64_MAX - NAMESPACE_TOTAL_SIZE < devices[device_index].namespaces_size[i]){
            RERR(UINT64_MAX, "ERROR, overflow detected at total size of namespaces\n");
        }

        NAMESPACE_TOTAL_SIZE += devices[device_index].namespaces_size[i];
    }

    return NAMESPACE_TOTAL_SIZE;
}


char* GET_DATA_FILENAME(const char* filename, uint8_t device_index) {
    int filename_size = strlen(filename) + strlen("./data/") + 3;

    char* res = (char*)malloc(filename_size);
    if (NULL == res) {
        RERR(res, "Malloc failed\n");
    }

    memset(res, 0, filename_size);

    strncat(res, "./data/", filename_size);
    res[strlen("./data/")] = device_index + '0';
    strncat(res, "/", filename_size);
    strncat(res, filename, filename_size);

    return res;
}
