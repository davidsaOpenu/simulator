/*
 * Copied from linux header to avoid needing kernel includes.
 */
#ifndef BSG_H
#define BSG_H

#define BSG_PROTOCOL_SCSI		0

#define BSG_SUB_PROTOCOL_SCSI_CMD	0
#define BSG_SUB_PROTOCOL_SCSI_TMF	1
#define BSG_SUB_PROTOCOL_SCSI_TRANSPORT	2

typedef  int32_t __s32;
typedef uint32_t __u32;
typedef uint64_t __u64;

struct sg_io_v4_v2622 {
	__s32 guard;		/* [i] 'Q' to differentiate from v3 */
	__u32 protocol;		/* [i] 0 -> SCSI , .... */
	__u32 subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	__u32 request_len;	/* [i] in bytes */
	__u64 request;		/* [i], [*i] {SCSI: cdb} */
	__u32 request_attr;	/* [i] {SCSI: task attribute} */
	__u32 request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	__u32 request_priority;	/* [i] {SCSI: task priority} */
	__u32 max_response_len;	/* [i] in bytes */
	__u64 response;		/* [i], [*o] {SCSI: (auto)sense data} */

	/* "din_" for data in (from device); "dout_" for data out (to device) */
	__u32 dout_iovec_count; /* [i] =0 -> "flat" data transfer */
	__u32 din_iovec_count;  /* [i] */
	__u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
	__u32 din_xfer_len;	/* [i] bytes to be transferred from device */
	__u64 dout_xferp;	/* [i], [*i] */
	__u64 din_xferp;	/* [i], [*o] */

	__u32 timeout;		/* [i] units: millisecond */
	__u32 flags;		/* [i] bit mask */
	__u64 usr_ptr;		/* [i->o] unused internally */
	__u32 spare_in;		/* [i] */

	__u32 driver_status;	/* [o] 0 -> ok */
	__u32 transport_status;	/* [o] 0 -> ok */
	__u32 device_status;	/* [o] {SCSI: command completion status} */
	__u32 retry_delay;	/* [o] {SCSI: status auxiliary information} */
	__u32 info;		/* [o] additional information */
	__u32 duration;		/* [o] time to complete, in milliseconds */
	__u32 response_len;	/* [o] bytes of response actually written */
	__s32 din_resid;	/* [o] actual_din_xfer_len - din_xfer_len */
	__u32 generated_tag;	/* [o] {SCSI: task tag that transport chose} */
	__u32 spare_out;	/* [o] */

	__u32 padding;
};

struct sg_io_v4_v2624 {
	__s32 guard;		/* [i] 'Q' to differentiate from v3 */
	__u32 protocol;		/* [i] 0 -> SCSI , .... */
	__u32 subprotocol;	/* [i] 0 -> SCSI command, 1 -> SCSI task
				   management function, .... */

	__u32 request_len;	/* [i] in bytes */
	__u64 request;		/* [i], [*i] {SCSI: cdb} */
	__u64 request_tag;	/* [i] {SCSI: task tag (only if flagged)} */
	__u32 request_attr;	/* [i] {SCSI: task attribute} */
	__u32 request_priority;	/* [i] {SCSI: task priority} */
	__u32 request_extra;	/* [i] {spare, for padding} */
	__u32 max_response_len;	/* [i] in bytes */
	__u64 response;		/* [i], [*o] {SCSI: (auto)sense data} */

        /* "dout_": data out (to device); "din_": data in (from device) */
	__u32 dout_iovec_count;	/* [i] 0 -> "flat" dout transfer else
				   dout_xfer points to array of iovec */
	__u32 dout_xfer_len;	/* [i] bytes to be transferred to device */
	__u32 din_iovec_count;	/* [i] 0 -> "flat" din transfer */
	__u32 din_xfer_len;	/* [i] bytes to be transferred from device */
	__u64 dout_xferp;	/* [i], [*i] */
	__u64 din_xferp;	/* [i], [*o] */

	__u32 timeout;		/* [i] units: millisecond */
	__u32 flags;		/* [i] bit mask */
	__u64 usr_ptr;		/* [i->o] unused internally */
	__u32 spare_in;		/* [i] */

	__u32 driver_status;	/* [o] 0 -> ok */
	__u32 transport_status;	/* [o] 0 -> ok */
	__u32 device_status;	/* [o] {SCSI: command completion status} */
	__u32 retry_delay;	/* [o] {SCSI: status auxiliary information} */
	__u32 info;		/* [o] additional information */
	__u32 duration;		/* [o] time to complete, in milliseconds */
	__u32 response_len;	/* [o] bytes of response actually written */
	__s32 din_resid;	/* [o] din_xfer_len - actual_din_xfer_len */
	__s32 dout_resid;	/* [o] dout_xfer_len - actual_dout_xfer_len */
	__u64 generated_tag;	/* [o] {SCSI: transport generated task tag} */
	__u32 spare_out;	/* [o] */

	__u32 padding;
};

/*
 * Vector of address/length pairs, used when dout_iovec_count (or din_)
 * is non-zero.  In that case, dout_xferp is a list of struct sg_v4_iovec
 * and dout_iovec_count is the number of entries in that list.  dout_xfer_len
 * is the total length of the list.
 */
struct sg_v4_iovec {
	__u64 iov_base;
	__u32 iov_len;
	__u32 __pad1;
};

#endif
