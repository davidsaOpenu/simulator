#include "onfi.h"
#include "common.h"
#include "ssd_file_operations.h"
#include <stdbool.h>

static onfi_status_reg_t g_status_register = {0};

static onfi_param_page_t g_parameter_page;

static inline void set_ready(bool ready)
{
    g_status_register.RDY = (ready ? 1 : 0);
    g_status_register.ARDY = (ready ? 1 : 0);
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

static void initialize_page_parameter(void)
{
    // =========== revision information and features block ===========
    // signature: "ONFI"
    g_parameter_page.signature[0] = 'O';
    g_parameter_page.signature[1] = 'N';
    g_parameter_page.signature[2] = 'F';
    g_parameter_page.signature[3] = 'I';

    // revision: ONFI 1.0
    g_parameter_page.revision = 0x0001;

    // features: none
    g_parameter_page.features.sixteen_bit_data_bus_width = 0;
    g_parameter_page.features.multiple_lun_operations = 0;
    g_parameter_page.features.nonsequential_page_programming = 0;
    g_parameter_page.features.interleaved_operations = 0;
    g_parameter_page.features.odd_to_even_page_copybackup = 0;
    g_parameter_page.features.reserved = 0;

    // optional commands: none
    g_parameter_page.optional_commands.page_cache_program = 0;
    g_parameter_page.optional_commands.read_cache_commands = 0;
    g_parameter_page.optional_commands.get_and_set_features = 0;
    g_parameter_page.optional_commands.read_status_enhanced = 0;
    g_parameter_page.optional_commands.copyback = 0;
    g_parameter_page.optional_commands.read_unique_id = 0;
    g_parameter_page.optional_commands.reserved = 0;

    // reserved: must be 0
    memset(&(g_parameter_page.reserved), 0, sizeof(g_parameter_page.reserved));

    // =========== manufacturer information block ===========
    g_parameter_page.manufacturer_info_block.device_manufacturer[0] = 0x4C;
    g_parameter_page.manufacturer_info_block.device_manufacturer[1] = 0x69;
    g_parameter_page.manufacturer_info_block.device_manufacturer[2] = 0x65;
    g_parameter_page.manufacturer_info_block.device_manufacturer[3] = 0x6C;
    g_parameter_page.manufacturer_info_block.device_manufacturer[4] = 0x26;
    g_parameter_page.manufacturer_info_block.device_manufacturer[5] = 0x49;
    g_parameter_page.manufacturer_info_block.device_manufacturer[6] = 0x64;
    g_parameter_page.manufacturer_info_block.device_manufacturer[7] = 0x6F;
    g_parameter_page.manufacturer_info_block.device_manufacturer[8] = 0x2D;
    g_parameter_page.manufacturer_info_block.device_manufacturer[9] = 0x4F;
    g_parameter_page.manufacturer_info_block.device_manufacturer[10] = 0x53;
    g_parameter_page.manufacturer_info_block.device_manufacturer[11] = 0x57;

    g_parameter_page.manufacturer_info_block.device_model[0] = 0x65;
    g_parameter_page.manufacturer_info_block.device_model[1] = 0x56;
    g_parameter_page.manufacturer_info_block.device_model[2] = 0x53;
    g_parameter_page.manufacturer_info_block.device_model[3] = 0x53;
    g_parameter_page.manufacturer_info_block.device_model[4] = 0x49;
    g_parameter_page.manufacturer_info_block.device_model[5] = 0x4D;
    g_parameter_page.manufacturer_info_block.device_model[6] = 0x20;
    g_parameter_page.manufacturer_info_block.device_model[7] = 0x4F;
    g_parameter_page.manufacturer_info_block.device_model[8] = 0x70;
    g_parameter_page.manufacturer_info_block.device_model[9] = 0x65;
    g_parameter_page.manufacturer_info_block.device_model[10] = 0x6E;
    g_parameter_page.manufacturer_info_block.device_model[11] = 0x55;
    g_parameter_page.manufacturer_info_block.device_model[12] = 0x20;
    g_parameter_page.manufacturer_info_block.device_model[13] = 0x4D;
    g_parameter_page.manufacturer_info_block.device_model[14] = 0x6F;
    g_parameter_page.manufacturer_info_block.device_model[15] = 0x64;
    g_parameter_page.manufacturer_info_block.device_model[16] = 0x65;
    g_parameter_page.manufacturer_info_block.device_model[17] = 0x6C;
    g_parameter_page.manufacturer_info_block.device_model[18] = 0x20;
    g_parameter_page.manufacturer_info_block.device_model[19] = 0x20;

    g_parameter_page.manufacturer_info_block.jedec_manufacturer = 0xCC;

    memset(&(g_parameter_page.manufacturer_info_block.data_code), 0, sizeof(g_parameter_page.manufacturer_info_block.data_code));

    memset(&(g_parameter_page.manufacturer_info_block.reserved), 0, sizeof(g_parameter_page.manufacturer_info_block.reserved));

    // =========== memory organization block ===========
    g_parameter_page.mem_org_block.data_bytes_per_page = GET_PAGE_SIZE(g_device_index);
    g_parameter_page.mem_org_block.spare_bytes_per_page = 0;
    g_parameter_page.mem_org_block.data_bytes_per_partial_page = 0;
    g_parameter_page.mem_org_block.spare_bytes_per_partial_page = 0;
    g_parameter_page.mem_org_block.pages_per_block = GET_PAGE_NB(g_device_index);
    g_parameter_page.mem_org_block.blocks_per_lun = GET_BLOCK_NB(g_device_index);
    g_parameter_page.mem_org_block.number_of_luns = GET_FLASH_NB(g_device_index);

    g_parameter_page.mem_org_block.address_cycles.row_address_cycles = 0x08;
    g_parameter_page.mem_org_block.address_cycles.column_address_cycles = 0x04;

    g_parameter_page.mem_org_block.bits_per_cell = 0x08;
    g_parameter_page.mem_org_block.bad_blocks_per_lun = 0;
    g_parameter_page.mem_org_block.block_endurance = 0xFF;
    g_parameter_page.mem_org_block.guaranteed_valid_blocks = 0xFF;
    g_parameter_page.mem_org_block.guaranteed_block_endurance = 0xFFFF;
    g_parameter_page.mem_org_block.programs_per_page = 0xFF;

    g_parameter_page.mem_org_block.partial_prog_attr.partial_program_constraints = 0;
    g_parameter_page.mem_org_block.partial_prog_attr.reserved1 = 0;
    g_parameter_page.mem_org_block.partial_prog_attr.partial_layout_spare = 0;
    g_parameter_page.mem_org_block.partial_prog_attr.reserved2 = 0;

    g_parameter_page.mem_org_block.ecc_bits = 0;

    g_parameter_page.mem_org_block.interleave_addr_bits.interleaved_address_bits = 0;
    g_parameter_page.mem_org_block.interleave_addr_bits.reserved = 0;

    g_parameter_page.mem_org_block.interleave_op_attr.overlapped_interleaving = 0;
    g_parameter_page.mem_org_block.interleave_op_attr.block_addr_restriction = 0;
    g_parameter_page.mem_org_block.interleave_op_attr.program_cache_supported = 0;
    g_parameter_page.mem_org_block.interleave_op_attr.program_cache_addr_restricted = 0;
    g_parameter_page.mem_org_block.interleave_op_attr.reserved = 0;

    memset(&(g_parameter_page.mem_org_block.reserved), 0, sizeof(g_parameter_page.mem_org_block.reserved));

    memset(&(g_parameter_page.electrical_params), 0, sizeof(g_parameter_page.electrical_params));

    g_parameter_page.vendor_block.vendor_revision_number = 0;

    memset(&(g_parameter_page.vendor_block.vendor_specific), 0, sizeof(g_parameter_page.vendor_block.vendor_specific));

    g_parameter_page.vendor_block.integrity_crc = _ONFI_CRC16((uint8_t *)&g_parameter_page, sizeof(g_parameter_page) - sizeof(g_parameter_page.vendor_block.integrity_crc));
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

onfi_ret_val _ONFI_INIT(void)
{
    initialize_page_parameter();
    return ONFI_RESET();
}

onfi_ret_val ONFI_READ(uint64_t row_address, uint32_t column_address,
                       uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount)
{
    if (o_buffer == NULL || o_read_bytes_amount == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    if (row_address >= GET_PAGE_NB(g_device_index) || column_address >= GET_PAGE_SIZE(g_device_index))
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        return ONFI_FAILURE;
    }

    const size_t amount_to_read = (buffer_size + column_address > GET_PAGE_SIZE(g_device_index)) ? (GET_PAGE_SIZE(g_device_index) - column_address) : buffer_size;

    if (SSD_PAGE_READ(g_device_index, CALC_FLASH(g_device_index, row_address), CALC_BLOCK(g_device_index, row_address), CALC_PAGE(g_device_index, row_address), 0, READ) != FTL_SUCCESS)
    {
        PERR("Failed reading\n")
        return ONFI_FAILURE;
    }

    if (ssd_read(GET_FILE_NAME(g_device_index), row_address * GET_PAGE_SIZE(g_device_index) + column_address, amount_to_read, o_buffer) != SSD_FILE_OPS_SUCCESS)
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

    if (row_address >= GET_PAGE_NB(g_device_index) || column_address >= GET_PAGE_SIZE(g_device_index))
    {
        PERR("Invalid address to read (row_address = %zu, column_address = %zu)\n", (size_t)row_address, (size_t)column_address)
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t amount_to_write = (buffer_size + column_address > GET_PAGE_SIZE(g_device_index)) ? (GET_PAGE_SIZE(g_device_index) - column_address) : buffer_size;

    if (SSD_PAGE_WRITE(g_device_index, CALC_FLASH(g_device_index, row_address), CALC_BLOCK(g_device_index, row_address), CALC_PAGE(g_device_index, row_address), 0, WRITE) != FTL_SUCCESS)
    {
        PERR("Failed writing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    if (ssd_write(GET_FILE_NAME(g_device_index), row_address * GET_PAGE_SIZE(g_device_index) + column_address, amount_to_write, buffer) != SSD_FILE_OPS_SUCCESS)
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
    if (row_address >= GET_PAGE_NB(g_device_index))
    {
        PERR("Invalid address to erasw (row_address = %zu)\n", (size_t)row_address)
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const uint64_t block_nb = CALC_BLOCK(g_device_index, row_address);
    const uint64_t flash_nb = CALC_FLASH(g_device_index, row_address);

    if (SSD_BLOCK_ERASE(g_device_index, flash_nb, block_nb) != FTL_SUCCESS) {
        PERR("Failed erasing\n")
        _ONFI_UPDATE_STATUS_REGISTER(&g_status_register, ONFI_FAILURE);
        return ONFI_FAILURE;
    }

    const size_t block_size = GET_PAGE_NB(g_device_index) * GET_PAGE_SIZE(g_device_index);
    const uint64_t first_page_in_block = block_nb * GET_PAGE_NB(g_device_index);

    if (ssd_erase(GET_FILE_NAME(g_device_index), first_page_in_block * GET_PAGE_SIZE(g_device_index), block_size) != SSD_FILE_OPS_SUCCESS) {
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
    if (timing_mode != 0 || o_buffer == NULL || buffer_size == 0)
    {
        PERR("Got invalid parameter\n");
        return ONFI_FAILURE;
    }

    // In principal, ONFI forbidens retransmitting the same parameter page, but for simplicity's sake we will do it.'
    while (buffer_size > 0)
    {
        size_t size = (buffer_size >= sizeof(onfi_param_page_t)) ? sizeof(onfi_param_page_t) : buffer_size;
        memcpy(o_buffer, &g_parameter_page, size);

        o_buffer += size;
        buffer_size -= size;
    }

    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_READ_STATUS(onfi_status_reg_t *o_status_register)
{
    if (o_status_register == NULL)
    {
        PERR("Got null paramater\n")
        return ONFI_FAILURE;
    }

    memcpy(o_status_register, &g_status_register, sizeof(g_status_register));
    return ONFI_SUCCESS;
}

onfi_ret_val ONFI_RESET(void)
{
    memset(&g_status_register, 0, sizeof(g_status_register));
    set_ready(true);

    return ONFI_SUCCESS;
}
