default: release_debug

.PHONY: default release debug all clean

include flags.mk
include cpp-utils.mk

CXX_FLAGS += -pthread

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

include finalize.mk
