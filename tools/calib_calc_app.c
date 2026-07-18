#include <stdint.h>
#include <stdio.h>

/* ±2g 기본 레인지에서 1g = 16384 LSB.
 * 센서를 수평으로 놓고 덤프했다는 가정 하에 accel_z 평균에서 중력분을 빼야
 * 순수 오프셋이 나옴 (accel_x/y, gyro는 정지 상태 기대값이 0이라 평균 = 오프셋).
 */
#define ACCEL_1G 16384

static int round_div(long long sum, int n)
{
    return (int)((sum >= 0 ? sum + n / 2 : sum - n / 2) / n);
}

int main(){
    FILE *in = fopen("mpu_dump.txt", "r");
    if (!in) {
        perror("fopen mpu_dump.txt fail");
        return 1;
    }

    long long sum_ax = 0, sum_ay = 0, sum_az = 0;
    long long sum_gx = 0, sum_gy = 0, sum_gz = 0;
    int ax, ay, az, temp, gx, gy, gz;
    int n = 0;

    while (fscanf(in, "%d %d %d %d %d %d %d",
                  &ax, &ay, &az, &temp, &gx, &gy, &gz) == 7) {
        sum_ax += ax; sum_ay += ay; sum_az += az;
        sum_gx += gx; sum_gy += gy; sum_gz += gz;
        n++;
    }
    fclose(in);

    if (n == 0) {
        fprintf(stderr, "no samples in mpu_dump.txt\n");
        return 1;
    }

    int off_ax = round_div(sum_ax, n);
    int off_ay = round_div(sum_ay, n);
    int off_az = round_div(sum_az, n) - ACCEL_1G;
    int off_gx = round_div(sum_gx, n);
    int off_gy = round_div(sum_gy, n);
    int off_gz = round_div(sum_gz, n);

    printf("samples: %d\n", n);
    printf("accel offset: %d %d %d\n", off_ax, off_ay, off_az);
    printf("gyro  offset: %d %d %d\n", off_gx, off_gy, off_gz);

    FILE *out = fopen("calib_avg.txt", "w");
    if (!out) {
        perror("fopen calib_avg.txt fail");
        return 1;
    }
    fprintf(out, "%d %d %d %d %d %d\n",
            off_ax, off_ay, off_az, off_gx, off_gy, off_gz);
    fclose(out);

    printf("saved to calib_avg.txt\n");
    return 0;
}
