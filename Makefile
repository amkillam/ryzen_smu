MOD 				:= ryzen_smu
VERSION				:= 0.1.8
TARGET				:= $(shell uname -r)
DKMS_ROOT_PATH			:= /usr/src/$(MOD)-$(VERSION)

KERNEL_MODULES			:= /lib/modules/$(TARGET)

ifneq ("","$(wildcard /usr/src/linux-headers-$(TARGET)/*)")
	KERNEL_BUILD		:= /usr/src/linux-headers-$(TARGET)
else
ifneq ("","$(wildcard /usr/src/kernels/$(TARGET)/*)")
	KERNEL_BUILD		:= /usr/src/kernels/$(TARGET)
else
	KERNEL_BUILD		:= $(KERNEL_MODULES)/build
endif
endif

obj-m				:= $(MOD).o
$(MOD)-objs		 	:= drv.o smu.o lib/smu_common.o

.PHONY: all modules clean dkms-install dkms-uninstall insmod checkmod

all: modules

debug:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) ccflags-y+=-DDEBUG modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

clean:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean
	rm -rf *.o

dkms-install:
	mkdir -p $(DKMS_ROOT_PATH)
	cp $(CURDIR)/dkms.conf $(DKMS_ROOT_PATH)
	cp $(CURDIR)/Makefile $(DKMS_ROOT_PATH)
	cp $(CURDIR)/*.c $(DKMS_ROOT_PATH)
	cp $(CURDIR)/*.h $(DKMS_ROOT_PATH)

	sed -e "s/@CFLGS@/${MCFLAGS}/" \
		-e "s/@VERSION@/$(VERSION)/" \
		-i $(DKMS_ROOT_PATH)/dkms.conf

	dkms add ryzen_smu/$(VERSION)
	dkms build ryzen_smu/$(VERSION)
	dkms install ryzen_smu/$(VERSION)

dkms-uninstall:
	dkms remove ryzen_smu/$(VERSION) --all
	rm -rf $(DKMS_ROOT_PATH)

insmod:
	sudo rmmod $(MOD).ko; true
	sudo insmod $(MOD).ko

checkmod:
	lsmod | grep $(MOD)
	cat /proc/modules | grep $(MOD)
	cat /sys/kernel/$(MOD)_drv/drv_version
	modinfo $(MOD)
