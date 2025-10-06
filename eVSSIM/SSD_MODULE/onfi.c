#include "onfi.h"
#include "common.h"
#include "ssd_file_operations.h"
#include <stdbool.h>

onfi_device_t *g_onfi_devices = NULL;

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

static onfi_param_page_t *get_parameter_page(uint8_t device_index)
{
    return &(g_onfi_devices[device_index].param_page);
}

static onfi_status_reg_t *get_status_reg(uint8_t device_index)
{
    return &(g_onfi_devices[device_index].status_reg);
}

static void initialize_page_parameter(uint8_t device_index)
{
    onfi_param_page_t *paramater_page = get_parameter_page(device_index);
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

onfi_ret_val ONFI_INIT(uint8_t device_index)
{
    initialize_page_parameter(device_index);
    return ONFI_RESET(device_index);
}

onfi_ret_val ONFI_READ(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                       uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount)
{
    if (o_buffer == NULL || o_read_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    if (row_address >= GET_PAGE_NB(device_index) || column_address >= GET_PAGE_SIZE(device_index))
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        return ONFI_FAILURE;
    }

    const size_t amount_to_read = (buffer_size + column_address > GET_PAGE_SIZE(device_index)) ? (GET_PAGE_SIZE(device_index) - column_address) : buffer_size;

    if (SSD_PAGE_READ(device_index, CALC_FLASH(device_index, row_address), CALC_BLOCK(device_index, row_address), CALC_PAGE(device_index, row_address), 0, READ) != FTL_SUCCESS)
    {
        PERR("Failed reading\n")
        return ONFI_FAILURE;
    }

    if (ssd_read(GET_FILE_NAME(device_index), row_address * GET_PAGE_SIZE(device_index) + column_address, amount_to_read, o_buffer) != SSD_FILE_OPS_SUCCESS)
    {
        PERR("Failed reading\n")
        return ONFI_FAILURE;
    }

    *o_read_bytes_amount = amount_to_read;
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_PAGE_PROGRAM(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                               const uint8_t *buffer, size_t buffer_size, size_t *o_programmed_bytes_amount)
{
    if (buffer == NULL || o_programmed_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    if (row_address >= GET_PAGE_NB(device_index) || column_address >= GET_PAGE_SIZE(device_index))
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t amount_to_write = (buffer_size + column_address > GET_PAGE_SIZE(device_index)) ? (GET_PAGE_SIZE(device_index) - column_address) : buffer_size;

    if (SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, row_address), CALC_BLOCK(device_index, row_address), CALC_PAGE(device_index, row_address), 0, WRITE) != FTL_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    if (ssd_write(GET_FILE_NAME(device_index), row_address * GET_PAGE_SIZE(device_index) + column_address, amount_to_write, buffer) != SSD_FILE_OPS_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    *o_programmed_bytes_amount = amount_to_write;
    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_BLOCK_ERASE(uint8_t device_index, uint64_t row_address)
{
    if (row_address >= GET_PAGE_NB(device_index))
    {
        PERR("Invalid address to erasw (row_address = %zu)\n", (size_t)row_address)
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const uint64_t block_nb = CALC_BLOCK(device_index, row_address);
    const uint64_t flash_nb = CALC_FLASH(device_index, row_address);

    if (SSD_BLOCK_ERASE(device_index, flash_nb, block_nb) != FTL_SUCCESS)
    {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t block_size = GET_PAGE_NB(device_index) * GET_PAGE_SIZE(device_index);
    const uint64_t first_page_in_block = block_nb * GET_PAGE_NB(device_index);

    if (ssd_erase(GET_FILE_NAME(device_index), first_page_in_block * GET_PAGE_SIZE(device_index), block_size) != SSD_FILE_OPS_SUCCESS)
    {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    _ONFI_UPDATE_STATUS_REGISTER(get_status_reg(device_index), ONFI_SUCCESS);
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_READ_ID(uint8_t device_index, uint8_t address, uint8_t *o_buffer, size_t buffer_size)
{
    (void)device_index;
    if (o_buffer == NULL)
    {
        PERR("Got null paramater\n");
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
        return ONFI_FAILURE;
    }
    }

    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_READ_PARAMETER_PAGE(uint8_t device_index, uint8_t timing_mode, uint8_t *o_buffer, size_t buffer_size)
{
    if (timing_mode != 0 || o_buffer == NULL || buffer_size == 0)
    {
        PERR("Got invalid parameter\n");
        return ONFI_FAILURE;
    }

    // In principal, ONFI forbidens retransmitting the same parameter page, but for simplicity's sake we will do it.'
    while (buffer_size > 0)
    {
        size_t size = (buffer_size >= sizeof(onfi_param_page_t)) ? sizeof(onfi_param_page_t) : buffer_size;
        memcpy(o_buffer, get_parameter_page(device_index), size);

        o_buffer += size;
        buffer_size -= size;
    }

    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_READ_STATUS(uint8_t device_index, onfi_status_reg_t *o_status_register)
{
    if (o_status_register == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    memcpy(o_status_register, get_status_reg(device_index), sizeof(onfi_status_reg_t));
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_RESET(uint8_t device_index)
{
    memset(get_status_reg(device_index), 0, sizeof(onfi_status_reg_t));
    set_ready(get_status_reg(device_index), true);

    return ONFI_SUCCESS;
}
