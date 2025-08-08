
SUBDIRS := unit simulation
SUBDIRS_ARTIFACTS := unit/unit_tests_main simulation/simulation_tests_main
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

host_tests_main:  $(TEST_OBJ) $(VSSIM_OBJ)
	g++ $(CFLAGS) $(W_ALL_ERR) -o host_tests_main $(TEST_OBJ) $(VSSIM_OBJ) $(COMMON_FLAGS)

%.o: %.cc
	g++ $(W_ALL_ERR) $(CFLAGS) -std=c++11 -c $<

%.o: %.c
	gcc $(W_ALL_ERR) $(CFLAGS) -c $<

.PHONY: clean distclean mklink

clean:
	rm -rf host_tests_main $(TEST_OBJ) $(VSSIM_OBJ) data/*

distclean: clean
	rm -rf   ssd_io_manager.h ssd_io_manager.c ssd_log_manager.h ssd_log_manager.c ssd_util.h \
		common.h ftl.h ftl.c ftl_sect_strategy.h ftl_sect_strategy.c ftl_obj_strategy.h \
		ftl_obj_strategy.c ftl_type.h ftl_gc_manager.h ftl_gc_manager.c ftl_inverse_mapping_manager.h \
		ftl_inverse_mapping_manager.c ftl_mapping_manager.h ftl_mapping_manager.c ftl_perf_manager.h \
        ftl_perf_manager.c vssim_config_manager.h vssim_config_manager.c uthash.h \
        logging_parser.h logging_parser.c logging_backend.h logging_backend.c \
        logging_rt_analyzer.h logging_rt_analyzer.c logging_offline_analyzer.h logging_offline_analyzer.c \
		monitor_test.h logging_manager.h logging_manager.c \
        logging_server.h logging_server.c www logging_statistics.c logging_statistics.h

mklink:
	ln -sf $(VSSIM_HOME)/SSD_MODULE/ssd_io_manager.h
	ln -sf $(VSSIM_HOME)/SSD_MODULE/ssd_io_manager.c
	ln -sf $(VSSIM_HOME)/SSD_MODULE/ssd_log_manager.h
	ln -sf $(VSSIM_HOME)/SSD_MODULE/ssd_log_manager.c
	ln -sf $(VSSIM_HOME)/SSD_MODULE/ssd_util.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_rt_analyzer.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_rt_analyzer.c
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_offline_analyzer.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_offline_analyzer.c
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_parser.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_parser.c
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_manager.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_manager.c
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_backend.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_backend.c
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_statistics.h
	ln -sf $(VSSIM_HOME)/LOG_MGR/logging_statistics.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/COMMON/common.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_type.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_gc_manager.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_gc_manager.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.c
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PAGE_MAP/TOOLS/uthash.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PERF_MODULE/ftl_perf_manager.h
	ln -sf $(VSSIM_HOME)/FTL_SOURCE/PERF_MODULE/ftl_perf_manager.c
	ln -sf $(VSSIM_HOME)/CONFIG/vssim_config_manager.h
	ln -sf $(VSSIM_HOME)/CONFIG/vssim_config_manager.c
	ln -sf $(VSSIM_HOME)/MONITOR/SERVER/logging_server.h
	ln -sf $(VSSIM_HOME)/MONITOR/SERVER/logging_server.c
	ln -sf $(VSSIM_HOME)/MONITOR/SERVER/www
	touch ssd.conf
