#ifndef __LIBOSD_H__
#define __LIBOSD_H__

#include <scsi/osd_initiator.h>
#include <scsi/osd_sec.h>
#include <scsi/osd_attributes.h>
#include <scsi/osd_sense.h>

/* For the sake of stable ABI osd_dev is dynamical allocated by library */

int osd_open(const char *osd_path, struct osd_dev **pod);
void osd_close(struct osd_dev *od);

/* Low level utils. Should not be needed */
int osdpath_to_bsgpath(const char *osd_path, char *bsg_path);

#endif /* ndef __LIBOSD_H__ */
