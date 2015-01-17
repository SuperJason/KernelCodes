#
# module build for study
#
PWD := $(shell pwd)

ANDROID_BUILD_TOP := /home/jason/aol/msm8610-la-1-8/LINUX/android
PATH := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/arm/arm-eabi-4.7/bin:$(PATH)
KERNEL_BUILD := $(ANDROID_BUILD_TOP)/out/target/product/ara/obj/KERNEL_OBJ

ifeq ($(KERNELRELEASE),)

all: modules 

modules:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-eabi- modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko .*.o.d *.mod.c .tmp_versions modules.order Module.symvers

.PHONY: modules clean 

else
    obj-m := hello.o ablock.o achar.o
endif


