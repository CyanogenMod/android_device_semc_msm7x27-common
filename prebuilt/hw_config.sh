#!/system/bin/busybox sh

# Vibrator configuration
echo 2400 > /sys/devices/platform/msm_pmic_vibrator/voltage_mv

# lm3530 LMU configuration
echo linear > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/br::mapping  # linear exp
echo 32768 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/br::rate::up   # 8, 1024, 2048, 4096, 8192, 16384, 32768, 65538
echo 32768 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/br::rate::down # 8, 1024, 2048, 4096, 8192, 16384, 32768, 65538
echo 254 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/br::limit       # 0 - 255
echo 26.0 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/br::fsc        # 5.0, 8.5, 12.0, 15.5, 19.0, 22.5, 26.0, 29.5
echo 4,16,64,250 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/curve::borders
echo 47,75,114,164,226 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/curve::targets
echo high-z > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/als::r1      # high-z, 9360, 5560 .. 677.6 (see chip mnual)
echo high-z > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/als::r2      # high-z, 9360, 5560 .. 677.6 (see chip mnual)
echo 128 > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/als::avg-t     # 32, 63, 128, 256, 512, 1024, 2048, 4096
echo i2c > /sys/devices/platform/i2c-adapter/i2c-0/0-0036/mode    # i2c, pwm, i2c_pwm, als, pwm_als, i2c_pwm_als, i2_als

# Proximity sensor configuration
echo  15 > /sys/devices/platform/proximity-sensor/semc/proximity-sensor/led_on_ms         # sensor LED on time in ms
echo  35 > /sys/devices/platform/proximity-sensor/semc/proximity-sensor/led_off_ms       # sensor LED off time in ms

echo "ondemand" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 90 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold
echo 30 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/down_differential
echo 500000 > /sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate
echo 245760 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

mount -o rw,remount -t yaffs2 /dev/block/mtdblock0 /system
chmod u+s /system/bin/charger
mount -o ro,remount -t yaffs2 /dev/block/mtdblock0 /system

# Keyboard LED configuration
echo 3 > /sys/devices/platform/msm_pmic_misc_led.0/als::cut-off   # would be active only in ligh zone "0"
echo user_als > /sys/devices/platform/msm_pmic_misc_led.0/control::mode  # user, als, user_als
echo 40 > /sys/devices/platform/msm_pmic_misc_led.0/max::current_ma  #  0, 10, 20 .. 150 mA
