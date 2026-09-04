VSSIM_HOME = ../../..

TEST_OBJ :=  unit_tests_main.o test_onfi_ops.o test_event_time.o onfi.o

TEST_TARGET := unit_tests_main

CFLAGS += -DFILEBEAT_CONF='"$(abspath $(VSSIM_HOME)/../infra/ELK/filebeat.yml)"'

include $(VSSIM_HOME)/tests/host/build.mk

$(TEST_TARGET): onfi.o $(VSSIM_OBJ)
