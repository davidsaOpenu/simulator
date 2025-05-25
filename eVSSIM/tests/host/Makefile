VSSIM_HOME = ../..
VSSIM_OBJ = vssim_config_manager.o \
			ftl.o ftl_mapping_manager.o ftl_inverse_mapping_manager.o \
			ftl_gc_manager.o ftl_perf_manager.o \
			ssd_log_manager.o ssd_io_manager.o \
			ftl_sect_strategy.o ftl_obj_strategy.o \
			logging_backend.o logging_parser.o logging_rt_analyzer.o logging_offline_analyzer.o \
			logging_manager.o logging_server.o logging_statistics.o

TEST_OBJ =  object_tests.o sector_tests.o log_mgr_tests.o ssd_io_emulator_tests.o \
			rt_analyzer_subscriber.o log_manager_subscriber.o host_tests_main.o   \
			offline_logger_tests.o ssd_write_read_test.o

CFLAGS = -I/opt/gtest/include -I$(VSSIM_HOME) -I$(VSSIM_HOME)/osc-osd -I$(VSSIM_HOME)/osc-osd/osd-target -I$(VSSIM_HOME)/QEMU \
	-g -DGTEST -DCOMPLIANCE_TESTS -L/opt/gtest/lib -L$(VSSIM_HOME)/osc-osd/osd-util -L$(VSSIM_HOME)/osc-osd/osd-target \
	-I/opt/libwebsockets/include -L/opt/libwebsockets/lib -L/usr/lib/x86_64-linux-gnu -Wl,-R/opt/libwebsockets/lib -g -m64 -pipe -O2 -Wall -W \
	-D_REENTRANT -fPIE -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_NETWORK_LIB -DQT_GUI_LIB -DQT_CORE_LIB \
	-I/usr/include/json-c/ -D__STDC_VERSION__=0

COMMON_FLAGS = -lpthread -lgtest -lgtest_main -losdutil -losdtgt -lsqlite3 -lwebsockets -ljson-c

W_ALL_ERR = -Wall -Werror

all: host_tests_main

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