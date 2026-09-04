// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "logging_rt_analyzer.h"
#include "onfi.h"

/* Devices Configuration */
ssd_config_t* devices = NULL;
uint8_t device_count = 0;

bool calculate_derived_values(ssd_config_t *device);
bool validate_namespace_capacity(ssd_config_t *device);
bool parse_config_line(const char* key, FILE* file, ssd_config_t* device);
bool parse_ns_config_line(const char* key, FILE* file, ssd_config_t* device, int ns_idx);
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
    int current_ns_index = -1;
    uint32_t ns_num = 0;
    int ns_key_len = 0;
    uint32_t i = 0;
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
            current_ns_index = -1;

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

        ns_num = 0;
        ns_key_len = 0;
        if (sscanf(key, "[ns%2u]%n", &ns_num, &ns_key_len) == 1 && ns_key_len > 0 && key[ns_key_len] == '\0') {
            if (ns_num == 0 || ns_num > MAX_NUMBER_OF_NAMESPACES)
                RERR(, "Invalid namespace number %u\n", ns_num);
            current_ns_index = (int)(ns_num - 1);
            continue;
        }

        if (current_device == NULL) {
            RERR(, "Configuration parameter found before device header: %s\n", key);
        }

        if (current_ns_index >= 0) {
            if (!parse_ns_config_line(key, pfData, current_device, current_ns_index)) {
                RERR(, "Unknown namespace configuration option: %s\n", key);
            }
            continue;
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
        if (!calculate_derived_values(&devices[i])) {
            free(devices);
            devices = NULL;
            device_count = 0;
            return;
        }
    }

    g_device_locks = (pthread_mutex_t*)calloc(sizeof(pthread_mutex_t) * device_count, 1);
    if (NULL == g_device_locks)
        RERR(, "g_device_locks allocation failed!\n");

    for (i = 0; i < device_count; i++) {
        if (pthread_mutex_init(&g_device_locks[i], NULL) != 0) {
            uint32_t j;
            for (j = 0; j < i; j++) {
                pthread_mutex_destroy(&g_device_locks[j]);
            }
            free(g_device_locks);
            g_device_locks = NULL;
            RERR(, "g_device_locks mutex init failed!\n");
        }
    }

    inverse_mappings_manager = (inverse_mapping_manager_t*)calloc(sizeof(inverse_mapping_manager_t) * device_count, 1);
    if (NULL == inverse_mappings_manager)
        RERR(, "inverse_mappings_manager allocation failed!\n");

    mapping_table = (uint64_t**)calloc(sizeof(uint64_t*) * device_count, 1);
    if (NULL == mapping_table)
        RERR(, "mapping_table allocation failed!\n");

    obj_manager = (obj_strategy_manager_t*)calloc(sizeof(obj_strategy_manager_t) * device_count, 1);
    if (NULL == obj_manager)
        RERR(, "obj_manager allocation failed!\n");

    if (mkdir(OSD_PATH, 0777) != 0 && errno != EEXIST)
        RERR(, "Failed to create " OSD_PATH " directory\n");

    g_init_ftl = (int*)calloc(sizeof(int) * device_count, 1);
    if (NULL == g_init_ftl)
        RERR(, "g_init_ftl allocation failed!\n");

    g_onfi_devices = (onfi_device_t*)calloc(sizeof(onfi_device_t) * device_count, 1);
    if (NULL == g_onfi_devices)
        RERR(, "g_onfi_devices allocation failed!\n");

    g_onfi_mt_managers = (onfi_mt_manager_t*)calloc(sizeof(onfi_mt_manager_t) * device_count, 1);
    if (NULL == g_onfi_mt_managers)
        RERR(, "g_onfi_mt_managers allocation failed!\n");

    g_onfi_device_locks = (pthread_mutex_t*)calloc(sizeof(pthread_mutex_t) * device_count, 1);
    if (NULL == g_onfi_device_locks)
        RERR(, "g_onfi_device_locks allocation failed!\n");

    ssds_manager = (ssd_manager_t*)calloc(sizeof(ssd_manager_t) * device_count, 1);
    if (NULL == ssds_manager)
        RERR(, "ssds_manager allocation failed!\n");

    gc_threads = calloc(device_count, sizeof(*gc_threads));
    if (NULL == gc_threads)
        RERR(, "gc_threads allocation failed!\n");

    rt_log_stats = (RTLogStatistics**)calloc(sizeof(RTLogStatistics*) * device_count, 1);
    if (NULL == rt_log_stats)
        RERR(, "rt_log_stats allocation failed!\n");

    analyzers_storage = (LoggerAnalyzerStorage**)calloc(sizeof(LoggerAnalyzerStorage*) * device_count, 1);
    if (NULL == analyzers_storage)
        RERR(, "analyzers_storage allocation failed!\n");

    log_manager = (LogManager**)calloc(sizeof(LogManager*) * device_count, 1);
    if (NULL == log_manager)
        RERR(, "log_manager allocation failed!\n");

    log_manager_threads = (pthread_t*)calloc(sizeof(pthread_t) * device_count, 1);
    if (NULL == log_manager_threads)
        RERR(, "log_manager_threads allocation failed!\n");

}

void TERM_SSD_CONFIG(void)
{
    if (g_device_locks != NULL) {
        uint8_t lock_index;
        for (lock_index = 0; lock_index < device_count; lock_index++) {
            pthread_mutex_destroy(&g_device_locks[lock_index]);
        }

        free(g_device_locks);
        g_device_locks = NULL;
    }

    free(inverse_mappings_manager);
    inverse_mappings_manager = NULL;

    free(mapping_table);
    mapping_table = NULL;

    free(obj_manager);
    obj_manager = NULL;

    if (rmdir(OSD_PATH) != 0 && errno != ENOENT) {
        RERR(, "Failed to remove " OSD_PATH " directory\n");
    }

    free(g_init_ftl);
    g_init_ftl = NULL;

    free(g_onfi_devices);
    g_onfi_devices = NULL;

    free(g_onfi_mt_managers);
    g_onfi_mt_managers = NULL;

    free(g_onfi_device_locks);
    g_onfi_device_locks = NULL;

    free(ssds_manager);
    ssds_manager = NULL;

    free(gc_threads);
    gc_threads = NULL;

    if (rt_log_stats != NULL) {
        uint8_t i;
        for (i = 0; i < device_count; i++) {
            if (rt_log_stats[i] != NULL) {
                free(rt_log_stats[i]);
                rt_log_stats[i] = NULL;
            }
        }
        free(rt_log_stats);
        rt_log_stats = NULL;
    }

    free(analyzers_storage);
    analyzers_storage = NULL;

    free(log_manager);
    log_manager = NULL;

    free(log_manager_threads);
    log_manager_threads = NULL;

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
    if (strcmp(key, "ONFI_MANAGER_THREADS") == 0) {
        return fscanf(file, "%d", &device->onfi_manager_threads) == 1;
    }
    if (strcmp(key, "ONFI_MANAGER_QUEUE_SIZE") == 0) {
        return fscanf(file, "%d", &device->onfi_manager_queue_size) == 1;
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

bool parse_ns_config_line(const char* key, FILE* file, ssd_config_t* device, int ns_idx) {
    if (strcmp(key, "STORAGE_STRATEGY") == 0) {
        if (fscanf(file, "%d", &device->ns_storage_strategy[ns_idx]) != 1) return false;
        // Propagate object strategy to the device level so the FTL selects the
        // correct code path. This is a temporary shim: once the FTL supports
        // per-namespace strategy selection, device->storage_strategy should be
        // derived only from device-level config, not from individual namespaces.
        if (device->ns_storage_strategy[ns_idx] == STRATEGY_OBJECT)
            device->storage_strategy = STRATEGY_OBJECT;
        return true;
    }
    if (strcmp(key, "NAMESPACE_PAGE_NB") == 0)
        return fscanf(file, "%" SCNu64, &device->ns_namespace_page_nb[ns_idx]) == 1;
    if (strcmp(key, "SIZE") == 0)
        return fscanf(file, "%" SCNu64, &device->namespaces_size[ns_idx]) == 1;
    if (strcmp(key, "OBJECT_KEY_SIZE") == 0)
        return fscanf(file, "%" SCNu64, &device->ns_object_key_size[ns_idx]) == 1;
    if (strcmp(key, "OBJECT_MAX_VALUE_SIZE") == 0)
        return fscanf(file, "%" SCNu64, &device->ns_object_max_value_size[ns_idx]) == 1;
    if (strcmp(key, "OBJECT_MAX_CAPACITY") == 0)
        return fscanf(file, "%" SCNu64, &device->ns_object_max_capacity[ns_idx]) == 1;
    return false;
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

// Get the number of pages in a block.
uint64_t GET_PAGE_NB(uint8_t device_index){
    if (devices == NULL) {
        return 0;
    }
    return devices[device_index].page_nb;
}

// Get the number of blocks in a flash.
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

uint64_t GET_TOTAL_NUMBER_OF_PAGES(uint8_t device_index) {
    return GET_PAGE_NB(device_index) * GET_BLOCK_NB(device_index) * GET_FLASH_NB(device_index);
}

ssd_config_t* GET_DEVICES(void){
    return devices;
}

// Count the namespaces configured on a device: the number of contiguous
// non-zero namespaces_size[] entries starting from index 0.
uint32_t GET_NAMESPACE_COUNT(uint8_t device_index){
    uint32_t count = 0;
    if (devices == NULL) {
        return 0;
    }
    while (count < MAX_NUMBER_OF_NAMESPACES &&
           devices[device_index].namespaces_size[count] != 0) {
        count++;
    }
    return count;
}

// Get the configured size (in bytes) of a namespace within a device.
uint64_t GET_NAMESPACE_SIZE(uint8_t device_index, uint32_t ns_index){
    if (devices == NULL || ns_index >= MAX_NUMBER_OF_NAMESPACES) {
        return 0;
    }
    return devices[device_index].namespaces_size[ns_index];
}

bool validate_namespace_capacity(ssd_config_t *device) {
    uint64_t disk_bytes = (uint64_t)device->page_size * device->page_nb
                          * device->block_nb * device->flash_nb;
    uint64_t ns_total = 0;
    uint32_t j;
    for (j = 0; j < MAX_NUMBER_OF_NAMESPACES; j++)
        ns_total += device->namespaces_size[j];
    if (ns_total == 0)
        return true;
    if (ns_total > disk_bytes) {
        PERR("Device %s: namespace total %" PRIu64 " bytes exceeds disk capacity %" PRIu64 " bytes\n",
             device->device_name, ns_total, disk_bytes);
        return false;
    }
    if (ns_total < disk_bytes)
        PERR("WARNING: Device %s: namespaces use %" PRIu64 " of %" PRIu64 " bytes (%.1f%% utilized)\n",
             device->device_name, ns_total, disk_bytes,
             100.0 * (double)ns_total / (double)disk_bytes);
    return true;
}

bool calculate_derived_values(ssd_config_t* device) {
    // Exception Handler
    if (device->flash_nb < device->channel_nb)
        RERR(false, "Wrong CHANNEL_NB %d\n", device->channel_nb);
    if (device->planes_per_flash != 1 && device->planes_per_flash % 2 != 0)
        RERR(false, "Wrong PLANES_PER_FLASH %d\n", device->planes_per_flash);

    // Calculate derived values
    device->sectors_per_page = device->page_size / device->sector_size;
    device->pages_per_flash = device->page_nb * device->block_nb;

    device->block_mapping_entry_nb = (uint64_t)device->block_nb * device->flash_nb;
    device->pages_in_ssd = device->page_nb * device->block_nb * device->flash_nb;

    if (device->channel_nb > 0)
        device->way_nb = device->flash_nb / device->channel_nb;

    // reserve one block for GC
    device->sectors_in_ssd = device->sectors_per_page * (device->pages_in_ssd - device->page_nb);

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

    device->gc_low_thr_page_nb = device->page_nb * (100 - device->gc_low_thr) * device->block_mapping_entry_nb / 100;
    device->gc_hi_thr_page_nb = device->page_nb * (100 - device->gc_hi_thr) * device->block_mapping_entry_nb / 100;
    device->gc_low_thr_interval_sec = 10;
    device->gc_hi_thr_interval_sec = 1;

    // if ONFI manager threads didn't set, change it to 1
    if (device->onfi_manager_threads <= 0) {
        device->onfi_manager_threads = 1;
    }

    // if ONFI manager queue size didn't set, default to 1024
    if (device->onfi_manager_queue_size <= 0) {
        device->onfi_manager_queue_size = 1024;
    }
    // Validate that the namespaces fit within the device capacity (errors on
    // overfit, warns on underfit) as part of finalizing the device.
    return validate_namespace_capacity(device);
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
