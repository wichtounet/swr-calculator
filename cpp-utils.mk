AUTO_CXX_SRC_FILES=
AUTO_SIMPLE_C_SRC_FILES=

# Configure the colors (optional)
ifneq (1,$(MU_NOCOLOR))
NO_COLOR=\x1b[0m
MODE_COLOR=\x1b[31;01m
FILE_COLOR=\x1b[35;01m
else
NO_COLOR=
MODE_COLOR=
FILE_COLOR=
endif

Q ?= @

# Create rules to compile each .cpp file of a folder

define folder_compile

# Object files

debug/$(1)/%.cpp.o: $(1)/%.cpp
	@mkdir -p debug/$(1)/
	@echo -e "$(MODE_COLOR)[debug]$(NO_COLOR) Compile $(FILE_COLOR)$(1)/$$*.cpp$(NO_COLOR)"
	$(Q)$(CXX) $(DEBUG_FLAGS) $(CXX_FLAGS) $(2) -MD -MF debug/$(1)/$$*.cpp.d -o debug/$(1)/$$*.cpp.o -c $(1)/$$*.cpp
	@ sed -i -e 's@^\(.*\)\.o:@\1.d \1.o:@' debug/$(1)/$$*.cpp.d

release/$(1)/%.cpp.o: $(1)/%.cpp
	@mkdir -p release/$(1)/
	@echo -e "$(MODE_COLOR)[release]$(NO_COLOR) Compile $(FILE_COLOR)$(1)/$$*.cpp$(NO_COLOR)"
	$(Q)$(CXX) $(RELEASE_FLAGS) $(CXX_FLAGS) $(2) -MD -MF release/$(1)/$$*.cpp.d -o release/$(1)/$$*.cpp.o -c $(1)/$$*.cpp
	@ sed -i -e 's@^\(.*\)\.o:@\1.d \1.o:@' release/$(1)/$$*.cpp.d

release_debug/$(1)/%.cpp.o: $(1)/%.cpp
	@mkdir -p release_debug/$(1)/
	@echo -e "$(MODE_COLOR)[release_debug]$(NO_COLOR) Compile $(FILE_COLOR)$(1)/$$*.cpp$(NO_COLOR)"
	$(Q)$(CXX) $(RELEASE_DEBUG_FLAGS) $(CXX_FLAGS) $(2) -MD -MF release_debug/$(1)/$$*.cpp.d -o release_debug/$(1)/$$*.cpp.o -c $(1)/$$*.cpp
	@ sed -i -e 's@^\(.*\)\.o:@\1.d \1.o:@' release_debug/$(1)/$$*.cpp.d

endef

# Create rules to compile cpp files in the given folder and gather source files from it

define auto_folder_compile

$(eval $(call folder_compile,$(1),$(2)))

AUTO_SRC_FILES += $(wildcard $(1)/*.cpp)
AUTO_CXX_SRC_FILES += $(wildcard $(1)/*.cpp)

endef

# Create rules to link an executable with a set of files

define add_executable

debug/bin/$(1): $(addsuffix .o,$(addprefix debug/,$(2)))
	@mkdir -p debug/bin/
	@echo -e "$(MODE_COLOR)[debug]$(NO_COLOR) Link $(FILE_COLOR)$$@$(NO_COLOR)"
	$(Q)$(CXX) $(DEBUG_FLAGS) -o $$@ $$+ $(LD_FLAGS) $(3)

release/bin/$(1): $(addsuffix .o,$(addprefix release/,$(2)))
	@mkdir -p release/bin/
	@echo -e "$(MODE_COLOR)[release]$(NO_COLOR) Link $(FILE_COLOR)$$@$(NO_COLOR)"
	$(Q)$(CXX) $(RELEASE_FLAGS) -o $$@ $$+ $(LD_FLAGS) $(3)

release_debug/bin/$(1): $(addsuffix .o,$(addprefix release_debug/,$(2)))
	@mkdir -p release_debug/bin/
	@echo -e "$(MODE_COLOR)[release_debug]$(NO_COLOR) Link $(FILE_COLOR)$$@$(NO_COLOR)"
	$(Q)$(CXX) $(RELEASE_DEBUG_FLAGS) -o $$@ $$+ $(LD_FLAGS) $(3)

endef

# Create an executable with all the files gather with auto_folder_compile

define auto_add_executable

$(eval $(call add_executable,$(1),$(AUTO_SRC_FILES)))
$(eval $(call add_executable_set,$(1),$(1)))

endef

# Create executable sets targets

define add_executable_set

release_$(1): $(addprefix release/bin/,$(2))
release_debug_$(1): $(addprefix release_debug/bin/,$(2))
debug_$(1): $(addprefix debug/bin/,$(2))
$(1): release_$(1)

endef

# Include D files

define auto_finalize

AUTO_DEBUG_D_FILES += $(AUTO_CXX_SRC_FILES:%.cpp=debug/%.cpp.d)
AUTO_RELEASE_D_FILES += $(AUTO_CXX_SRC_FILES:%.cpp=release/%.cpp.d)
AUTO_RELEASE_DEBUG_D_FILES += $(AUTO_CXX_SRC_FILES:%.cpp=release_debug/%.cpp.d)

endef
