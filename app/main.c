/* EEPROM 캘리브레이션 오프셋을 로드해 MPU6050 값을 보정하고 OLED에 표시.
 * 오프셋 생성 순서: test/mpu_dump_app → calib_calc_app → eeprom_write_app */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

#include "mpu6050_app.h"
#include "at24c256_app.h"
#include "ssd1306_app.h"

#define UPDATE_US   200000   /* 5Hz */
#define AVG_SAMPLES 10
#define SAMPLE_US   (UPDATE_US / AVG_SAMPLES)

/* 상보필터: 자이로 적분 + accel 각도 혼합. 캘리브레이션이 특정 자세 기준이라
 * 결과 각도는 절대 수평이 아닌 그 기준 자세 대비 상대각임 (큰 각도일수록 오차 커짐) */
#define ALPHA        0.9f    /* 자이로 비중, 시정수 ~2초 */
#define DT_S         (UPDATE_US / 1000000.0f)
#define GYRO_LSB_DPS 131.0f  /* ±250dps 기본 레인지: 131 LSB = 1°/s */
#define RAD2DEG      (180.0f / (float)M_PI)

static volatile sig_atomic_t running = 1;

static void on_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* AVG_SAMPLES번 읽어 평균 (표시 흔들림 방지). 반환값은 실제 반영된 샘플 수, 0이면 전부 실패 */
static int mpu6050_read_avg(int fd, struct mpu6050_data *out)
{
    struct mpu6050_data d;
    long sum[6] = {0};
    int got = 0;

    for (int i = 0; i < AVG_SAMPLES; i++) {
        if (mpu6050_read_data(fd, &d) == 0) {
            sum[0] += d.accel_x; sum[1] += d.accel_y; sum[2] += d.accel_z;
            sum[3] += d.gyro_x;  sum[4] += d.gyro_y;  sum[5] += d.gyro_z;
            got++;
        }
        usleep(SAMPLE_US);
    }

    if (got == 0)
        return 0;

    out->accel_x = sum[0] / got;
    out->accel_y = sum[1] / got;
    out->accel_z = sum[2] / got;
    out->gyro_x  = sum[3] / got;
    out->gyro_y  = sum[4] / got;
    out->gyro_z  = sum[5] / got;
    return got;
}

int main(void)
{
    struct at24c256_calib_data calib;
    struct mpu6050_data d;
    char line[SSD1306_COLS + 1];
    int mpu_fd, oled_fd;
    float pitch = 0.0f, roll = 0.0f;

    signal(SIGINT, on_sigint);

    if (at24c256_read_calib(&calib) < 0)
        return 1;

    printf("calib loaded: accel(%d %d %d) gyro(%d %d %d)\n",
           calib.accel_offset_x, calib.accel_offset_y, calib.accel_offset_z,
           calib.gyro_offset_x, calib.gyro_offset_y, calib.gyro_offset_z);

    mpu_fd = mpu6050_open();
    if (mpu_fd < 0)
        return 1;

    oled_fd = ssd1306_open();
    if (oled_fd < 0) {
        close(mpu_fd);
        return 1;
    }

    ssd1306_clear(oled_fd);
    ssd1306_print(oled_fd, 0, "MPU6050 CALIBRATED");

    while (running) {
        if (mpu6050_read_avg(mpu_fd, &d) == 0)
            continue;   /* I2C 글리치 등 일시적 실패는 다음 주기에 재시도 */

        int ax = d.accel_x - calib.accel_offset_x;
        int ay = d.accel_y - calib.accel_offset_y;
        int az = d.accel_z - calib.accel_offset_z;
        int gx = d.gyro_x - calib.gyro_offset_x;
        int gy = d.gyro_y - calib.gyro_offset_y;
        int gz = d.gyro_z - calib.gyro_offset_z;

        /* 기준 자세에서 accel 벡터는 (0,0,1g)라 각도는 0°/0° */
        float acc_pitch = atan2f(-(float)ax,
                                 sqrtf((float)ay * ay + (float)az * az)) * RAD2DEG;
        float acc_roll  = atan2f((float)ay, (float)az) * RAD2DEG;

        /* gy→pitch, gx→roll로 대응. 부호는 보드 장착 방향에 따라 다르니
         * 반대로 움직이면 뒤집을 것 */
        pitch = ALPHA * (pitch + (float)gy / GYRO_LSB_DPS * DT_S)
              + (1.0f - ALPHA) * acc_pitch;
        roll  = ALPHA * (roll + (float)gx / GYRO_LSB_DPS * DT_S)
              + (1.0f - ALPHA) * acc_roll;

        snprintf(line, sizeof(line), "AX:%7d", ax);
        ssd1306_print(oled_fd, 1, line);
        snprintf(line, sizeof(line), "AY:%7d", ay);
        ssd1306_print(oled_fd, 2, line);
        snprintf(line, sizeof(line), "AZ:%7d", az);
        ssd1306_print(oled_fd, 3, line);
        snprintf(line, sizeof(line), "GX:%7d", gx);
        ssd1306_print(oled_fd, 4, line);
        snprintf(line, sizeof(line), "GY:%7d", gy);
        ssd1306_print(oled_fd, 5, line);
        snprintf(line, sizeof(line), "GZ:%7d", gz);
        ssd1306_print(oled_fd, 6, line);
        snprintf(line, sizeof(line), "P:%+6.1f R:%+6.1f", pitch, roll);
        ssd1306_print(oled_fd, 7, line);
        /* 갱신 주기 대기는 read_avg 안의 샘플링 딜레이(10 x 20ms)가 겸함 */
    }

    printf("\nexit\n");
    ssd1306_clear(oled_fd);
    close(oled_fd);
    close(mpu_fd);
    return 0;
}
