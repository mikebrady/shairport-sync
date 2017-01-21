LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := libshairport_config-c
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_SRC_FILES := grammar.c  libconfig.c  scanctx.c  scanner.c  strbuf.c

LOCAL_CFLAGS := -g -O2 -Wall -Wshadow -Wextra -Wdeclaration-after-statement -Wno-unused-parameter

include $(BUILD_STATIC_LIBRARY)

