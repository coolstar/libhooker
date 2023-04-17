TARGET = iphone:latest:11.0
ARCHS=arm64 arm64e
include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = libhooker libblackjack libsubstitute libsubstrate

libhooker_FILES = libhooker/dyld/dyldSyms.c libhooker/dyld/dyld4Find.c libhooker/misc/misc.c libhooker/as-aarch64/as-aarch64.c libhooker/disas-aarch64/disas-aarch64.c libhooker/mem/writeMem-darwin.c libhooker/mem/shadowMem.c libhooker/mem/shadowMem.cpp libhooker/func/funcHook.c libhooker/mach/machWrapper.c
libhooker_CFLAGS = -Ilibhooker/include/apple -Ilibhooker/include/ -DTHEOS -fvisibility=hidden
libhooker_CCFLAGS = -Ilibhooker/include/apple -Ilibhooker/include/ -DTHEOS -fvisibility=hidden -std=c++11
libhooker_LINKAGE_TYPE = both

libblackjack_FILES = libblackjack/objc/objc.c libhooker/as-aarch64/as-aarch64.c
libblackjack_CFLAGS = -Ilibhooker/include/apple -Ilibhooker/include/ -fvisibility=hidden
libblackjack_LDFLAGS = -lobjc -lhooker -L$(THEOS_OBJ_DIR)

libsubstitute_FILES = substitute-shim/substitute-shim.c
libsubstitute_CFLAGS = -Ilibhooker/include/ -Ilibblackjack/include/
libsubstitute_LDFLAGS = -lhooker -lblackjack -L$(THEOS_OBJ_DIR)

libsubstrate_FILES = substrate-shim/substrate-shim.c
libsubstrate_CFLAGS = -Ilibhooker/include/ -Ilibblackjack/include/
libsubstrate_LDFLAGS = -lhooker -lblackjack -L$(THEOS_OBJ_DIR) -lobjc

include $(THEOS_MAKE_PATH)/library.mk

TOOL_NAME = libhookerTest
libhookerTest_FILES = $(libhooker_FILES) $(libblackjack_FILES) unittest/main.c
libhookerTest_CFLAGS = -Ilibhooker/include/apple -Ilibhooker/include/ -DTHEOS -fvisibility=hidden
libhookerTest_CCFLAGS = -Ilibhooker/include/apple -Ilibhooker/include/ -DTHEOS -fvisibility=hidden -std=c++11
libhookerTest_LDFLAGS = -lobjc
libhookerTest_CODESIGN_FLAGS = -Sunittest/webcontent.xml

include $(THEOS_MAKE_PATH)/tool.mk
