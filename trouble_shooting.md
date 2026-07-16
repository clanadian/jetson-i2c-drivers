# Jetson Nano I2C 드라이버 트러블슈팅 노트

## 1. 구형 커널(4.9)이라 콜백 시그니처가 다름

- `i2c_driver.probe`: 최신 커널은 1인자(`struct i2c_client *`)인데, 4.9는 **2인자** 구식 시그니처.
  ```c
  int (*probe)(struct i2c_client *, const struct i2c_device_id *);
  ```
- `i2c_driver.remove`: 최신 커널은 `void` 리턴인데, 4.9는 **`int` 리턴**. `void`로 선언하면 구조체 필드 타입 불일치로 컴파일 에러.
  ```c
  int (*remove)(struct i2c_client *);
  ```
- (참고, SPI 프로젝트에서 겪었던 것) `class_create()`도 6.4+ 커널은 1인자(`class_create(name)`)인데 그 이전엔 2인자(`class_create(THIS_MODULE, name)`).

**교훈**: 레퍼런스 코드나 최신 문서를 그대로 베끼지 말고, 실제 타겟 커널 헤더(`linux/i2c.h` 등)에서 구조체 필드 시그니처를 직접 확인해야 함.

## 2. chrdev 등록 방식: `register_chrdev` 대신 `alloc_chrdev_region`

이전 SPI 프로젝트(`spi_dev_v3.c`)는 `register_chrdev(230, name, fops)`처럼 major를 하드코딩하고 마이너 0~255를 통째로 받는 옛날 방식 사용. 이번엔 `alloc_chrdev_region`으로 변경:

```c
static dev_t devno;  // 전역(static) — init에서 채우고 exit에서 씀

// init
ret = alloc_chrdev_region(&devno, 0, 1, MPU6050_I2C_DEV_NAME);
cdev_init(&mpu6050_cdev, &mpu6050_i2c_fops);
cdev_add(&mpu6050_cdev, devno, 1);

// exit — register 계열이 아니라 cdev 계열 해제 함수 사용
cdev_del(&mpu6050_cdev);
unregister_chrdev_region(devno, 1);  // & 없음! (alloc은 &devno, unregister는 devno)
```

**이유**:
- major 번호 직접 골라서 하드코딩하면 그 머신에 이미 그 번호 쓰는 다른 드라이버 있을 시 충돌 위험 (`cat /proc/devices`로 확인 필요했음). alloc은 커널이 빈 번호 알아서 배정.
- 기기가 여러 개(MPU6050/OLED/EEPROM)일 때 매번 수동으로 빈 major 찾는 게 번거로움.
- 유저스페이스는 `/dev/mpu6050` 경로로만 접근하므로 major가 몇 번으로 배정되든 무관 (`device_create()`가 udev한테 알려줘서 자동으로 노드 생성됨).
- **주의**: `register_chrdev`용 해제 함수(`unregister_chrdev`)와 `cdev_add`용 해제 함수(`cdev_del`+`unregister_chrdev_region`)는 서로 안 섞임. 등록 방식에 맞는 해제 함수 짝을 맞춰야 함.

## 3. IntelliSense(VSCode) 헤더 오류는 대부분 무시해도 됨

- `.vscode/c_cpp_properties.json`에 커널 소스 경로(`kernel-4.9/include`, `arch/arm64/include`) 추가함.
- 그래도 `#include errors detected`, `incomplete type` 같은 빨간줄이 계속 뜸. 원인 추적 결과:
  - 실제 커널 빌드는 컴파일러에 `-D__KERNEL__`을 자동으로 넘기는데 IntelliSense는 이게 없어서 `#ifdef __KERNEL__` 블록들이 통째로 빠짐.
  - arm64는 자기 전용 `asm/preempt.h`, `asm/types.h` 같은 파일이 없고 `asm-generic/`으로 연결해주는 **wrapper 파일**이 필요한데, 이건 `make modules_prepare`를 돌려야 생성됨(`include/generated/`). 이 소스 트리는 그 과정을 안 거쳐서 wrapper가 없음.
  - 그래서 `linux/module.h` → ... → `linux/preempt.h` → `asm/preempt.h` 체인에서 파싱이 끊김.
- **결론**: 이 오류들은 에디터(IntelliSense)만의 문제고 실제 빌드(Jetson의 Makefile/KDIR 사용)엔 영향 없음. 굳이 하나씩 땜빵하지 않고 "오타 감지용" 정도로만 활용하기로 함.

## 4. SMBus 블록 리드 함수 이름 조심

- `i2c_smbus_read_block_data()` (X, 쓰면 안 됨): 정식 SMBus 프로토콜. **슬레이브가 길이 바이트를 먼저 보내는** 방식이라, MPU6050처럼 그 프로토콜 없는 기기에 쓰면 첫 데이터 바이트를 길이로 오인식해서 완전히 망가짐.
- `i2c_smbus_read_i2c_block_data(client, command, length, values)` (O): 마스터가 길이를 직접 지정하는 일반 I2C용. MPU6050엔 이걸 써야 함.
- 리턴값은 "읽은 바이트 개수"이지 데이터 자체가 아님. `!= length`로 체크하면 에러(음수)랑 부분 읽기(양수인데 개수 모자람) 둘 다 잡힘 (`< 0`만 체크하는 것보다 안전).

## 5. 커널 코드는 `u8`/`s16`/`s32` 쓰기 (`uint8_t` 아님)

`uint8_t`/`int16_t`는 유저스페이스(`<stdint.h>`) 컨벤션. 커널 모듈에서는 `<linux/types.h>`의 `u8`/`s8`/`u16`/`s16`/`u32`/`s32`를 씀 (커널 함수 시그니처들도 다 이걸 씀).

## 6. 14바이트 버스트 리드 vs accel/gyro 따로 읽기

accel(6B)+temp(2B)+gyro(6B)가 레지스터 0x3B~0x48에 연속으로 붙어있어서, 따로따로 두 번 읽으면 그 사이 시간차만큼 accel/gyro 샘플 시점이 어긋남. `i2c_smbus_read_i2c_block_data(client, 0x3B, 14, buf)` 한 번으로 통째로 읽어서 시간 정합성 확보 + 트랜잭션 수 감소.

## 7. 헤더 파일(.h)에 뭘 넣어야 하는지

`static` 함수/변수는 이 파일 안에서만 쓰이니 `.c`에 남겨도 됨. 헤더에 넣어야 하는 건 **커널 모듈과 유저스페이스 앱이 서로 합의해야 하는 것**(ABI 계약) — 예: `struct mpu6050_data`(파싱된 accel/gyro/temp 값 구조체)는 나중에 유저 앱이 `read()`로 받은 바이트를 올바르게 해석하려면 정확히 같은 레이아웃을 알아야 하므로 헤더에 선언하고 양쪽에서 `#include`.

## 8. probe/remove와 init/exit, open/release의 타이밍 차이

- `module_init`/`module_exit`: `insmod`/`rmmod`에 묶임 (모듈 로드/언로드 시 1회)
- `probe`/`remove`: 디바이스 트리 매칭 시점에 커널이 호출 (보통 init 안에서 드라이버 등록할 때 1회, 앱 실행과 무관)
- `open`/`release`: 유저 앱이 실제로 `open()`/`close()`할 때마다 반복 (앱 실행 횟수만큼)

## 9. (보류) 캐시 + mutex

`read()`마다 매번 새로 I2C 읽어서 바로 파싱+전달하는 지금 구조는 mutex 불필요 (지역 변수만 써서 공유 상태 없음). 전역 캐시(`g_cache`)를 도입하면 그 순간부터 mutex 필요해짐 — 여러 프로세스가 동시에 `read()`만 해도 경쟁 상태 생길 수 있어서. 지금은 1~8단계 범위엔 불필요하다고 판단, 9단계(인터럽트/워크큐) 갈 때 같이 도입하기로 함.

## 10. 디바이스 노드 생성 위치: `module_init`에서 `probe`로 이동

- 기존엔 `module_init()`에서 `alloc_chrdev_region` → `cdev_add` → `device_create()`를 다 실행해서, 하드웨어 연결 여부와 무관하게 `/dev/mpu6050_i2c_dev` 노드가 생성됨.
- 하지만 실제 MPU6050 초기화(WHO_AM_I 확인, PWR_MGMT_1 설정)는 `probe()`에서 일어남 — 센서 미연결/초기화 실패 상태에서도 유저스페이스가 디바이스 파일에 접근 가능했던 게 문제.
- 이 상태에서 `read()`가 호출되면 `g_client == NULL`인 채로 I2C 접근을 시도 → NULL pointer dereference로 커널 Oops 가능.
- **변경**: 캐릭터 디바이스 등록/노드 생성을 `probe()` 내부로 이동. 순서: I2C 디바이스 매칭 → 센서 확인 → 초기화 성공 → `cdev_add()` → `device_create()`. 이제 `/dev/mpu6050_i2c_dev`는 하드웨어가 실제로 연결되고 초기화된 경우에만 생성됨.

**교훈**: 디바이스 노드는 모듈이 로드됐다는 이유만으로 만드는 게 아니라, `probe()`에서 하드웨어 초기화가 성공한 뒤에 만들어야 안전함 — 존재하지 않는 하드웨어로의 접근 자체가 차단되고, 드라이버 생명주기가 실제 하드웨어 상태와 일치하게 됨.

## 11. `file_operations.owner`로 모듈 reference count 수동 관리 제거

- 기존엔 `open()`/`release()`에서 직접 `try_module_get(THIS_MODULE)` / `module_put(THIS_MODULE)`을 호출해서 모듈 참조 카운트를 관리했음. 하지만 이건 커널이 이미 제공하는 VFS 동작과 중복되는 코드.
- `.owner = THIS_MODULE`만 지정하면 VFS가 `open()` 시 자동으로 reference count를 올리고 `close()` 시 자동으로 내려줌 — 수동 관리 불필요.
  ```c
  static const struct file_operations mpu6050_i2c_fops = {
      .owner = THIS_MODULE,
      .open = mpu6050_i2c_open,
      .read = mpu6050_i2c_read,
      .release = mpu6050_i2c_release,
  };
  ```

**교훈**: 커널 프레임워크가 이미 관리하는 리소스는 직접 재구현하지 말고 제공되는 인터페이스(`.owner`)를 활용해야 함.

## 12. `module_i2c_driver()`로 드라이버 등록 보일러플레이트 제거

- 기존엔 `module_init`/`module_exit`을 직접 작성하고 그 안에서 `i2c_add_driver()`/`i2c_del_driver()`를 호출. 단순 등록/해제 코드가 반복되고, 하드웨어 초기화 코드와 모듈 로딩 코드가 섞여서 구조가 복잡해짐.
  ```c
  static int __init mpu6050_i2c_init(void)
  {
      return i2c_add_driver(&mpu6050_driver);
  }

  static void __exit mpu6050_i2c_exit(void)
  {
      i2c_del_driver(&mpu6050_driver);
  }

  module_init(mpu6050_i2c_init);
  module_exit(mpu6050_i2c_exit);
  ```
- 위 여섯 줄을 아래 한 줄로 대체 가능:
  ```c
  module_i2c_driver(mpu6050_driver);
  ```
- 하드웨어 작업은 `probe`/`remove`, 모듈 등록 관리는 `module_i2c_driver()`로 역할이 분리됨.

**교훈**: 커널이 제공하는 subsystem helper 매크로를 쓰면 직접 관리해야 하는 코드와 실수 가능성이 줄어듦.