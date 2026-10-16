# TizenRT Debug Analyzer

TizenRT UART/커널 로그를 파싱하고 GPT가 근거를 확인하며 장애를 분석하는
로컬 플러그인이다. Python 3.10 이상이면 별도 런타임 패키지 없이 동작한다.

`codex/qemu-armv8m-kernel-tc`의 커밋 `5c0120f685ff47ab17f7ec26c0554965e312bc27`에서
시작했다. 원본 worktree의 미커밋 health-monitor 변경은 가져오지 않았다.

## 로그 하나 분석하기

이 디렉터리에서 실행한다.

```sh
python3 -m tizenrt_debug analyze /absolute/path/uart.log --format markdown
python3 -m tizenrt_debug analyze /absolute/path/uart.log --output report.json
cat /absolute/path/uart.log | python3 -m tizenrt_debug analyze - --format json
```

출력은 첫 장애, 사건별 레지스터/상태, 근거 줄 번호, 원인 후보, 다음 확인 사항이다.
`no_recognized_failure`는 정상 동작을 증명하는 판정이 아니다. 미분류 출력은
원본 로그와 함께 읽어야 한다. CLI의 종료 코드 0은 분석 성공이며 펌웨어 PASS가 아니다.

[실제 로그 분석 예제](examples/diagnosis.md)와 [검증 기록](references/validation.md)에서
DIVBYZERO·stack·MPU 분석 결과와 확인된 범위를 볼 수 있다.

ELF가 있으면 해당 펌웨어 빌드와 주소 배치가 일치하는지 확인한 뒤 실행한다.

```sh
python3 -m tizenrt_debug analyze /absolute/path/uart.log --format markdown \
  --elf /absolute/path/tinyara \
  --addr2line /absolute/path/arm-none-eabi-addr2line
python3 -m tizenrt_debug analyze /absolute/path/uart.log \
  --map /absolute/path/tinyara.map
```

ELF 조회는 PC/LR/명시적 frame을 사용한다. raw stack word 전체를 call frame으로
해석하지 않는다. map 조회는 가장 가까운 앞쪽 심볼 후보이며 정확한 함수 범위나
소스 줄을 증명하지 않는다. loadable app에는 해당 app ELF와 load offset이 필요하다.

## GPT 플러그인

플러그인은 **MCP 도구 5개 + 분석 스킬 1개**로 구성된다. MCP가 구조화된 근거를
제공하고 호스트의 GPT가 추가 문맥을 읽어 설명한다. 별도 OpenAI API 키나 모델
호출을 내장하지 않는다. `plugin.json`/`mcp.json`은 portable package이고,
`.codex-plugin/plugin.json`/`.mcp.json`은 로컬 Codex 호환 설정이다.

| 도구 | 역할 |
| --- | --- |
| `analyze_log` | 로그 파일 또는 텍스트를 분석하고 report ID 반환 |
| `get_report` | incidents/findings/registers/heap/tasks/history 등 페이지 조회 |
| `get_log_excerpt` | 분석 시점의 원본 줄을 줄 번호와 함께 조회 |
| `list_log_formats` | 34개 진단 계열의 config·소스·해석 지침 조회 |
| `symbolize_with_map` | 동일 빌드 map의 심볼 후보 연결 |

사용 예:

> 이 TizenRT 로그에서 최초 장애를 찾고, CFSR와 콜스택을 근거로 원인 후보와
> 다음 확인 위치를 알려줘. 재부팅 후 로그는 이전 fault와 분리해줘.

커스텀 스킬 `analyze-tizenrt-log`가 위 도구와 미분류 로그를 함께 읽도록 안내한다.

### 로컬 Codex 설치

저장소 루트를 marketplace로 등록한 후 설치한다. 기존 사용자 설정을 변경하는
이 설치 단계는 소스 개발/검증과 별도이다.

```sh
codex plugin marketplace add /absolute/path/to/tizenrt-log-analyzer
codex plugin add tizenrt-debug-analyzer@tizenrt-local
```

설치 후 플러그인을 선택한 새 세션에서 `list_log_formats`와 `analyze_log`가 실제로
호출되는지 확인한다. CLI marketplace 목록에 표시되는 것만으로 설치/도구 연결이
완료된 것은 아니다. 이 패키지 개발 검증에는 독립 MCP 클라이언트 검증과 Codex
marketplace 탐색이 포함되며, 현재 대화에 설치된 플러그인으로 간주하지 않는다.

### 로그 파일 디렉터리 지정

기본 설정은 붙여넣은 텍스트를 분석한다. 파일을 직접 읽을 서버에는 로그/map
디렉터리를 명시한다.

```sh
python3 server.py --root /absolute/path/to/logs --root /absolute/path/to/build
```

MCP 클라이언트의 args에 위 `--root` 인자를 추가하거나 서버 환경변수
`TIZENRT_LOG_ROOTS`에 경로를 지정한다(macOS/Linux는 `:`로 구분).
stdio 서버는 클라이언트가 실행하며, 직접 실행하면 표준입력의 MCP 요청을 기다린다.
클라이언트가 지원하는 경우 아래 설정에서 실제 절대 경로를 사용한다.

```json
{
  "mcpServers": {
    "tizenrt-debug": {
      "command": "python3",
      "args": ["/absolute/plugin/server.py", "--root", "/absolute/logs"]
    }
  }
}
```

로컬 stdio 서버는 사용자의 컴퓨터에서 동작한다. ChatGPT 웹에서 쓰는 원격 MCP
서비스 배포/연결은 이 패키지에 포함되지 않는다. 파일 업로드를 지원하는 호스트는
읽은 로그 내용을 `text`로 전달할 수 있다.

## 수집·분석 범위

- [진단 로그 카탈로그](references/log-formats.md): 소스에서 확인한 34개 계열.
- [파서 지원 범위](references/parser-coverage.md): 구조화 지원과 GPT가 원문으로
  해석해야 하는 계열을 구분한다.
- 저장소의 `tools/crash-corpus/`: QEMU 진단 recipe, 반복 실행 collector,
  실제 raw log·결과 manifest·빌드 식별 정보.
- `source-inventory.json.gz`: 알려진 출력 API를 lexical scan한 목록이다.
  조건부 컴파일 때문에 출력되지 않는 항목도 포함하며, 모든 출력의 의미 분석이나
  실제 실행 완료를 뜻하지 않는다.

원본 진단은 보존한다. timeout, 관찰된 fault, QEMU 종료, guest reboot/TASH 복귀를
구분한다. QEMU 결과로 실제 보드의 watchdog/flash/무선/타이밍을 판단하지 않는다.
기준 브랜치에 없는 health-monitor 확장의 전용 포맷은 별도 추가가 필요하다.

CLI는 최대 32 MiB/250,000줄, MCP는 최대 8 MiB를 받는다. MCP report는 최근 4개를
메모리에 유지한다. 큰 사건과 목록은 페이지로 읽고 `next_offset`을 따른다.
summary의 `test_summaries`와 symbolization의 `entries`도 페이지 객체이다.
단일 항목이 응답 한도를 넘으면 해당 항목 위치를 표시하고 다음 항목으로 진행한다.
원문은 `get_log_excerpt` 또는 CLI JSON으로 확인한다.

## 검증과 재패키징

```sh
python3 -m unittest discover -s tests -v
# 선택: 공식 Python MCP 클라이언트로 stdio 왕복 검증
python3 -m venv /tmp/tizenrt-mcp-client
/tmp/tizenrt-mcp-client/bin/pip install 'mcp>=1.30,<2'
/tmp/tizenrt-mcp-client/bin/python tests/mcp_sdk_smoke.py
```

저장소 루트에서 `python3 tools/tizenrt-debug/package_plugin.py`를 실행하면
`build/tizenrt-debug/tizenrt-debug-analyzer-0.1.0.zip`을 만든다. 개발 캐시와
marketplace를 제외한 독립 패키지이며 public directory에 업로드하지 않는다.

MCP transport와 도구 계약은 [stdio 규약](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports)과
[tools 규약](https://modelcontextprotocol.io/specification/2025-06-18/server/tools)을 따른다.
