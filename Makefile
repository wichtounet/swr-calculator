default: release_debug

.PHONY: default release debug all clean

include make-utils/flags.mk
include make-utils/cpp-utils.mk

# Use C++26
$(eval $(call use_cpp26))

CXX_FLAGS += -pthread -isystem cpp-httplib

$(eval $(call auto_folder_compile,src))
$(eval $(call auto_add_executable,swr_calculator))

release_debug: release_debug_swr_calculator
release: release_swr_calculator
debug: debug_swr_calculator

all: release release_debug debug

clean:
	rm -rf release/
	rm -rf release_debug/
	rm -rf debug/

include make-utils/cpp-utils-finalize.mk
