# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# The default audio HAL module, which is a stub, that is loaded if no other
# device specific modules are present. The exact load order can be seen in
# libhardware/hardware.c
#
# The format of the name is audio.<type>.<hardware/etc>.so where the only

ifeq ($(strip $(BOARD_USES_AUDIO_N)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := audio.primary.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_SRC_FILES := \
	audio_hw.c
LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libaudioroute \
	libexpat
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/expat/lib \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects)
ifeq ($(strip $(BOARD_USES_NXVOICE)),true)
LOCAL_CFLAGS += -DUSES_NXVOICE
LOCAL_SHARED_LIBRARIES += \
	libnxvoice \
	libpvo \
	libpovosource
LOCAL_C_INCLUDES += \
	device/nexell/library/nx-smartvoice \
	device/nexell/library/libpowervoice
endif

ifeq ($(QUICKBOOT), 1)
LOCAL_CFLAGS += -DQUICKBOOT
endif

include $(BUILD_SHARED_LIBRARY)

endif
