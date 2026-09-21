VSSIM_HOME = ../../..

TEST_OBJ := onfi_multithreaded_main.o test_onfi.o

TEST_TARGET := onfi_multithreaded_main

include $(VSSIM_HOME)/tests/host/build.mk

.PHONY: mklink_data

$(TEST_TARGET): $(VSSIM_OBJ) mklink_data

mklink_data:
	ln -sf $(VSSIM_HOME)/tests/host/data
