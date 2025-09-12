#include "onfi.h"
#include <stdbool.h>

static onfi_status_reg_t g_status_register = {0};

static inline void set_ready(bool ready)
{
    g_status_register.RDY = (ready ? 1 : 0);
    g_status_register.ARDY = (ready ? 1 : 0);
}

// Status helper: FAILC <- old FAIL; FAIL <- last_ret.
void update_status_register(onfi_status_reg_t* status_reg, onfi_ret_val last_op_ret_val)
{
    if (!status_reg) {
        return;
    }
    status_reg->FAILC = status_reg->FAIL;
    status_reg->FAIL  = last_op_ret_val;

    // Reserved bits must be 0
    status_reg->R  = 0;
    status_reg->R2 = 0;
    status_reg->R3 = 0;
    // RDY/ARDY are set by the command path; WP left unchanged by this helper
}

onfi_ret_val ONFI_READ(uint64_t row_address, uint32_t column_address,
                       uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount)
{
    (void)row_address;
    (void)column_address;
    (void)o_buffer;
    (void)buffer_size;
    (void)o_read_bytes_amount;
    return ONFI_FAILURE;
}

onfi_ret_val ONFI_PAGE_PROGRAM(uint64_t row_address, uint32_t column_address,
                               const uint8_t *buffer, size_t buffer_size, size_t *o_programmed_bytes_amount)
{
    (void)row_address;
    (void)column_address;
    (void)buffer;
    (void)buffer_size;
    (void)o_programmed_bytes_amount;
    return ONFI_FAILURE;
}

onfi_ret_val ONFI_BLOCK_ERASE(uint64_t row_address)
{
    (void)row_address;
    return ONFI_FAILURE;
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