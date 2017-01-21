
LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/droid_conf.mk

include $(CLEAR_VARS)
LOCAL_MODULE := shairport
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := shairport.c common.c rtsp.c mdns.c mdns_external.c rtp.c player.c alac.c audio.c 
LOCAL_SRC_FILES += droid-lacks-src/random.c

## 
$(info "Please get libdaemon from https://android.googlesource.com/platform/external/libdaemon")
##
LOCAL_C_INCLUDES := $(LOCAL_PATH) \
  $(LOCAL_PATH)/droid-lacks-include \
  external/libdaemon

LOCAL_CFLAGS := -Wno-missing-field-initializers \
  -DSYSCONFDIR=\"/etc\" \
  -D__android__

LOCAL_SHARED_LIBRARIES :=  libc liblog \
  libcutils libdaemon 

LOCAL_STATIC_LIBRARIES := \
  libshairport_popt \
  libshairport_config-c

ifeq ($(strip $(CONFIG_CUSTOMPIDDIR)),yes)
  LOCAL_CFLAGS += \
	-DPIDDIR=\"$(CUSTOM_PID_DIR)\"
endif

ifeq ($(strip $(CONFIG_SOXR)),yes)
  LOCAL_STATIC_LIBRARIES += libshairport_soxr
endif

ifeq ($(strip $(CONFIG_OPENSSL)),yes)
  LOCAL_SHARED_LIBRARIES +=  libssl libcrypto
  LOCAL_C_INCLUDES += external/openssl \
	external/openssl/include 
endif

ifeq ($(strip $(CONFIG_DNS_SD)),yes)
  LOCAL_SRC_FILES += mdns_dns_sd.c droid-lacks-src/stpcpy.c
  LOCAL_C_INCLUDES += external/mdnsresponder/mDNSShared 
  LOCAL_SHARED_LIBRARIES += libmdnssd
endif

ifeq ($(strip $(CONFIG_AVAHI)),yes)
  LOCAL_SRC_FILES += mdns_avahi.c
endif

ifeq ($(strip $(CONFIG_TINYSVCMDNS)),yes)
  LOCAL_SRC_FILES += mdns_tinysvcmdns.c \
    tinysvcmdns.c \
    droid-lacks-src/ifaddrs.c
endif

ifeq ($(strip $(CONFIG_ALSA)),yes)
  LOCAL_SRC_FILES +=  audio_alsa.c
  LOCAL_CFLAGS += -D_POSIX_SOURCE -D_POSIX_C_SOURCE
  LOCAL_C_INCLUDES += external/alsa-lib/include
  LOCAL_SHARED_LIBRARIES += libasound
endif 

ifeq ($(strip $(CONFIG_TINYALSA)),yes)
	LOCAL_SHARED_LIBRARIES += libtinyalsa
endif

ifeq ($(strip $(CONFIG_SNDIO)),yes)
  LOCAL_SRC_FILES += audio_sndio.c
endif

ifeq ($(strip $(CONFIG_STDOUT)),yes)
  LOCAL_SRC_FILES += audio_stdout.c
endif

ifeq ($(strip $(CONFIG_PIPE)),yes)
  LOCAL_SRC_FILES += audio_pipe.c
endif

ifeq ($(strip $(CONFIG_DUMMY)),yes)
  LOCAL_SRC_FILES += audio_dummy.c
endif

ifeq ($(strip $(CONFIG_RKTUBE)),yes)
  LOCAL_SRC_FILES += audio_rktube.c
endif

ifeq ($(strip $(CONFIG_AO)),yes)
  LOCAL_SRC_FILES += audio_ao.c
endif

ifeq ($(strip $(CONFIG_PULSE)),yes)
  LOCAL_SRC_FILES += audio_pulse.c
endif

include $(BUILD_EXECUTABLE)

include $(call all-subdir-makefiles)
