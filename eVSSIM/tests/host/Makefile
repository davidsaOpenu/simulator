
SUBDIRS := unit simulation functional onfi_multithreaded
SUBDIRS_ARTIFACTS := unit/unit_tests_main simulation/simulation_tests_main functional/functional_tests_main onfi_multithreaded/onfi_multithreaded_main
LOCAL_ARTIFACTS := $(notdir $(SUBDIRS_ARTIFACTS))

.PHONY: all $(SUBDIRS) clean distclean mklink clean_artifacts
all: $(SUBDIRS)
	cp $(SUBDIRS_ARTIFACTS) .

$(SUBDIRS):
	$(MAKE) -C $@

clean distclean mklink:
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@; done

clean distclean: clean_artifacts

clean_artifacts:
	rm -f $(LOCAL_ARTIFACTS)
