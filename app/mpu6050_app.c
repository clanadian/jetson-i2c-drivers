#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "mpu6050_app.h"

#define MPU6050_DEV "/dev/mpu6050_i2c"

int mpu6050_open(void)
{
    int fd = open(MPU6050_DEV, O_RDONLY);
    if (fd < 0)
        perror("mpu6050 open fail");
    return fd;
}

int mpu6050_read_data(int fd, struct mpu6050_data *data)
{
    ssize_t n = read(fd, data, sizeof(*data));

    if (n != (ssize_t)sizeof(*data)) {
        fprintf(stderr, "mpu6050 read fail: n=%zd errno=%d (%s)\n",
                n, errno, strerror(errno));
        return -1;
    }
    return 0;
}
