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
ssd_config_t* devices = NULL;
uint8_t device_count = 0;

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

    char key[64];
    uint8_t device_index = 0;

    int i = 0;
    ssd_config_t *current_device = NULL;

    while (fscanf(pfData, "%63s", key) != EOF) {

        // Skip empty lines
        if (!strlen(key))
            continue;

        uint32_t disk_num = 0;

        // Match string of format "[nvmeXX]"
        if (sscanf(key, "[nvme%2u]", &disk_num) == 1) {
            if (disk_num == 0 || disk_num + 1 == INVALID_DEVICE_INDEX) {
                RERR(, "Invalid device number, out of scope\n");
            }

            if (current_device == NULL) {
                if (disk_num != 1) {
                    RERR(, "Invalid device number, starting from 1 not with %u\n", disk_num);
                }
            } else if (disk_num != (uint32_t)(device_index + 2)) {
                RERR(, "Invalid device number, not sequential. It should be %u not %u\n", (device_index + 2), disk_num);
            }

            // Set the current dev index.
            device_index = disk_num - 1;

            // Create data directory for the device
            char* dirname = GET_DATA_FILENAME(device_index, "");
            if (dirname == NULL)
                RERR(, "GET_DATA_FILENAME failed!\n");

            mkdir(dirname, 0777);
            free(dirname);

            devices = (ssd_config_t*)realloc(devices, sizeof(ssd_config_t) * (device_index + 1));
            if (NULL == devices)
                RERR(, "devices allocation failed!\n");

            // Clean the new config.
            memset((sizeof(ssd_config_t) * (device_index)) + (uint8_t*)devices, 0, sizeof(ssd_config_t));

            current_device = &devices[device_index];

            // Start after the opening bracket '[' and copy 6 characters: "nvmeXX"
            strncpy(current_device->device_name, key + 1, 6);
            current_device->device_name[6] = '\0';

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

            // Increase the counter on new namespace. 
            current_device->current_namespace_nb++;
            continue;
        }

        if (!parse_config_line(key, pfData, current_device)) {
            RERR(, "Unknown configuration option: %s.\n", key);
        }
    }

    fclose(pfData);

    // Set the device to the last device index + 1.
    device_count = device_index + 1;

    // Finalize the devices
    for (i = 0; i < device_count; i++)
    {
        calculate_derived_values(&devices[i]);
    }

    inverse_mappings_manager = (inverse_mapping_manager_t*)calloc(sizeof(inverse_mapping_manager_t) * device_count, 1);
    if (NULL == inverse_mappings_manager)
        RERR(, "inverse_mappings_manager allocation failed!\n");

    mapping_table = (uint64_t***)calloc(sizeof(uint64_t**) * device_count , 1);

    if (NULL == mapping_table)
        RERR(, "mapping_table allocation failed!\n");

    for (i = 0; i < device_count; i++)
    {
        mapping_table[i] = (uint64_t**)calloc(sizeof(uint64_t*) * MAX_NUMBER_OF_NAMESPACES, 1);
    }

    g_init_ftl = (int*)calloc(sizeof(int) * device_count, 1);
    if (NULL == g_init_ftl)
        RERR(, "g_init_ftl allocation failed!\n");

    ssds_manager = (ssd_manager_t*)calloc(sizeof(ssd_manager_t) * device_count, 1);
    if (NULL == ssds_manager)
        RERR(, "ssds_manager allocation failed!\n");
}

void TERM_SSD_CONFIG(void)
{
    uint32_t i;

    free(inverse_mappings_manager);
    inverse_mappings_manager = NULL;

    for (i = 0; i < device_count; i++)
    {
        free(mapping_table[i]);
    }

    free(mapping_table);
    mapping_table = NULL;

    free(g_init_ftl);
    g_init_ftl = NULL;

    free(ssds_manager);
    ssds_manager = NULL;

    free(devices);
    devices = NULL;

    device_count = 0;
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
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].sector_size;
}

uint32_t GET_PAGE_SIZE(uint8_t device_index){
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].page_size;
}

uint64_t GET_PAGE_NB(uint8_t device_index){
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].page_nb;
}

uint64_t GET_BLOCK_NB(uint8_t device_index){
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].block_nb;
}

uint32_t GET_FLASH_NB(uint8_t device_index){
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].flash_nb;
}

ssd_config_t* GET_DEVICES(void){
    return devices;
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

    device->block_mapping_entry_nb = (uint64_t)device->block_nb * device->flash_nb;
    device->pages_in_ssd = device->page_nb * device->block_nb * device->flash_nb;

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
#endif // PAGE_MAP
}

char* GET_DATA_FILENAME(uint8_t device_index, const char* filename) {
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
