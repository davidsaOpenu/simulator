#include "onfi.h"
#include "common.h"
#include "ssd_file_operations.h"
#include <stdbool.h>

static onfi_status_reg_t g_status_register = {0};

static inline void set_ready(bool ready)
{
    g_status_register.RDY = (ready ? 1 : 0);
    g_status_register.ARDY = (ready ? 1 : 0);
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

onfi_ret_val ONFI_READ(uint64_t row_address, uint32_t column_address,
                       uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount)
{
    if (o_buffer == NULL || o_read_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    if (row_address >= PAGE_NB || column_address >= GET_PAGE_SIZE())
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        return ONFI_FAILURE;
    }

    const size_t amount_to_read = (buffer_size + column_address > GET_PAGE_SIZE()) ? (GET_PAGE_SIZE() - column_address) : buffer_size;

    if (SSD_PAGE_READ(CALC_FLASH(row_address), CALC_BLOCK(row_address), CALC_PAGE(row_address), 0, READ) != FTL_SUCCESS)
    {
        PERR("Failed reading\n")
        return ONFI_FAILURE;
    }

    if (ssd_read(GET_FILE_NAME(), row_address * GET_PAGE_SIZE() + column_address, amount_to_read, o_buffer) != SSD_FILE_OPS_SUCCESS)
    {
        PERR("Failed reading\n")
        return ONFI_FAILURE;
    }

    *o_read_bytes_amount = amount_to_read;
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_PAGE_PROGRAM(uint64_t row_address, uint32_t column_address,
                               const uint8_t *buffer, size_t buffer_size, size_t *o_programmed_bytes_amount)
{
    if (buffer == NULL || o_programmed_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    if (row_address >= PAGE_NB || column_address >= GET_PAGE_SIZE())
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t amount_to_write = (buffer_size + column_address > GET_PAGE_SIZE()) ? (GET_PAGE_SIZE() - column_address) : buffer_size;

    if (SSD_PAGE_WRITE(CALC_FLASH(row_address), CALC_BLOCK(row_address), CALC_PAGE(row_address), 0, WRITE) != FTL_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    if (ssd_write(GET_FILE_NAME(), row_address * GET_PAGE_SIZE() + column_address, amount_to_write, buffer) != SSD_FILE_OPS_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    *o_programmed_bytes_amount = amount_to_write;
    _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_BLOCK_ERASE(uint64_t row_address)
{
    if (row_address >= PAGE_NB)
    {
        PERR("Invalid address to erasw (row_address = %zu)\n", (size_t)row_address)
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const uint64_t block_nb = CALC_BLOCK(row_address);
    const uint64_t flash_nb = CALC_FLASH(row_address);

    if (SSD_BLOCK_ERASE(flash_nb, block_nb) != FTL_SUCCESS) {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t block_size = PAGE_NB * GET_PAGE_SIZE();
    const uint64_t first_page_in_block = block_nb * PAGE_NB;

    if (ssd_erase(GET_FILE_NAME(), first_page_in_block * GET_PAGE_SIZE(), block_size) != SSD_FILE_OPS_SUCCESS) {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_READ_ID(uint8_t address, uint8_t *o_buffer, size_t buffer_size)
{
    (void)address;
    (void)o_buffer;
    (void)buffer_size;
    return ONFI_FAILURE;
}

onfi_ret_val ONFI_READ_PARAMETER_PAGE(uint8_t timing_mode, uint8_t *o_buffer, size_t buffer_size)
{
    (void)timing_mode;
    (void)o_buffer;
    (void)buffer_size;
    return ONFI_FAILURE;
}

onfi_ret_val ONFI_READ_STATUS(uint8_t *o_status_register)
{
    (void)o_status_register;
    return ONFI_FAILURE;
}

onfi_ret_val ONFI_RESET(void)
{
    return ONFI_FAILURE;
}