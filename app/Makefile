# 메인 앱 빌드 (Jetson Nano 위에서 gcc로 직접 빌드)
CC ?= gcc
CFLAGS ?= -Wall -O2

OBJS = main.o mpu6050_app.o at24c256_app.o ssd1306_app.o ssd1306_fonts.o

main_app: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f main_app *.o

.PHONY: clean
