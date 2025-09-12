VSSIM_HOME = ../../..

TEST_OBJ :=  unit_tests_main.o test_ssd_file_ops.o test_onfi_ops.o

TEST_TARGET := unit_tests_main

include $(VSSIM_HOME)/tests/host/build.mk

$(TEST_TARGET): ssd_file_operations.o onfi.o
