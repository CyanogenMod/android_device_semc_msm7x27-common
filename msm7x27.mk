#
# Copyright (C) 2011 The CyanogenMod Project
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
#

# These is the hardware-common overlay, which points to the location
# of hardware-specific resource overrides, typically the frameworks and
# application settings that are stored in resourced.


PRODUCT_SPECIFIC_DEFINES += TARGET_PRELINKER_MAP=$(TOP)/device/semc/msm7x27-common/prelink-linux-arm-x8.map

PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/prebuilt/ueventd.delta.rc:root/ueventd.delta.rc \
    device/semc/msm7x27-common/prebuilt/init.delta.rc:root/init.delta.rc \
    device/semc/msm7x27-common/recovery.fstab:root/recovery.fstab \
    device/semc/msm7x27-common/prebuilt/vold.fstab:system/etc/vold.fstab \
    device/semc/msm7x27-common/prebuilt/hw_config.sh:system/etc/hw_config.sh 

PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.xml:system/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/base/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml

PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/prebuilt/gps.conf:system/etc/gps.conf

#recovery resources
PRODUCT_COPY_FILES += \
    bootable/recovery/res/images/icon_clockwork.png:root/res/images/icon_clockwork.png \
    bootable/recovery/res/images/icon_error.png:root/res/images/icon_error.png \
    bootable/recovery/res/images/icon_installing.png:root/res/images/icon_installing.png \
    bootable/recovery/res/images/indeterminate1.png:root/res/images/indeterminate1.png \
    bootable/recovery/res/images/indeterminate2.png:root/res/images/indeterminate2.png \
    bootable/recovery/res/images/indeterminate3.png:root/res/images/indeterminate3.png \
    bootable/recovery/res/images/indeterminate4.png:root/res/images/indeterminate4.png \
    bootable/recovery/res/images/indeterminate5.png:root/res/images/indeterminate5.png \
    bootable/recovery/res/images/indeterminate6.png:root/res/images/indeterminate6.png \
    bootable/recovery/res/images/progress_empty.png:root/res/images/progress_empty.png \
    bootable/recovery/res/images/progress_fill.png:root/res/images/progress_fill.png

PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/base/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

#Kernel modules
PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/modules/dm-mod.ko:system/lib/modules/dm-mod.ko \
    device/semc/msm7x27-common/modules/dm-crypt.ko:system/lib/modules/dm-crypt.ko \
    device/semc/msm7x27-common/modules/twofish.ko:system/lib/modules/twofish.ko \
    device/semc/msm7x27-common/modules/twofish_common.ko:system/lib/modules/twofish_common.ko \
    device/semc/msm7x27-common/modules/mddi.ko:system/lib/modules/mddi.ko \
    device/semc/msm7x27-common/prebuilt/04modules:system/etc/init.d/04modules


#HOTSPOT
PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/modules/sdio.ko:system/lib/modules/sdio.ko \
    device/semc/msm7x27-common/modules/tiwlan_drv.ko:system/lib/modules/tiwlan_drv.ko \
    device/semc/msm7x27-common/modules/tiap_drv.ko:system/lib/modules/tiap_drv.ko \
    device/semc/msm7x27-common/prebuilt/tiap_loader.sh:system/bin/tiap_loader.sh \
    device/semc/msm7x27-common/prebuilt/10dnsconf:system/etc/init.d/10dnsconf \
    device/semc/msm7x27-common/prebuilt/10hostapconf:system/etc/init.d/10hostapconf \
    device/semc/msm7x27-common/prebuilt/hostapd.conf:system/etc/wifi/softap/hostapd.conf \
    device/semc/msm7x27-common/prebuilt/dnsmasq.conf:system/etc/wifi/dnsmasq.conf \
    device/semc/msm7x27-common/prebuilt/tiwlan.ini:system/etc/wifi/tiwlan.ini \
    device/semc/msm7x27-common/prebuilt/tiwlan_ap.ini:system/etc/wifi/softap/tiwlan_ap.ini \
    device/semc/msm7x27-common/prebuilt/tiwlan_firmware.bin:system/etc/wifi/tiwlan_firmware.bin \
    device/semc/msm7x27-common/prebuilt/tiwlan_firmware_ap.bin:system/etc/wifi/softap/tiwlan_firmware_ap.bin 

#netfilter
#PRODUCT_COPY_FILES += \
#    device/semc/msm7x27-common/modules/netfilter/ax8netfilter.ko:system/lib/modules/netfilter/ax8netfilter.ko \
#    device/semc/msm7x27-common/modules/netfilter/x_tables.ko:system/lib/modules/netfilter/x_tables.ko \
#    device/semc/msm7x27-common/modules/netfilter/ip_tables.ko:system/lib/modules/netfilter/ip_tables.ko \
#    device/semc/msm7x27-common/modules/netfilter/iptable_filter.ko:system/lib/modules/netfilter/iptable_filter.ko \
#    device/semc/msm7x27-common/modules/netfilter/nf_conntrack.ko:system/lib/modules/netfilter/nf_conntrack.ko \
#    device/semc/msm7x27-common/modules/netfilter/xt_multiport.ko:system/lib/modules/netfilter/xt_multiport.ko \
#    device/semc/msm7x27-common/modules/netfilter/xt_state.ko:system/lib/modules/netfilter/xt_state.ko \
#    device/semc/msm7x27-common/modules/netfilter/nf_defrag_ipv4.ko:system/lib/modules/netfilter/nf_defrag_ipv4.ko \
#    device/semc/msm7x27-common/modules/netfilter/nf_conntrack_ipv4.ko:system/lib/modules/netfilter/nf_conntrack_ipv4.ko \
#    device/semc/msm7x27-common/modules/netfilter/nf_nat.ko:system/lib/modules/netfilter/nf_nat.ko \
#    device/semc/msm7x27-common/modules/netfilter/iptable_nat.ko:system/lib/modules/netfilter/iptable_nat.ko \
#    device/semc/msm7x27-common/modules/netfilter/ipt_MASQUERADE.ko:system/lib/modules/netfilter/ipt_MASQUERADE.ko \
#    device/semc/msm7x27-common/modules/netfilter/iptables:system/bin/iptables

#crappy headset
PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/prebuilt/SystemConnector.apk:system/app/SystemConnector.apk 

PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/prebuilt/AutoVolumeControl.txt:system/etc/AutoVolumeControl.txt 

#xrecovery
PRODUCT_COPY_FILES += \
    device/semc/msm7x27-common/prebuilt/chargemon:system/bin/chargemon \
    device/semc/msm7x27-common/prebuilt/sh:system/xbin/sh


PRODUCT_PACKAGES += \
    mount_system \
    hostap \
    librs_jni \
    gralloc.delta \
    copybit.delta \
    gps.delta \
    libOmxCore \
    libmm-omxcore \
    com.android.future.usb.accessory


# we have enough storage space to hold precise GC data
PRODUCT_TAGS += dalvik.gc.type-precise

PRODUCT_PROPERTY_OVERRIDES += \
    rild.libpath=/system/lib/libril-qc-1.so \
    rild.libargs=-d/dev/smd0 \
    ro.ril.hsxpa=1 \
    ro.ril.gprsclass=10 \
    ro.telephony.default_network=0 \
    ro.telephony.call_ring.multiple=false \
    wifi.interface=tiwlan0 \
    wifi.supplicant_scan_interval=15 \
    keyguard.no_require_sim=true \
    ro.com.google.locationfeatures=1 \
    dalvik.vm.dexopt-flags=m=y \
    dalvik.vm.heapsize=24m \
    dalvik.vm.dexopt-data-only=1 \
    dalvik.vm.lockprof.threshold=500 \
    dalvik.vm.execution-mode=int:jit \
    dalvik.vm.checkjni=false \
    ro.kernel.android.checkjni=0 \
    ro.opengles.version=131072  \
    ro.compcache.default=0 \
    ro.product.locale.language=en \
    ro.product.locale.region=US \
    wifi.hotspot.ti=1 \
    BUILD_UTC_DATE=0


