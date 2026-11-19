# Build and link the NBT App SDK from an application's Makefile:
#
#   NBT_SDK_DIR ?= ../sdk
#   NBT_SDK_BUILD_DIR := $(abspath build)/sdk
#   include $(NBT_SDK_DIR)/nbt-sdk.mk
#
#   CXXFLAGS += $(NBT_SDK_INC)
#   app-host: $(APP_OBJS) $(NBT_SDK_HOST_LIBS)
#   	$(CXX) $(APP_OBJS) $(NBT_SDK_HOST_LIBS) $(NBT_SDK_HOST_LDLIBS) -o $@
#
# The SDK is built by its own Makefile (targets libs-host, libs-asan,
# libs-qnx) into NBT_SDK_BUILD_DIR. QNX_ENV and QNX_SCREEN_LIBDIR are passed on.

# The targets below must not become the including Makefile's default goal.
nbt_sdk_default_goal := $(.DEFAULT_GOAL)

NBT_SDK_DIR ?= ../sdk
override NBT_SDK_ROOT := $(abspath $(NBT_SDK_DIR))
NBT_SDK_BUILD_DIR ?= $(NBT_SDK_ROOT)/build
NBT_SDK_LIB_DIR = $(NBT_SDK_BUILD_DIR)/lib

NBT_SDK_INC := -I$(NBT_SDK_ROOT)/include

# Link order: the mixer uses the SDK; gles2 stands alone.
nbt_sdk_libs = $(NBT_SDK_LIB_DIR)/libnbt-audio-mixer-$(1).a $(NBT_SDK_LIB_DIR)/libnbt-gles2-$(1).a \
	$(NBT_SDK_LIB_DIR)/libnbt-sdk-$(1).a
NBT_SDK_HOST_LIBS = $(call nbt_sdk_libs,host)
NBT_SDK_ASAN_LIBS = $(call nbt_sdk_libs,asan)
NBT_SDK_QNX_LIBS = $(call nbt_sdk_libs,qnx)

NBT_SDK_HOST_LDLIBS := -lX11 -lEGL -lGLESv2 -lm -ldl -lpthread
# kdMain must be exported for libKD (see qnx/exports.list).
NBT_SDK_QNX_LDFLAGS := -Wl,--dynamic-list,$(NBT_SDK_ROOT)/qnx/exports.list
NBT_SDK_QNX_LDLIBS := -lscreen -lEGL -lGLESv2 -lsocket -lm

NBT_SDK_MAKE = $(MAKE) --no-print-directory -C $(NBT_SDK_ROOT) BUILD_DIR=$(NBT_SDK_BUILD_DIR) \
	QNX_ENV="$(QNX_ENV)" QNX_SCREEN_LIBDIR="$(QNX_SCREEN_LIBDIR)"

.PHONY: nbt-sdk-host nbt-sdk-asan nbt-sdk-qnx
nbt-sdk-host:
	+@$(NBT_SDK_MAKE) libs-host
nbt-sdk-asan:
	+@$(NBT_SDK_MAKE) libs-asan
nbt-sdk-qnx:
	+@$(NBT_SDK_MAKE) libs-qnx

$(NBT_SDK_HOST_LIBS): nbt-sdk-host ;
$(NBT_SDK_ASAN_LIBS): nbt-sdk-asan ;
$(NBT_SDK_QNX_LIBS): nbt-sdk-qnx ;

.DEFAULT_GOAL := $(nbt_sdk_default_goal)
