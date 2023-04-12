BOARD_VENDOR := amazon

# headers
TARGET_SPECIFIC_HEADER_PATH := device/amazon/hdx-common/include

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := MSM8974
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Platform
TARGET_BOARD_PLATFORM := msm8974
TARGET_BOARD_PLATFORM_GPU := qcom-adreno330

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := krait
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true

# Binder API version
TARGET_USES_64_BIT_BINDER := true

# Amazon KitKat libraries have text relocations, don't error out when
# using them.  TODO: Look through each lib which generates this error
# and see if there are possible replacements using other device binaries
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true

# Kernel
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x02000000 --tags_offset 0x01e00000
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_SEPARATED_DT := true
BOARD_KERNEL_IMAGE_NAME := zImage

# Kernel config
TARGET_KERNEL_CLANG_COMPILE := true
TARGET_KERNEL_SOURCE := kernel/amazon/hdx-common
TARGET_KERNEL_CONFIG := hdx-common_defconfig
TARGET_KERNEL_SELINUX_CONFIG := selinux_defconfig

# Enable debug on eng builds
ifeq ($(TARGET_BUILD_VARIANT),eng)
TARGET_KERNEL_ADDITIONAL_CONFIG:= debug_defconfig
endif

TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-linux-androideabi-

# Shims
TARGET_LD_SHIM_LIBS := \
    /system/vendor/bin/ATFWD-daemon|libshim_libbinder.so \
    /system/lib/liblog.so|libshim_liblog.so \
    /system/lib/libcutils.so|libshim_liblog.so \
    /system/vendor/lib/lib-imsdpl.so|libshim_libcrypto.so \
    /system/vendor/bin/mpdecision|libshim_atomic.so \
    /system/vendor/bin/xtwifi-client|libshim_libcrypto.so

# QCOM hardware
BOARD_USES_QCOM_HARDWARE := true

# Audio
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_POST_PROC := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_NEW_SAMPLE_RATE := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
BOARD_USES_ALSA_AUDIO := true
USE_CUSTOM_AUDIO_POLICY := 1
USE_XML_AUDIO_POLICY_CONF := 1

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_QCOM := true
BOARD_HAS_QCA_BT_AR3002 := true
WIFI_BT_STATUS_SYNC := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/amazon/hdx-common/bluetooth

# Lineage Hardware
BOARD_HARDWARE_CLASS := \
    hardware/lineage/lineagehw 

# Filesystem
BOARD_BOOTIMAGE_PARTITION_SIZE     := 0xA00000
BOARD_CACHEIMAGE_PARTITION_SIZE    := 1073741824
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 0xA00000
BOARD_SYSTEMIMAGE_PARTITION_SIZE   := 1308622848
BOARD_USERDATAIMAGE_PARTITION_SIZE := 12549323776

# Use mke2fs instead of make_ext4fs
TARGET_USES_MKE2FS := true

# Graphics
USE_OPENGL_RENDERER := true
TARGET_USES_C2D_COMPOSITION := true
TARGET_USES_OVERLAY := true
TARGET_USES_ION := true
TARGET_DISPLAY_USE_RETIRE_FENCE := true
OVERRIDE_RS_DRIVER := libRSDriver_adreno.so
TARGET_ADDITIONAL_GRALLOC_10_USAGE_BITS := 0x02000000U

# camera HAL1
TARGET_HAS_LEGACY_CAMERA_HAL1 := true

# Surfaceflinger optimization for VD surfaces
TARGET_FORCE_HWC_FOR_VIRTUAL_DISPLAYS := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true

# Shader cache config options
# Maximum size of the  GLES Shaders that can be cached for reuse.
# Increase the size if shaders of size greater than 12KB are used.
MAX_EGL_CACHE_KEY_SIZE := 12*1024

# Maximum GLES shader cache size for each app to store the compiled shader
# binaries. Decrease the size if RAM or Flash Storage size is a limitation
# of the device.
MAX_EGL_CACHE_SIZE := 2048*1024

# HIDL
DEVICE_MANIFEST_FILE := device/amazon/hdx-common/manifest.xml
DEVICE_MATRIX_FILE := device/amazon/hdx-common/compatibility_matrix.xml

# Wifi

BOARD_HAS_QCOM_WLAN                 := true
BOARD_HAS_ATH_WLAN_AR6004           := true
BOARD_CONFIG_ATH6KL_USB             := true
BOARD_HAS_CFG80211_KERNEL3_4        := true
BOARD_WLAN_DEVICE                   := qcwcn
WPA_SUPPLICANT_VERSION              := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER         := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB    := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
BOARD_HOSTAPD_DRIVER                := NL80211
BOARD_HOSTAPD_PRIVATE_LIB           := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
PRODUCT_VENDOR_MOVE_ENABLED         := true
WIFI_DRIVER_FW_PATH_STA             := "sta"
WIFI_DRIVER_FW_PATH_AP              := "ap"
WIFI_DRIVER_FW_PATH_P2P             := "p2p"
WIFI_DRIVER_FW_PATH_PARAM           := "/sys/module/wlan/parameters/fwpath"

# Filesystem
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PERSISTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_OEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_FS_CONFIG_GEN := device/amazon/hdx-common/config.fs
TARGET_NOT_USE_GZIP_RECOVERY_RAMDISK := true

# Power
TARGET_HAS_LEGACY_POWER_STATS := true
TARGET_HAS_NO_WIFI_STATS := true
TARGET_USES_INTERACTION_BOOST := true

# Temporary
USE_CAMERA_STUB := true

# Recovery
TARGET_RECOVERY_FSTAB := device/amazon/hdx-common/rootdir/etc/fstab.qcom

# Use HW crypto for ODE
TARGET_HW_DISK_ENCRYPTION := true
TARGET_LEGACY_HW_DISK_ENCRYPTION := true

# Added to indicate that protobuf-c is supported in this build
PROTOBUF_SUPPORTED := true

# ANT+ - TODO: Confirm this - TODO: Confirm this - TODO: Confirm this - TODO: Confirm this
BOARD_ANT_WIRELESS_DEVICE := "vfs-prerelease"

ifeq ($(HOST_OS),linux)
  ifeq ($(TARGET_BUILD_VARIANT),eng)
    ifeq ($(WITH_DEXPREOPT),)
      WITH_DEXPREOPT := true
      WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true
    endif
  endif
endif

# Extended filesystem support
TARGET_EXFAT_DRIVER := sdfat

# Webkit
ENABLE_WEBGL := true
TARGET_FORCE_CPU_UPLOAD := true

TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
BOARD_USES_MMCUTILS := true
BOARD_HAS_LARGE_FILESYSTEM := true
BOARD_HAS_NO_MISC_PARTITION := true
BOARD_HAS_NO_SELECT_BUTTON := true

# TWRP Recovery
RECOVERY_FSTAB_VERSION := 2
TARGET_RECOVERY_QCOM_RTC_FIX := true
RECOVERY_GRAPHICS_USE_LINELENGTH := true
BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_23x41.h\"
RECOVERY_SDCARD_ON_DATA := true
TW_BRIGHTNESS_PATH := /sys/class/leds/lcd-backlight/brightness
TW_CUSTOM_BATTERY_PATH := /sys/class/power_supply/bq27x41
TW_USE_TOOLBOX := true
TW_USE_TOYBOX := true
TW_EXCLUDE_SUPERSU := true
TW_EXCLUDE_TWRPAPP := true
ifneq (,$(strip $(wildcard bootable/recovery-twrp/twrp.cpp)))
RECOVERY_VARIANT := twrp
endif

# hdx old bootloader dtb compatibility fix + bootloader signature exploit patch
BOARD_CUSTOM_BOOTIMG_MK := device/amazon/hdx-common/mkboot.mk

# SELinux policies
# qcom sepolicy
include device/qcom/sepolicy-legacy/sepolicy.mk

BOARD_SEPOLICY_DIRS += \
    device/amazon/hdx-common/sepolicy

MALLOC_SVELTE := true

# Disable API check
WITHOUT_CHECK_API := true
