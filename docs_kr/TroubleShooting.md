# 트러블슈팅

## 목차
- [흔한](#common)  
- [보드별](#board-specific)

<a id="common"></a>
## 공통
빌드 환경을 수동으로 설정하는 동안 아래 문제가 발생합니다.

### Kconfig-프런트엔드의 문제
#### hconf에서 빌드 중단
gperf 3.1 및 kconfig-frontends-4.11.0.1을 사용하는 경우,
Kconfig-frontend 빌드(만들기) 시간에 아래에서 만날 수 있습니다.
```
~/kconfig-frontends-4.11.0.1$ make
.
.
.
In file included from libs/parser/yconf.c:252:0:
libs/parser/hconf.gperf:153:1: error: conflicting types for ‘kconf_id_lookup’
libs/parser/hconf.gperf:12:31: note: previous declaration of ‘kconf_id_lookup’ was here
 static const struct kconf_id *kconf_id_lookup(register const char *str, register GPERF_LEN_TYPE len);
                               ^~~~~~~~~~~~~~~
Makefile:1404: recipe for target 'libs/parser/libs_parser_libkconfig_parser_la-yconf.lo' failed
make[1]: *** [libs/parser/libs_parser_libkconfig_parser_la-yconf.lo] Error 1
```

해결 방법:
`kconfig-frontends-4.11.0.1/libs/parser/hconf.c`를 아래와 같이 수정하세요.
```diff
--- a/libs/parser/hconf.c
+++ b/libs/parser/hconf.c
@@ -172,7 +172,7 @@ __attribute__ ((__gnu_inline__))
 #endif
 #endif
 const struct kconf_id *
-kconf_id_lookup (register const char *str, register unsigned int len)
+kconf_id_lookup (register const char *str, register GPERF_LEN_TYPE len)
 {
   enum
     {
```

#### mconf의 실행 오류
Kconfig-frontend를 설치한 후 ```make menuconfig```가 실행되면 누군가 아래를 충족합니다.
```
kconfig-mconf: error while loading shared libraries: libkconfig-parser-x.xx.0.so: cannot open shared object file: No such file or directory
Makefile.unix:579: recipe for target 'menuconfig' failed
make: *** [menuconfig] Error 127
```
해결 방법:
```
cd <Kconfig-frontend_package_PATH>
./configure --prefix=/usr
make
sudo make install
```

### 툴체인 관련 문제
```make ```가 실행되면 아래 오류가 발생할 수 있습니다.
```
arm-none-eabi-gcc: Command not found
```
툴체인 경로가 ```PATH```에 추가되지 않은 경우 이 문제가 발생할 수 있습니다.  
해결 방법:
```
export PATH=<Your Toolchain PATH>:$PATH
```
도구 체인 경로가 ```PATH```에 추가된 경우 이는 도구 체인이 
세트는 64비트 시스템에서 32비트 라이브러리를 사용합니다. 이 문제를 위해 아래 툴체인을 설치하십시오. 대안으로 
.  
해결 방법:
```
> $ sudo apt-get install -y gcc-arm-none-eabi
```

### Proto 버퍼 빌드 관련 문제
gRPC 사용 시 필수인 Proto 버퍼(a.k.a., protobuf)가 활성화되면, 
아래와 같이 빌드 중단을 만날 수 있습니다.
```
AR: helloxx_main.o
make[2]: Leaving directory '/TizenRT/apps/examples/helloxx'
make[2]: Entering directory '/TizenRT/apps/examples/grpc_greeter_client'
protoc -I . --cpp_out=. helloworld.proto
make[2]: protoc: Command not found
Makefile:86: recipe for target 'helloworld.pb.cc' failed
make[2]: *** [helloworld.pb.cc] Error 127
make[2]: Leaving directory '/TizenRT/apps/examples/grpc_greeter_client'
Makefile:109: recipe for target 'examples/grpc_greeter_client_all' failed
make[1]: *** [examples/grpc_greeter_client_all] Error 2
make[1]: Leaving directory '/TizenRT/apps'
LibTargets.mk:158: recipe for target '../apps/libapps.a' failed
make: *** [../apps/libapps.a] Error 2
```
이는 ```protoc``` 설치가 누락되어 발생합니다.  
[프로토 버퍼의 README](https://github.com/Samsung/TizenRT/blob/master/external/protobuf/README.md)를 찾아주세요.

<a id="board-specific"></a>
## 보드별
### ARTIK
#### 프로그래밍 관련 문제
USB 연결이 설정되지 않으면 누군가 다음을 충족합니다.
```
[Command] make download ALL
Generating partition map ... Done
Open On-Chip Debugger 0.10.0-dirty (2017-09-02-08:32)
Licensed under GNU GPL v2
For bug reports, read
http://openocd.org/doc/doxygen/bugs.html
adapter speed: 2000 kHz
Info : auto-selecting first available session transport "jtag". To override use 'transport select '.
force hard breakpoints
trst_and_srst separate srst_gates_jtag trst_push_pull srst_push_pull connect_deassert_srst
adapter_nsrst_assert_width: 50
adapter_nsrst_delay: 100
debug_level: -1

Makefile.unix:543: recipe for target 'download' failed
make: *** [download] Error 1
[STATUS] FAILED
```

해결하려면,  
1. [USB 장치 규칙](https://github.com/Samsung/TizenRT/blob/master/build/configs/artik053/README.md#add-usb-device-rules)를 참조하세요.
2. USB 케이블을 변경하거나 다시 연결하고 USB 연결을 확인하세요.
