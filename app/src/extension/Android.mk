LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := nova_probe
LOCAL_SRC_FILES := window_probe.c
LOCAL_CFLAGS := -Wall -Wextra -Werror -fvisibility=hidden
LOCAL_LDLIBS := -landroid -ldl
include $(BUILD_SHARED_LIBRARY)
