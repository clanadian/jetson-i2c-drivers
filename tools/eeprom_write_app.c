#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "../include/at24c256_i2c.h"

int main(){
    FILE *in = fopen("calib_avg.txt", "r");
    if (!in) {
        perror("fopen calib_avg.txt fail");
        return 1;
    }

    int ax, ay, az, gx, gy, gz;
    if (fscanf(in, "%d %d %d %d %d %d", &ax, &ay, &az, &gx, &gy, &gz) != 6) {
        fprintf(stderr, "calib_avg.txt parse fail\n");
        fclose(in);
        return 1;
    }
    fclose(in);

    /* s16 범위 확인 — 평균이라 보통 안 벗어나지만, 벗어나면 잘린 값이 저장되므로 차단 */
    int vals[6] = { ax, ay, az, gx, gy, gz };
    for (int i = 0; i < 6; i++) {
        if (vals[i] < -32768 || vals[i] > 32767) {
            fprintf(stderr, "offset %d out of s16 range: %d\n", i, vals[i]);
            return 1;
        }
    }

    struct at24c256_calib_data calib = {
        .accel_offset_x = (int16_t)ax,
        .accel_offset_y = (int16_t)ay,
        .accel_offset_z = (int16_t)az,
        .gyro_offset_x  = (int16_t)gx,
        .gyro_offset_y  = (int16_t)gy,
        .gyro_offset_z  = (int16_t)gz,
    };

    int fd = open("/dev/at24c256_i2c", O_WRONLY);
    if (fd < 0) {
        perror("at24c256 open fail");
        return 1;
    }

    /* open 직후 f_pos = 0 이라 EEPROM 주소 0부터 기록됨 */
    ssize_t n = write(fd, &calib, sizeof(calib));
    close(fd);

    if (n != (ssize_t)sizeof(calib)) {
        fprintf(stderr, "eeprom write fail (%zd) errno=%d (%s)\n",
                n, errno, strerror(errno));
        return 1;
    }

    printf("wrote %zd bytes to eeprom\n", n);
    printf("accel offset: %d %d %d\n", ax, ay, az);
    printf("gyro  offset: %d %d %d\n", gx, gy, gz);
    return 0;
}
