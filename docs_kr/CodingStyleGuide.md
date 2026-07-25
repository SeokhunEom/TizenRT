# 개요

통합 코딩 스타일을 사용하면 TizenRT를 쉽게 유지 관리할 수 있습니다. 플랫폼 개발자는 이 문서에 설명된 코딩 스타일을 따라야 합니다. 선호하는 스타일은 BSD/Allman. **[Mxx]**입니다. 필수 **[Rxx]**: 권장

# C 코딩 스타일

## 댓글

**[R01]** 두 가지 주석 스타일이 모두 허용됩니다. C89 "/* … */" 스타일 및 C99 “//” 스타일입니다.

[예제-Do]
```c
/*
    test example
    write();
    read();
*/

//test example
//write();
//read();
```
## 도움말

코드를 더 쉽게 읽을 수 있도록 함수 구분 기호(80자)를 삽입합니다.
```c
//------------------------------------------------------------------------------

int func1()
{
;
}

//------------------------------------------------------------------------------

int func2()
{
;
}

//------------------------------------------------------------------------------
```
## 들여쓰기

**[M01]** 탭을 사용하세요. 모든 탭은 4자입니다. 들여쓰기 전용 탭(스페이스 키 없음)입니다.
```
 •|---means tab (with 4 spaces option)
 •|-------means tab (with 8 spaces option)
 •^means one char of space
```
[예제-Do]
```c
static int config_init(void)
{
|---int index;
|---/* create configfile */
|---entry = create(“abcdefg", NULL, NULL, NULL, NULL,
|---|---|---^^NULL, NULL);
|---if (!entry) {
|---|---return 0;
|---}
|---entry->size = 0;
|---return 0;
}
```
[예-하지 마세요]
```c
static int__init ikconfig_init(void)
{
|-------int index;
/* create configfile */
^^^^^^^entry = create(“abcdefg", NULL, NULL, NULL, NULL,
^^^^^^^^^^^^^^NULL, NULL);
^^if (!entry)
|-------return 0;
^^entry->size = 0;
^^return 0;
}
```

<br>

**[M02]** switch 문에서 'case'는 'switch'와 동일한 들여쓰기를 가지며 새 줄에 배치됩니다.

[예제-Do]
```c
switch (state) {
case START:
    break;
case RUNNING:
    break;
case STOP:
    break;
default:
    break;
}
```
[예-하지 마세요]
```c
switch (state) {
    case START:
        break;
    case RUNNING:
        break;
    case STOP:
        break;
    default:
        break;
}
```

<br>

**[R03]** goto 라벨은 들여쓰기되지 않지만 단일 공백은 허용됩니다.

[예제-Do]
```c
    if (value != value2) {
        goto out;
    }

…
out:
    return 0;
```
[예-하지 마세요]
```c
    if (value != value2) {
        goto out;
    }

…
    out:
    return 0;
```
## Space

**[M03]** 키워드에는 다음과 같은 공백 규칙이 있습니다.

- (대부분의) 키워드 뒤에 공백을 넣으십시오.

    - 예: if, 스위치, 대소문자, for, do, while

- 키워드처럼 함수 뒤에 공백을 넣지 마세요.

    - 예: sizeof, typeof, alignof, __attribute__

- ^은 공간 1문자를 의미합니다.

[예제-Do]
```c
if^(!value) {
    result = 0;
…
for^(;;) {
    i = j;
…
while^(!value) {
    i++
…
printf(“test%d”, sizeof(value));
```
[예-하지 마세요]
```c
if(!value) {
    result = 0;
…
for(;;) {
    i = j;
…
while(!value) {
    i++
…
printf(“test%d”, sizeof^(value));
```

<br>

**[M04]** 운영자는 다음과 같은 공간 규칙을 따릅니다.

- 대부분의 이항 및 삼항 연산자 주위(양쪽에) 공백을 넣으세요.

    - 예: = + -< > * / % | & ^ <= >= == != ? :

- 단항 연산자 뒤에 공백을 넣지 마십시오.

    - 예: & * + -~ !

- 단항 "++" 및 단항 "--"는 한쪽에 공백이 허용되지 않습니다.

- "." 주위에 공백을 넣지 마십시오. 및 "->" 구조 멤버 연산자

- 캐스트 연산자 뒤에 공백을 넣지 마십시오.

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```c
bool value^=^true;
function(&people->hand);
people->age++;
struct exlink^*link;
struct exlink^*saved_link;
read(&link->left);
tmp += (unsigned long)test[i];
```
[예-하지 마세요]
```c
bool value=true;
function(&^people->hand);
people->age^++;
struct exlink^*^link;
struct exlink^*^saved_link;
read(&link^->^left);
tmp += (unsigned long)^test[i];
```

<br>

**[M05]** 구분 기호에는 다음과 같은 공간 규칙이 있습니다.

- 줄에 내용이 있는 경우 중괄호를 닫은 뒤에 공백을 넣으세요.

    - 예외 : 닫는 중괄호 '},' 뒤의 쉼표

- 쉼표 뒤에 공백을 넣으세요.

- 괄호 '(', ')' 안에 공백을 넣지 마십시오.

- 여는 대괄호 '[' 앞과 대괄호 '[', ']' 내부에 공백을 넣지 마십시오.

- 함수 호출에서 함수 이름 뒤에 공백을 넣지 마십시오.

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```c
struct exlink[10];
read(&link->left, &link->right);
if (value > 0) {
    ;
}^else if (value == 0) {
    ;
}^else {
    ;
}

if (function()) {
    read(1);
    write(&people->hand);
}	
```
[예-하지 마세요]
```c
struct exlink^[^10^];
read(^&link->left^, &link->right^);
if (value > 0) {
    ;
}else if (value == 0) {
    ;
}else {
    ;
}

if (function^()) {
    read^(1);
    write^(&people->hand);
}
```
## Brace

**[M06]** 함수에는 다음 줄 시작 부분에 여는 중괄호가 있습니다.

[예제-Do]
```c
int function(void)
{
    ;
} 
```
[예-하지 마세요]
```c
int function(void) {
    ;
}
```

<br>

**[M07]** 라인의 마지막 개방형 버팀대입니다. 닫는 중괄호는 한 줄만 비어 있습니다.

- 예외 : else, while은 닫는 중괄호 '}'를 따라야 합니다.

[예제-Do]
```c
if (value > 0) {
    ;
} else if (value == 0) {
    ;
}
…..
do {
    i = j;
} while (result);
```
[예-하지 마세요]
```c
if (value > 0)
{	
    ;
} 
else if (value == 0)
{
    ;
}
…..
do
{
    i = j;
} while (result);
```
**[M08]** 열거형, 공용체, 구조체의 여는 중괄호가 같은 줄에 표시됩니다.

[예제-Do]
```c
struct people {
    string name;
    int age;
};
```
[예-하지 마세요]
```c
struct people 
{
    string name;
    int age;
};
```

<br>

~~**[R04]** 단일 문으로 충분할 경우 불필요하게 중괄호를 사용하지 마세요.~~

## 라인

**[R05]** 줄 끝 부분에 후행 공백을 넣지 마세요.

**[R06]** 파일 끝에 줄 바꿈 없이 줄을 추가하는지 확인하세요.

- [EOF]는 파일 끝을 의미합니다.

- ^은 공간 1문자를 의미합니다.

[예제-Do]
```c
void function(void)
{
…
    printf(“test\n”);
…
}
EXPORT_SYMBOL(log);
[EOF]
```
[예-하지 마세요]
```c
void function(void)
{
…
    printf(“test\n”);^^^^^^^^^^
…
}
EXPORT_SYMBOL(log);[EOF]
```
## 파일 구성

**[R07]** 코드 구성의 순서입니다.

- 저작권

- 파일 코멘트

- 헤더 파일 포함

- 상수 및 매크로 정의

- 내보낸(공용) 기능 구현

- 내부(보호되는) 기능 구현

[예제-Do]
```c
/**
 * @copyright
 * In XYZ R&D Center (XYZ under a contract between)
 * @par
 * LLC "XYZ", Ltd (X3, ... of ...)
 * @par
 * Copyright (c) XYZ Ltd 2018. All rights reserved.
 */
/**
 * @file [package/]src/functionname.c
 * @author Xyan Pedro <xy.po@tizen.org>
 * @version 42
 * @date Created Mon, 4 Jul 2016 17:14:00 +0300
 * @date Modified Fri, 17 Feb 2017 12:43:12 +0200
 */

#include "headerfile.h"
..
#define MAGIC_START 	“START"
..
int public_function(void)
{
..
}
..
static int protectedfuction(void)
{
...
}
```
날짜와 버전은 다음을 통해 얻을 수 있습니다: git log -1 --format=format:%aD $file

**[M09]** 외부 공개 헤더(API)와 내부 헤더가 분리되어 있습니다.

|유형|전화로||에서 정의됨
|---|---|---|
|내보낸(공개) 함수|외부 모듈|외부 공개 헤더|
|내부(보호된) 기능|모듈 내부|내부 헤더|
|정적(비공개) 함수|동일 파일의 함수|소스 파일|

~~**[M10]** 외부 공개 헤더에는 doxygen 스타일 주석이 있습니다.~~

## 선언/Definition

**[R02]** 인라인 키워드는 저장소 클래스와 유형 사이에 있어야 합니다.

[예제-Do]
```c
static inline int example_function(void)
{
    return (0);
}
```
[예-하지 마세요]
```c
inline static int example_function(void)
{
    return (0);
}
```
**[M11]** 함수 프로토타입에는 데이터 유형 및 반환 유형과 함께 매개변수 이름을 포함합니다.

[예제-Do]
```c
extern int function(struct type *ex);
```
[예-하지 마세요]
```c
extern int function(struct type *);
```
**[R08]** 여러 문이 포함된 매크로는 'do-while' 블록으로 묶어야 합니다.

**[R09]** 매크로의 '#' 기호는 첫 번째 열에 위치해야 합니다.

**[R10]** 매크로 정의에서 모든 표현식과 표현식의 인수는 각각 '(' 및 ')'로 묶어야 합니다.

[예제-Do]
```c
#define example_kiocb(x, filp)              \
    do                                      \
    {                                       \
        struct task_struct*tsk= current;    \
        (x)->ki_flags= 0;                   \
        (x)->ki_users= 1;                   \
        (x)->ki_key= 3;                     \
        (x)->ki_filp= (filp);               \
        (x)->ki_ctx= NULL;                  \
     } while (0)
```
[예-하지 마세요]
```c
#define example_kiocb(x, filp)              \
    {                                       \
        struct task_struct*tsk= current;    \
        x->ki_flags= 0;                     \
        x->ki_users= 1;                     \
        x->ki_key= 3;                       \
        x->ki_filp= filp;                   \
        x->ki_ctx= NULL;                    \
    }
```
**[R11]** 동일한 선언에서 구조 태그와 변수 또는 typedef를 모두 선언하지 마세요.

[예제-Do]
```c
struct example {
    int lock;
    const char *device_id;
};
static struct example example_busy[CHANNELS] = {
    [4] = { 1, "cascade" },
};
```
[예-하지 마세요]
```c
struct example{
    int lock;
    const char *device_id;
}example_busy[CHANNELS] = {
    [4] = { 1, "cascade" },
};

typedef struct example_t{
    int lock;
    const char *device_id;
}example;
```
**[R12]** 각 변수는 새 줄에 선언되어야 합니다.

[예제-Do]
```c
void example(void)
{
    int i;
    int index;
```
[예-하지 마세요]
```c
void example(void)
{
    int i, index;
```
## 이름 지정

**[M12]** 대소문자를 혼합할 수 없습니다. 이름에서 단어를 구분하려면 밑줄('_')을 사용하세요.

[예제-Do]
```c
#define TEST_VALUE	32
extern struct list_head_hash[TEST_VALUE];

int example_function(void)
{
    return(TEST_VALUE - 1);
}
```
[예-하지 마세요]
```c
#define Test_Value	32
extern struct list_head_hash[Test_Value];

int ExampleFunction(void)
{
    return(Test_Value - 1);
}
```
**[R13]** 열거형의 상수 및 라벨을 정의하는 매크로 이름은 대문자, 숫자, '_' 문자로 구성됩니다.

[예제-Do]
```c
#define MAGIC_START     “START"
#define MAGIC_END       “END“

#define MAGIC_SIZE(sizeof(MAGIC_START) -1)
```
[예-하지 마세요]
```c
#define MagicStart      “START"
#define magic_end       “END“

#define magic_size(sizeof(MagicStart) -1)
```
**[R14]** 기능 이름은 동사 + 명사로 기능을 잘 표현합니다.

**[R15]** 범위가 현재 소스 파일로 제한된 로컬 함수에는 '정적' 키워드와 '__'로 시작하는 이름이 있습니다.

**[R16]** 내부 헤더의 보호(전역) 함수 이름은 '_' 문자로 시작됩니다.

[예제-Do]
```c
EXPORT_API void datacall_prepare_call(void);
static boolean __call_dc_outgoingcall_endhandle(void);
```
[예-하지 마세요]
```c
EXPORT_API void datacall_call_prepare(void);
static boolean call_dc_outgoingcall_endhandle(void);
```

# C++ 코딩 스타일

## 가드 정의

**[M01]** 가드 기호 형식은 다음과 같아야 합니다. __<PROJECT>_<FILE>_H__.

- __<PROJECT> 없이 고유성을 보장할 수 있는 경우 생략할 수 있습니다.

[예제-Do]
```cpp
// MySample.h
#ifndef __TEST_MY_SAMPLE_H__
#define __TEST_MY_SAMPLE_H__
class MySample
{
    // Do something...
};
#endif /* __TEST_MY_SAMPLE_H__ */
Or
 
// MySample.h
#ifndef __MY_SAMPLE_H__
#define __MY_SAMPLE_H__
  
class MySample
{ 
    // Do something...
};
#endif /* __MY_SAMPLE_H__ */
```
## 이름 지정

### 파일 이름

**[R01]** 파일 이름은 클래스 이름과 동일해야 합니다. 고유성을 위해 네임스페이스의 약어를 접두사로 추가할 수 있습니다. (언서스코어 없음, 낙타 표기법 없음)

[예제-Do]
```cpp
// Sample.h
class Sample
{
    ...
};

Or

// ViTest.h
namespace visual
{
class Test
{
...
};
}
```
### 네임스페이스 이름

**[M02]** 모두 소문자여야 하며 중간에 밑줄을 포함할 수 있습니다.

- 예: app_assist

### 유형 이름

**[M03]** 클래스 이름, 구조 이름, 열거형 이름입니다. 대문자, 낙타 표기법으로 시작해야 하며 밑줄은 사용할 수 없습니다.

### 변수 이름

**[M04]** 소문자로 시작해야 합니다.

### 기능 이름

**[M05]** 이름은 소문자, 카멜 표기법으로 시작해야 합니다. '동사' + '명사' 스타일을 유지하세요.

  
- 정규 함수

    - global - 소문자로 시작하고 모든 문자는 소문자여야 합니다. 밑줄을 삽입할 수 있습니다. (C 코딩 규칙과 동일)

[예제-Do]
```cpp
const char* test_get_name(void);
```
- static - 접두어로 두 개의 밑줄이 있어야 합니다.

[예제-Do]
```cpp
static int __aaa_bbb(); 
```
- 멤버 함수

    - 소문자로 시작해야 하며 각 새 단어에는 대문자가 있어야 합니다. 밑줄이 없습니다.

[예제-Do]
```cpp
int createSomething();
```
### Member 변수 이름

**[M06]** 두 개의 밑줄이 있어야 하며 그 뒤에 소문자도 있어야 합니다. 낙타 표기법이며 이름 중간에 밑줄이 없습니다.

- Exception : 구조 멤버 변수는 밑줄 없이 소문자로 시작해야 합니다.

[예제-Do]
```cpp
private:
    int __aaaaBbbbb;
```
### 상수 이름

**[M07]** 모든 문자는 대문자여야 합니다. 밑줄은 허용됩니다.

[예제-Do]
```cpp
// header file in class (public)
public:
    static const int TEST_MAX = 10; 
// header file in class (private)
private:
    static const int __TEST_MAX = 10;
// cpp file
static const int __TEST_MAX = 10;
```
## 들여쓰기

**[M08]** 탭을 사용하세요. 모든 탭은 4자입니다. 들여쓰기 전용 탭(스페이스 키 없음)입니다.

**[M09]** 스위치와 케이스는 동일한 들여쓰기에 있어야 합니다.

[예제-Do]
```cpp
switch (...) {
case 1:
    break;
case 2:
    break;
default:
    break;
};
```
**[R02]** 생성자 이니셜라이저는 모두 한 줄에 있을 수도 있고 들여쓰기 한 번으로 후속 줄에 올 수도 있습니다.

[예제-Do]
```cpp
// Point.cpp
 
#include "Point.h"
 
Point::Point() :
    __x(0),
    __y(0)
{
    // TO DO...
}
```
## Space

**[M10]** 키워드에는 다음과 같은 공백 규칙이 있습니다.

- (대부분의) 키워드 뒤에 공백을 넣으십시오.

    - 예: if, 스위치, 대소문자, for, do, while

- 키워드처럼 함수 뒤에 공백을 넣지 마세요.

    - 예: sizeof, typeof, alignof, __attribute__

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```cpp
if^(!value) {
    result = 0;
…
for^(;;) {
    i = j;
…
while^(!value) {
    i++
…
printf(“test%d”, sizeof(value));
```
[예-하지 마세요]
```cpp
if(!value) {
    result = 0;
…
for(;;) {
    i = j;
…
while(!value) {
    i++
…
printf(“test%d”, sizeof^(value));
```
**[M11]** 운영자는 다음과 같은 공간 규칙을 따릅니다.

- 대부분의 이진 및 삼항 연산자 주위(양쪽에) 공백을 넣으세요.

    - 예: = + -< > * / % | & ^ <= >= == != ? :

- 단항 연산자 뒤에 공백을 넣지 마십시오.

    - 예: & * + -~ !

- 단항 "++" 및 단항 "--"에는 한쪽에 공백이 허용되지 않습니다.

- "." 주위에 공백을 넣지 마십시오. 및 "->" 구조 멤버 연산자

- 캐스트 연산자 뒤에 공백을 넣지 마십시오.

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```cpp
bool value^=^true;
function(&people->hand);
people->age++;
struct exlink^*link;
strcut exlink^*saved_link;
read(&link->left);
tmp += (unsigned long)test[i];
```
[예-하지 마세요]
```cpp
bool value=true;
function(&^people->hand);
people->age^++;
struct exlink^*^link;
strcut exlink^*^saved_link;
read(&link^->^left);
tmp += (unsigned long)^test[i];
```
**[M12]** 구분 기호에는 다음과 같은 공간 규칙이 있습니다.

- 줄에 내용이 있는 경우 중괄호를 닫은 뒤에 공백을 넣으세요.

    - 예외: 닫는 중괄호 '},' 뒤의 쉼표

- 쉼표 뒤에 공백을 넣으세요.

- 괄호 '(', ')' 안에 공백을 넣지 마십시오.

- 여는 대괄호 '[' 앞과 대괄호 '[', ']' 내부에 공백을 넣지 마십시오.

- 함수 호출에서 함수 이름 뒤에 공백을 넣지 마십시오.

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```cpp
struct exlink[10];
read(&link->left, &link->right);
if (value > 0) {
    ;
}^else if (value == 0) {
    ;
}^else {
    ;
}

if (function()) {
    read(1);
    write(&people->hand);
}	
```
[예-하지 마세요]
```cpp
struct exlink^[^10^];
read(^&link->left^, &link->right^);
if (value > 0) {
    ;
}else if (value == 0) {
    ;
}else {
    ;
}

if (function^()) {
    read^(1);
    write^(&people->hand);
}
```
**[M13]** 포인터 표현식 : 별표는 유형 또는 변수 이름 옆에 배치할 수 있습니다.

[예제-Do]
```cpp
int *a;
int* b; // preferred by BSD style
```
## 라인

**[R03]** 줄 끝 부분에 후행 공백을 넣지 마세요.

- ^는 공간 1문자를 의미합니다.

[예제-Do]
```cpp
void function(void)
{
…
    printf(“test\n”);
…
}
EXPORT_SYMBOL(log);
```
[예-하지 마세요]
```cpp
void function(void)
{
…
    printf(“test\n”);^^^^^^^^^^
…
}
EXPORT_SYMBOL(log);
```
## Brace

**[M14]** 함수에는 다음 줄 시작 부분에 여는 중괄호가 있습니다.

[예제-Do]
```cpp
int function(void)
{
    ;
} 
```
[예-하지 마세요]
```cpp
int function(void) {
    ;
}
```
**[M15]** 라인의 마지막 개방형 버팀대입니다. 닫는 중괄호는 한 줄만 비어 있습니다.

- 예외 : else, while은 닫는 중괄호 '}'를 따라야 합니다.

[예제-Do]
```cpp
if (value > 0) {
    ;
} else if (value == 0) {
    ;
}
…..
do {
    i = j;
} while (result);
```
[예-하지 마세요]
```cpp
if (value > 0)
{	
    ;
} 
else if (value == 0)
{
    ;
}
…..
do 
{
    i = j;
} while (result);
```
**[M16]** 열거형, 공용체, 구조체의 여는 중괄호는 같은 줄에 있습니다.

[예제-Do]
```cpp
struct people {
    string name;
    int age;
};
```
[예-하지 마세요]
```cpp
struct people
{
    string name;
    int age;
};
```
~~**[R04]** 단일 문으로 충분할 경우 불필요하게 중괄호를 사용하지 마세요.~~

## 주문

### 기능 매개변수

**[R05]** 함수를 정의할 때 매개변수 순서는 입력, in/out, 그 다음 출력입니다.

[예제-Do]
```cpp
void Foo::goo(int in, int* b, int* out)
{
    // to do...
}
```
### 포함

**[R06]** 가독성을 높이고 숨겨진 종속성을 방지하려면 표준 순서를 사용하세요.

C 라이브러리, C++ 라이브러리, 관련 헤더, 기타 라이브러리의 .h, 프로젝트의 .h.

[예제-Do]
```cpp
#include <sys/types.h>
#include <unistd.h>
#include <hash_map>
#include <vector>
#include "foo/server/fooserver.h"
#include "base/basictypes.h"
#include "base/commandlineflags.h"
#include "foo/server/bar.h
```
### 선언

**[R07]** 클래스 내에서 지정된 선언 순서를 사용합니다.

public: private 이전:, 데이터 멤버(변수) 이전의 메서드 등

- Typedef 및 열거형

- 상수

- 공개 메소드

- 공용 멤버 변수

- 보호된 방법

- 보호된 멤버 변수

- 개인 메소드

- 개인 멤버 변수

- 친구

[예제-Do]
```cpp
#ifndef _SAMPLE_H_
#define _SAMPLE_H_
// header
#include <iostream>
#include <algorithm>
// Typedef
typedef unsigned int uint;
typedef unsigned long long ull;
// Constants
const int NUMBER = 100;
const char* const MY_NAME = "KilDong";
class Sample
{
public: // public member function
    Sample();
    ~Sample();
    void pubFunc();
public: // public member variable
    int a;
    int b;
protected: // protected member function
    void proFunc();
protected: // protected member variable
    int __c;
    int __d;
private: // private member function
    void priFunc();
private: // private member variable
    int __e;
    int __f;
// friend
friend class SampleImpl;
}; // Sample
#endif // _SAMPLE_H_
```
## 클래스

### 생성자

**[R08]** 생성자에 복잡한 초기화를 넣지 마세요.

### 참조 인수

**[R09]** 모든 참조 유형은 상수 유형이어야 합니다. 출력 매개변수인 경우 포인터형으로 선언한다. 널 포인터를 전달하거나 함수가 입력의 포인터를 저장하려는 경우 const 참조 유형 대신 const 포인터 유형을 사용할 수 있습니다.

[예제-Do]
```cpp
void printVec(const std::string& vec, int* out);
```
### 명시적

**[R10]** 생성자에 매개변수가 하나인 경우 '명시적' 키워드를 사용하여 바람직하지 않은 유형 변환을 방지하세요.

### 인터페이스 클래스

**[R11]** 순수 가상 함수로 구성, 접두사 'I' 추가

[예제-Do]
```cpp
class IShape
{
public:
    virtual void draw() = 0;
    virtual void setColor(int r, int g, int b, int a) = 0;
    virtual void setPosition(int x, int y) = 0;
    virtual ~IShape() { }
};
```
### 상속

**[R12]** 다중 상속은 인터페이스 클래스에만 허용됩니다.

### Template 클래스

**[R13]** 복잡한 템플릿 클래스를 사용하지 마세요.

### 예외

**[R14]** 예외를 사용하지 마세요.

### Stream 클래스

**[R15]** 스트림 클래스를 사용하지 마세요.

## 신뢰성

**[R16]** 합리적일 때마다 const 키워드를 사용하는 것이 좋습니다.

**[R17]** 길고 복잡한 기능을 만들지 마세요.

**[R18]** C++ 스타일 캐스트 사용을 권장합니다.

- static_cast - C 스타일 캐스트와 동일합니다.

- const_cast - const 한정자를 제거합니다.

- reinterpret_cast - 이 안전하지 않은 변환으로 수행 중인 작업을 알고 있는 경우에만 이 항목을 사용하세요. 일반적으로 사용되지 않습니다.

- dynamic_cast : 애플리케이션에서는 이 캐스팅을 사용하지 마세요.

  
**[R19]** 헤더 파일에 '네임스페이스 사용'을 사용하지 마세요.

[예제-Do]
```cpp
// Widget.h
#include <map>
#include <string>

class Widget 
{
public:
    // ctor/dtor/member functions...
private:
   typedef std::map<std::string, Widget*> WidgetTable;
   WidgetTable table_;
};
```
[예-하지 마세요]
```cpp
// Widget.h
#include <map>
#include <string>
using namespace std;

class Widget 
{
public:
    // ctor/dtor/member functions...
private:
    typedef map<string, Widget*> WidgetTable;
    WidgetTable table_;
};
```
**[R20]** 기본 인수를 사용하지 마세요.

**[R21]** sizeof(유형 이름)를 사용하지 마세요.

**[R22]** 다중 헤더 보호 장치에 doxygen 베일을 사용하세요.

```
/** @cond DOXYGEN_VEIL */
#ifndef __A_DAEMON_H__
#define __A_DAEMON_H__
/** @endcond DOXYGEN_VEIL */
```
