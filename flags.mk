WARNING_FLAGS += -Wextra -Wall -Wuninitialized -Wno-long-long -Winit-self

CXX_FLAGS += -Iinclude -Icpp-httplib $(WARNING_FLAGS)
LD_FLAGS += $(CXX_FLAGS)

# Custom optimization flags
DEBUG_FLAGS += -g
RELEASE_DEBUG_FLAGS += -g -O2
RELEASE_FLAGS += -g -DNDEBUG -O3 -fomit-frame-pointer

RELEASE_FLAGS += -march=native
RELEASE_DEBUG_FLAGS += -march=native
