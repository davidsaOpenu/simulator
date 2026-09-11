VSSIM_HOME = ../../..

TEST_OBJ :=  unit_tests_main.o test_onfi_ops.o onfi.o

TEST_TARGET := unit_tests_main

include $(VSSIM_HOME)/tests/host/build.mk

$(TEST_TARGET): onfi.o $(VSSIM_OBJ)
