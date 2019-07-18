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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := \
    audio_hw.c
LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    liblog \
    libcutils \
    libtinyalsa \
    libaudioroute \
    libexpat
LOCAL_C_INCLUDES += \
    external/tinyalsa/include \
    external/expat/lib \
    hardware/libhardware/include \
    system/core/libsystem/include \
    system/media/audio/include \
    system/core/libutils/include \
    $(call include-path-for, audio-route) \
    $(call include-path-for, audio-effects)
ifeq ($(strip $(BOARD_USES_NXVOICE)),true)
LOCAL_CFLAGS += -DUSES_NXVOICE
LOCAL_SHARED_LIBRARIES += \
    libnxvoice
LOCAL_C_INCLUDES += \
    device/nexell/library/include \
    device/nexell/library/nx-smartvoice
endif

ifneq ($(filter pvo,$(SVOICE_ECNR_VENDOR)),)
LOCAL_SHARED_LIBRARIES += \
    libpvo \
    libpovosource
LOCAL_C_INCLUDES += \
    device/nexell/library/libpowervoice
endif

ifneq ($(filter mwsr,$(SVOICE_ECNR_VENDOR)),)
LOCAL_SHARED_LIBRARIES += \
    libmwsr
LOCAL_C_INCLUDES += \
    device/nexell/library/libmwsr
endif

ifeq ($(QUICKBOOT), 1)
LOCAL_CFLAGS += -DQUICKBOOT
endif

SND_BT_CARD_ID ?= 0
SND_BT_DEVICE_ID ?= 0
SND_BT_SCO_CARD_ID ?= 0
SND_BT_SCO_DEVICE_ID ?= 2

LOCAL_CFLAGS += -DSND_BT_CARD_ID=$(SND_BT_CARD_ID)
LOCAL_CFLAGS += -DSND_BT_DEVICE_ID=$(SND_BT_DEVICE_ID)
LOCAL_CFLAGS += -DSND_BT_SCO_CARD_ID=$(SND_BT_SCO_CARD_ID)
LOCAL_CFLAGS += -DSND_BT_SCO_DEVICE_ID=$(SND_BT_SCO_DEVICE_ID)

include $(BUILD_SHARED_LIBRARY)
