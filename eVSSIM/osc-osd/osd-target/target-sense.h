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
#ifndef __TARGET_SENSE_H
#define __TARGET_SENSE_H

#define sense_header_build(data, len, key, code, additional_len) \
	_sense_header_build(data, len, key, code, additional_len, __func__, __LINE__)
int _sense_header_build(uint8_t *data, int len, uint8_t key, uint16_t code,
		       uint8_t additional_len, const char *file, int line);

/*#define sense_info_build(data, len, not_init_funcs, completed_funcs, pid, oid) \
	_sense_info_build(data, len, not_init_funcs, completed_funcs, pid, oid, __FILE__, __LINE__)
int _sense_info_build(uint8_t *data, int len, uint32_t not_init_funcs,
		     uint32_t completed_funcs, uint64_t pid, uint64_t oid, const char *file, int line);
*/
/*#define sense_csi_build(data, len, csi) \
	_sense_csi_build(data, len, csi, __FILE__, __LINE__)
int _sense_csi_build(uint8_t *data, int len, uint64_t csi, const char *file, int line);
*/
#define sense_basic_build(sense, key, code, pid, oid) \
	_sense_basic_build(sense, key, code, pid, oid, __func__, __LINE__)
int _sense_basic_build(uint8_t *sense, uint8_t key, uint16_t code,
                      uint64_t pid, uint64_t oid, const char *file, int line);

#define sense_build_sdd(sense, key, code, pid, oid) \
	_sense_build_sdd(sense, key, code, pid, oid, __func__, __LINE__)
int _sense_build_sdd(uint8_t *sense, uint8_t key, uint16_t code,
		    uint64_t pid, uint64_t oid, const char *file, int line);

#define sense_build_sdd_csi(sense, key, code, pid, oid, csi) \
	_sense_build_sdd_csi(sense, key, code, pid, oid, csi, __func__, __LINE__)
int _sense_build_sdd_csi(uint8_t *sense, uint8_t key, uint16_t code,
		        uint64_t pid, uint64_t oid, uint64_t csi, const char *file, int line);

#define ASC(x) ((x & 0xFF00) >> 8)
#define ASCQ(x) (x & 0x00FF)

static inline int sense_test_type(uint8_t *sense, uint8_t key, uint16_t code)
{
	return (sense[1] == key && sense[2] == ASC(code) && 
		sense[3] == ASCQ(code));
}

#endif  /* __TARGET_SENSE_H */
