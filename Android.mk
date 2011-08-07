ifneq ($(filter mimmi robyn shakira , $(TARGET_BOOTLOADER_BOARD_NAME)),)
    include $(all-subdir-makefiles)
endif
