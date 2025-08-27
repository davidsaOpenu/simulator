VSSIM_HOME = ../../..

TEST_OBJ :=  object_tests.o sector_tests.o log_mgr_tests.o ssd_io_emulator_tests.o \
			rt_analyzer_subscriber.o log_manager_subscriber.o simulation_tests_main.o   \
			offline_logger_tests.o ssd_write_read_test.o ssd_program_compatible_test.o onfi_ops_test.o

TEST_TARGET := simulation_tests_main

include $(VSSIM_HOME)/tests/host/build.mk

.PHONY: mklink_data

$(TEST_TARGET):  $(VSSIM_OBJ) mklink_data

mklink_data:
	ln -sf $(VSSIM_HOME)/tests/host/data
