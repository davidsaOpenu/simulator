/*
 * Initial command descriptor block parsing.  Gateway into the
 * core osd functions in osd.c.
 *
 * Copyright (C) 2007 OSD Team <pvfs-osd@osc.edu>
 * Copyright (C) 2010 University of Connecticut
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "osd.h"
#include "osd-util/osd-sense.h"
#include "target-sense.h"
#include "cdb.h"
#include "osd-util/osd-util.h"
#include "list-entry.h"

/*
 * Aggregate parameters for function calls in this file.
 */

struct cdb_continuation_descriptor {
	uint16_t type;
	uint32_t length;
	union {
		struct sg_list	sglist;
		const void	*desc_specific_hdr;
	};
};

struct cdb_continuation {
	uint32_t num_descriptors;
	struct cdb_continuation_descriptor *descriptors;
};

struct command {
	struct osd_device *osd;
	uint8_t *cdb;
	uint16_t action;
	uint8_t getset_cdbfmt;
	uint64_t retrieved_attr_off;  /* -1 means empty.  0 is a legal value. */
	struct getattr_list get_attr;
	struct setattr_list set_attr;
	const uint8_t *indata;
	struct cdb_continuation cont;
	uint64_t inlen;
	uint8_t *outdata;
	uint64_t outlen;
	uint64_t used_outlen;
	uint32_t get_used_outlen;
	uint8_t sense[OSD_MAX_SENSE];
	int senselen;
};

static int get_attr_page(struct command *cmd, uint64_t pid, uint64_t oid,
			 uint8_t isembedded, uint16_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint8_t *cdb = cmd->cdb;
	uint32_t page = get_ntohl(&cmd->cdb[52]);
	uint32_t alloc_len = get_ntohl(&cdb[56]);
	void *outbuf = NULL;


	if (page == 0 || alloc_len == 0)/* nothing to be retrieved. osd2r00 */
		return 0;	   	/* Sec 5.2.2.2 */

	if (numoid > 1 && page != CUR_CMD_ATTR_PG)
		goto out_param_list_err;

	if (!cmd->outdata)
		goto out_param_list_err;

	outbuf = &cmd->outdata[cmd->retrieved_attr_off];
	return osd_getattr_page(cmd->osd, pid, oid, page, outbuf, alloc_len,
				isembedded, &cmd->get_used_outlen, cdb_cont_len,
				cmd->sense);

out_param_list_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_PARAMETER_LIST_LENGTH_ERROR, pid,
				 oid);
}

static int set_attr_value(struct command *cmd, uint64_t pid, uint64_t oid,
			  uint8_t isembedded, uint16_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	int err = 0;
	uint64_t i = 0;
	uint8_t *cdb = cmd->cdb;
	uint32_t page = get_ntohl(&cmd->cdb[64]);
	uint32_t number = get_ntohl(&cdb[68]);
	uint32_t len = get_ntohl(&cdb[72]); /* XXX: bug in std? sizeof(len) = 4B */
	uint64_t offset = get_ntohoffset(&cdb[76]);

	if (page == 0)
		return 0; /* nothing to set. osd2r00 Sec 5.2.2.2 */

	err = osd_begin_txn(cmd->osd);
	assert(err == 0);

	for (i = oid; i < oid+numoid; i++) {
		ret = osd_set_attributes(cmd->osd, pid, i, page, number,
					 &cmd->indata[offset], len, isembedded,
					 cdb_cont_len, cmd->sense);
		if (ret != 0)
			break;
	}

	err = osd_end_txn(cmd->osd);
	assert(err == 0);

	return ret;
}

static int set_one_attr_value(struct command *cmd, uint64_t pid, uint64_t oid,
			      uint8_t isembedded, uint16_t numoid, uint32_t cdb_cont_len)
{
        int ret = 0;
	int err = 0;
	uint8_t *cdb = cmd->cdb;
	uint64_t i = 0;
	uint32_t page = get_ntohl(&cmd->cdb[52]);
	uint32_t number = get_ntohl(&cdb[56]);
	uint16_t len = get_ntohs(&cdb[60]); 
	void *value = &cmd->cdb[62];

	/* nothing to set. osd2r03 Sec 5.2.4.2 */
	if (page == 0)
	        return 0; 

	/* terminate command with check command status, set sense to illegal
	   reguest osd2r03 Sec 5.2.4.2 */
	if (len > 18)
	        goto out_invalid_param;
	
	/* terminate command with check command status, set sense to illegal
	   request osd2r03 Sec 5.2.4.2 */
	if (page == 0xFFFFFFFF || number == 0xFFFFFFFF)
	        goto out_invalid_param;
        
	err = osd_begin_txn(cmd->osd);
	assert(err == 0);

	for (i = oid; i < oid+numoid; i++) {
    	        ret = osd_set_attributes(cmd->osd, pid, i, page, number,
					 value, len, isembedded, cdb_cont_len, cmd->sense);
		if (ret != 0)
		        break;
	}

	err = osd_end_txn(cmd->osd);
	assert(err == 0);

	return ret;

out_invalid_param:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}
/*
 * TODO: proper return codes
 * returns:
 * ==0: success
 *  >0: failure, senselen is returned.
 */
static int get_attr_list(struct command *cmd, uint64_t pid, uint64_t oid,
			 uint8_t isembedded, uint16_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	int err = 0;
	int within_txn = 0;
	uint64_t i = 0;
	uint8_t list_type;
	uint8_t listfmt = RTRVD_SET_ATTR_LIST;
	uint32_t getattr_list_len = get_ntohl(&cmd->cdb[52]);
	uint32_t list_in_len, list_len = 0;
	uint64_t list_off = get_ntohoffset(&cmd->cdb[56]);
	uint32_t list_alloc_len = get_ntohl(&cmd->cdb[60]);
	const uint8_t *list_hdr = &cmd->indata[list_off];
	uint8_t *outbuf;
	uint8_t *cp = NULL;

	if (getattr_list_len == 0)
		return 0; /* nothing to retrieve, osd2r00 Sec 5.2.2.3 */

	if (!cmd->outdata)
		goto out_param_list_err;
	outbuf = &cmd->outdata[cmd->retrieved_attr_off];

	if (getattr_list_len != 0 && getattr_list_len < LIST_HDR_LEN)
		goto out_param_list_err;

	/* available bytes in getattr list, need at least a header */
	list_in_len = cmd->inlen - list_off;
	if (list_in_len < 8)
		goto out_param_list_err;

	if ((list_off & 0x7) || (list_alloc_len & 0x7))
		goto out_param_list_err;

	list_type = list_hdr[0] & 0xF;
	if (list_type != RTRV_ATTR_LIST)
		goto out_invalid_param_list;

	list_len = getattr_list_len - 8;
	if (list_len & 0x7) /* multiple of 8 */
		goto out_param_list_err;
	if (list_len + 8 < list_in_len)
		goto out_param_list_err;

	if (numoid > 1)
		listfmt = RTRVD_CREATE_MULTIOBJ_LIST;
	outbuf[0] = listfmt; /* fill list header */
	outbuf[1] = outbuf[2] = outbuf[3] = 0;

	err = osd_begin_txn(cmd->osd);
	assert(err == 0);
	within_txn = 1;

	list_hdr += 8;
	cp = outbuf + 8;
	cmd->get_used_outlen = 8;
	list_alloc_len -= 8;
	while (list_len > 0) {
		uint32_t page = get_ntohl(&list_hdr[0]);
		uint32_t number = get_ntohl(&list_hdr[4]);

		for (i = oid; i < oid+numoid; i++) {
			uint32_t get_used_outlen;
			ret = osd_getattr_list(cmd->osd, pid, i, page, number,
					       cp, list_alloc_len, isembedded,
					       listfmt, &get_used_outlen, cdb_cont_len,
					       cmd->sense);
			if (ret != 0) {
				cmd->senselen = ret;
				goto out_err;
			}

			list_alloc_len -= get_used_outlen;
			cmd->get_used_outlen += get_used_outlen;
			cp += get_used_outlen;
		}

		list_len -= 8;
		list_hdr += 8;
	}
	set_htonl(&outbuf[4], cmd->get_used_outlen - 8);

	err = osd_end_txn(cmd->osd);
	assert(err == 0);

	return OSD_OK; /* success */

out_err:
	if (within_txn) {
		err = osd_end_txn(cmd->osd);
		assert(err == 0);
	}
	return ret;

out_param_list_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_PARAMETER_LIST_LENGTH_ERROR, pid,
				 oid);

out_invalid_param_list:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_PARAM_LIST, pid,
				 oid);
}


/*
 * TODO: proper return codes
 * returns:
 * ==0: success
 *  >0: failure, senselen is returned.
 */
static int set_attr_list(struct command *cmd, uint64_t pid, uint64_t oid,
			 uint8_t isembedded, uint16_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	int err = 0;
	int within_txn = 0;
	uint64_t i = 0;
	uint8_t list_type;
	uint8_t *cdb = cmd->cdb;
	uint32_t setattr_list_len = get_ntohl(&cdb[68]);
	uint32_t list_len = 0;
	uint32_t list_off = get_ntohoffset(&cdb[72]);
	const uint8_t *list_hdr = &cmd->indata[list_off];

	if (setattr_list_len == 0)
		return 0; /* nothing to set, osd2r00 Sec 5.2.2.3 */

	if (setattr_list_len != 0 && setattr_list_len < LIST_HDR_LEN)
		goto out_param_list_err;

	list_type = list_hdr[0] & 0xF;
	if (list_type != RTRVD_SET_ATTR_LIST)
		goto out_param_list_err;

	list_len = setattr_list_len - 8;
	if (list_len & 0x7) /* multiple of 8, values are padded */
		goto out_param_list_err;

	err = osd_begin_txn(cmd->osd);
	assert(err == 0);
	within_txn = 1;

	list_hdr = &list_hdr[8]; /* XXX: osd errata */
	while (list_len > 0) {
		uint32_t page = get_ntohl(&list_hdr[LE_PAGE_OFF]);
		uint32_t number = get_ntohl(&list_hdr[LE_NUMBER_OFF]);
		uint32_t len = get_ntohs(&list_hdr[LE_LEN_OFF]);
		uint32_t pad = 0;

		/* set attr on multiple objects if that is the case */
		for (i = oid; i < oid+numoid; i++) {
			ret = osd_set_attributes(cmd->osd, pid, i, page,
						 number, &list_hdr[LE_VAL_OFF], len,
						 isembedded, cdb_cont_len, cmd->sense);
			if (ret != 0) {
				cmd->senselen = ret;
				goto out_err;
			}
		}

		pad = (0x8 - ((LE_VAL_OFF + len) & 0x7)) & 0x7;
		list_hdr += LE_VAL_OFF + len + pad;
		list_len -= LE_VAL_OFF + len + pad;
	}

	err = osd_end_txn(cmd->osd);
	assert(err == 0);

	return 0; /* success */

out_err:
	if (within_txn) {
		err = osd_end_txn(cmd->osd);
		assert(err == 0);
	}
	return ret;

out_param_list_err:
	if (within_txn) {
		err = osd_end_txn(cmd->osd);
		assert(err == 0);
	}
	cmd->senselen = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
					  OSD_ASC_PARAMETER_LIST_LENGTH_ERROR,
					  pid, oid);
	return cmd->senselen;
}

static int parse_getattr_list(struct command *cmd, uint64_t pid, uint64_t oid)
{
       int ret = 0;
       uint64_t i = 0;
       uint8_t list_type;
       uint32_t getattr_list_len = get_ntohl(&cmd->cdb[52]);
       uint32_t list_in_len, list_len = 0;
       uint64_t list_off = get_ntohoffset(&cmd->cdb[56]);
       uint32_t list_alloc_len = get_ntohl(&cmd->cdb[60]);
       const uint8_t *list_hdr = &cmd->indata[list_off];
       uint8_t *cp = NULL;

       if (getattr_list_len == 0)
               return 0; /* nothing to retrieve, osd2r00 Sec 5.2.2.3 */

       if (getattr_list_len != 0 && getattr_list_len < LIST_HDR_LEN)
               goto out_param_list_err;

       /* available bytes in getattr list, need at least a header */
       list_in_len = cmd->inlen - list_off;
       if (list_in_len < 8)
               goto out_param_list_err;

       if ((list_off & 0x7) || (list_alloc_len & 0x7))
               goto out_param_list_err;

       list_type = list_hdr[0] & 0xF;
       if (list_type != RTRV_ATTR_LIST)
               goto out_invalid_param_list;

       list_len = getattr_list_len - 8;
       if (list_len & 0x7) /* multiple of 8 */
               goto out_param_list_err;
       if (list_len + 8 < list_in_len)
               goto out_param_list_err;

       if (list_len > 0) {
               cmd->get_attr.sz = list_len/8;
               /* XXX: This leaks memory. free it somewhere */
               cmd->get_attr.le = Malloc(cmd->get_attr.sz *
                                         sizeof(*(cmd->get_attr.le)));
               if (!cmd->get_attr.le)
                       goto out_hw_err;
       }

       i = 0;
       list_hdr += 8;
       for (i = 0; i < list_len/8; i++) {
               cmd->get_attr.le[i].page = get_ntohl(&list_hdr[0]);
               cmd->get_attr.le[i].number = get_ntohl(&list_hdr[4]);
               list_hdr += 8;
       }

       return 0; /* success */

out_param_list_err:
       return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
                                OSD_ASC_PARAMETER_LIST_LENGTH_ERROR, pid,
                                oid);

out_invalid_param_list:
       return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
                                OSD_ASC_INVALID_FIELD_IN_PARAM_LIST, pid,
                                oid);
out_hw_err:
       return sense_basic_build(cmd->sense, OSD_SSK_HARDWARE_ERROR,
                                OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}


static int parse_setattr_list(struct command *cmd, uint64_t pid, uint64_t oid)
{
	int ret = 0;
	uint32_t i = 0;
	uint8_t list_type;
	uint8_t *cdb = cmd->cdb;
	uint32_t setattr_list_len = get_ntohl(&cmd->cdb[68]);
	uint32_t list_len = 0;
	uint32_t list_off = get_ntohoffset(&cmd->cdb[72]);
	const uint8_t *list_hdr = &cmd->indata[list_off];
	uint8_t pad = 0;

	if (setattr_list_len == 0)
		return 0; /* nothing to set, osd2r00 Sec 5.2.2.3 */

	if (setattr_list_len != 0 && setattr_list_len < LIST_HDR_LEN)
		goto out_param_list_err;

	list_type = list_hdr[0] & 0xF;
	if (list_type != RTRVD_SET_ATTR_LIST)
		goto out_param_list_err;

	list_len = setattr_list_len - 8;
	if (list_len & 0x7) /* multiple of 8, values are padded */
		goto out_param_list_err;

	if (list_len > 0) {
		/*
		 * This malloc provides for the maximum number of attrs,
		 * but it can be much smaller if the attrs have any
		 * values.
		 */
		cmd->set_attr.sz = list_len >> 4; /* min(list_len) == 16 */
		/* XXX: this leaks memory */
		cmd->set_attr.le = Malloc(cmd->set_attr.sz *
					  sizeof(*(cmd->set_attr.le)));
		if (!cmd->set_attr.le)
			goto out_hw_err;
	}

	i = 0;
	pad = 0;
	list_hdr += 8;
	cmd->set_attr.sz = 0;  /* start counting again */
	while (list_len > 0) {
		cmd->set_attr.le[i].page = get_ntohl(&list_hdr[LE_PAGE_OFF]);
		cmd->set_attr.le[i].number =
			get_ntohl(&list_hdr[LE_NUMBER_OFF]);
		cmd->set_attr.le[i].len = get_ntohs(&list_hdr[LE_LEN_OFF]);
		cmd->set_attr.le[i].cval = &list_hdr[LE_VAL_OFF];

		pad = (0x8 - ((LE_VAL_OFF + cmd->set_attr.le[i].len) & 0x7)) &
			0x7;
		list_hdr += LE_VAL_OFF + cmd->set_attr.le[i].len + pad;
		list_len -= LE_VAL_OFF + cmd->set_attr.le[i].len + pad;
		++i;
		++cmd->set_attr.sz;
	}

	return 0;

out_param_list_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_PARAMETER_LIST_LENGTH_ERROR, pid,
				 oid);
out_hw_err:
	return sense_basic_build(cmd->sense, OSD_SSK_HARDWARE_ERROR,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}


/*
 * returns:
 * ==0: success
 *  >0: failed, sense len set accordingly
 */
static int get_attributes(struct command *cmd, uint64_t pid, uint64_t oid,
			  uint16_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint8_t isembedded = true;

	if (numoid < 1)
		goto out_cdb_err;

	if (cmd->action == OSD_GET_ATTRIBUTES)
		isembedded = true;

	if (cmd->getset_cdbfmt == GETPAGE_SETVALUE) {
	        return get_attr_page(cmd, pid, oid, isembedded, numoid, cdb_cont_len);
	} else if (cmd->getset_cdbfmt == GETLIST_SETLIST) {
	        return get_attr_list(cmd, pid, oid, isembedded, numoid, cdb_cont_len);
	} else if (cmd->getset_cdbfmt == GETFIELD_SETVALUE){
	        return 0;
	}else {
		goto out_cdb_err;
	}

out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}

/*
 * returns:
 * ==0: success
 *  >0: failed, sense len set accordingly
 */
static int set_attributes(struct command *cmd, uint64_t pid, uint64_t oid,
			  uint32_t numoid, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint8_t isembedded = true;

	if (numoid < 1)
		goto out_cdb_err;

	if (cmd->action == OSD_SET_ATTRIBUTES)
		isembedded = true;

	if (cmd->getset_cdbfmt == GETPAGE_SETVALUE) {
	        return set_attr_value(cmd, pid, oid, isembedded, numoid, cdb_cont_len);
	} else if (cmd->getset_cdbfmt == GETLIST_SETLIST) {
	        return set_attr_list(cmd, pid, oid, isembedded, numoid, cdb_cont_len);
	} else if (cmd->getset_cdbfmt == GETFIELD_SETVALUE) {
	        return set_one_attr_value(cmd, pid, oid, isembedded, numoid, cdb_cont_len);
	} else {
		goto out_cdb_err;
	}

out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}

static int cdb_copy_user_objects(struct command *cmd)
{
        int ret = 0;							
	uint16_t descriptor_type = 0;
	uint8_t dupl_method = cmd->cdb[14];
	uint64_t destination_pid = get_ntohll(&cmd->cdb[16]);
	uint64_t requested_oid = get_ntohll(&cmd->cdb[24]);
	struct cdb_continuation_descriptor *copy_desc;
	const struct copy_user_object_source *cuos;

	/* osd2r04 6.4 - there should be exactly one copy user object
	   source descriptor and at most one extension capabilities
	   descriptor */
	if (cmd->cont.num_descriptors == 1 &&
	    cmd->cont.descriptors[0].type != COPY_USER_OBJECT_SOURCE) {
		copy_desc = &cmd->cont.descriptors[0];
	} else if (cmd->cont.num_descriptors == 2 &&
		 cmd->cont.descriptors[0].type == COPY_USER_OBJECT_SOURCE &&
		 cmd->cont.descriptors[1].type == EXTENSION_CAPABILITIES) {
		copy_desc = &cmd->cont.descriptors[0];
	} else if (cmd->cont.num_descriptors == 2 &&
		 cmd->cont.descriptors[1].type == COPY_USER_OBJECT_SOURCE &&
		 cmd->cont.descriptors[0].type == EXTENSION_CAPABILITIES) {
		copy_desc = &cmd->cont.descriptors[1];
	} else {
		goto out_cdb_err;
	}

	cuos = (typeof(cuos))copy_desc->desc_specific_hdr;
	if (copy_desc->length != 18)
	        goto out_cdb_err;

	ret = osd_copy_user_objects(cmd->osd, destination_pid, requested_oid,
				    cuos, dupl_method, cmd->sense);
out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, destination_pid,
				requested_oid);
	return ret;
}

/*
 * returns:
 * ==0: success
 *  >0: error, cmd->sense is appropriately set
 */
static int cdb_create(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	int err = 0;
	int within_txn = 0;
	uint64_t i = 0;
	uint32_t page;
	uint64_t oid;
	uint8_t *cdb = cmd->cdb;
	uint64_t pid = get_ntohll(&cdb[16]);
	uint64_t requested_oid = get_ntohll(&cdb[24]);
	uint16_t numoid = get_ntohs(&cdb[32]);
	uint8_t local_sense[OSD_MAX_SENSE];

	TICK_TRACE(cdb_create);
	if (numoid > 1 && cmd->getset_cdbfmt == GETPAGE_SETVALUE) {
		page = get_ntohl(&cmd->cdb[52]);
		if (page != CUR_CMD_ATTR_PG)
			goto out_cdb_err;
	}

	err = osd_begin_txn(cmd->osd);
	assert(err == 0);

	ret = osd_create(cmd->osd, pid, requested_oid, numoid, cdb_cont_len, cmd->sense);

	err = osd_end_txn(cmd->osd);
	assert(err == 0);

	if (ret != 0)
		return ret;

	numoid = (numoid == 0) ? 1 : numoid;
	oid = osd_get_created_oid(cmd->osd, numoid);

	TICK_TRACE(cdb_create_setattr);
	ret = set_attributes(cmd, pid, oid, numoid, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;
	TICK_TRACE(cdb_create_getattr);
	ret = get_attributes(cmd, pid, oid, numoid, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;

	TICK_TRACE(cdb_create);
	return ret; /* success */

out_remove_obj:
	for (i = oid; i < oid+numoid; i++)
	        osd_remove(cmd->osd, pid, i, cdb_cont_len, local_sense);
	return ret; /* preserve ret */

out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, pid,
				requested_oid);
	return ret;
}

/*
 * returns:
 * ==0: on success
 *  >0: on failure. no side-effects of create partition remain.
 */
static int cdb_create_collection(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret;
	uint8_t sense[OSD_MAX_SENSE];
	uint64_t pid = get_ntohll(&cmd->cdb[16]);
	uint64_t cid, requested_cid = get_ntohll(&cmd->cdb[24]);

	ret = osd_create_collection(cmd->osd, pid, requested_cid, cdb_cont_len, sense);
	if (ret != 0)
		return ret;

	cid = cmd->osd->ccap.oid;
	ret = set_attributes(cmd, pid, cid, 1, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;
	ret = get_attributes(cmd, pid, cid, 1, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;

	return ret;

out_remove_obj:
	osd_remove(cmd->osd, pid, cid, cdb_cont_len, sense);
	return ret;
}

/*
 * returns:
 * ==0: on success
 *  >0: on failure. no side-effects of create partition remain.
 */
static int cdb_create_partition(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint64_t pid = 0;
	uint64_t requested_pid = get_ntohll(&cmd->cdb[16]);
	uint8_t local_sense[OSD_MAX_SENSE];

	ret = osd_create_partition(cmd->osd, requested_pid, cdb_cont_len, cmd->sense);
	if (ret != 0)
		return ret;

	pid = cmd->osd->ccap.pid;
	ret = set_attributes(cmd, pid, PARTITION_OID, 1, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;
	ret = get_attributes(cmd, pid, PARTITION_OID, 1, cdb_cont_len);
	if (ret != 0)
		goto out_remove_obj;

	return ret;

out_remove_obj:
	osd_remove(cmd->osd, pid, PARTITION_OID, cdb_cont_len, local_sense);
	return ret;
}

/*
 * returns:
 * ==0: on success
 *  >0: on failure
 */
static int cdb_remove_partition(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint64_t pid = get_ntohll(&cmd->cdb[16]);

	ret = set_attributes(cmd, pid, PARTITION_OID, 1, cdb_cont_len);
	if (ret != 0)
		return ret;
	ret = get_attributes(cmd, pid, PARTITION_OID, 1, cdb_cont_len);
	if (ret != 0)
		return ret;

	return osd_remove_partition(cmd->osd, pid, cdb_cont_len, cmd->sense);
}

/*
 * returns:
 * ==0: on success
 *  >0: on failure
 */
static int cdb_remove(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint64_t pid = get_ntohll(&cmd->cdb[16]);
	uint64_t oid = get_ntohll(&cmd->cdb[24]);

	ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret != 0)
		return ret;
	ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret != 0)
		return ret;

	return osd_remove(cmd->osd, pid, oid, cdb_cont_len, cmd->sense);
}

/*
 * returns:
 * ==0: success
 *  >0: error; either it is a genuine error or it is OSD_SSK_RECOVERED_ERROR.
 */
static int cdb_query(struct command *cmd, uint64_t pid, uint64_t cid,
		     uint32_t cdb_cont_len)
{
	uint8_t *cdb = cmd->cdb;
	uint64_t alloc_len = get_ntohll(&cdb[32]);

	uint64_t matches_cid = get_ntohll(&cdb[40]);
	struct cdb_continuation_descriptor *query_desc;
	struct cdb_continuation_descriptor *descriptors = cmd->cont.descriptors;

	if (matches_cid == 0) {
		/* osd2r04 6.26.1 - if matches_cid is zero,
		   there should be one query list descriptor */
		if (cmd->cont.num_descriptors != 1 ||
		    descriptors[0].type != QUERY_LIST) {
			goto out_query_cdb_err;
		}
		query_desc = &descriptors[0];
	} else {
		/* osd2r04 6.26.1 - if matches_cid is nonzero,
		   there should be one query list descriptor and
		   one extension capabilities descriptor */
		if (cmd->cont.num_descriptors != 2) {
			goto out_query_cdb_err;
		}
		if ((descriptors[0].type == QUERY_LIST) &&
		    (descriptors[1].type == EXTENSION_CAPABILITIES)) {
			query_desc = &descriptors[0];
		} else if ((descriptors[1].type == QUERY_LIST) &&
			 (descriptors[0].type == EXTENSION_CAPABILITIES)) {
			query_desc = &descriptors[1];
		} else {
			goto out_query_cdb_err;
		}
	}

	return osd_query(cmd->osd, pid, cid, query_desc->length, alloc_len,
			 query_desc->desc_specific_hdr, cmd->outdata,
			 &cmd->used_outlen, cdb_cont_len, cmd->sense);
out_query_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
}

/*
 * returns:
 * ==0: success
 *  >0: error; either it is a genuine error or it is OSD_SSK_RECOVERED_ERROR.
 */
static int cdb_read(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0, rec_err_sense = 0;
	uint8_t ddt = cmd->cdb[10];
	uint64_t pid = get_ntohll(&cmd->cdb[16]);
	uint64_t oid = get_ntohll(&cmd->cdb[24]);
	uint64_t len = get_ntohll(&cmd->cdb[32]);
	uint64_t offset = get_ntohll(&cmd->cdb[40]);

	struct sg_list sgl, *sglist;
	const uint8_t *indata;

	/* osd2r04 6.27 - at most one sg list descriptor
			  and no other descriptors */
	if (cmd->cont.num_descriptors > 1 ||
	    (cmd->cont.num_descriptors == 1 &&
	     cmd->cont.descriptors[0].type != SCATTER_GATHER_LIST)) {
		return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
					OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	}

	if (cmd->cont.num_descriptors == 1) {
		sglist = &cmd->cont.descriptors[0].sglist;
		indata = cmd->indata + cdb_cont_len;
		ddt = DDT_SGL;
	} else {
		indata = cmd->indata;
		sglist = NULL;
		ddt = DDT_CONTIG;
	}

	ret = osd_read(cmd->osd, pid, oid, len, offset, indata, cmd->outdata,
		       &cmd->used_outlen, sglist, cmd->sense, ddt);
	if (ret) {
		/* only tolerate recovered error, return for others */
		if (!sense_test_type(cmd->sense, OSD_SSK_RECOVERED_ERROR,
				     OSD_ASC_READ_PAST_END_OF_USER_OBJECT))
			return ret;
		rec_err_sense = ret; /* save ret */
	}

	ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret)
		return ret;
	ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret)
		return ret;
	return rec_err_sense; /* return 0 or recoved error sense length */
}

static int cdb_read_map(struct command *cmd, uint32_t cdb_cont_len)
{
        int ret = 0;
	uint64_t pid = get_ntohll(&cmd->cdb[16]);
	uint64_t oid = get_ntohll(&cmd->cdb[24]);
	uint64_t alloc_len = get_ntohll(&cmd->cdb[32]);
	uint64_t offset = get_ntohll(&cmd->cdb[40]);
	uint16_t map_type = get_ntohs(&cmd->cdb[48]);
     
	ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret)
	        return ret;
	ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret)
	        return ret;
	
	return osd_read_map(cmd->osd, pid, oid, alloc_len, offset, map_type, 
			    cmd->outdata, &cmd->used_outlen, cdb_cont_len,  cmd->sense);
}
	
/*
 *  actions for various (pid, list_attr) combinations:
 *
 *  0, 0: get pids within root, no attrs returned. results in param. data
 *  0, 1: get pids within root. results are returned as described below:
 *  	- all root object (since pid = 0) pages described in get attr segment
 *  	  of CDB are returned in retrieved attr segment.
 *  	- for each pid computed by the LIST command all paritition pages
 *  	  described in get attr segment of CDB shall be returned in
 *  	  command parameter data
 *  	- if there is any page associated with userobject or collection error
 *  	  is flagged.
 * !0, 0: get oids within the pid, no attr returned. results in param. data
 * !0, 1: get oids within the pid. results are returned as described below:
 * 	- all parition object (since pid != 0) pages described in get attr
 * 	  segment are returned in retrieved attr segment.
 * 	- for each oid computed by the LIST command all user object pages
 * 	  described in get attr segment of CDB shall be returned in
 * 	  command parition data
 * 	- if there is any page associated with root or collection objects
 * 	  then error is flagged.
 */
static int cdb_list(struct command *cmd)
{
	int ret = 0;
	uint8_t *cdb = cmd->cdb;
	uint8_t list_attr = (cdb[11] & 0x40) >> 6;
	uint8_t sort_order = (cdb[11] & 0x0F);
	uint64_t pid = get_ntohll(&cdb[16]);
	uint32_t list_id = get_ntohl(&cdb[48]);
	uint64_t alloc_len = get_ntohll(&cdb[32]);
	uint64_t initial_oid = get_ntohll(&cdb[40]);

	if (cmd->getset_cdbfmt == GETPAGE_SETVALUE)
		goto out_cdb_err; /* TODO: implement this */

	if (list_attr == 1 && cmd->getset_cdbfmt == GETPAGE_SETVALUE)
		goto out_cdb_err;

	if (sort_order != 0)
		goto out_cdb_err;

	ret = parse_getattr_list(cmd, pid, initial_oid);
	if (ret)
		goto out_cdb_err;

	/*
	 * TODO: not implementing the following:
	 * get pids within root
	 * get pids within root + their attrs
	 * get/set attributes along with list for that
	 * setvalue not implemented
	 */
	return osd_list(cmd->osd, list_attr, pid, alloc_len, initial_oid,
			&cmd->get_attr, list_id, cmd->outdata,
			&cmd->used_outlen, cmd->sense);

	/* get/set attr not implemented */

out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, pid,
				initial_oid);
	return ret;
}

static int cdb_set_member_attributes(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint64_t pid = get_ntohll(&cmd->cdb[16]);
	uint64_t cid = get_ntohll(&cmd->cdb[24]);

	ret = parse_setattr_list(cmd, pid, cid);
	if (ret)
		goto out_cdb_err;

	ret = osd_set_member_attributes(cmd->osd, pid, cid, &cmd->set_attr,
					cdb_cont_len, cmd->sense);
	if (ret)
		return ret;

	/*
	 * TODO: currently not setting any attributes in collection. Also not
	 * getting any attributes, either for the collection or for the
	 * objects within collection
	 */
	return 0;

out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
	return ret;
}


static inline int std_get_set_attr(struct command *cmd, uint64_t pid,
				   uint64_t oid, uint32_t cdb_cont_len)
{
        int ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret)
		return ret;

	return get_attributes(cmd, pid, oid, 1, cdb_cont_len);
}

static int cdb_cas(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint8_t *cdb = cmd->cdb;
	uint64_t pid = get_ntohll(&cdb[16]);
	uint64_t oid = get_ntohll(&cdb[24]);
	uint64_t len = get_ntohll(&cdb[32]); /* len of cmp/swap */
	uint64_t off = get_ntohll(&cdb[40]); /* offset in dataout */
	uint64_t cmp, swap;

	if (cmd->outdata == NULL || cmd->indata == NULL)
		goto out_cdb_err;

	if (len < sizeof(swap))
		goto out_cdb_err;

	/*
	 * cmp & swap always start at offset 0, get/set attributes follow
	 * them
	 */
	cmp = get_ntohll(&cmd->indata[0]);
	swap = get_ntohll(&cmd->indata[8]);

	ret = osd_cas(cmd->osd, pid, oid, cmp, swap, cmd->outdata + off,
		      &cmd->used_outlen, cmd->sense);
	if (ret)
		return ret;

	ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret != 0)
		return ret;
	return get_attributes(cmd, pid, oid, 1, cdb_cont_len);

out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}


static int cdb_fa(struct command *cmd, uint32_t cdb_cont_len)
{
	int ret = 0;
	uint8_t *cdb = cmd->cdb;
	uint64_t pid = get_ntohll(&cdb[16]);
	uint64_t oid = get_ntohll(&cdb[24]);
	uint64_t len = get_ntohll(&cdb[32]); /* datain len */
	uint64_t off = get_ntohll(&cdb[40]); /* offset in dataout */
	uint64_t add;

	if (cmd->outdata == NULL || cmd->indata == NULL)
		goto out_cdb_err;

	if (len < sizeof(add))
		goto out_cdb_err;

	/* add always start at offset 0, get/set attributes follow them */
	add = get_ntohll(&cmd->indata[0]);

	ret = osd_fa(cmd->osd, pid, oid, add, cmd->outdata + off,
		     &cmd->used_outlen, cmd->sense);
	if (ret)
		return ret;

	ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
	if (ret != 0)
		return ret;
	return get_attributes(cmd, pid, oid, 1, cdb_cont_len);

out_cdb_err:
	ret = sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

static int exec_gen_cas(struct command *cmd, uint64_t pid, uint64_t oid,
			const uint8_t **setattr_list, uint32_t *orig_page,
			uint32_t *orig_number, uint8_t **orig,
			uint16_t *orig_len, uint32_t *list_len,
			uint8_t *cas_res)
{
	int ret;
	uint8_t pad, list_type;
	uint32_t page, number;
	const uint8_t *cmp, *swap;
	uint16_t cmp_len, swap_len;
	const uint8_t *list = *setattr_list;
	uint32_t setattr_list_len = get_ntohl(&cmd->cdb[68]);

	if (setattr_list_len < LIST_HDR_LEN) /* need atleast cmp & swap */
		goto out_param_list_err;

	list_type = list[0] & 0xF;
	if (list_type != RTRVD_SET_ATTR_LIST)
		goto out_param_list_err;

	*list_len = get_ntohl(&list[4]); /* XXX: osd errata */
	if ((*list_len + 8) != setattr_list_len)
		goto out_param_list_err;
	if (*list_len & 0x7) /* multiple of 8, values are padded */
		goto out_param_list_err;

	/* grab cmp & swap */
	list = &list[8]; /* XXX: osd errata */
	page = get_ntohl(&list[0]);
	number = get_ntohl(&list[4]);
	cmp_len = get_ntohs(&list[8]);
	if (cmp_len == 0)
		cmp = NULL;
	else
		cmp = &list[10];
	pad = (0x8 - ((LE_VAL_OFF + cmp_len) & 0x7)) & 0x7;
	list += LE_VAL_OFF + cmp_len + pad;
	*list_len -= LE_VAL_OFF + cmp_len + pad;
	if (*list_len < LE_VAL_OFF)
		goto out_cdb_err; /* need both cmp & swap */
	assert(page == get_ntohl(&list[0]));
	assert(number == get_ntohl(&list[4]));
	swap_len = get_ntohs(&list[8]);
	if (swap_len == 0)
		swap = NULL;
	else
		swap = &list[10];
	pad = (0x8 - ((LE_VAL_OFF + swap_len) & 0x7)) & 0x7;
	list += LE_VAL_OFF + swap_len + pad;
	*list_len -= LE_VAL_OFF + swap_len + pad;
	*setattr_list = list;

	*orig = NULL;
	*cas_res = 0;
	ret = osd_gen_cas(cmd->osd, pid, oid, page, number, cmp, cmp_len,
			  swap, swap_len, orig, orig_len, cmd->sense);
	if (ret != OSD_OK) {
		cmd->senselen = ret;
		goto out_err;
	}

	if (*orig_len == cmp_len && memcmp(cmp, *orig, cmp_len) == 0)
		*cas_res = 1;
	*orig_page = page;
	*orig_number = number;
	return OSD_OK;

out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
out_param_list_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_PARAMETER_LIST_LENGTH_ERROR, pid,
				 oid);
out_err:
	return ret;
}


static int exec_cas_setattr(struct command *cmd, uint64_t pid, uint64_t oid,
			    const uint8_t *list, uint32_t list_len, uint32_t cdb_cont_len)
{
	int ret;
	uint32_t page, number;
	uint16_t len;
	uint8_t pad;

	if (list_len < LE_VAL_OFF) /* need atleast one list entry */
		goto out_cdb_err;

	while (list_len > 0) {
		page = get_ntohl(&list[0]);
		number = get_ntohl(&list[4]);
		len = get_ntohs(&list[8]);
		pad = 0;

		ret = osd_set_attributes(cmd->osd, pid, oid, page, number,
					 &list[10], len, true, cdb_cont_len,
					 cmd->sense);
		if (ret != 0) {
			cmd->senselen = ret;
			goto out_err;
		}

		pad = (0x8 - ((LE_VAL_OFF + len) & 0x7)) & 0x7;
		list += LE_VAL_OFF + len + pad;
		list_len -= LE_VAL_OFF + len + pad;
	}
	return OSD_OK;

out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
out_err:
	return ret;
}


/*
 * Following the order of the indata where cmp and swap values are the first
 * entries in the list, in the retrieved attributes case also, the first
 * entry will be the original value returned by the CAS operation. But if
 * there are more attributes to be fetched, then we need to call
 * get_attributes function. Since get_attributes creates the whole list along
 * with header, we need a way to stuff original value in the front of the
 * list. To do this we tamper with the retrieved_attr_off so that
 * get_attributes creates its list at an offset. Then we stuff the original
 * value from CAS op after the list header making it the first entry.
 */
static int exec_getattr(struct command *cmd, uint64_t pid, uint64_t oid,
			uint32_t orig_page, uint32_t orig_number,
			uint8_t *orig, uint16_t orig_len, uint32_t cdb_cont_len)
{
	int ret;
	uint8_t *cp, *sp;
	uint8_t *cdb = cmd->cdb;
	uint64_t old_retr_attr_off;
	uint32_t alloc_len = get_ntohl(&cdb[60]);
	uint32_t list_len, orig_le_len;

	/* modify retrieved_attr_off, ignore 1st and then get attr */
	old_retr_attr_off = cmd->retrieved_attr_off;
	orig_le_len = roundup8(LE_VAL_OFF + orig_len);
	if (alloc_len - 8 < orig_le_len) /* need space for atleast le+hdr */
		goto out_cdb_err;
	cmd->retrieved_attr_off += orig_le_len;
	cp = &cmd->outdata[cmd->retrieved_attr_off];
	memset(cp, 0, LIST_HDR_LEN);
	set_htonl(&cdb[60], alloc_len - orig_le_len);
	ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
	cmd->retrieved_attr_off = old_retr_attr_off;
	if (ret != OSD_OK)
		goto out_err;
	if (cp[0] != RTRVD_SET_ATTR_LIST) /* getattr list was empty */
		cp[0] = RTRVD_SET_ATTR_LIST;

	/* move header to the start of the buffer */
	cp = &cmd->outdata[old_retr_attr_off];
	sp = &cmd->outdata[old_retr_attr_off + orig_le_len];
	memcpy(cp, sp, LIST_HDR_LEN);
	cp += LIST_HDR_LEN;

	/* stuff original value from CAS operation */
	if (orig != NULL && orig_len > 0) {
		ret = le_pack_attr(cp, alloc_len - LIST_HDR_LEN, orig_page,
				   orig_number, orig_len, orig);
		free(orig);
		orig = NULL;
	} else {
		ret = le_pack_attr(cp, alloc_len - LIST_HDR_LEN, orig_page,
				   orig_number, 0, NULL);
	}
	if (ret <= 0) {
		goto out_cdb_err;
	} else {
		if (orig_le_len <= alloc_len - LIST_HDR_LEN)
			assert((uint32_t)ret == orig_le_len);
		else
			assert((uint32_t)ret == alloc_len - LIST_HDR_LEN);
	}

	/* modify list len to reflect new entry */
	cp -= LIST_HDR_LEN;
	list_len = get_ntohl(&cp[4]) + orig_le_len;
	set_htonl(&cp[4], list_len);
	cmd->get_used_outlen += list_len + 8;
	assert(cmd->get_used_outlen <= alloc_len);
	return OSD_OK;

out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
out_err:
	return ret;
}


/*
 * OSD_GEN_CAS: the CAS operation is applied to the first attribute being
 * set. Even when there is only attribute, the set attribute value format
 * *CANNOT* be used, since initiator needs to pass two values: 'cmp' and
 * 'swap'. The 'cmp' and 'swap' will always be first and second values in
 * the set attribute list format.
 */
static int cdb_gen_cas(struct command *cmd, int osd_cmd, uint32_t cdb_cont_len)
{
	int ret;
	uint8_t *cdb = cmd->cdb;
	uint64_t pid = get_ntohll(&cdb[16]);
	uint64_t oid = get_ntohll(&cdb[24]);
	uint32_t list_len = 0;
	uint32_t list_off = get_ntohoffset(&cmd->cdb[72]);
	const uint8_t *list = &cmd->indata[list_off];
	const uint8_t *list_pos;
	uint8_t *orig;
	uint16_t orig_len;
	uint32_t orig_page = 0, orig_number = 0;
	uint8_t cas_res = 0;

	TICK_TRACE(cdb_gen_cas);
	if (cmd->getset_cdbfmt != GETLIST_SETLIST)
		goto out_cdb_err;

	ret = exec_gen_cas(cmd, pid, oid, &list, &orig_page, &orig_number,
			   &orig, &orig_len, &list_len, &cas_res);
	if (ret != OSD_OK)
		goto out_err;

	if (osd_cmd == OSD_GEN_CAS && list_len == 0)
		goto get_attr;
	else if (osd_cmd == OSD_COND_SETATTR &&
		 (list_len == 0 || cas_res == 0)) /* cas failed */
		goto get_attr;

	/* set remaining attributes */
	ret = exec_cas_setattr(cmd, pid, oid, list, list_len, cdb_cont_len);
	if (ret != OSD_OK)
		goto out_err;
get_attr:
	ret = exec_getattr(cmd, pid, oid, orig_page, orig_number, orig,
			   orig_len, cdb_cont_len);
	TICK_TRACE(cdb_gen_cas);
	if (ret == OSD_OK)
		return ret;

out_cdb_err:
	if (orig)
		free(orig);
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
out_err:
	if (orig)
		free(orig);
	return ret;
}

static int parse_cdb_continuation_segment(struct command *cmd,
					  uint32_t cdb_cont_len,
					  uint64_t pid, uint64_t oid)
{
	const uint8_t *cdb_cont = cmd->indata;
	uint8_t cont_format = cdb_cont[0];
	uint16_t cont_action = get_ntohs(&cdb_cont[2]);
	uint32_t used_bytes = 0;

	if (((cdb_cont_len % 8) != 0) || (cdb_cont_len < 48)) {
		/* TODO: need to check if cdb_cont_len is greater than the value
		 * in the maximum CDB continuation length attribute in the root
		 * information attributes page (7.1.3.8)
		 */
osd_warning("%s:%d:", __FILE__, __LINE__);
		goto out_cdb_err;
	}

	if (cont_action != cmd->action) {
osd_warning("%s:%d: cont_action=0x%x cmd->action=0x%x cdb_cont_len=%d cont_format=%d",
	__FILE__, __LINE__, cont_action, cmd->action, cdb_cont_len, cont_format);
		goto out_cdb_err;
	}

	/* continuation format 1 is the only format defined in OSDr204 */
	if (cont_format != 1) {
osd_warning("%s:%d:", __FILE__, __LINE__);
		goto out_cdb_err;
	}

	/* XXX need to check continuation integrity check value */

	/* start parsing the descriptors */

	/* skip over continuation header */
	cdb_cont += 40;
	while (cdb_cont < cmd->indata + cdb_cont_len) {
		struct cdb_continuation *cont = &cmd->cont;
		struct cdb_continuation_descriptor *desc;
		const struct cdb_continuation_descriptor_header *desc_hdr =
			(typeof(desc_hdr))cdb_cont;
		uint16_t type = get_ntohs(&desc_hdr->type);
		uint32_t length = get_ntohl(&desc_hdr->length);
		uint8_t pad_length = desc_hdr->pad_length & 0x7;

		uint32_t desc_len = length + pad_length;

		if (cont->num_descriptors % 8 == 0) {
			size_t newsize = (cont->num_descriptors+8)*
				sizeof(struct cdb_continuation_descriptor);
			cont->descriptors = realloc(cont->descriptors, newsize);
		}
		desc = &cont->descriptors[cont->num_descriptors++];
		desc->type = type;
		desc->length = length;

		if (desc_len%8 != 0) {
			/* TODO: need to check if descriptor length is greater
			 * than the value of the supported CDB continuation
			 * descriptor type information attributes page (7.1.3.8)
			 */
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		desc_len += sizeof(*desc_hdr);

// osd_warning("%s:%d: desc_len=%d cdb_cont_len=%d uptilnow=%zu", __FILE__, __LINE__, desc_len, cdb_cont_len, cdb_cont-cmd->indata);

		if ((cdb_cont + desc_len) > (cmd->indata + cdb_cont_len)) {
			/* XXX The spec doesnt say what to do if the
			   length of this descriptor goes past the end
			   of the continuation.  For now, we'll just
			   return an error. */
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		switch (type) {

		case NO_MORE_DESCRIPTORS: {
			/* last descriptor */
			break;
		}

		case SCATTER_GATHER_LIST: {
			if (pad_length != 0) {
			     /* osd2r04 5.4.2 - pad length must be 0 */
osd_warning("%s:%d:", __FILE__, __LINE__);
			     goto out_cdb_err;
			}

			desc->sglist.num_entries =
				length/sizeof(struct sg_list_entry);
			/* entries are at the end of the header */
			desc->sglist.entries =
				(const struct sg_list_entry *)(desc_hdr+1);
			break;
		}

		case QUERY_LIST: {
			if (pad_length != 0) {
			     /* osd2r04 5.4.3 - pad length must be 0 */
osd_warning("%s:%d:", __FILE__, __LINE__);
			     goto out_cdb_err;
			}

			desc->desc_specific_hdr = (const void *)(desc_hdr+1);
			break;
		}

		case USER_OBJECT: {
			/* not supported yet */
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		case COPY_USER_OBJECT_SOURCE: {
			desc->desc_specific_hdr = (const uint8_t *)(desc_hdr+1);
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		case EXTENSION_CAPABILITIES: {
			/* not supported yet */
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		default:
osd_warning("%s:%d:", __FILE__, __LINE__);
			goto out_cdb_err;
		}

		/* move pointer to next descriptor */
		cdb_cont += desc_len;
	}

	return OSD_OK;

 out_cdb_err:
	return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
				 OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}

/*
 * Compare, and complain if iscsi did not deliver enough bytes to
 * satisfy the WRITE.  It could deliver more for padding, so don't
 * check for exact.
 */
static int verify_enough_input_data(struct command *cmd, uint64_t cdblen)
{
	int ret = 0;

	if (cdblen > cmd->inlen) {
		osd_error("%s: supplied data %llu but cdb says %llu",
			  __func__, llu(cmd->inlen), llu(cdblen));
		ret = osd_error_bad_cdb(cmd->sense);
	}
	return ret;
}

/* in exec_service_action(), most OSD commands do not allow CDB
   continuations.  The following function is a quick check to make sure
   there are no continuations. */
static inline int
check_no_continuations(struct command *cmd, uint32_t cdb_cont_len,
		       uint64_t pid, uint64_t oid)
{
	if (cdb_cont_len != 0) {
		return sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
					OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	}
	return 0;
}

static void exec_service_action(struct command *cmd)
{
	struct osd_device *osd = cmd->osd;
	uint8_t *cdb = cmd->cdb;
	uint8_t *sense = cmd->sense;
	uint32_t cdb_cont_len = get_ntohl(&cdb[48]);
	uint64_t pid = get_ntohll(&cdb[16]);
	uint64_t oid = get_ntohll(&cdb[24]);
	int ret;
	
//	osd_debug("%s: start 0x%04x", __func__, cmd->action);

	if (cdb_cont_len != 0) {
		ret = parse_cdb_continuation_segment(cmd, cdb_cont_len,
						     pid, oid);
		if (ret != OSD_OK)
			goto out_exec;
	}
	
	switch (cmd->action) {
	case OSD_APPEND: {
		uint8_t ddt;
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);

		ret = verify_enough_input_data(cmd, len);
		if (ret)
			break;

		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		/* initial version from OSC supported scatter/gather
		   on APPEND.  However, it is not supported in
		   official osd2 spec. */
		ddt = DDT_CONTIG;

		ret = osd_append(osd, pid, oid, len, cmd->indata, cdb_cont_len, sense, ddt);
		if (ret)
			break;

		ret = std_get_set_attr(cmd, pid, oid, cdb_cont_len);
		break;
	}
	
	case OSD_CLEAR: {
	  	uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);
		uint64_t offset = get_ntohll(&cdb[40]);
	
		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		ret = osd_clear(osd, pid, oid, len, offset, cdb_cont_len, sense);
		if (ret)
			break;

		ret = std_get_set_attr(cmd, pid, oid, cdb_cont_len);
		break;
	}

	case OSD_COPY_USER_OBJECTS: {
		ret = cdb_copy_user_objects(cmd);
		break;
	}

	case OSD_CREATE: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;
	        ret = cdb_create(cmd, cdb_cont_len);
		break;
	}
	case OSD_CREATE_AND_WRITE: {
		uint8_t ddt;
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t requested_oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);
		uint64_t offset = get_ntohll(&cdb[40]);
		struct sg_list sgl, *sglist;
		const uint8_t *indata;

		ret = verify_enough_input_data(cmd, len);
		if (ret)
			break;

		/* osd2r04 6.6 - at most one sg list descriptor
				 and no other descriptors */
		if (cmd->cont.num_descriptors > 1 ||
		    (cmd->cont.num_descriptors == 1 &&
		     cmd->cont.descriptors[0].type != SCATTER_GATHER_LIST)) {
			sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
					  OSD_ASC_INVALID_FIELD_IN_CDB, pid,
					  oid);
		}

		if (cmd->cont.num_descriptors == 1) {
			sglist = &cmd->cont.descriptors[0].sglist;
			indata = cmd->indata + cdb_cont_len;
			ddt = DDT_SGL;
		} else {
			sglist = NULL;
			indata = cmd->indata;
			ddt = DDT_CONTIG;
		}

		ret = osd_create_and_write(osd, pid, requested_oid, len,
					   offset, indata, cdb_cont_len, sglist,
					   sense, ddt);
		break;
	}
	case OSD_CREATE_CLONE: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}
	case OSD_CREATE_COLLECTION: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;
	        ret = cdb_create_collection(cmd, cdb_cont_len);
		break;
	}
	case OSD_CREATE_PARTITION: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     PARTITION_OID);
		if (ret)
			break;
	        ret = cdb_create_partition(cmd, cdb_cont_len);
		break;
	}
	case OSD_CREATE_SNAPSHOT: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}
	case OSD_CREATE_USER_TRACKING_COLLECTION: {
                uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t requested_cid = get_ntohll(&cdb[24]);
		uint64_t source_cid = get_ntohll(&cdb[40]);
		
		ret = osd_create_user_tracking_collection(osd, pid, requested_cid, 
							  cdb_cont_len, source_cid, sense);
		break;
	}
	case OSD_DETACH_CLONE: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}
	case OSD_FLUSH: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);
		uint64_t offset = get_ntohll(&cdb[40]);
		int flush_scope = cdb[10] & 0x3;

		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		ret = osd_flush(osd, pid, oid, len, offset, flush_scope, cdb_cont_len, sense);
		break;
	}
	case OSD_FLUSH_COLLECTION: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		int flush_scope = cdb[10] & 0x3;

		ret = check_no_continuations(cmd, cdb_cont_len, pid, cid);
		if (ret)
			break;

		ret = osd_flush_collection(osd, pid, cid, flush_scope, cdb_cont_len, sense);
		break;
	}
	case OSD_FLUSH_OSD: {
		int flush_scope = cdb[10] & 0x3;

		ret = check_no_continuations(cmd, cdb_cont_len, 0, 0);
		if (ret)
			break;

		ret = osd_flush_osd(osd, flush_scope, cdb_cont_len, sense);
		break;
	}
	case OSD_FLUSH_PARTITION: {
		uint64_t pid = get_ntohll(&cdb[16]);
		int flush_scope = cdb[10] & 0x3;
		ret = check_no_continuations(cmd, cdb_cont_len,
					     pid, PARTITION_OID);
		if (ret)
			break;

		ret = osd_flush_partition(osd, pid, flush_scope, cdb_cont_len, sense);
		break;
	}
	case OSD_FORMAT_OSD: {
		uint64_t capacity = get_ntohll(&cdb[32]);
		ret = check_no_continuations(cmd, cdb_cont_len, 0, 0);
		if (ret)
			break;

		ret = osd_format_osd(osd, capacity, cdb_cont_len, sense);

		if (ret == OSD_OK)
			ret = std_get_set_attr(cmd, 0, 0, cdb_cont_len);

		break;
	}
	case OSD_GET_ATTRIBUTES: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);

		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
		if (ret)
			break;
		ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
		break;

	}
	case OSD_GET_MEMBER_ATTRIBUTES: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		ret = osd_get_member_attributes(osd, pid, cid, cdb_cont_len, sense);
		break;
	}
	case OSD_LIST: {
		ret = cdb_list(cmd);
		break;
	}
	case OSD_LIST_COLLECTION: {
		uint8_t list_attr = (cdb[11] & 0x40) >> 6;
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		uint32_t list_id = get_ntohl(&cdb[48]);
		uint64_t alloc_len = get_ntohll(&cdb[32]);
		uint64_t initial_oid = get_ntohll(&cdb[40]);
		ret = osd_list_collection(cmd->osd, list_attr, pid, cid,
					  alloc_len, initial_oid,
					  &cmd->get_attr, list_id,
					  cmd->outdata, &cmd->used_outlen,
					  cmd->sense);
		if (ret)
			break;

		ret = std_get_set_attr(cmd, pid, cid, cdb_cont_len);
		break;
	}
	case OSD_PERFORM_SCSI_COMMAND:
	case OSD_PERFORM_TASK_MGMT_FUNC:
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;

		ret = osd_error_unimplemented(cmd->action, sense);
		// XXX : uncomment get/set attributes block whenever this
		// gets implemented
		/*if (ret)
			break;

		ret = std_get_set_attr(cmd, pid, oid);*/
		break;

	case OSD_PUNCH: {
	  	uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);
		uint64_t offset = get_ntohll(&cdb[40]);
		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		ret = osd_punch(osd, pid, oid, len, offset, cdb_cont_len, sense);
		if (ret) 
		        break;
		
		ret = std_get_set_attr(cmd, pid, oid, cdb_cont_len);
		break;

	}

	case OSD_QUERY: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		ret = cdb_query(cmd, pid, cid, cdb_cont_len);
		if (ret)
			break;
		ret = std_get_set_attr(cmd, pid, cid, cdb_cont_len);
		break;
	}

	case OSD_READ_MAP: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;
	        ret = cdb_read_map(cmd, cdb_cont_len);	       
	        break;
	}
	case OSD_READ_MAPS_AND_COMPARE: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}

	case OSD_READ: {
	        ret = cdb_read(cmd, cdb_cont_len);
		break;
	}
	case OSD_REFRESH_SNAPSHOT_OR_CLONE: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}
	case OSD_REMOVE: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;
	        ret = cdb_remove(cmd, cdb_cont_len);
		break;
	}
	case OSD_REMOVE_COLLECTION: {
		uint8_t fcr = (cdb[11] & 0x1);
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		ret = check_no_continuations(cmd, cdb_cont_len, pid, cid);
		if (ret)
			break;

		ret = osd_remove_collection(osd, pid, cid, fcr, cdb_cont_len, sense);
		break;
	}
	case OSD_REMOVE_MEMBER_OBJECTS: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t cid = get_ntohll(&cdb[24]);
		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		ret = osd_remove_member_objects(osd, pid, cid, cdb_cont_len,  sense);
		break;
	}
	case OSD_REMOVE_PARTITION: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     PARTITION_OID);
		if (ret)
			break;

	        ret = cdb_remove_partition(cmd, cdb_cont_len);
		break;
	}
	case OSD_RESTORE_PARTITION_FROM_SNAPSHOT: {
		/* new command added in osd2r04 */
		ret = osd_error_unimplemented(cmd->action, sense);
		break;
	}
	case OSD_SET_ATTRIBUTES: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);

		ret = check_no_continuations(cmd, cdb_cont_len, pid, oid);
		if (ret)
			break;

		TICK_TRACE(cdb_set_attributes);
		ret = set_attributes(cmd, pid, oid, 1, cdb_cont_len);
		if (ret)
			break;
		ret = get_attributes(cmd, pid, oid, 1, cdb_cont_len);
		TICK_TRACE(cdb_set_attributes);
		break;
	}
	case OSD_SET_KEY: {
		int key_to_set = cdb[11] & 0x3;
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t key = get_ntohll(&cdb[24]);
		uint8_t seed[20];
		memcpy(seed, &cdb[32], 20);
		ret = osd_set_key(osd, key_to_set, pid, key, seed, sense);
		break;
	}
	case OSD_SET_MASTER_KEY: {
		int dh_step = cdb[11] & 0x3;
		uint64_t key = get_ntohll(&cdb[24]);
		uint32_t param_len = get_ntohl(&cdb[32]);
		uint32_t alloc_len = get_ntohl(&cdb[36]);
		ret = check_no_continuations(cmd, cdb_cont_len, 0, 0);
		if (ret)
			break;

		ret = osd_set_master_key(osd, dh_step, key, param_len,
					 alloc_len, cmd->outdata,
					 &cmd->used_outlen, cdb_cont_len, sense);
		break;
	}
	case OSD_SET_MEMBER_ATTRIBUTES: {
		ret = check_no_continuations(cmd, cdb_cont_len,
					     get_ntohll(&cdb[16]),
					     get_ntohll(&cdb[24]));
		if (ret)
			break;

	        ret = cdb_set_member_attributes(cmd, cdb_cont_len);
		break;
	}
	case OSD_WRITE: {
		uint64_t pid = get_ntohll(&cdb[16]);
		uint64_t oid = get_ntohll(&cdb[24]);
		uint64_t len = get_ntohll(&cdb[32]);
		uint64_t offset = get_ntohll(&cdb[40]);
		uint8_t ddt;
		struct sg_list sgl, *sglist;
		const uint8_t *indata;
		ret = verify_enough_input_data(cmd, len);
		if (ret)
			break;

		/* osd2r04 6.40 - at most one sg list descriptor
				  and no other descriptors */
		if (cmd->cont.num_descriptors > 1 ||
		    (cmd->cont.num_descriptors == 1 &&
		     cmd->cont.descriptors[0].type != SCATTER_GATHER_LIST)) {
			sense_basic_build(cmd->sense, OSD_SSK_ILLEGAL_REQUEST,
					  OSD_ASC_INVALID_FIELD_IN_CDB, pid,
					  oid);
		}

		if (cmd->cont.num_descriptors == 1) {
			sglist = &cmd->cont.descriptors[0].sglist;
			indata = cmd->indata + cdb_cont_len;
			ddt = DDT_SGL;
		} else {
			sglist = NULL;
			indata = cmd->indata;
			ddt = DDT_CONTIG;
		}

		ret = osd_write(osd, pid, oid, len, offset, indata,
				sglist, sense, ddt);
		if (ret)
			break;

		ret = std_get_set_attr(cmd, pid, oid, cdb_cont_len);
		break;
	}
	case OSD_CAS: {
	        ret = cdb_cas(cmd, cdb_cont_len);
		break;
	}
	case OSD_FA: {
	        ret = cdb_fa(cmd, cdb_cont_len);
		break;
	}
	case OSD_GEN_CAS: {
	        ret = cdb_gen_cas(cmd, OSD_GEN_CAS, cdb_cont_len);
		break;
	}
	case OSD_COND_SETATTR: {
	        ret = cdb_gen_cas(cmd, OSD_COND_SETATTR, cdb_cont_len);
		break;
	}
	default:
		ret = osd_error_unimplemented(cmd->action, sense);
	}

	if (ret)
		osd_debug("%s: done  0x%04x => %d", __func__, cmd->action, ret);

 out_exec:
	/*
	 * All the above return bytes of sense data put into sense[]
	 * in case of an error.  But sometimes they also return good
	 * data.  Just save the senselen here and continue.
	 */
	cmd->senselen = ret;
}

/*
 * What's the total number of bytes this command might produce?  Look
 * only at the CDB parameters, not the iSCSI transport lengths.
 */
static int calc_max_out_len(struct command *cmd)
{
	uint64_t end = 0;

	/* These commands return data. */
	switch (cmd->action) {
	case OSD_LIST:
	case OSD_LIST_COLLECTION:
	case OSD_READ:
	case OSD_QUERY:
	case OSD_CAS:
	case OSD_FA:
	case OSD_GEN_CAS:
		cmd->outlen = get_ntohll(&cmd->cdb[32]);
		break;
	case OSD_SET_MASTER_KEY:
		cmd->outlen = get_ntohl(&cmd->cdb[36]);
		break;
	default:
		cmd->outlen = 0;
	}

	/*
	 * All commands might have getattr requests.  Add together
	 * the result offset and the allocation length to get the
	 * end of their write.
	 */
	cmd->retrieved_attr_off = 0;
	if (cmd->getset_cdbfmt == GETPAGE_SETVALUE) {
		cmd->retrieved_attr_off = get_ntohoffset(&cmd->cdb[60]);
		if (cmd->retrieved_attr_off != -1LLU)
			end = cmd->retrieved_attr_off + get_ntohl(&cmd->cdb[56]);
	} else if (cmd->getset_cdbfmt == GETLIST_SETLIST) {
		cmd->retrieved_attr_off = get_ntohoffset(&cmd->cdb[64]);
		if (cmd->retrieved_attr_off != -1LLU)
			end = cmd->retrieved_attr_off + get_ntohl(&cmd->cdb[60]);
	} else if (cmd->getset_cdbfmt == GETFIELD_SETVALUE) {
	        return 0;
	} else {
		return -1; /* TODO: proper error code */
	}

	if (end > cmd->outlen)
		cmd->outlen = end;

	return 0 /* TODO: proper error code */;
}

/*
 * Inputs are write data from client.  Output are for the read results that
 * OSD will produce.  You can modify the data_out and data_out_len to return
 * a new buffer, or short read result.
 */
int osdemu_cmd_submit(struct osd_device *osd, uint8_t *cdb,
		      const uint8_t *data_in, uint64_t data_in_len,
		      uint8_t **data_out, uint64_t *data_out_len,
		      uint8_t *sense_out, int *senselen_out)
{
	int ret = 0;
	struct command cmd = {
		.osd = osd,
		.cdb = cdb,
		.action = (cdb[8] << 8) | cdb[9],
		.getset_cdbfmt = (cdb[11] & 0x30) >> 4,
		.get_attr = {
			.sz = 0,
			.le = 0,
		},
		.set_attr = {
			.sz = 0,
			.le = 0,
		},
		.indata = data_in,
		.inlen = data_in_len,
		.outdata = NULL,
		.used_outlen = 0,
		.get_used_outlen = 0,
		.sense = { 0 },  /* maybe no need to initialize to zero */
		.senselen = 0,
		.cont = {
			.num_descriptors = 0,
			.descriptors = NULL,
		},
	};

	/* check cdb opcode and length */
	if (cdb[0] != VARLEN_CDB || cdb[7] != OSD_CDB_SIZE - 8)
		goto out_opcode_err;

	/* Sets retrieved_attr_off, outlen. */
	ret = calc_max_out_len(&cmd);
	if (ret < 0)
		goto out_cdb_err;

	if (*data_out != NULL) {
		cmd.outdata = *data_out;  /* use buffer from iscsi */
		/* verify sane initiator, but should give underflow instead */
		if (cmd.outlen != *data_out_len)
			goto out_cdb_err;
	} else {
		if (cmd.outlen) {
			/* old way: malloc our own outbuf, iscsi will free it */
			cmd.outdata = Malloc(cmd.outlen);
			if (!cmd.outdata)
				goto out_hw_err;
		}
	}

	exec_service_action(&cmd); /* run the command. */

	/*
	 * If some retrieved attributes are going back (get_used_outlen),
	 * advance the overall used_outlen value, and possibly zero the
	 * unused bytes of the data section.
	 *
	 * If the retrieved attribute offset was -1, though, there should be
	 * no get_used_outlen.  But we'll check anyway.
	 */
	if (cmd.get_used_outlen > 0 && cmd.retrieved_attr_off != -1LLU) {
		/* Unused data-in bytes must contain zero, says the spec. */
		if (cmd.used_outlen < cmd.retrieved_attr_off)
			memset(cmd.outdata + cmd.used_outlen, 0,
			       cmd.retrieved_attr_off - cmd.used_outlen);
		cmd.used_outlen = cmd.retrieved_attr_off + cmd.get_used_outlen;
	}

	/* Return the data buffer and get attributes. */
	if (cmd.outlen > 0) {
		assert(cmd.used_outlen <= cmd.outlen);
		if (cmd.used_outlen > 0) {
			*data_out = cmd.outdata;
			*data_out_len = cmd.used_outlen;
		} else {
			goto out_free_resource;
		}
	}
	goto out;

out_opcode_err:
	cmd.senselen = sense_header_build(cmd.sense, sizeof(cmd.sense),
					  OSD_SSK_ILLEGAL_REQUEST,
					  OSD_ASC_INVALID_COMMAND_OPCODE, 0);
	goto out_free_resource;

out_cdb_err:
	cmd.senselen = sense_header_build(cmd.sense, sizeof(cmd.sense),
					  OSD_SSK_ILLEGAL_REQUEST,
					  OSD_ASC_INVALID_FIELD_IN_CDB, 0);
	goto out_free_resource;

out_hw_err:
	cmd.senselen = sense_header_build(cmd.sense, sizeof(cmd.sense),
					  OSD_SSK_HARDWARE_ERROR,
					  OSD_ASC_SYSTEM_RESOURCE_FAILURE, 0);
	goto out_free_resource;

out_free_resource:
	if (cmd.outlen && *data_out == NULL)
		free(cmd.outdata);  /* old way */
	*data_out_len = 0;

out:
	if (cmd.cont.descriptors != NULL) {
		free(cmd.cont.descriptors);
	}

	if (cmd.senselen == 0) {
		return SAM_STAT_GOOD;
	} else {
		/* valid sense data, length is ret, maybe good data too */
		*senselen_out = cmd.senselen;
		memcpy(sense_out, cmd.sense, cmd.senselen);
		return SAM_STAT_CHECK_CONDITION;
	}
}

