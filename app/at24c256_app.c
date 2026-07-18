#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "at24c256_app.h"

#define AT24C256_DEV "/dev/at24c256_i2c"

int at24c256_read_calib(struct at24c256_calib_data *calib)
{
    int fd = open(AT24C256_DEV, O_RDONLY);
    if (fd < 0) {
        perror("at24c256 open fail");
        return -1;
    }

    /* open 직후 f_pos = 0 이라 EEPROM 주소 0부터 읽음 */
    ssize_t n = read(fd, calib, sizeof(*calib));
    close(fd);

    if (n != (ssize_t)sizeof(*calib)) {
        fprintf(stderr, "eeprom read fail: n=%zd errno=%d (%s)\n",
                n, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int at24c256_write_calib(const struct at24c256_calib_data *calib)
{
    int fd = open(AT24C256_DEV, O_WRONLY);
    if (fd < 0) {
        perror("at24c256 open fail");
        return -1;
    }

    ssize_t n = write(fd, calib, sizeof(*calib));
    close(fd);

    if (n != (ssize_t)sizeof(*calib)) {
        fprintf(stderr, "eeprom write fail: n=%zd errno=%d (%s)\n",
                n, errno, strerror(errno));
        return -1;
    }
    return 0;
}
