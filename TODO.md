# Jetson Nano I2C 드라이버 프로젝트 TODO

## 0. 환경 세팅 (완료된 것들)
- [x] SD카드 이미지 굽기 (JetPack 4.6.6)
- [x] 헤드리스 초기설정 완료
- [x] 이더넷 연결 + SSH 접속 확인
- [x] apt update/upgrade + 커널 재부팅 (4.9.337-tegra 확정)
- [x] i2c-tools, build-essential, git, vim 설치
- [x] nvidia-l4t-kernel-headers 버전 일치 확인
- [x] WSL에 L4T 32.7.6 커널 소스 세팅 (VSCode 참조용)
- [x] MPU6050 모듈 수령 + 핀헤더 납땜용 파츠 확인

## 1. 하드웨어 배선 및 1차 검증
- [ ] MPU6050 핀헤더 납땜 (직핀 사용)
- [ ] Jetson 40핀 헤더에 배선 (VCC/GND/SDA/SCL, I2C-0 버스)
- [ ] `ls /dev/i2c-*` 로 버스 노드 확인
- [ ] `i2cdetect -y -r 0` 로 빈 버스 스캔 (에러 없이 실행되는지)
- [ ] MPU6050 연결 후 `i2cdetect -y -r 0` → 0x68(또는 0x69) 뜨는지 확인
- [ ] `i2cget -y 0 0x68 0x75` → WHO_AM_I 값 0x68 확인 (배선/통신 최종 검증)

## 2. 기존 드라이버 확인 (참고용, 이미 결론 남)
- [x] `CONFIG_INV_MPU6050_I2C is not set` 확인 → 기존 드라이버 없음, 바로 직접 구현 단계로

## 3. 디바이스 트리 오버레이
- [ ] MPU6050용 오버레이 .dts 작성 (compatible 커스텀 문자열, reg = 0x68)
- [ ] dtc로 컴파일 → .dtbo 생성
- [ ] extlinux.conf에 오버레이 등록 (Jetson 방식)
- [ ] 재부팅 후 오버레이 적용 확인 (`ls /proc/device-tree/` 등)

## 4. 커널 모듈 뼈대
- [ ] Makefile 작성 (obj-m, KDIR 등)
- [ ] i2c_driver 구조체 작성 (probe/remove, of_match_table)
- [ ] probe() 2-인자 구식 시그니처 확인하며 작성 (4.9 커널 기준)
- [ ] insmod/rmmod로 probe 진입 확인 (dmesg 로그만 찍어보기)

## 5. 캐릭터 디바이스 등록
- [ ] alloc_chrdev_region / cdev_add
- [ ] file_operations (open/read/release) 기본 뼈대
- [ ] /dev/mpu6050 노드 생성 확인
- [ ] 유저 공간에서 open/close 테스트

## 6. 레지스터 통신 로직
- [ ] i2c_smbus_read_byte_data로 WHO_AM_I 읽어서 dmesg에 출력
- [ ] PWR_MGMT_1(0x6B)에 0x00 써서 슬립 해제
- [ ] ACCEL_XOUT_H(0x3B)부터 6바이트 블록 읽기
- [ ] GYRO_XOUT_H(0x43)부터 6바이트 블록 읽기

## 7. read() 콜백 완성
- [ ] raw accel/gyro 값을 구조체에 담아 copy_to_user
- [ ] mutex로 공유 데이터(캐시된 값) 보호
- [ ] 유저 공간 테스트 프로그램으로 값 읽어보기

## 8. 유저 앱 파이프라인
- [ ] 폴링 루프 (일정 주기로 /dev/mpu6050 read)
- [ ] raw 데이터 → 각도 계산 로직
- [ ] SSD1306 출력 연동 (기존 드라이버 사용할지 직접 짤지 결정)
- [ ] EEPROM 저장 연동 (기존 at24 드라이버 사용할지 직접 짤지 결정)

## 9. 마무리 / 여유 있으면
- [ ] EEPROM에 캘리브레이션 오프셋 저장 → 부팅 시 자동 보정
- [ ] polling → interrupt/workqueue 업그레이드 (시간 남으면)
- [ ] 문서화 / 발표 자료용 다이어그램 정리

---
**우선순위 원칙**: 1~8번까지가 "폴링 기반 풀 파이프라인" 핵심 범위.
인터럽트(9번)는 시간 남을 때만 — 처음부터 욕심내지 않기.

## 10. mpu6050_i2c.c 코드 리뷰 수정사항 (컴파일 에러급)
- [ ] `mpu6050_i2c_open`이 두 번 정의됨 — 두 번째 것(module_put 있는 쪽)을 `mpu6050_i2c_release`로 개명
- [ ] `struct file_operations mpu6050_i2c_driver`가 `struct i2c_driver mpu6050_i2c_driver`랑 이름 겹침 → `mpu6050_i2c_fops`로 개명
- [ ] probe/remove forward declaration이 `struct spi_device *spi`로 되어있음(SPI 복붙 잔재) → 실제 정의(`struct i2c_client *`)랑 맞추거나 삭제
- [ ] `mpu6050_i2c_remove()` 인자 0개 → `struct i2c_client *client` 추가 필요
- [ ] `sturct` 오타 → `struct`
- [ ] `i2c_smbus_read_i2c_block_data(..., *values)` → `*values`가 아니라 `values`(포인터 그대로)
- [ ] 함수명 불일치: 정의는 `mpu6050_i2c_read_block_data`, 호출은 `mpu6050_i2c_read_block` → 통일
- [ ] `mpu6050_i2c_read(struct file *flip, char __user *buf)` 인자 부족 → `size_t count, loff_t *off` 추가 (fops `.read` 시그니처 맞춰야 함), 관련 forward declaration(ioctl 시그니처로 되어있는 것)도 삭제/수정

## 11. mpu6050_i2c.c 코드 리뷰 수정사항 (로직 버그)
- [ ] `MKDEV(MPU6050_I2C_DEV_MAJOR, 0)` (init/exit 둘 다) → `devno`로 교체 (alloc_chrdev_region 쓰기로 했으니 하드코딩 major 안 씀)
- [ ] `cdev_init()`/`cdev_add()` 완전히 빠져있음 → `<linux/cdev.h>` include, `static struct cdev mpu6050_cdev;` 추가, init에서 cdev_init+cdev_add, exit에서 cdev_del
- [ ] `class_create(MPU6050_I2C_DEV_NAME)` → 이 커널(4.9)은 2-인자라 `class_create(THIS_MODULE, MPU6050_I2C_DEV_NAME)`로 수정

## 12. mpu6050_i2c.c 코드 리뷰 수정사항 (사소함)
- [ ] `mpu6050_i2c.h`의 `MPU6050_I2C_MAGIC` — ioctl 안 쓰기로 했으니 미사용, 정리 대상
- [ ] `mpu6050_i2c_read()`의 `//파싱` 자리 — accel/gyro 파싱 로직 아직 미구현
- [ ] `flip` → `filp` 오타로 보임 (동작엔 문제없음)