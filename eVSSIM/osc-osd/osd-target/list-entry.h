/*
 * List format.
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
#ifndef __LIST_ENTRY_H
#define __LIST_ENTRY_H

#include <stdint.h>

int le_pack_attr(void *buf, uint32_t buflen, uint32_t page, uint32_t number,
		 uint16_t valen, const void *val);

int le_multiobj_pack_attr(void *buf, uint32_t buflen, uint64_t oid, 
			  uint32_t page, uint32_t number, uint16_t valen,
			  const void *val);

#endif /* __LIST_ENTRY_H */
