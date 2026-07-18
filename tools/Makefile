# 테스트 앱 빌드 (Jetson Nano 위에서 gcc로 직접 빌드)
CC ?= gcc
CFLAGS ?= -Wall -O2

APPS = mpu_dump_app calib_calc_app eeprom_write_app eeprom_read_oled_app

all: $(APPS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(APPS)

.PHONY: all clean
