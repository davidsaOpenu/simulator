OBJ = system_tests.o

VSSIM_OBJ = build/vssim_config_manager.o \
			build/ftl.o build/ftl_sect_strategy.o build/ftl_obj_strategy.o \
			build/ftl_mapping_manager.o build/ftl_inverse_mapping_manager.o \
			build/ftl_gc_manager.o build/ftl_perf_manager.o \
			build/ssd.o build/ssd_log_manager.o build/ssd_io_manager.o

CFLAGS = -I/opt/gtest/include -g

all: unit_tests

unit_tests: $(OBJ) $(VSSIM_OBJ)
	g++ -Wall -o unit_tests $(OBJ) $(VSSIM_OBJ) -lpthread -lgtest -lgtest_main

%.o: %.cc
	g++ -Wall $(CFLAGS) -c $<

%.o: %.c
	gcc -Wall $(CFLAGS) -c $<

.PHONY: clean distclean mklink

clean:
	-rm unit_tests $(OBJ) $(VSSIM_OBJ) data/*

distclean: clean
	rm src/*.h src/*.c src/ssd_monitor

mklink:
	ln -s ../../SSD_MODULE/ssd_io_manager.h src/
	ln -s ../../SSD_MODULE/ssd_io_manager.c src/
	ln -s ../../SSD_MODULE/ssd_log_manager.h src/
	ln -s ../../SSD_MODULE/ssd_log_manager.c src/
	ln -s ../../SSD_MODULE/ssd_util.h src/
	ln -s ../../FTL_SOURCE/COMMON/common.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_gc_manager.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_gc_manager.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.c src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_type.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.h src/
	ln -s ../../FTL_SOURCE/PAGE_MAP/TOOLS/uthash.h src/
	ln -s ../../FTL_SOURCE/PERF_MODULE/ftl_perf_manager.h src/
	ln -s ../../FTL_SOURCE/PERF_MODULE/ftl_perf_manager.c src/
	ln -s ../../CONFIG/vssim_config_manager.h src/
	ln -s ../../CONFIG/vssim_config_manager.c src/
	ln -s ../../MONITOR/SSD_MONITOR_PM/ssd_monitor_p src/ssd_monitor
