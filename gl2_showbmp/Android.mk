LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	common.cpp \
	gl2_drawrgb.cpp \
	gl2_drawrgba.cpp \
	TimeUtils.cpp \
	main.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    libui \
    libgui

LOCAL_STATIC_LIBRARIES += libglTest

LOCAL_C_INCLUDES += $(call include-path-for, opengl-tests-includes)

LOCAL_MODULE:= test-opengl-gl2_showbmp

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

include $(BUILD_EXECUTABLE)
