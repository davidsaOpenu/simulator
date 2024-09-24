sbindir ?= $(PREFIX)/sbin

ifneq ($(shell test -e /usr/include/linux/signalfd.h && echo 1),)
CFLAGS += -DUSE_SIGNALFD
endif

UNAME ?= $(shell uname)
ifneq (FreeBSD,$(UNAME))
ifneq (Darwin,$(UNAME))
AIO=1
EPOLL=1
endif
endif

ifneq ($(IBMVIO),)
CFLAGS += -DIBMVIO -DUSE_KERNEL
TGTD_OBJS += $(addprefix ibmvio/, ibmvio.o)
TGTD_OBJS += bs_mmap.o tgtif.o
endif

ifneq ($(ISCSI),)
CFLAGS += -DISCSI
TGTD_OBJS += $(addprefix iscsi/, conn.o param.o session.o \
		iscsid.o target.o chap.o sha1.o md5.o transport.o iscsi_tcp.o \
		isns.o)
TGTD_OBJS += bs_rdwr.o
ifneq ($(AIO),)
TGTD_OBJS += bs_aio.o
endif

ifneq ($(ISCSI_RDMA),)
CFLAGS += -DISCSI_RDMA
TGTD_OBJS += iscsi/iscsi_rdma.o
LIBS += -libverbs -lrdmacm
endif
endif

ifneq ($(FCP),)
CFLAGS += -DFCP -DUSE_KERNEL
TGTD_OBJS += $(addprefix fc/, fc.o)
TGTD_OBJS += bs_mmap.o tgtif.o
endif

ifneq ($(FCOE),)
TGTD_OBJS += $(addprefix fcoe/,\
	openfc_target.o \
	openfc_scst.o \
	fcs_state.o \
	fc_exch.o \
	fc_disc_targ.o \
	fc_frame.o \
	fc_local_port.o \
	fc_remote_port.o \
	fc_sess.o \
	fc_virt_fab.o \
	fc_port.o \
	fcoe_if.o fcoe_dev.o \
	sa_event.o sa_timer.o sa_hash_kern.o sa_state.o\
	crc32_le.o crc32_le_tab.o)
TGTD_OBJS += bs_rdwr.o
endif

#
# OSD software implementation is in an external library.  The backingstore
# implementation for tgt to use it is built here.
#
ifneq ($(OSDEMU),)
CFLAGS += -DOSDEMU
# use threaded implementation?
#CFLAGS += -DOSDTHREAD
TGTD_OBJS += bs_osdemu.o
OSD_ROOT := ../..
OSD_TARGET_DIR := $(OSD_ROOT)/osd-target
OSD_UTIL_DIR := $(OSD_ROOT)/osd-util
DEPLIBS := $(OSD_TARGET_DIR)/libosdtgt.a $(OSD_UTIL_DIR)/libosdutil.a
LIBS += $(DEPLIBS) -lm

ifneq ($(TGT_EXTRA_LIBS),)
# In OSDEMU builds we link with libsqlite3 passed from external Makefile
LIBS += $(TGT_EXTRA_LIBS)
endif # TGT_EXTRA_LIBS

all: $(PROGRAMS)
bs_osdemu.o: bs_osdemu.c $(DEPLIBS) Makefile
	$(CC) -c $(CFLAGS) -I$(OSD_TARGET_DIR) $< -o $@
endif # OSDEMU

INCLUDES += -I.

CFLAGS += -D_GNU_SOURCE
CFLAGS += $(INCLUDES)
CFLAGS += -g -O2 -Wall -Wstrict-prototypes -fno-strict-aliasing -fPIC
CFLAGS += -DTGT_VERSION=\"$(VERSION)$(EXTRAVERSION)\"

LIBS += -lpthread

TGTD ?= tgtd

PROGRAMS += $(TGTD) tgtadm tgtimg
TGTD_OBJS += tgtd.o mgmt.o target.o scsi.o log.o driver.o util.o work.o \
		parser.o spc.o sbc.o mmc.o nolun.o osd.o scc.o smc.o \
		ssc.o bs_ssc.o libssc.o \
		bs_null.o bs.o libcrc32c.o

ifneq ($(EPOLL),)
	TGTD_OBJS += bs_sg.o
	TGTD_OBJS += linux/os.o
else
	CFLAGS += -Ibsd/
	TGTD_OBJS += bsd/epoll.o
	TGTD_OBJS += bsd/os.o
	# sqlite3 libs are found elsewhere in BSD
	LIBS := -L/usr/local/lib $(LIBS)
endif

TGTD_DEP = $(TGTD_OBJS:.o=.d)

.PHONY:all
all: $(PROGRAMS)

$(TGTD): $(TGTD_OBJS)
	$(CC) $^ -o $@ $(LIBS)

-include $(TGTD_DEP)

tgtadm: tgtadm.o
	$(CC) $^ -o $@

-include tgtadm.d

tgtimg: tgtimg.o libssc.o libcrc32c.o
	$(CC) $^ -o $@

-include tgtimg.d libssc.d

%.o: %.c
	$(CC) -c $(CFLAGS) $*.c -o $*.o
	@$(CC) -MM $(CFLAGS) -MF $*.d -MT $*.o $*.c

.PHONY: install
install: $(PROGRAMS)
	install -d -m 755 $(DESTDIR)$(sbindir)
	install -m 755 $(PROGRAMS) $(DESTDIR)$(sbindir)

.PHONY: clean
clean:
	rm -f *.[od] $(PROGRAMS) iscsi/*.[od] ibmvio/*.[od] fc/*.[od] fcoe/*.[od]
