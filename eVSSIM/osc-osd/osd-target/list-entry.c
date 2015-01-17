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
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "osd.h"
#include "list-entry.h"
#include "osd-util/osd-util.h"

/* 
 * retrieve attr in list entry format; tab 129 Sec 7.1.3.3
 *
 * returns: 
 * -EINVAL: error, alignment error
 * -EOVERFLOW: error, if not enough room to even start the entry
 * >0: success. returns number of bytes copied into buf.
 */
int le_pack_attr(void *buf, uint32_t buflen, uint32_t page, uint32_t number,
		 uint16_t valen, const void *val)
{
	uint8_t pad = 0;
	struct list_entry *list_entry = buf;
	uint8_t *cp = buf;
	uint32_t len = buflen;

	/* 
	 * XXX: osd-errata: buf and buflen must be 8B aligned cannot ensure
	 * alignment: XXX:SD  LIST with list_attr does not follow the spec,
	 * see the comment in mtq.c:mtq_list_oids_attr. 
	 */
	if ((buflen & 0x7) || ((uintptr_t) buf & 0x7))
		return -EINVAL;

	if (buflen < LE_MIN_ITEM_LEN)
		return -EOVERFLOW;

	set_htonl(&list_entry->page, page);
	set_htonl(&list_entry->number, number);

	/* length field is not modified to reflect truncation, sec 5.2.2.2 */
	set_htons(&list_entry->len, valen);
	memset(list_entry->reserved, 0, sizeof(list_entry->reserved));

	if (val != NULL) {
		buflen -= LE_VAL_OFF;
		if ((uint32_t)valen > buflen)
			valen = buflen;
		memcpy(&cp[LE_VAL_OFF], val, valen);
	} else {
		assert(valen == 0); /* osd2r02, null attr len == 0, 7.1.3.1 */
		valen = 0;
	}

	len = valen + LE_VAL_OFF;
	pad = roundup8(len) - len;
	cp += len; 
	len += pad;
	if (pad)
		memset(cp, 0, pad);
	return len;
}

/*
 * retrieve attr in list entry format; tab 130 Sec 7.1.3.4
 *
 * returns:
 * -EINVAL: error, alignment error
 * -EOVERFLOW: error, if not enough room to even start the entry
 * >0: success. returns number of bytes copied into buf.
 */
int le_multiobj_pack_attr(void *buf, uint32_t buflen, uint64_t oid, 
			  uint32_t page, uint32_t number, uint16_t valen,
			  const void *val)
{
	int ret = 0;
	uint8_t *cp = buf;

	if (buflen < MLE_MIN_ITEM_LEN)
		return -EOVERFLOW;

	set_htonll(cp, oid);

	/* 
	 * test if layout of struct multiobj_list_entry is similar to 
	 * struct list_entry prefixed with oid.
	 */
	assert(MLE_VAL_OFF == (LE_VAL_OFF+sizeof(oid)));

	ret = le_pack_attr(cp+sizeof(oid), buflen-sizeof(oid), page, number,
			   valen, val);
	if (ret > 0)
		ret = MLE_PAGE_OFF + ret; /* add sizeof(oid) to length */
	return ret;
}

