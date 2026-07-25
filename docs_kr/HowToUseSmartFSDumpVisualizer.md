# SmartFS 사용 방법 덤프 시각화 장치(SDV)

## SDV란 무엇입니까?

SmartFS SDV(Dump Visualizer)에는 두 가지 주요 기능이 있습니다.
1. 자동 덤프
   : 보드에서 SmartFS 파티션을 자동으로 덤프합니다(Artik053, imxrt1020).
     - A 덤프 파일은 사용자가 수동으로 지정하고 열 수도 있습니다.
2. SmartFS 분석
   : SmartFS의 다음 내용을 구문 분석하고 시각화합니다.
     1) 논리 섹터 정보(섹터 ID, 시퀀스 번호, CRC 값 및 섹터 상태)
     2) 저널링 정보(대상 섹터, 거래 유형/status, 오프셋, 데이터 크기)
     3) SmartFS 기하학(총 섹터 수, 섹터 크기)
     4) 디렉토리 계층 구조(TASH에 표시되는 디렉터리 및 파일 트리)


## SDV를 실행하는 방법은 무엇입니까?
SDV를 실행하려면 먼저 대상 PC에 Java 8(또는 최신 Java)을 다운로드하여 설치하십시오.
[자바 다운로드](https://www.java.com/ko/download/manual.jsp)

>**Note** TizenRT IDE를 함께 사용하기 위해서는 Java 8이 설치되어 있어야 합니다.

SDV는 이제 Linux(Ubuntu) 및 Windows용으로 출시될 수 있습니다(일부 기능이 제한됨).

>**Note** Mac OS 출시도 가능하지만 아직 출시되지 않았습니다.

다음 링크에서 SDV 릴리스(zip/tar.gz)를 다운로드한 후,
[우분투](../tools/fs/FS_Dump_Parser/Release/SDV_v2.0_20200210_Ubuntu.tar.gz)
[윈도우](../tools/fs/FS_Dump_Parser/Release/SDV_v1.1_20190903_Windows.zip)

릴리스 파일의 압축을 풀고 Windows용 SDV 또는 Ubuntu용 SDV.exe를 각각 실행합니다.


## SDV 사용 예
SDV를 시작하면 아래와 같은 초기 화면이 나옵니다.

![초기의](../docs/media/Initial_screen_20200210.png)

도구 모음(왼쪽 상단)에서 녹색 플러그 버튼을 클릭하면 자동 덤프를 실행할 수 있습니다. 보드 충돌이 발생한 후
. 안내 메시지는 다음과 같이 표시됩니다.

![연결 끊기](../docs/media/Disconnect_msg.png)

>**Note** 기존 직렬 화면을 닫아야 합니다.  
>         그렇지 않으면 덤프 데몬에 의해 생성된 파티션 내용은  
>         SDV에 삭제되는 대신 직렬 화면에 인쇄되었습니다.

모든 SmartFS 컨텐츠가 덤프된 후 분석이 수행되고 보드에서 추출된 정보가 SDV 뷰에 표시됩니다.

![저널링로드됨](../docs/media/Journaling_loaded.png)

>**Note** SDV에 SmartFS 정보가 표시될 때까지 기다려 주십시오.  
>         덤핑에 몇 분 정도 걸리기 때문에..

smartfs 덤프 파일*의 내용은 "열기" 메뉴를 통해 열어서 표시할 수도 있습니다.

>**Note** 이제 자동 덤프는 Artik053 및 imxrt1020 보드에서만 가능합니다.
>         따라서 MediaTek 및 STM 보드의 경우 먼저 보드에서 SmartFS 콘텐츠를 덤프하십시오.  
>         'Airoha IoT Flash Tool'과 'STM32 CubeProgrammer'를 각각 사용합니다.
