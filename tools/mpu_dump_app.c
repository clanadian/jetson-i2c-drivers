#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "../include/mpu6050_i2c.h"

int main(){
    int fd = open("/dev/mpu6050_i2c", O_RDONLY);
    if (fd < 0) {
        perror("mpu6050 open fail");
        return 1;
    }

    FILE *fp = fopen("mpu_dump.txt", "w");
    if (!fp) {
        perror("fopen txt fail");
        close(fd);
        return 1;
    }

    for (int i = 0; i < 300; i++){
        struct mpu6050_data data;
        ssize_t n = read(fd, &data, sizeof(data));
        if (n != (ssize_t)sizeof(data)){
            fprintf(stderr, "mpu6050 read fail at %d: n=%zd errno=%d (%s)\n",
                    i, n, errno, strerror(errno));
            continue;
        }
        fprintf(fp, "%d %d %d %d %d %d %d\n", data.accel_x, data.accel_y, data.accel_z, data.temp, data.gyro_x, data.gyro_y, data.gyro_z);
        usleep(2000);
    }

    fclose(fp);
    close(fd);
}