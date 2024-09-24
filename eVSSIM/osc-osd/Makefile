# Top level Makefile to build everything

-include Makedefs

MK_PATH ?= ./
util := $(MK_PATH)/osd-util
tgt := $(MK_PATH)/tgt
target := $(MK_PATH)/osd-target
CHECKPATCH ?= ~/dev/git/pub/scsi-misc/scripts/checkpatch.pl
checkpatch_2_kdev = checkpatch-2-kdev

.PHONY: all clean
# comment out any below to remove from compelation
all: target
all: stgt

clean: stgt_clean target_clean util_clean

.PHONY: util util_clean
util:
	$(MAKE) -C $(util)

util_clean:
	$(MAKE) -C $(util) clean

.PHONY: target target_test target_clean
target: util
	$(MAKE) -C $(target)

target_clean:
	$(MAKE) -C $(target) clean

OTGTD = otgtd
ifeq ($(PANASAS_OSD),1)
	OTGTD = pan_tgtd
	ifeq ($(PANASAS_OSDSIM),1)
		OTGTD = pansim_tgtd
	endif
endif

.PHONY: stgt stgt_checkpatch stgt_tgt_only stgt_clean
stgt: target
	$(MAKE) TGT_EXTRA_LIBS=-lsqlite3 OSDEMU=1 ISCSI=1 TGTD=$(OTGTD) -C $(tgt)/usr

stgt_checkpatch:
	cd $(tgt);git show | $(CHECKPATCH) - |  $(checkpatch_2_kdev) $(PWD)/$(tgt)

stgt_tgt_only:
	$(MAKE) ISCSI=1 IBMVIO=1 ISCSI_RDMA=1 FCP=1 FCOE=1 -C $(tgt)/usr

stgt_clean:
	$(MAKE) -C $(tgt)/usr clean

# distribution tarball, just makefile lumps
.PHONY: dist osd_checkpatch
MVD := osd-toplevel-$(shell date +%Y%m%d)
dist:
	tar cf - Makefile Makedefs | bzip2 -9c > $(MVD).tar.bz2

# ctags generation for all projects
ctags:
	ctags -R $(util) $(target) $(tgt) /usr/include

osd_checkpatch:
	git show | $(CHECKPATCH) - | $(checkpatch_2_kdev) $(PWD)

# ==== Remote compelation ================================================
# make R=remote_machine R_PATH=source_path_on_remote remote
#
# If you need this to work on panasas machines and inside kdevelop
# /.automount/nfs.paneast.panasas.com/root/home/bharrosh/osc-osd/
# should link on local machine to same files (.i.e $R_PATH
# should point localy and remotely to the same files and $R_PATH better
# be the real remote path and not through some soft link. Sigh)
#
R ?= "compute-6-21"
R_PATH ?= "/.automount/nfs.paneast.panasas.com/root/home/bharrosh/osc-osd/"

.PHONY: remote remote_clean
remote:
	git diff --ignore-submodules HEAD > diff_upload.patch
	git push -f ssh://$(R)/$(R_PATH) ;
	cd tgt; \
		git diff HEAD > ../tgt_diff_upload.patch; \
		git push -f ssh://$(R)/$(R_PATH)/tgt ; \
	cd ..;
	scp diff_upload.patch tgt_diff_upload.patch $(R):$(R_PATH)/
	ssh $(R) "cd $(R_PATH); gmake z__at_remote;"

z__at_remote:
	git reset --hard HEAD; \
	apply_no_error(){ git apply diff_upload.patch; return 0; }; \
	apply_no_error; \
	cd tgt; \
		git reset --hard HEAD; \
		tgt_apply_no_error(){ git apply ../tgt_diff_upload.patch; return 0; }; \
		tgt_apply_no_error; \
	cd ..;
	gmake MK_PATH=$(R_PATH) -C $(R_PATH)

remote_clean:
	ssh $(R) "cd $(R_PATH);gmake -C $(R_PATH) clean"
