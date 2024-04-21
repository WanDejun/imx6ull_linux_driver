KERNELDIR := /home/wandejun/linux/imx6ull/linux/NXP_linux
CURRENT_PATH := $(shell pwd)
MODULES_NAME := semaphore
APP_NAME := $(MODULES_NAME)APP
obj-m := $(MODULES_NAME).o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules
	# arm-linux-gnueabihf-gcc $(APP_NAME).c -o $(APP_NAME).elf
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
	# rm $(APP_NAME).elf