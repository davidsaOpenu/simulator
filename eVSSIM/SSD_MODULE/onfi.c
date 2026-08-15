#include "onfi.h"
#include "common.h"
#include "vssim_config_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

onfi_flash_device_t **g_onfi_flash_devices = NULL;
onfi_manager_t *g_onfi_managers = NULL;
pthread_mutex_t *g_onfi_device_locks = NULL;

#define JEDEC_MANUFACTURER_ID 0xCC
#define DEVICE_ID 0x10

static inline void set_ready(onfi_status_reg_t *reg, bool ready)
{
    reg->RDY = (ready ? 1 : 0);
    reg->ARDY = (ready ? 1 : 0);
}

// Code & comments taken from ONFI 1.0 specification appendix A., adjusted for calculating from buffer instead of stdin
uint16_t _ONFI_CRC16(uint8_t *data, size_t data_size)
{
    // Bit by bit algorithm without augmented zero bytes
    const unsigned long crcinit = 0x4F4E; // Initial CRC value in the shift register
    const int order = 16;                 // Order of the CRC-16
    const unsigned long polynom = 0x8005; // Polynomial
    unsigned long i, j, c, bit;
    unsigned long crc = crcinit; // Initialize the shift register with 0x4F4E
    unsigned long data_in;
    unsigned long crcmask = ((((unsigned long)1 << (order - 1)) - 1) << 1) | 1;
    unsigned long crchighbit = (unsigned long)1 << (order - 1);
    // Input byte stream, one byte at a time, bits processed from MSB to LSB
    for (i = 0; i < data_size; ++i)
    {
        data_in = data[i];
        c = (unsigned long)data_in;
        for (j = 0x80; j; j >>= 1)
        {
            bit = crc & crchighbit;
            crc <<= 1;
            if (c & j)
                bit ^= crchighbit;
            if (bit)
                crc ^= polynom;
        }
        crc &= crcmask;
    }
    return crc;
}

static onfi_param_page_t *get_parameter_page(uint8_t device_index, uint32_t flash_index)
{
    return &(g_onfi_flash_devices[device_index][flash_index].param_page);
}

static onfi_status_reg_t *get_status_reg(uint8_t device_index, uint32_t flash_index)
{
    return &(g_onfi_flash_devices[device_index][flash_index].status_reg);
}

static void initialize_page_parameter(uint8_t device_index, uint32_t flash_index)
{
    onfi_param_page_t *paramater_page = get_parameter_page(device_index, flash_index);
    // =========== revision information and features block ===========
    // signature: "ONFI"
    paramater_page->signature[0] = 'O';
    paramater_page->signature[1] = 'N';
    paramater_page->signature[2] = 'F';
    paramater_page->signature[3] = 'I';

    // revision: ONFI 1.0
    paramater_page->revision = 0x0001;

    // features: none
    paramater_page->features.sixteen_bit_data_bus_width = 0;
    paramater_page->features.multiple_lun_operations = 0;
    paramater_page->features.nonsequential_page_programming = 0;
    paramater_page->features.interleaved_operations = 0;
    paramater_page->features.odd_to_even_page_copybackup = 0;
    paramater_page->features.reserved = 0;

    // optional commands: none
    paramater_page->optional_commands.page_cache_program = 0;
    paramater_page->optional_commands.read_cache_commands = 0;
    paramater_page->optional_commands.get_and_set_features = 0;
    paramater_page->optional_commands.read_status_enhanced = 0;
    paramater_page->optional_commands.copyback = 0;
    paramater_page->optional_commands.read_unique_id = 0;
    paramater_page->optional_commands.reserved = 0;

    // reserved: must be 0
    memset(&(paramater_page->reserved), 0, sizeof(paramater_page->reserved));

    // =========== manufacturer information block ===========
    paramater_page->manufacturer_info_block.device_manufacturer[0] = 0x4C;
    paramater_page->manufacturer_info_block.device_manufacturer[1] = 0x69;
    paramater_page->manufacturer_info_block.device_manufacturer[2] = 0x65;
    paramater_page->manufacturer_info_block.device_manufacturer[3] = 0x6C;
    paramater_page->manufacturer_info_block.device_manufacturer[4] = 0x26;
    paramater_page->manufacturer_info_block.device_manufacturer[5] = 0x49;
    paramater_page->manufacturer_info_block.device_manufacturer[6] = 0x64;
    paramater_page->manufacturer_info_block.device_manufacturer[7] = 0x6F;
    paramater_page->manufacturer_info_block.device_manufacturer[8] = 0x2D;
    paramater_page->manufacturer_info_block.device_manufacturer[9] = 0x4F;
    paramater_page->manufacturer_info_block.device_manufacturer[10] = 0x53;
    paramater_page->manufacturer_info_block.device_manufacturer[11] = 0x57;

    paramater_page->manufacturer_info_block.device_model[0] = 0x65;
    paramater_page->manufacturer_info_block.device_model[1] = 0x56;
    paramater_page->manufacturer_info_block.device_model[2] = 0x53;
    paramater_page->manufacturer_info_block.device_model[3] = 0x53;
    paramater_page->manufacturer_info_block.device_model[4] = 0x49;
    paramater_page->manufacturer_info_block.device_model[5] = 0x4D;
    paramater_page->manufacturer_info_block.device_model[6] = 0x20;
    paramater_page->manufacturer_info_block.device_model[7] = 0x4F;
    paramater_page->manufacturer_info_block.device_model[8] = 0x70;
    paramater_page->manufacturer_info_block.device_model[9] = 0x65;
    paramater_page->manufacturer_info_block.device_model[10] = 0x6E;
    paramater_page->manufacturer_info_block.device_model[11] = 0x55;
    paramater_page->manufacturer_info_block.device_model[12] = 0x20;
    paramater_page->manufacturer_info_block.device_model[13] = 0x4D;
    paramater_page->manufacturer_info_block.device_model[14] = 0x6F;
    paramater_page->manufacturer_info_block.device_model[15] = 0x64;
    paramater_page->manufacturer_info_block.device_model[16] = 0x65;
    paramater_page->manufacturer_info_block.device_model[17] = 0x6C;
    paramater_page->manufacturer_info_block.device_model[18] = 0x20;
    paramater_page->manufacturer_info_block.device_model[19] = 0x20;

    paramater_page->manufacturer_info_block.jedec_manufacturer = JEDEC_MANUFACTURER_ID;

    memset(&(paramater_page->manufacturer_info_block.data_code), 0, sizeof(paramater_page->manufacturer_info_block.data_code));

    memset(&(paramater_page->manufacturer_info_block.reserved), 0, sizeof(paramater_page->manufacturer_info_block.reserved));

    // =========== memory organization block ===========
    paramater_page->mem_org_block.data_bytes_per_page = GET_PAGE_SIZE(device_index);
    paramater_page->mem_org_block.spare_bytes_per_page = 0;
    paramater_page->mem_org_block.data_bytes_per_partial_page = 0;
    paramater_page->mem_org_block.spare_bytes_per_partial_page = 0;
    paramater_page->mem_org_block.pages_per_block = GET_PAGE_NB(device_index);
    paramater_page->mem_org_block.blocks_per_lun = GET_BLOCK_NB(device_index);
    paramater_page->mem_org_block.number_of_luns = GET_FLASH_NB(device_index);

    paramater_page->mem_org_block.address_cycles.row_address_cycles = 0x08;
    paramater_page->mem_org_block.address_cycles.column_address_cycles = 0x04;

    paramater_page->mem_org_block.bits_per_cell = 0x08;
    paramater_page->mem_org_block.bad_blocks_per_lun = 0;
    paramater_page->mem_org_block.block_endurance = 0xFF;
    paramater_page->mem_org_block.guaranteed_valid_blocks = 0xFF;
    paramater_page->mem_org_block.guaranteed_block_endurance = 0xFFFF;
    paramater_page->mem_org_block.programs_per_page = 0xFF;

    paramater_page->mem_org_block.partial_prog_attr.partial_program_constraints = 0;
    paramater_page->mem_org_block.partial_prog_attr.reserved1 = 0;
    paramater_page->mem_org_block.partial_prog_attr.partial_layout_spare = 0;
    paramater_page->mem_org_block.partial_prog_attr.reserved2 = 0;

    paramater_page->mem_org_block.ecc_bits = 0;

    paramater_page->mem_org_block.interleave_addr_bits.interleaved_address_bits = 0;
    paramater_page->mem_org_block.interleave_addr_bits.reserved = 0;

    paramater_page->mem_org_block.interleave_op_attr.overlapped_interleaving = 0;
    paramater_page->mem_org_block.interleave_op_attr.block_addr_restriction = 0;
    paramater_page->mem_org_block.interleave_op_attr.program_cache_supported = 0;
    paramater_page->mem_org_block.interleave_op_attr.program_cache_addr_restricted = 0;
    paramater_page->mem_org_block.interleave_op_attr.reserved = 0;

    memset(&(paramater_page->mem_org_block.reserved), 0, sizeof(paramater_page->mem_org_block.reserved));

    memset(&(paramater_page->electrical_params), 0, sizeof(paramater_page->electrical_params));

    paramater_page->vendor_block.vendor_revision_number = 0;

    memset(&(paramater_page->vendor_block.vendor_specific), 0, sizeof(paramater_page->vendor_block.vendor_specific));

    paramater_page->vendor_block.integrity_crc = _ONFI_CRC16((uint8_t *)paramater_page, sizeof(onfi_param_page_t) - sizeof(paramater_page->vendor_block.integrity_crc));
}

// Status helper: FAILC <- old FAIL; FAIL <- last_ret.
void _ONFI_UPDATE_STATUS_REGISTER(onfi_status_reg_t *status_reg, onfi_ret_val last_op_ret_val)
{
    if (!status_reg)
    {
        return;
    }
    status_reg->FAILC = status_reg->FAIL;
    status_reg->FAIL = last_op_ret_val;

    // Reserved bits must be 0
    status_reg->R = 0;
    status_reg->R2 = 0;
    status_reg->R3 = 0;
    // RDY/ARDY are set by the command path; WP left unchanged by this helper
}

/*
======= ONFI Commands =======
*/

static onfi_ret_val _onfi_read(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                               uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount)
{
    if (o_buffer == NULL || o_read_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (row_address >= GET_TOTAL_NUMBER_OF_PAGES(device_index) || column_address >= GET_PAGE_SIZE(device_index))
    {
        PERR("Invalid address (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        return ONFI_FAILURE;
    }

    const uint32_t flash_index = CALC_FLASH(device_index, row_address);
    const size_t amount_to_read = (buffer_size + column_address > GET_PAGE_SIZE(device_index)) ? (GET_PAGE_SIZE(device_index) - column_address) : buffer_size;

    if (SSD_PAGE_READ(device_index, flash_index, CALC_BLOCK(device_index, row_address), CALC_PAGE(device_index, row_address), 0, READ) != FTL_SUCCESS)
    {
        PERR("Failed reading\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    *o_read_bytes_amount = amount_to_read;
    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_page_program(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                                       const uint8_t *buffer, size_t buffer_size, size_t *o_programmed_bytes_amount)
{
    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (row_address >= GET_TOTAL_NUMBER_OF_PAGES(device_index) || column_address >= GET_PAGE_SIZE(device_index))
    {
        PERR("Invalid address (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        return ONFI_FAILURE;
    }

    const uint32_t flash_index = CALC_FLASH(device_index, row_address);

    if (buffer == NULL || o_programmed_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t amount_to_write = (buffer_size + column_address > GET_PAGE_SIZE(device_index)) ? (GET_PAGE_SIZE(device_index) - column_address) : buffer_size;

    if (SSD_PAGE_WRITE(device_index, flash_index, CALC_BLOCK(device_index, row_address), CALC_PAGE(device_index, row_address), 0, WRITE) != FTL_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    *o_programmed_bytes_amount = amount_to_write;
    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_block_erase(uint8_t device_index, uint64_t row_address)
{
    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (row_address >= GET_TOTAL_NUMBER_OF_PAGES(device_index))
    {
        PERR("Invalid address to erase (row_address = %zu)\n", (size_t)row_address)
        return ONFI_FAILURE;
    }

    const uint64_t block_nb = CALC_BLOCK(device_index, row_address);
    const uint32_t flash_nb = CALC_FLASH(device_index, row_address);

    if (SSD_BLOCK_ERASE(device_index, flash_nb, block_nb, ERASE) != FTL_SUCCESS)
    {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_nb), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_nb), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_read_id(uint8_t device_index, uint32_t flash_index, uint8_t address, uint8_t *o_buffer, size_t buffer_size)
{
    if (o_buffer == NULL)
    {
        PERR("Got null paramater\n");
        return ONFI_FAILURE;
    }

    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (flash_index >= GET_FLASH_NB(device_index)) {
        PERR("Got invalid flash index %u\n", flash_index)
        return ONFI_FAILURE;
    }

    switch (address)
    {
    case ONFI_SIGNATURE_ADDR:
    {
        static const uint8_t onfi_signature[4] = {'O', 'N', 'F', 'I'};

        // Copy up to buffer_size bytes
        const size_t copy_size = (buffer_size >= sizeof(onfi_signature)) ? sizeof(onfi_signature) : buffer_size;
        memcpy(o_buffer, onfi_signature, copy_size);
        break;
    }
    case JEDEC_ID_ADDR:
    {
        static const uint8_t ids[2] = {JEDEC_MANUFACTURER_ID, DEVICE_ID};

        // Copy up to buffer_size bytes
        const size_t copy_size = (buffer_size >= sizeof(ids)) ? sizeof(ids) : buffer_size;
        memcpy(o_buffer, ids, copy_size);
        break;
    }
    default:
    {
        PERR("Got invalid address\n");
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }
    }

    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_read_parameter_page(uint8_t device_index, uint32_t flash_index, uint8_t timing_mode, uint8_t *o_buffer, size_t buffer_size)
{
    if (timing_mode != 0 || o_buffer == NULL || buffer_size == 0)
    {
        PERR("Got invalid parameter\n");
        return ONFI_FAILURE;
    }

    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (flash_index >= GET_FLASH_NB(device_index)) {
        PERR("Got invalid flash index %u\n", flash_index)
        return ONFI_FAILURE;
    }

    // In principal, ONFI forbiddens retransmitting the same parameter page, but for simplicity's sake we will do it.
    while (buffer_size > 0)
    {
        size_t size = (buffer_size >= sizeof(onfi_param_page_t)) ? sizeof(onfi_param_page_t) : buffer_size;
        memcpy(o_buffer, get_parameter_page(device_index, flash_index), size);

        o_buffer += size;
        buffer_size -= size;
    }

    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_read_status(uint8_t device_index, uint32_t flash_index, onfi_status_reg_t *o_status_register)
{
    if (o_status_register == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (flash_index >= GET_FLASH_NB(device_index)) {
        PERR("Got invalid flash index %u\n", flash_index)
        return ONFI_FAILURE;
    }

    memcpy(o_status_register, get_status_reg(device_index, flash_index), sizeof(onfi_status_reg_t));
    return ONFI_SUCCESS;
}

static onfi_ret_val _onfi_reset(uint8_t device_index, uint32_t flash_index)
{
    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    if (flash_index >= GET_FLASH_NB(device_index)) {
        PERR("Got invalid flash index %u\n", flash_index)
        return ONFI_FAILURE;
    }

    memset(get_status_reg(device_index, flash_index), 0, sizeof(onfi_status_reg_t));
    set_ready(get_status_reg(device_index, flash_index), true);

    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index, flash_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

/*
======= Internal Types =======
*/

typedef enum {
    ONFI_OP_READ,
    ONFI_OP_PROGRAM,
    ONFI_OP_ERASE,
    ONFI_OP_READ_ID,
    ONFI_OP_READ_PARAMETER_PAGE,
    ONFI_OP_READ_STATUS,
    ONFI_OP_RESET
} onfi_op_type_t;

/* ONFI multithreaded request describes an ONFI operation to be submitted to an ONFI worker */
struct onfi_request {
    // request parameters
    onfi_op_type_t type;
    uint8_t device_index;
    uint32_t flash_index;

    uint64_t row_address;
    uint32_t column_address;
    uint8_t* buffer;
    size_t buffer_size;
    uint8_t address;
    uint8_t timing_mode;

    // response parameters
    onfi_ret_val result;
    size_t* bytes_transferred;

    // sync parameters
    pthread_mutex_t done_lock;
    pthread_cond_t done_cond;
    int done;
};

typedef struct onfi_request onfi_request_t;

/* Fixed-size thread-safe ring buffer of request pointers */
struct onfi_queue {
    onfi_request_t** requests;
    int head;    // current pop index
    int tail;    // current push index

    int size;    // max size
    int count;   // current size

    // sync
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
};

/*
======= ONFI Multithreaded Queue Functions =======
*/

/**
 * Initialize a fixed-size ring buffer queue with capacity size
 * Returns -1 if allocation failed.
 **/
static int onfi_mt_queue_init(onfi_queue_t* q, int size)
{
    q->requests = (onfi_request_t**)calloc(size, sizeof(onfi_request_t*));
    if (q->requests == NULL) {
        return -1;
    }

    q->head = 0;
    q->tail = 0;
    q->size = size;
    q->count = 0;
    q->shutdown = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

/**
 * Destroy a queue.
 * Returns -1 and doesn't destroy the queue if it still has pending requests.
 **/
static int onfi_mt_queue_destroy(onfi_queue_t* q)
{
    if (q->count != 0)
        return -1;
    free(q->requests);
    q->requests = NULL;
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    return 0;
}

/**
 * Push a new request into the queue.
 * Blocks if the queue is full.
 * Returns -1 and drop the request if the queue is shut down.
 **/
static int onfi_mt_queue_push(onfi_queue_t* q, onfi_request_t* req)
{
    pthread_mutex_lock(&q->lock);

    while ((q->count >= q->size) && !q->shutdown)
        pthread_cond_wait(&q->not_full, &q->lock);

    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    q->requests[q->tail] = req;
    q->tail = (q->tail + 1) % q->size;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

/**
 * Pop the next request from the queue.
 * Blocks if the queue is empty.
 * Returns NULL when the queue is empty and has been shut down.
 **/
static onfi_request_t* onfi_mt_queue_pop(onfi_queue_t* q)
{
    pthread_mutex_lock(&q->lock);

    while (q->count == 0 && !q->shutdown)
        pthread_cond_wait(&q->not_empty, &q->lock);

    if (q->shutdown && q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    onfi_request_t* req = q->requests[q->head];
    q->head = (q->head + 1) % q->size;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return req;
}

/* Set the shutdown flag and wake up any threads blocked on push/pop */
static void onfi_mt_queue_shutdown(onfi_queue_t* q)
{
    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

/*
======= ONFI Multithreaded Worker =======
*/

/* Execute the request's ONFI command */
static onfi_ret_val run_onfi_command(onfi_request_t *req)
{
    switch (req->type) {
    case ONFI_OP_READ:
        return _onfi_read(req->device_index, req->row_address, req->column_address,
                          req->buffer, req->buffer_size, req->bytes_transferred);
    case ONFI_OP_PROGRAM:
        return _onfi_page_program(req->device_index, req->row_address, req->column_address,
                                  req->buffer, req->buffer_size, req->bytes_transferred);
    case ONFI_OP_ERASE:
        return _onfi_block_erase(req->device_index, req->row_address);
    case ONFI_OP_READ_ID:
        return _onfi_read_id(req->device_index, req->flash_index, req->address, req->buffer, req->buffer_size);
    case ONFI_OP_READ_PARAMETER_PAGE:
        return _onfi_read_parameter_page(req->device_index, req->flash_index, req->timing_mode, req->buffer, req->buffer_size);
    case ONFI_OP_READ_STATUS:
        return _onfi_read_status(req->device_index, req->flash_index, (onfi_status_reg_t *)req->buffer);
    case ONFI_OP_RESET:
        return _onfi_reset(req->device_index, req->flash_index);
    }

    return ONFI_FAILURE;
}

static void* onfi_mt_worker(void* arg)
{
    onfi_queue_t* queue = (onfi_queue_t*)arg;

    // process requests until the queue is shutdown
    while (true) {
        onfi_request_t* req = onfi_mt_queue_pop(queue);
        if (req == NULL)
            break;

        // use the onfi device lock before performing any onfi command because current implementation (ssd io manager)
        // doesn't support multithread and is not safe at the flash level.
        // The lock will be removed in the future after making everything safe.
        pthread_mutex_lock(&g_onfi_device_locks[req->device_index]);

        req->result = run_onfi_command(req);

        pthread_mutex_unlock(&g_onfi_device_locks[req->device_index]);

        pthread_mutex_lock(&req->done_lock);
        req->done = 1;
        pthread_cond_signal(&req->done_cond);
        pthread_mutex_unlock(&req->done_lock);
    }

    return NULL;
}

/*
======= ONFI Request Functions =======
*/

/* Tear down the first n threads + queues of a manager */
static int onfi_mt_cleanup(onfi_manager_t* onfi_manager, int n)
{
    int i, ret = 0;

    // shutdown the first n queues
    for (i = 0; i < n; i++) {
        onfi_mt_queue_shutdown(&onfi_manager->queues[i]);
    }

    // destroy the first n queues
    for (i = 0; i < n; i++) {
        pthread_join(onfi_manager->threads[i], NULL);
        if (onfi_mt_queue_destroy(&onfi_manager->queues[i]) != 0)
            ret = -1;
    }

    // free onfi manager variables
    free(onfi_manager->threads);
    free(onfi_manager->queues);
    onfi_manager->threads = NULL;
    onfi_manager->queues = NULL;
    onfi_manager->initialized = 0;
    return ret;
}

/**
 * Allocate and initialize a new request. Returns NULL on failure.
 **/
static onfi_request_t* onfi_request_create(onfi_op_type_t type, uint8_t device_index)
{
    onfi_request_t* req = (onfi_request_t*)calloc(1, sizeof(onfi_request_t));
    if (!req)
        return NULL;

    req->type = type;
    req->device_index = device_index;
    req->done = 0;
    pthread_mutex_init(&req->done_lock, NULL);
    pthread_cond_init(&req->done_cond, NULL);
    return req;
}

/**
 * Free a request and its sync primitives.
 **/
static void onfi_request_destroy(onfi_request_t* req)
{
    pthread_mutex_destroy(&req->done_lock);
    pthread_cond_destroy(&req->done_cond);
    free(req);
}

/**
 * Submits a request for async processing via ONFI threads.
 * This function can block if the target thread's queue is full.
 * Returns -1 and drops the request if the queue is shut down.
 **/
static int onfi_request_submit(onfi_request_t* req)
{
    if (req->device_index >= device_count)
        return -1;

    onfi_manager_t* onfi_manager = &g_onfi_managers[req->device_index];
    // reject submissions before INIT, after TERM, or in serial mode
    if (!onfi_manager->initialized || onfi_manager->threads == NULL)
        return -1;

    // calculate target onfi worker thread (one worker per flash chip)
    int flash_index;
    switch (req->type) {
    case ONFI_OP_READ_ID:
    case ONFI_OP_READ_PARAMETER_PAGE:
    case ONFI_OP_READ_STATUS:
    case ONFI_OP_RESET:
        flash_index = (int)req->flash_index;
        break;
    default:
        flash_index = CALC_FLASH(req->device_index, req->row_address);
        break;
    }
    int onfi_thread_index = flash_index % GET_FLASH_NB(req->device_index);

    // push request
    return onfi_mt_queue_push(&onfi_manager->queues[onfi_thread_index], req);
}

/**
 * Validates the request and either submits it to the target ONFI worker
 * (multithreaded mode) or executes it inline (serial mode).
 * Returns NULL if the request could not be created/submitted.
 **/
static onfi_handle_t *onfi_request_dispatch(onfi_request_t *req)
{
    if (req->device_index >= device_count) {
        onfi_request_destroy(req);
        return NULL;
    }

    onfi_manager_t *onfi_manager = &g_onfi_managers[req->device_index];
    if (!onfi_manager->initialized) {
        onfi_request_destroy(req);
        return NULL;
    }

    if (onfi_manager->threads == NULL) {
        // serial mode: execute the command inline on the calling thread
        req->result = run_onfi_command(req);
        pthread_mutex_lock(&req->done_lock);
        req->done = 1;
        pthread_cond_signal(&req->done_cond);
        pthread_mutex_unlock(&req->done_lock);
    } else {
        // multithreaded mode
        if (onfi_request_submit(req) != 0) {
            onfi_request_destroy(req);
            return NULL;
        }
    }

    return (onfi_handle_t *)req;
}

/*
======= ONFI Manager API =======
*/

onfi_ret_val ONFI_INIT(uint8_t device_index)
{
    if (device_index >= device_count) {
        PERR("Got invalid device index\n")
        return ONFI_FAILURE;
    }

    uint32_t flash;
    for (flash = 0; flash < GET_FLASH_NB(device_index); flash++) {
        initialize_page_parameter(device_index, flash);
        if (_onfi_reset(device_index, flash) != ONFI_SUCCESS) {
            return ONFI_FAILURE;
        }
    }

    onfi_manager_t* onfi_manager = &g_onfi_managers[device_index];
    if (onfi_manager->initialized)
        return ONFI_SUCCESS;

    pthread_mutex_init(&g_onfi_device_locks[device_index], NULL);
    ssd_config_t* cfg = &devices[device_index];

    if (cfg->onfi_multithreaded) {
        // create one worker thread + queue per flash chip
        const uint32_t flash_nb = GET_FLASH_NB(device_index);

        onfi_manager->threads = (pthread_t*)calloc(flash_nb, sizeof(pthread_t));
        onfi_manager->queues = (onfi_queue_t*)calloc(flash_nb, sizeof(onfi_queue_t));
        if (!onfi_manager->threads || !onfi_manager->queues) {
            onfi_mt_cleanup(onfi_manager, 0);
            pthread_mutex_destroy(&g_onfi_device_locks[device_index]);
            return ONFI_FAILURE;
        }

        // create and start onfi worker threads
        uint32_t i;
        for (i = 0; i < flash_nb; i++) {
            if (onfi_mt_queue_init(&onfi_manager->queues[i], cfg->onfi_manager_queue_size) != 0 ||
                    pthread_create(&onfi_manager->threads[i], NULL, onfi_mt_worker, &onfi_manager->queues[i]) != 0) {
                onfi_mt_queue_destroy(&onfi_manager->queues[i]);
                onfi_mt_cleanup(onfi_manager, i);
                pthread_mutex_destroy(&g_onfi_device_locks[device_index]);
                return ONFI_FAILURE;
            }
        }
    }

    onfi_manager->initialized = 1;
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_TERM(uint8_t device_index)
{
    if (device_index >= device_count)
        return ONFI_FAILURE;

    onfi_manager_t* onfi_manager = &g_onfi_managers[device_index];
    if (!onfi_manager->initialized) {
        return ONFI_SUCCESS;
    }

    int ret = ONFI_SUCCESS;
    if (onfi_manager->threads != NULL) {
        if (onfi_mt_cleanup(onfi_manager, GET_FLASH_NB(device_index)) != 0)
            ret = ONFI_FAILURE;
    } else {
        onfi_manager->initialized = 0;
    }

    pthread_mutex_destroy(&g_onfi_device_locks[device_index]);
    return ret;
}

onfi_ret_val ONFI_WAIT(onfi_handle_t* handle)
{
    if (handle == NULL)
        return ONFI_FAILURE;

    onfi_request_t* req = (onfi_request_t*)handle;
    if (!req)
        return ONFI_FAILURE;

    // wait until the request is done
    pthread_mutex_lock(&req->done_lock);
    while (!req->done)
        pthread_cond_wait(&req->done_cond, &req->done_lock);

    int result = req->result;
    pthread_mutex_unlock(&req->done_lock);

    onfi_request_destroy(req);
    return result;
}

onfi_handle_t* ONFI_READ(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                         uint8_t* buffer, size_t buffer_size, size_t* o_read_bytes_amount)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_READ, device_index);
    if (!req)
        return NULL;

    req->row_address = row_address;
    req->column_address = column_address;
    req->buffer = buffer;
    req->buffer_size = buffer_size;
    req->bytes_transferred = o_read_bytes_amount;

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_PAGE_PROGRAM(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                                 const uint8_t* buffer, size_t buffer_size, size_t* o_programmed_bytes_amount)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_PROGRAM, device_index);
    if (!req)
        return NULL;

    req->row_address = row_address;
    req->column_address = column_address;
    req->buffer = (uint8_t*)buffer;
    req->buffer_size = buffer_size;
    req->bytes_transferred = o_programmed_bytes_amount;

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_BLOCK_ERASE(uint8_t device_index, uint64_t row_address)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_ERASE, device_index);
    if (!req)
        return NULL;

    req->row_address = row_address;

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_READ_ID(uint8_t device_index, uint32_t flash_index, uint8_t address, uint8_t* buffer, size_t buffer_size)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_READ_ID, device_index);
    if (!req)
        return NULL;

    req->flash_index = flash_index;
    req->address = address;
    req->buffer = buffer;
    req->buffer_size = buffer_size;

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_READ_PARAMETER_PAGE(uint8_t device_index, uint32_t flash_index, uint8_t timing_mode, uint8_t* buffer, size_t buffer_size)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_READ_PARAMETER_PAGE, device_index);
    if (!req)
        return NULL;

    req->flash_index = flash_index;
    req->buffer = buffer;
    req->buffer_size = buffer_size;
    req->timing_mode = timing_mode;

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_READ_STATUS(uint8_t device_index, uint32_t flash_index, onfi_status_reg_t* status)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_READ_STATUS, device_index);
    if (!req)
        return NULL;

    req->flash_index = flash_index;
    req->buffer = (uint8_t*)status;
    req->buffer_size = sizeof(onfi_status_reg_t);

    return onfi_request_dispatch(req);
}

onfi_handle_t* ONFI_RESET(uint8_t device_index, uint32_t flash_index)
{
    onfi_request_t* req = onfi_request_create(ONFI_OP_RESET, device_index);
    if (!req)
        return NULL;

    req->flash_index = flash_index;

    return onfi_request_dispatch(req);
}
