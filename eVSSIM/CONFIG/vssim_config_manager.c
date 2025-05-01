// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include <string.h>

/* Devices Configuration */
ssd_config_t devices[MAX_DEVICES];
uint8_t device_count = 0;
uint8_t current_device_index = 0;

/* SSD Configuration */
uint32_t SECTOR_SIZE;
uint32_t PAGE_SIZE;

uint64_t SECTOR_NB;
uint64_t PAGE_NB;
uint32_t FLASH_NB;
uint64_t BLOCK_NB;
uint32_t CHANNEL_NB;
uint32_t PLANES_PER_FLASH;

uint32_t SECTORS_PER_PAGE;
uint64_t PAGES_PER_FLASH;
uint64_t PAGES_IN_SSD;

uint32_t WAY_NB;

/* Mapping Table */
uint32_t DATA_BLOCK_NB;
uint64_t BLOCK_MAPPING_ENTRY_NB;		// added by js

#ifdef PAGE_MAP
uint64_t PAGE_MAPPING_ENTRY_NB;		// added by js
uint64_t EACH_EMPTY_TABLE_ENTRY_NB;	// added by js

uint64_t EMPTY_TABLE_ENTRY_NB;		// added by js
uint64_t VICTIM_TABLE_ENTRY_NB;		// added by js
#endif

/* NAND Flash Delay */
int REG_WRITE_DELAY;
int CELL_PROGRAM_DELAY;
int REG_READ_DELAY;
int CELL_READ_DELAY;
int BLOCK_ERASE_DELAY;
int CHANNEL_SWITCH_DELAY_W;
int CHANNEL_SWITCH_DELAY_R;

int DSM_TRIM_ENABLE;
int IO_PARALLELISM;

int STAT_SCOPE;
int STAT_TYPE;

/* Garbage Collection */
#ifdef PAGE_MAP
double GC_THRESHOLD;			// added by js
int GC_THRESHOLD_BLOCK_NB;		// added by js
int GC_THRESHOLD_BLOCK_NB_EACH;		// added by js
int GC_VICTIM_NB;
double GC_L2_THRESHOLD;
int GC_L2_THRESHOLD_BLOCK_NB;
#endif

int GC_LOW_THR;
int GC_HI_THR;

/* Storage strategy (1 = sector-based, 2 = object-based */
int STORAGE_STRATEGY;

char DEVICE_NAME[MAX_DEVICE_NAME_LEN] = {0,};
char gFile_Name[PATH_MAX] = {0,};
char OSD_PATH[PATH_MAX] = {0,};
char STAT_PATH[PATH_MAX] = {0,};

config_param options[] = {
    {"FILE_NAME", "%s", gFile_Name},
    {"PAGE_SIZE", "%d", &PAGE_SIZE},
    {"PAGE_NB", "%d", &PAGE_NB},
    {"SECTOR_SIZE", "%d", &SECTOR_SIZE},
    {"FLASH_NB", "%d", &FLASH_NB},
    {"BLOCK_NB", "%d", &BLOCK_NB},
    {"PLANES_PER_FLASH", "%d", &PLANES_PER_FLASH},
    {"REG_WRITE_DELAY", "%d", &REG_WRITE_DELAY},
    {"CELL_PROGRAM_DELAY", "%d", &CELL_PROGRAM_DELAY},
    {"REG_READ_DELAY", "%d", &REG_READ_DELAY},
    {"CELL_READ_DELAY", "%d", &CELL_READ_DELAY},
    {"BLOCK_ERASE_DELAY", "%d", &BLOCK_ERASE_DELAY},
    {"CHANNEL_SWITCH_DELAY_R", "%d", &CHANNEL_SWITCH_DELAY_R},
    {"CHANNEL_SWITCH_DELAY_W", "%d", &CHANNEL_SWITCH_DELAY_W},
    {"DSM_TRIM_ENABLE", "%d", &DSM_TRIM_ENABLE},
    {"IO_PARALLELISM", "%d", &IO_PARALLELISM},
    {"CHANNEL_NB", "%d", &CHANNEL_NB},
    {"STAT_TYPE", "%d", &STAT_TYPE},
    {"STAT_SCOPE", "%d", &STAT_SCOPE},
    {"STORAGE_STRATEGY", "%d", &STORAGE_STRATEGY},
    {"GC_LOW_THR", "%d", &GC_LOW_THR},
    {"GC_HI_THR", "%d", &GC_HI_THR},
#if defined FTL_MAP_CACHE
    {"CACHE_IDX_SIZE", "%d", &CACHE_IDX_SIZE},
#endif
#ifdef SSD_WRITE_BUFFER
    {"WRITE_BUFFER_SIZE", "%u", &WRITE_BUFFER_SIZE},
#endif
#if defined FAST_FTL || defined LAST_FTL
    {"LOG_RAND_BLOCK_NB", "%d", &LOG_RAND_BLOCK_NB},
    {"LOG_SEQ_BLOCK_NB", "%d", &LOG_SEQ_BLOCK_NB},
#endif
    {NULL, NULL, NULL},
};

int parse_header(char* str);
void update_globals(void);
void set_device_object(ssd_config_t *device, bool should_zero_globals);

void INIT_SSD_CONFIG(void)
{
    FILE *pfData = fopen("./data/ssd.conf", "r");

    if (pfData == NULL){
        PERR("Can't open file: ./data/ssd.conf\n");
        exit(1);
    }

    memset(devices, 0x00, sizeof(ssd_config_t) * MAX_DEVICES);

    int i = 0;
    uint8_t deviceIndex = 0;

    char *szCommand = (char*)malloc(1024);
    memset(szCommand, 0x00, 1024);
    while (fscanf(pfData, "%s", szCommand) != EOF){
        if (szCommand[0] == '[') {
            if (deviceIndex > MAX_DEVICES) {
                RERR(, "Maximum number of devices reached\n");
            }
            if (deviceIndex != 0) {
                update_globals();
                set_device_object(&devices[deviceIndex - 1], true);
            }
            ++deviceIndex;
            if (parse_header(szCommand) == 0) {
                RERR(, "Invalid nvme device format: %s\n", szCommand);
            }
            continue;
        }
        if (strcmp(szCommand, "STAT_PATH") == 0){
            if (fgets(STAT_PATH, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read STAT_PATH\n");
            continue;
        }
        if (strcmp(szCommand, "OSD_PATH") == 0){
            if (fgets(OSD_PATH, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read OSD_PATH\n");
            continue;
        }
        for (i = 0; options[i].name != NULL; i++)
            if (strcmp(szCommand, options[i].name) == 0)
                break;
        if (options[i].name == NULL)
            RERR(, "Wrong option %s\n", szCommand);
        if (fscanf(pfData, options[i].type, options[i].ptr) == EOF)
            RERR(, "Can't read %s\n", szCommand);

        memset(szCommand, 0x00, 1024);
    }

    // Update the last device
    update_globals();
    set_device_object(&devices[deviceIndex - 1], false);

    fclose(pfData);
	free(szCommand);
}

char* GET_FILE_NAME(void){
	return gFile_Name;
}

uint32_t GET_SECTOR_SIZE(void){
    return SECTOR_SIZE;
}

uint32_t GET_PAGE_SIZE(void){
    return PAGE_SIZE;
}

ssd_config_t* GET_DEVICES(void){
    return devices;
}

int parse_header(char *str) {
    if (str == NULL) {
        return 0;
    }

    int disk_num, offset;

    // Match string of format "[nvmeXX]"
    if (sscanf(str, "[nvme%2d]%n", &disk_num, &offset) != 1) {
        return 0;
    }

    // Extra validation
    if (disk_num == 0 || strlen(str) != offset) {
        return 0;
    }

    // Copy device name to global buffer (without brackets)
    // Start after the opening bracket '[' and copy 6 characters: "nvmeXX"
    strncpy(DEVICE_NAME, str + 1, 6);
    DEVICE_NAME[6] = '\0';

    return 1;
}

void update_globals(void) {
    /* Exception Handler */
    if (FLASH_NB < CHANNEL_NB)
        RERR(, "Wrong CHANNEL_NB %d\n", CHANNEL_NB);
    if (PLANES_PER_FLASH != 1 && PLANES_PER_FLASH % 2 != 0)
        RERR(, "Wrong PLANAES_PER_FLASH %d\n", PLANES_PER_FLASH);

	/* SSD Configuration */
	SECTORS_PER_PAGE = PAGE_SIZE / SECTOR_SIZE;
	PAGES_PER_FLASH = PAGE_NB * BLOCK_NB;
	SECTOR_NB = (uint64_t)SECTORS_PER_PAGE * (uint64_t)PAGE_NB * (uint64_t)BLOCK_NB * (uint64_t)FLASH_NB;

	/* Mapping Table */
	BLOCK_MAPPING_ENTRY_NB = (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
	PAGES_IN_SSD = (int64_t)PAGE_NB * (int64_t)BLOCK_NB * (int64_t)FLASH_NB;

#ifdef PAGE_MAP
	PAGE_MAPPING_ENTRY_NB = (int64_t)PAGE_NB * (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
	EACH_EMPTY_TABLE_ENTRY_NB = (int64_t)BLOCK_NB / (int64_t)PLANES_PER_FLASH;

	EMPTY_TABLE_ENTRY_NB = FLASH_NB * PLANES_PER_FLASH;
	VICTIM_TABLE_ENTRY_NB = FLASH_NB * PLANES_PER_FLASH;

	DATA_BLOCK_NB = BLOCK_NB;
#endif

	/* Garbage Collection */
#ifdef PAGE_MAP
	GC_THRESHOLD = 0.2; // 70%
	GC_THRESHOLD_BLOCK_NB = (int)((1-GC_THRESHOLD) * (double)BLOCK_MAPPING_ENTRY_NB);
	GC_THRESHOLD_BLOCK_NB_EACH = (int)((1-GC_THRESHOLD) * (double)EACH_EMPTY_TABLE_ENTRY_NB);
	GC_VICTIM_NB = GC_THRESHOLD_BLOCK_NB * 0.5;//each time GC is called, clean 50% of threashold

    GC_L2_THRESHOLD = 0.1; //90%
	GC_L2_THRESHOLD_BLOCK_NB = (int)((1-GC_L2_THRESHOLD) * (double)BLOCK_MAPPING_ENTRY_NB);
#endif
}

void set_device_object(ssd_config_t* device, bool should_zero_globals) {
    strncpy(device->device_name, DEVICE_NAME, MAX_DEVICE_NAME_LEN);
    strncpy(device->file_name, gFile_Name, PATH_MAX);

    device->sector_size = SECTOR_SIZE;
    device->page_size = PAGE_SIZE;

	device->sector_nb = SECTOR_NB;
	device->page_nb = PAGE_NB;
	device->flash_nb = FLASH_NB;
	device->block_nb = BLOCK_NB;
	device->channel_nb = CHANNEL_NB;
	device->planes_per_flash = PLANES_PER_FLASH;

	device->sectors_per_page = SECTORS_PER_PAGE;
	device->pages_per_flash = PAGES_PER_FLASH;
	device->pages_in_ssd = PAGES_IN_SSD;
	
	device->way_nb = WAY_NB;

	device->data_block_nb = DATA_BLOCK_NB;
	device->block_mapping_entry_nb = BLOCK_MAPPING_ENTRY_NB;

#ifdef PAGE_MAP
    device->page_mapping_entry_nb = PAGE_MAPPING_ENTRY_NB;
    device->each_empty_table_entry_nb = EACH_EMPTY_TABLE_ENTRY_NB;

	device->empty_table_entry_nb = EMPTY_TABLE_ENTRY_NB;
	device->victim_table_entry_nb = VICTIM_TABLE_ENTRY_NB;
#endif

    device->reg_write_delay = REG_WRITE_DELAY;
    device->cell_program_delay = CELL_PROGRAM_DELAY;
	device->reg_read_delay = REG_READ_DELAY;
	device->cell_read_delay = CELL_READ_DELAY;
	device->block_erase_delay = BLOCK_ERASE_DELAY;
	device->channel_switch_delay_r = CHANNEL_SWITCH_DELAY_R;
	device->channel_switch_delay_w = CHANNEL_SWITCH_DELAY_W;

	device->dsm_trim_enable = DSM_TRIM_ENABLE;
	device->io_parallelism = IO_PARALLELISM;

#ifdef PAGE_MAP
    device->gc_threshold = GC_THRESHOLD;
    device->gc_threshold_block_nb = GC_THRESHOLD_BLOCK_NB;
	device->gc_threshold_block_nb_each = GC_THRESHOLD_BLOCK_NB_EACH;
	device->gc_victim_nb = GC_VICTIM_NB;
	device->gc_l2_threshold_block_nb = GC_L2_THRESHOLD_BLOCK_NB;
#endif

    device->gc_low_thr = GC_LOW_THR;
    device->gc_hi_thr = GC_HI_THR;

    device->stat_type = STAT_TYPE;
    device->stat_scope = STAT_SCOPE;
    strncpy(device->stat_path, STAT_PATH, PATH_MAX);
    strncpy(device->osd_path, OSD_PATH, PATH_MAX);

    // This is temporary for the tests to pass since their using globals
    if (should_zero_globals)
    {
        SECTOR_SIZE = 0;
        PAGE_SIZE = 0;

        SECTOR_NB = 0;
        PAGE_NB = 0;
        FLASH_NB = 0;
        BLOCK_NB = 0;
        CHANNEL_NB = 0;
        PLANES_PER_FLASH = 0;

        SECTORS_PER_PAGE = 0;
        PAGES_PER_FLASH = 0;
        PAGES_IN_SSD = 0;

        WAY_NB = 0;

        DATA_BLOCK_NB = 0;
        BLOCK_MAPPING_ENTRY_NB = 0;

        #ifdef PAGE_MAP
        PAGE_MAPPING_ENTRY_NB = 0;
        EACH_EMPTY_TABLE_ENTRY_NB = 0;

        EMPTY_TABLE_ENTRY_NB = 0;
        VICTIM_TABLE_ENTRY_NB = 0;
        #endif

        REG_WRITE_DELAY = 0;
        CELL_PROGRAM_DELAY = 0;
        REG_READ_DELAY = 0;
        CELL_READ_DELAY = 0;
        BLOCK_ERASE_DELAY = 0;
        CHANNEL_SWITCH_DELAY_W = 0;
        CHANNEL_SWITCH_DELAY_R = 0;

        DSM_TRIM_ENABLE = 0;
        IO_PARALLELISM = 0;

        STAT_SCOPE = 0;
        STAT_TYPE = 0;

        #ifdef PAGE_MAP
        GC_THRESHOLD = 0;
        GC_THRESHOLD_BLOCK_NB = 0;
        GC_THRESHOLD_BLOCK_NB_EACH = 0;
        GC_VICTIM_NB = 0;
        GC_L2_THRESHOLD = 0;
        GC_L2_THRESHOLD_BLOCK_NB = 0;
        #endif

        GC_LOW_THR = 0;
        GC_HI_THR = 0;

        STORAGE_STRATEGY = 0;

        memset(DEVICE_NAME, 0x00, MAX_DEVICE_NAME_LEN);
        memset(gFile_Name, 0x00, PATH_MAX);
        memset(OSD_PATH, 0x00, PATH_MAX);
        memset(STAT_PATH, 0x00, PATH_MAX);
    }
}
