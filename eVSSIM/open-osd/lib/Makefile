#
# Makefile for the libosd user-mode library
#
# Copyright (C) 2008 Panasas Inc.  All rights reserved.
#
# Authors:
#   Boaz Harrosh <bharrosh@panasas.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2
#

# include path for osd public headers
LIB_INC=../include

# path for source files common with kernel library
SRC_PATH=../drivers/scsi/osd

DEPEND=.dependencies

DEFINES=-DCONFIG_SCSI_OSD_DPRINT_SENSE=1
# osd_initiator debug prints
# DEFINES+=-DCONFIG_SCSI_OSD_DEBUG=y

INCLUDES=-I$(LIB_INC) -I$(LIB_INC)/open-osd
INCLUDES += $(FOO)
CWARN := -Wall -W -Wwrite-strings \
	-Wundef -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs

CWARN_NOT=-Wcast-qual -Wcast-align -Wbad-function-cast

CFLAGS = $M -fPIC $(CWARN) $(INCLUDES) $(DEFINES)
ifeq ($(M), -m32)
LDFLAGS += -melf_i386
endif
LDFLAGS += --no-undefined -lc

# --no-allow-shlib-undefined 
COMMON_OBJ=osd_initiator.o
LIB_OBJ=hexdump.o kalloc.o bsgdev.o osddev.o

all: $(DEPEND) libosd.so

libosd.so: $(COMMON_OBJ) $(LIB_OBJ)
	$(LD) -shared $(LDFLAGS) -o $@ $^

# to overide local headers we compile this file localy
osd_initiator.c:
	ln -sf $(SRC_PATH)/osd_initiator.c

clean: ;
	rm -vf *.o *.so osd_initiator.c $(DEPEND)

# every thing should compile if Makefile changed
%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $(@:.o=.c)

#.dependencies below
$(DEPEND): $(COMMON_OBJ:.o=.c) $(LIB_OBJ:.o=.c)
	$(CC) -MM $(CFLAGS) $^ > $@

ifneq (clean, $(MAKECMDGOALS))
-include $(DEPEND)
endif
