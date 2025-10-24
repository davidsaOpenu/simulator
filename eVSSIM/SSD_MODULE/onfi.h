#ifndef ONFI_H
#define ONFI_H

#include <stdint.h>
#include <stddef.h>

typedef enum
{
    ONFI_SUCCESS = 0,
    ONFI_FAILURE = 1
} onfi_ret_val;

#define ONFI_SIGNATURE_ADDR 0x20
#define JEDEC_ID_ADDR 0x00

// One-byte packed status register per ONFI 1.0 layout.
#pragma pack(push, 1)
typedef struct
{
    onfi_ret_val FAIL : 1;  // Bit 0: Last operation failed
                            // (If set to one, then the last command failed. If cleared to zero, then the last command was successful)
    onfi_ret_val FAILC : 1; // Bit 1: Previous operation (before last) failed.
    uint8_t R : 1;          // Bit 2: Reserved (always 0)
    uint8_t R2 : 1;         // Bit 3: Reserved (always 0)
    uint8_t R3 : 1;         // Bit 4: Reserved (always 0)
    uint8_t ARDY : 1;       // Bit 5: Array Ready Array Ready
                            // (If set to one, then there is no array operation in progress. If cleared to zero, then there is a command being processed
                            // (RDY is cleared to zero) or an array operation in progress.)
    uint8_t RDY : 1;        // Bit 6: Ready (If set to one, then the flash is ready for another command and all other bits in the status value are valid.
                            // If cleared to zero, then the last command issued is not yet complete and SR bits 5:0 are invalid and shall be ignored)
    uint8_t WP : 1;         // Bit 7: Write Protect (0 = write protected)
} __attribute__((packed)) onfi_status_reg_t;
#pragma pack(pop)

// ONFI parameter page is 256 bytes. We expose a packed struct for convenience.
#pragma pack(push, 1)
typedef struct
{
    // Bytes 0–3: Signature ("ONFI")
    char signature[4]; // 'O', 'N', 'F', 'I'

    // Bytes 4–5: Revision number (e.g., 0x0001 -> supports ONFI 1.0)
    uint16_t revision;

    // Byte 6-7: Features supported
    struct
    {
        uint8_t sixteen_bit_data_bus_width : 1;
        uint8_t multiple_lun_operations : 1;
        uint8_t nonsequential_page_programming : 1;
        uint8_t interleaved_operations : 1;
        uint8_t odd_to_even_page_copybackup : 1;
        uint16_t reserved : 11;
    } features;

    // Byte 8-9: Optional commands supported
    struct
    {
        uint8_t page_cache_program : 1;
        uint8_t read_cache_commands : 1;
        uint8_t get_and_set_features : 1;
        uint8_t read_status_enhanced : 1;
        uint8_t copyback : 1;
        uint8_t read_unique_id : 1;
        uint16_t reserved : 10;
    } optional_commands;

    // Bytes 10-31: Reserved (0)
    uint8_t reserved[22]; // set with zeros

    // Manufacturer information block
    struct
    {
        char device_manufacturer[12]; // Bytes 32-43
        char device_model[20];        // Bytes 44-63
        uint8_t jedec_manufacturer;   // Bytes 64
        uint8_t data_code[2];         // Bytes 65-66
        uint8_t reserved[13];         // Bytes 67-79
    } manufacturer_info_block;

    // Memory organization block
    struct
    {
        // 80–83: Number of data bytes per page
        uint32_t data_bytes_per_page;

        // 84–85: Number of spare bytes per page
        uint16_t spare_bytes_per_page;

        // 86–89: Number of data bytes per partial page
        uint32_t data_bytes_per_partial_page;

        // 90–91: Number of spare bytes per partial page
        uint16_t spare_bytes_per_partial_page;

        // 92–95: Number of pages per block
        uint32_t pages_per_block;

        // 96–99: Number of blocks per logical unit (LUN)
        uint32_t blocks_per_lun;

        // 100: Number of logical units (LUNs)
        uint8_t number_of_luns;

        // 101: Address cycles (4 bits row, 4 bits column)
        struct
        {
            uint8_t row_address_cycles : 4;    // Bits 0–3
            uint8_t column_address_cycles : 4; // Bits 4–7
        } address_cycles;

        // 102: Number of bits per cell
        uint8_t bits_per_cell;

        // 103–104: Bad blocks maximum per LUN
        uint16_t bad_blocks_per_lun;

        // 105–106: Block endurance
        uint16_t block_endurance;

        // 107: Guaranteed valid blocks at beginning of target
        uint8_t guaranteed_valid_blocks;

        // 108–109: Block endurance for guaranteed valid blocks
        uint16_t guaranteed_block_endurance;

        // 110: Number of programs per page
        uint8_t programs_per_page;

        // 111: Partial programming attributes
        struct
        {
            uint8_t partial_program_constraints : 1; // Bit 0
            uint8_t reserved1 : 3;                   // Bits 1–3
            uint8_t partial_layout_spare : 1;        // Bit 4
            uint8_t reserved2 : 3;                   // Bits 5–7
        } partial_prog_attr;

        // 112: Number of bits ECC correctability
        uint8_t ecc_bits;

        // 113: Number of interleaved address bits (0–3), reserved (4–7)
        struct
        {
            uint8_t interleaved_address_bits : 4;
            uint8_t reserved : 4;
        } interleave_addr_bits;

        // 114: Interleaved operation attributes
        struct
        {
            uint8_t overlapped_interleaving : 1;       // Bit 0
            uint8_t block_addr_restriction : 1;        // Bit 1
            uint8_t program_cache_supported : 1;       // Bit 2
            uint8_t program_cache_addr_restricted : 1; // Bit 3
            uint8_t reserved : 4;                      // Bits 4–7
        } interleave_op_attr;

        // 115–127: Reserved
        uint8_t reserved[13];

    } mem_org_block;

    // Bytes 128 - 163 Electrical parameters block
    uint8_t electrical_params[36];

    // Vendor block
    struct
    {
        uint16_t vendor_revision_number; // 164–165: Vendor-specific revision
        uint8_t vendor_specific[88];     // 166–253: Vendor-specific data
        uint16_t integrity_crc;          // 254–255: CRC for parameter page
    } vendor_block;
} __attribute__((packed)) onfi_param_page_t;

typedef struct {
    onfi_status_reg_t status_reg;
    onfi_param_page_t param_page;
} __attribute__((packed)) onfi_device_t;
#pragma pack(pop)

extern onfi_device_t *g_onfi_devices;

/**** Some ONFI helper functions ****/
uint16_t _ONFI_CRC16(uint8_t *data, size_t data_size);

void _ONFI_UPDATE_STATUS_REGISTER(onfi_status_reg_t *status_reg, onfi_ret_val last_op_ret_val);

onfi_ret_val ONFI_INIT(uint8_t device_index);

/**
* Reads from a page of data identified by the given row address starting at the column address specified.
* o_buffer is filled with the read bytes and o_read_bytes_amount is set to the amount of read bytes.
* Reading beyond the end of a page results in indeterminate values being written to the buffer.
* 
* device_index - index (0-based) of the target SSD device
* row_address - consists of LUN address, block address and page number (ppn - physical page number)
* column_address - offset in page
* o_buffer - will be filled with page read
* buffer_size - output buffer size (the amount of bytes asked to be read)
* o_read_bytes_amount - will be set according to number of bytes read
**/
onfi_ret_val ONFI_READ(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                       uint8_t *o_buffer, size_t buffer_size, size_t *o_read_bytes_amount);

/**
* Programs a page or portion of a page of data to the page identified by the given row address starting at the column address specified.
* Writing beyond the end of a page is undefined. 
* 
* device_index - index (0-based) of the target SSD device
* row_address - consists of LUN address, block address and page number (ppn - physical page number)
* column_address - offset in page
* buffer -  contains data to program
* buffer_size - data buffer size (the amount of bytes asked to be programmed/written)
* o_programmed_bytes_amount - will be set according to number of bytes programmed
**/
onfi_ret_val ONFI_PAGE_PROGRAM(uint8_t device_index, uint64_t row_address, uint32_t column_address,
                               const uint8_t *buffer, size_t buffer_size, size_t *o_programmed_bytes_amount);

/**
* Erases the block consisting of the specified row address (ppn - physical page number).
* After a successful Block Erase, all bits are set to one in the block.
* 
* device_index - index (0-based) of the target SSD device
* row_address - consists of LUN address, block address and page number (ppn - physical page number)
**/
onfi_ret_val ONFI_BLOCK_ERASE(uint8_t device_index, uint64_t row_address);

/**
* Returns ID of specified address. There are two options:
* 1. When the address is 20h the function returns the ONFI signature if the target supports the ONFI specification.
*    The ONFI signature is the ASCII encoding of ‘ONFI’ where ‘O’ = 4Fh, ‘N’ = 4Eh, ‘F’ = 46h, and ‘I’ = 49h.
*    Reading beyond four bytes yields indeterminate values. 
* 2. When the address is 00h the function returns the JEDEC manufacturer ID and the device ID for the particular NAND part.
*    Reading beyond the first two bytes yields values as specified by the manufacturer. In our case, there is no meaning to those bytes,
*    so reading beyond those two bytes yields indeterminate values.
*
* device_index - index (0-based) of the target SSD device - currently all the SDD devices will return the same values.
* address - only addresses of 00h (JEDEC_ID_ADDR) and 20h (ONFI_SIGNATURE_ADDR) are valid according to ONFI specification
*           (20h for ONFI signature and 00h for the JEDEC manufacturer ID and device ID)
* o_buffer - will be filled with ID of specified address
* buffer_size - output buffer size (the amount of bytes asked to be read)
**/
onfi_ret_val ONFI_READ_ID(uint8_t device_index, uint8_t address, uint8_t *o_buffer, size_t buffer_size);

/**
 * Retrieves the data structure that describes some flash’s behavioral parameters as described in parameter page data
 * structure definition (according to 5.4.1 in ONFI 1.0 specification).
 * Values in the parameter page are static and shall not change. See onfi_param_page_t structure for full values reference.
 * If buffer size is larger than the parameter page data structure then fill the buffer with repeating copies of it.
 * 
 * device_index - index (0-based) of the target SSD device
 * timing_mode - will be 0 as this is the only mode we are going to support (reserved)
 * o_buffer - will be filled with the parameter page data as described in parameter page data structure definition 
 *            (see onfi_param_page_t structure which is written according to 5.4.1 in ONFI 1.0 specification)
 * buffer_size - output buffer size (the amount of bytes asked to be read)
 **/
onfi_ret_val ONFI_READ_PARAMETER_PAGE(uint8_t device_index, uint8_t timing_mode, uint8_t *o_buffer, size_t buffer_size);

/**
 * Retrieves one byte status value of the flash and for the last operation issued.
 * 
 * device_index - index (0-based) of the target SSD device
 * o_status_register - a pointer to one byte value which represents status register fields according to
 * section 5.10 in the ONFI 0.1 specification (see onfi_status_reg_t which is one bytes struct).
 **/
onfi_ret_val ONFI_READ_STATUS(uint8_t device_index, onfi_status_reg_t *o_status_register);

/**
 * Clears the status register of the SSD device.
 * 
 * device_index - index (0-based) of the target SSD device
 **/
onfi_ret_val ONFI_RESET(uint8_t device_index);

#endif // ONFI_H
