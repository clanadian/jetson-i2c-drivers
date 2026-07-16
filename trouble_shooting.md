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

## 13. AT24C256 write 후 대기: `msleep(5)` 대신 ACK 폴링

- EEPROM은 write 커맨드(I2C 트랜잭션 자체)가 끝나도, 실제로 셀에 데이터를 기록하는 내부 write cycle이 그 뒤에 이어짐 (AT24C256 데이터시트 tWR, 최대 5ms). 이 동안 EEPROM은 자기 슬레이브 주소로 오는 요청에 ACK을 안 해줌.
- `msleep(5)`로 무조건 최대치만큼 재우는 방법도 고려했지만, 대부분의 경우 5ms보다 일찍 끝나는데도 매번 최악의 경우만큼 기다리는 게 비효율적이라 판단해서, **ACK 폴링** 방식을 선택함.
  ```c
  static int at24c256_wait_ready(void)
  {
      unsigned long timeout = jiffies + msecs_to_jiffies(AT24C256_WRITE_TIMEOUT_MS);

      do {
          if (i2c_master_send(g_client, NULL, 0) >= 0)
              return 0;
      } while (time_before(jiffies, timeout));

      return -EIO;
  }
  ```
- 데이터 없는 더미 write를 반복 시도해서, EEPROM이 ACK 해주는 순간(= write cycle 끝남) 바로 감지. 5ms는 더 이상 기다리지 않는 타임아웃 상한으로만 사용.
- **참고**: AT24C256은 ready/interrupt 핀이 물리적으로 없는 칩(VCC/GND/SDA/SCL/WP/A0~A2뿐)이라, 이 ACK 폴링은 인터럽트를 흉내내는 게 아니라 진짜 폴링임. MPU6050처럼 INT 핀이 있는 칩만 진짜 인터럽트 기반 감지가 가능함.
- **참고**: `msleep(5)`를 썼더라도 그 동안 I2C 버스 자체가 잠기는 건 아님 — `i2c_master_send()`는 트랜잭션(START~STOP) 하는 순간에만 어댑터 락을 잡고 끝나자마자 바로 풀어줌. `msleep`은 그 락이 이미 풀린 뒤 드라이버 코드 안에서 우리 프로세스만 재우는 것이므로, 그 사이 다른 기기(MPU6050, SSD1306 등)는 버스를 자유롭게 씀. 영향받는 건 "이 EEPROM 자체"뿐 — write cycle 중이라 자기 주소로 오는 요청에 ACK을 안 해줄 뿐임.

**교훈**: 하드웨어에 상태를 알려줄 신호선이 없으면, 아무리 "효율적으로 짜도" 결국 폴링일 수밖에 없음 — 폴링 자체를 없앨 수는 없고, "얼마나 빨리 감지하느냐"만 개선 가능.

## 14. AT24C256 read: `i2c_smbus_*` 대신 `i2c_transfer()` + `struct i2c_msg` 직접 구성

- MPU6050(4번 항목)은 레지스터 주소가 1바이트라 `i2c_smbus_read_i2c_block_data(client, command, length, buf)` 한 줄로 끝났음 — 이 함수는 내부적으로 메시지 구성과 트랜잭션 처리를 다 알아서 해줌.
- AT24C256은 내부 메모리 주소가 **2바이트**(32KB 주소공간이라 1바이트로는 표현이 안 됨)라서, SMBus 계열 함수가 지원하는 "command 1바이트" 형식 자체에 안 맞음. `i2c_smbus_*` 함수들은 정해진 프로토콜 모양(1바이트 command + 데이터)만 지원하므로, "2바이트 주소를 먼저 보내고 반복 START로 이어서 읽는" 트랜잭션은 이 함수들로 표현할 방법이 없음.
- 그래서 한 단계 아래 레이어인 `i2c_transfer()` + `struct i2c_msg` 배열을 직접 구성해야 함:
  ```c
  struct i2c_msg msgs[2] = {
      { .addr = g_client->addr, .flags = 0,        .len = 2,     .buf = addr }, // 주소 write
      { .addr = g_client->addr, .flags = I2C_M_RD, .len = count, .buf = kbuf }, // 반복 START 후 read
  };
  i2c_transfer(g_client->adapter, msgs, 2);
  ```
- `i2c_smbus_*` 함수들도 사실 내부적으로는 이 `i2c_transfer()`/`i2c_msg` 위에 구현된 편의 래퍼일 뿐임. 그 래퍼가 지원하지 않는 모양의 트랜잭션이 필요하면, 래퍼를 억지로 맞춰 쓰려 하지 말고 한 단계 내려가서 메시지 배열을 직접 짜야 함.
- 직접 짜는 대신 잃는 것: 메시지 배열 구성뿐 아니라 데이터 버퍼(`kzalloc`/`kfree`)까지 전부 우리가 책임져야 함 — SMBus 헬퍼처럼 그런 부분을 대신 처리해주는 게 없음.

**교훈**: 디바이스의 주소/프로토콜 형태가 SMBus 표준 패턴(1바이트 command)에 맞으면 `i2c_smbus_*`로 간단히 끝나지만, 안 맞으면(다바이트 주소 등) `i2c_transfer()` + `i2c_msg`로 내려가서 메시지 구성과 버퍼 관리를 직접 해야 함.

## 15. 유저가 넘긴 `count`로 커널 스택에 VLA 잡으면 안 됨

- SSD1306 write() 초안에서 `u8 kbuf[count + 1];`처럼 짰다가 걸러냄. `count`는 유저가 `write()` 호출 시 마음대로 넣는 값이라, 이론상 아주 큰 값을 넣으면 그만큼의 배열을 스택 위에 잡으려다 스택 오버플로우가 남 (커널 스택은 보통 8~16KB로 매우 작음).
- 리눅스 커널은 이 위험 때문에 2016년경 "kill the VLAs" 작업으로 커널 전체에서 VLA를 걷어낸 이력이 있음 — 스타일 문제가 아니라 실제로 위험한 패턴.
- 해결: 상한값(`SSD1306_MAX_LEN`, `AT24C256_PAGE_SIZE` 등) 먼저 검증하고, 스택 대신 `kzalloc`/`kfree`로 힙에 할당.
  ```c
  if (count > SSD1306_MAX_LEN)
      return -EINVAL;
  kbuf = kzalloc(count + 1, GFP_KERNEL);
  ```

**교훈**: 함수 인자로 들어온 값(특히 유저스페이스에서 온 `size_t`)을 스택 배열 크기로 그대로 쓰면 안 됨 — 반드시 상한 체크 후 힙 할당.

## 16. file_operations 슬롯과 인자 개수가 안 맞는 커널 헬퍼는 래퍼로 감싸야 함

- `.llseek` 슬롯은 `loff_t (*)(struct file *, loff_t, int)` 딱 3개 인자만 받는 함수 포인터 타입. 근데 커널이 제공하는 `fixed_size_llseek(file, offset, whence, size)`는 4개 인자(범위 상한 `size`까지) 필요해서 타입이 안 맞아 `.llseek`에 직접 대입 불가 (컴파일 에러).
- C는 인자 일부를 미리 고정해서 인자 개수가 적은 새 함수를 만드는 기능(다른 언어의 부분적용/커링)이 없음 — 이름을 뭘로 붙이든 인자 개수 자체가 안 맞으면 소용없음.
- 해결: 딱 3개 인자짜리 래퍼 함수를 만들어서 그 안에서 4번째 인자(우리 디바이스만 아는 값)를 직접 채워 호출.
  ```c
  static loff_t ssd1306_lseek(struct file *filp, loff_t offset, int whence){
      return fixed_size_llseek(filp, offset, whence, 168);
  }
  ```

**교훈**: 커널 헬퍼 함수를 쓰고 싶은데 `file_operations` 슬롯 시그니처랑 인자 개수가 안 맞으면, 슬롯 시그니처에 맞춘 래퍼 함수를 만들어 그 안에서 감싸 호출.

## 17. write() 성공 후 `*ppos` 전진은 커널이 자동으로 안 해줌

- POSIX 관례상 write() 성공하면 파일 위치가 쓴 만큼 전진해야 다음 write()가 이어서 써지는데, 이건 VFS가 알아서 해주는 게 아니라 **드라이버가 직접 `*ppos`를 갱신**해야 함.
  ```c
  *ppos += count / 6;      // SSD1306: 바이트 수 → 글자칸 수로 환산
  if (*ppos > 167)
      *ppos = 167;          // 유효 범위(0~167) 밖으로 못 나가게 클램프
  ```
- 범위 체크도 직접 해야 함: `lseek()`은 `fixed_size_llseek`이 알아서 범위를 막아주지만, write() 안에서 `*ppos +=`로 직접 전진시키는 건 그 안전장치를 거치지 않음. 클램프 안 하면 다음 write() 때 존재하지 않는 페이지 번호가 계산되어 잘못된 커맨드가 나감.

**교훈**: `*ppos` 갱신은 드라이버 책임 — 갱신 후 유효 범위를 벗어나지 않는지 별도로 체크해야 함.

## 18. ioctl은 "명령", read/write는 "데이터" — 채널을 섞지 않음

- SSD1306 화면 전체 지우기("clear") 기능을 만들 때, write()에 매직 문자열을 넣어서 명령으로 해석하게 만드는 방법도 가능은 했지만, write()/read()는 애초에 "위치에 데이터를 흘려보내는" 용도지 "이 동작을 수행해라"는 명령 개념이 없음.
- 그래서 명령은 `.unlocked_ioctl`로 분리:
  ```c
  #define SSD1306_IOC_CLEAR  _IO('S', 1)
  ```
  앱은 `ioctl(fd, SSD1306_IOC_CLEAR)`로 호출 — 문자열이 아니라 컴파일타임에 확정된 정수 명령 코드가 오가는 방식.

**교훈**: "데이터를 옮기는 것"과 "동작을 지시하는 것"은 다른 통로(read/write vs ioctl)를 쓰는 게 커널 관례.