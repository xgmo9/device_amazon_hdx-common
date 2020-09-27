LOCAL_PATH := $(call my-dir)

ifneq ($(filter apollo thor,$(TARGET_DEVICE)),)

ifneq ($(TARGET_SIMULATOR),true)
include $(call first-makefiles-under,$(LOCAL_PATH))

include $(CLEAR_VARS)

FIRMWARE_DXHDCP2_IMAGES := \
    dxhdcp2.b00 dxhdcp2.b01 dxhdcp2.b02 dxhdcp2.b03 dxhdcp2.mdt
FIRMWARE_DXHDCP2_SYMLINKS := $(addprefix $(TARGET_OUT_VENDOR)/firmware/,$(notdir $(FIRMWARE_DXHDCP2_IMAGES)))
$(FIRMWARE_DXHDCP2_SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "DXHDCP2 Firmware link: $@"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf /firmware/image/$(notdir $@) $@
ALL_DEFAULT_INSTALLED_MODULES += $(FIRMWARE_DXHDCP2_SYMLINKS)

WCD9320_IMAGES := \
    wcd9320_anc.bin wcd9320_mbhc.bin

WCD9320_SYMLINKS := $(addprefix $(TARGET_OUT_VENDOR)/firmware/wcd9320/,$(WCD9320_IMAGES))
$(WCD9320_SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "wcd9320 firmware link: $@"
	@mkdir -p $(dir $@)
	@rm -rf $@
	tf=$(notdir $@); if [ "$$tf" = "wcd9320_mbhc.bin" ]; then tf="mbhc.bin"; fi; ln -sf /data/misc/audio/$$tf $@

ALL_DEFAULT_INSTALLED_MODULES += $(WCD9320_SYMLINKS)

endif

endif
