# LINK Protocol · 로컬 주소 v0.1

LINK Browser의 C++ 구현은 LINK Protocol의 `link://` 주소를 등록된 로컬 디렉터리에 대응시키고 문서를 읽습니다. LINK(Linked Interactive Network)는 생태계, LINK Protocol은 주소·연결 규칙, LINK Browser는 네이티브 브라우저의 이름입니다. 현재 실험적 로컬 v0.1이며 원격 서버 연결이나 네트워크 메시지·전송 규칙은 구현하지 않았습니다.

## 허용 주소

```ebnf
address = "link://local/", world ;
world   = (letter | digit | "-"), { letter | digit | "-" } ;
letter  = "A" … "Z" | "a" … "z" ;
digit   = "0" … "9" ;
```

위 문법은 percent decoding을 한 번 적용한 뒤의 주소 경로입니다. 스킴과 authority는 소문자 `link://local`과 정확히 일치해야 합니다. 공간 이름은 비어 있지 않은 단일 ASCII 경로 조각입니다. 주소 경로 원문은 최대 2,048 bytes이며 주변 공백은 자동으로 제거하지 않습니다. 밑줄·점·공백·한글은 공간 주소 이름에 허용하지 않습니다. 문서 안의 표시 이름은 UTF-8 한글을 사용할 수 있습니다.

```text
link://local/room-a
link://local/room-b
link://local/room%2Da
```

첫 번째와 세 번째 주소는 같은 `room-a` 디렉터리를 가리킵니다. percent encoding의 16진수 대소문자는 구분하지 않습니다. 일반 문자열의 대소문자는 바꾸지 않으며 실제 파일 이름의 대소문자 구분은 해당 파일 시스템을 따릅니다. Windows의 루트 포함 검사와 중복 리소스 검사는 대소문자를 구분하지 않습니다. UI와 `World.address`는 입력한 LINK 주소를 유지합니다.

다른 authority, userinfo, port, query, fragment, 추가 경로 조각, 끝의 `/`는 지원하지 않습니다. `%2F`, `%5C`처럼 인코딩한 경로 구분자, 잘못된 `%`·16진수·잘린 escape, 중첩 인코딩으로 `%`가 남는 입력도 거절합니다. ASCII 제어문자·DEL, 역슬래시, `:`, `?`, `#`, `@`는 원문과 디코딩 결과 모두에서 허용하지 않습니다. `..`, `%2e%2e`, 절대 경로는 공간 이름 문법에 맞지 않아 거절됩니다.

| 입력 | 결과 |
| --- | --- |
| `link://local/room-a` | 등록된 루트 아래 `room-a/world.link` |
| `link://local/room-a/` | 추가 `/` 거절 |
| `link://local/room-a?mode=1` | query 거절 |
| `link://local:8000/room-a` | port 거절 |
| `link://user@local/room-a` | userinfo 거절 |
| `link://remote/room-a` | 미지원 authority 거절 |
| `link://local/%2e%2e` | 공간 이름 문법 위반 |
| `link://local/%252e%252e` | 중첩 인코딩 거절 |
| `link://local/room-a%2Fother` | 인코딩한 구분자 거절 |

## 루트 설정과 해석 순서

기본 루트는 실행 파일 옆 `worlds` 디렉터리입니다. 빌드는 `native/worlds` 예제를 그 위치로 복사합니다. 다른 루트는 `--worlds` 인수로 명시합니다. 작업 디렉터리의 암묵적인 `worlds` 폴더를 탐색하지 않습니다.

```powershell
.\build\native\Release\LINK.exe --worlds "C:\LINK\worlds"
```

해석은 [loader.cpp](../native/src/loader.cpp)의 `resolveLink`와 `resolveResource`가 수행합니다.

1. URI의 고정 스킴·authority와 경로 문법을 확인하고 경로를 한 번 디코딩합니다.
2. 등록된 `worlds` 루트를 `std::filesystem::canonical`로 정규화합니다. 루트는 존재해야 합니다.
3. 공간 후보를 canonical로 해석해 심볼릭 링크·junction을 따라간 실제 디렉터리를 얻습니다.
4. 후보의 경로 구성 요소를 루트와 비교합니다. 단순 문자열 접두사가 아니라 전체 구성 요소가 일치해야 하며 루트 자체를 공간으로 사용할 수 없습니다.
5. 공간 디렉터리 안의 고정 진입 파일 `world.link`를 일반 파일로 확인합니다.

예를 들어 `worlds/room-a`가 `C:\private`로 이어지는 junction이면 최종 경로가 루트를 벗어나므로 거절합니다. 루트 안의 다른 디렉터리를 가리키는 링크는 최종 경로가 포함 범위를 지키면 허용됩니다. 설정한 루트 자체가 링크인 경우에는 그 canonical 대상이 명시적으로 등록한 루트가 됩니다.

## 공간 내부 리소스

`elements`, `style`, `scripts`의 문자열은 `world.link`가 속한 **공간 디렉터리 기준 상대 경로**입니다. 이번 버전은 다른 공간 디렉터리 참조도 허용하지 않습니다.

```text
scene.element
./appearance.style
scripts/door.script
```

문자열을 한 번 디코딩하고 URI와 같은 금지문자·인코딩 검사를 적용합니다. 절대 경로·드라이브·UNC 경로와 모든 `..` 경로 조각을 거절합니다. `./`는 허용합니다. 각 파일을 canonical로 해석한 최종 경로가 해당 공간 안에 있어야 하며 일반 파일이어야 합니다. 따라서 공간 내부의 파일 링크가 다른 공간·루트 밖 파일로 이어져도 거절합니다. 실제 파일이 없거나 접근할 수 없으면 로딩 실패입니다.

Script 목록의 문자열은 중복될 수 없고, 다른 문자열이 같은 canonical 파일로 해석되는 중복도 거절합니다. 노드의 `script`는 목록에 적힌 **원문 문자열과 정확히 일치**해야 합니다. Script 파싱 결과의 키도 이 원문 문자열입니다. 예를 들어 목록의 `scripts/door.script`와 바인딩의 `./scripts/door.script`는 다른 문자열이므로 바인딩 오류입니다.

## 포탈의 LINK 이동

진입 문서의 선택 `portals` 목록은 파일 참조 대신 다른 로컬 공간의 LINK 주소를 지정합니다. 예를 들어 사진 복도의 포탈은 `link://local/hello-world`를 엽니다. 포탈 주소도 위 authority·인코딩·경로·canonical 포함 검사를 통과해야 하고 대상 공간 디렉터리가 존재해야 합니다. 최대 16개이며 감지 상자·ID의 실제 문법은 [제작 언어 명세](../native/docs/language.md#1인칭-걷기와-포탈)를 따릅니다.

포탈을 선언하는 단계에서는 대상 디렉터리만 확인하며 대상 문서를 읽거나 실행하지 않습니다. 사용자가 감지 상자에 걸어 들어갈 때 새 탐색 요청을 만들고 해당 주소의 world.link·리소스·Script를 준비합니다. 준비 실패는 현재 실행을 유지하고, 취소와 최신 세대 요청 검사를 그대로 적용합니다. 포탈 이동은 공간 간 탐색이므로 각 공간 내부 파일의 상대 참조 제한을 완화하지 않습니다. 원격 네트워크 요청은 수행하지 않습니다.

## 로딩·오류·취소

진입 문서는 최대 64 KiB, Element·Style 문서는 각각 1 MiB, Script 파일은 각각 128 KiB, Script 목록은 최대 16개입니다. 읽는 도중 실제 누적 bytes를 검사하므로 파일 크기만 미리 확인하고 신뢰하지 않습니다. 파일은 매번 다시 읽으며 reload는 새 파싱 결과와 초기 상태를 만듭니다. 파일 내용은 **UTF-8, BOM 없음**입니다. lexer는 잘못된 UTF-8도 거절합니다.

구문·의미 오류는 `파일:행:열` 형태입니다. 행·열은 1부터 시작하고 열은 UTF-8 bytes 기준입니다. 파일 누락·LINK 경로 오류처럼 소스 내 위치가 없는 경우에는 해당 주소 또는 파일 이름과 `1:1`을 사용합니다. 알 수 없는 필드·중복 ID·미지원 버전·누락 재질·미선언 Script 바인딩·Script 구문 오류를 활성화 전에 검사합니다.

백그라운드 로더는 `std::stop_token`을 파일 읽기, 노드 파싱, 검증, Script 준비 사이에 확인합니다. 최신 요청의 세대 번호를 확인한 후보만 활성화합니다. 취소는 안전한 확인 지점에서 적용하며 실행 중인 임의 C++ 작업을 강제로 중단하지 않습니다. 준비 실패는 기존 실행을 유지하고, 검증된 후보의 활성화가 실패하면 정리한 실패 상태에서 주소·홈으로 다시 시도할 수 있습니다.

## 범위와 보안 경계

이 검사는 신뢰하는 로컬 문서가 잘못된 경로를 참조하는 문제를 줄입니다. canonical 검사와 실제 파일 열기 사이에는 시간 차이가 있으므로, 다른 프로세스가 동시에 링크·파일을 바꾸는 TOCTOU 공격까지 막는 OS 파일 핸들 기반 샌드박스는 아닙니다. native 파서·렌더러의 프로세스 격리도 제공하지 않습니다. 제한된 Script API와 평가 예산을 운영용 완전 격리로 해석하지 않습니다.

원격 LINK authority, DNS·서버 발견, TCP/UDP 전송, 인증·계정, 멀티플레이·공유 상태, OS `link://` 핸들러 등록은 후속 범위입니다. 주소를 파싱했다는 사실만으로 LINK Protocol의 네트워크 전송을 구현했다고 주장하지 않습니다. 문법·객체·Script·Style 규칙은 [LINK World Standards](link-world-v0.1.md)를 따릅니다. 주소·리소스 해석과 경로 제한을 검사하는 코드는 [native/tests](../native/tests)에 있으며 빌드 후 `ctest --preset release`로 실행합니다.
