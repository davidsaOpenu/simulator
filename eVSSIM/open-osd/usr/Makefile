#
# Makefile for the open-osd user-mode applications
#
# Copyright (C) 2008 Panasas Inc.  All rights reserved.
#
# Authors:
#   Boaz Harrosh <bharrosh@panasas.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2
#
#

# include path for osd public headers
LIB_INC=../include
# path for source files common with kernel library
SRC_PATH=../drivers/scsi/osd
EXOFS_PATH=../fs/exofs

DEPEND=.dependencies

DEFINES=-DCONFIG_SCSI_OSD_DPRINT_SENSE=1
# osd_initiator debug prints for osd_ktests.c
# DEFINES+=-DCONFIG_SCSI_OSD_DEBUG=y

INCLUDES= -I$(LIB_INC) -I$(LIB_INC)/open-osd/ -I../lib -I./ -I$(SRC_PATH)
INCLUDES+= -I$(EXOFS_PATH)

CWARN := -Wall -W -Wwrite-strings \
	-Wundef -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs

CWARN_NOT=-Wcast-qual -Wcast-align -Wbad-function-cast

CFLAGS = $M -fPIC $(CWARN) $(INCLUDES) $(DEFINES)
ifeq ($(M), -m32)
LDFLAGS += -melf_i386
endif

OSD_LIBS=-L../lib -losd

ALL = osd_test mkfs.exofs osd
all: $(DEPEND) $(ALL)

clean: $(ALL:=_clean)
	rm -vf $(DEPEND) $(ALL)  *.o

# ============== osd_test ======================================================
OT_COMMON_OBJ=osd_ktests.o
OT_OBJ=osd_test.o

# add for osd_ktests user-mode debugging
# CFLAGS += -DCONFIG_SCSI_OSD_DEBUG

osd_ktests.c:
	ln -sf $(SRC_PATH)/osd_ktests.c

osd_test:  $(OT_OBJ) $(OT_COMMON_OBJ)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(OSD_LIBS)

osd_test_clean:
	rm -vf $(OT_COMMON_OBJ:.o=.c)

$(DEPEND): $(OT_COMMON_OBJ:.o=.c) $(OT_OBJ:.o=.c)

# =============== mkexofs ======================================================
mkfs_OBJ=mkexofs_main.o mkexofs.o

# add for mkexofs debugging
# CFLAGS += -DCONFIG_MKEXOFS_DEBUG

mkfs.exofs:  $(mkfs_OBJ)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(OSD_LIBS)

mkfs.exofs_clean:

$(DEPEND): $(mkfs_OBJ:.o=.c)

# =============== osd ==========================================================
osd_OBJ=osd.o

osd:  $(osd_OBJ)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(OSD_LIBS)

osd_clean:

$(DEPEND): $(osd_OBJ:.o=.c)

# =============== common rules =================================================
# every thing should compile if Makefile changed
%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $(@:.o=.c)

#.============== dependencies genaration =======================================
$(DEPEND):
	$(CC) -MM $(CFLAGS) $^ > $@

ifneq (clean, $(MAKECMDGOALS))
-include $(DEPEND)
endif
