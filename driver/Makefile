# Jetson Nano (L4T 32.7.6, kernel 4.9.337-tegra) out-of-tree 커널 모듈 빌드용
obj-m += mpu6050_i2c.o
obj-m += at24c256_i2c.o
obj-m += ssd1306_i2c.o

# 타겟 커널 소스 경로 - Jetson Nano 위에서 직접 빌드할 땐 보통 아래 경로로 존재
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: default clean
