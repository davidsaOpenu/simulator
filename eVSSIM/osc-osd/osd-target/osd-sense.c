/*
 * Sense generation.
 *
 * Copyright (C) 2007 OSD Team <pvfs-osd@osc.edu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <sys/types.h>
#include <stdint.h>

#include "target-sense.h"
#include "osd-util/osd-sense.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-defs.h"

/*
 * Descriptor format sense data.  See spc3 p 31.  Returns length of
 * buffer used so far.
 */
int _sense_header_build(uint8_t *data, int len, uint8_t key, uint16_t code,
		       uint8_t additional_len, const char *file, int line)
{
	if (len < 8)
		return 0;
	data[0] = 0x72;  /* current, not deferred */
	data[1] = key;
	data[2] = ASC(code);
	data[3] = ASCQ(code);
	data[7] = additional_len;  /* additional length, beyond these 8 bytes */
	osd_warning("%s:%d: _sense_header_build key=%d code=%x additional_len=%d",
		 file, line, key, code, additional_len);

	return 8;
}

/*
 * OSD object identification sense data descriptor.  Always need one
 * of these, then perhaps some others.  This goes just beyond the 8
 * byte sense header above.  The length of this is 32. osd2r01 Sec 4.14.2.1
 */
static int sense_info_build(uint8_t *data, int len, uint32_t not_init_funcs,
		     uint32_t completed_funcs, uint64_t pid, uint64_t oid)
{
	if (len < 32)
		return 0;
	data[0] = 0x6;
	data[1] = 0x1e;
	set_htonl(&data[8], not_init_funcs);
	set_htonl(&data[12], completed_funcs);
	set_htonll(&data[16], pid);
	set_htonll(&data[24], oid);
	osd_warning("  identification pid=%lx oid=%lx",pid, oid);
	return 32;
}

/*
 * SPC-3 command-specific information sense data descriptor.  p. 33.
 */
static int sense_csi_build(uint8_t *data, int len, uint64_t csi)
{
	if (len < 12)
		return 0;
	data[0] = 0x1;
	data[1] = 0xa;
	set_htonll(&data[4], csi);
	osd_warning("  command-specific information=%lx",csi);
	return 12;
}

/*
 * Helper to create sense data where no processing was initiated or completed,
 * and just a header and basic info descriptor are required.  Assumes full 252
 * byte sense buffer.
 */
int _sense_basic_build(uint8_t *sense, uint8_t key, uint16_t code,
		      uint64_t pid, uint64_t oid, const char *file, int line)
{
	uint8_t off = 0;
	uint8_t len = OSD_MAX_SENSE;
	uint32_t nifunc = 0x303010b0;  /* non-reserved bits */

	off = _sense_header_build(sense+off, len-off, key, code, 32, file, line);
	off += sense_info_build(sense+off, len-off, nifunc, 0, pid, oid);
	return off;
}

/*
 * Same as basic_build?  Delete one.
 */
int _sense_build_sdd(uint8_t *sense, uint8_t key, uint16_t code,
		    uint64_t pid, uint64_t oid, const char *file, int line)
{
	uint8_t off = 0;
	uint8_t len = OSD_MAX_SENSE;
	uint32_t nifunc = 0x303010b0;  /* non-reserved bits */

	off = _sense_header_build(sense+off, len-off, key, code, 32, file, line);
	off += sense_info_build(sense+off, len-off, nifunc, 0, pid, oid);
	return off;
}

/*
 * Sense header, info section with pid and oid, and "info" section
 * with an arbitrary u64.
 */
int _sense_build_sdd_csi(uint8_t *sense, uint8_t key, uint16_t code,
		        uint64_t pid, uint64_t oid, uint64_t csi, const char *file, int line)
{
	uint8_t off = 0;
	uint8_t len = OSD_MAX_SENSE;
	uint32_t nifunc = 0x303010b0;  /* non-reserved bits */

	off = _sense_header_build(sense+off, len-off, key, code, 44, file, line);
	off += sense_info_build(sense+off, len-off, nifunc, 0, pid, oid);
	off += sense_csi_build(sense+off, len-off, csi);
	return off;
}

