/*
 * Single interface from stgt to OSD software implementation.
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
#ifndef __CDB_H
#define __CDB_H

/* module interface */
struct osd_device;

struct osd_device *osd_device_alloc(void);
void osd_device_free(struct osd_device *osd);
int osd_open(const char *root, struct osd_device *osd);
int osd_close(struct osd_device *osd);
int osdemu_cmd_submit(struct osd_device *osd, uint8_t *cdb,
                      const uint8_t *data_in, uint64_t data_in_len,
		      uint8_t **data_out, uint64_t *data_out_len,
		      uint8_t *sense_out, int *senselen_out);
int osd_set_name(struct osd_device *osd, char *osdname);

#endif /* __CDB_H */
