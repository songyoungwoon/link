# LINK Authoring v1 · 읽고, 제안하고, 검증한 뒤 저장하기

`link-authoring-v1`은 **기존 LINK World Standards v0.1 문서를 편집하기 위한 기계 판독 계약**입니다. AI 도구와 시각적 제작기가 같은 선언 모델을 살펴보고, ID를 대상으로 작은 변경을 제안하며, 같은 C++ 검증기를 통과한 결과를 저장합니다. Element·Script·Style의 문법과 실행 의미는 바꾸지 않습니다.

월드 파일을 JSON으로 바꾸는 규격이 아닙니다. JSON은 도구의 inspection·patch·진단 메시지에만 사용하고, 실제 월드는 `world.link`, Element, Style, Script 문서로 유지합니다. 이 계약 역시 실험적이며 공인 표준이 아닙니다.

| 구성 | 역할 |
| --- | --- |
| `LINKAuthor.exe` | 창 없이 로컬 월드를 검사·조회하고 patch를 검증·적용하는 CLI |
| `AuthoringSession` | 조회·부분 편집·후보 검증·undo/redo·저장을 공유하는 C++ 편집 세션 |
| [LINK Architect](architect.md) | 같은 편집 세션과 네이티브 렌더러를 사용하는 시각적 제작 프로그램 |
| [patch JSON Schema](schemas/authoring-patch.schema.json) | 허용 patch 구조·속성·수치 범위 |
| [inspection JSON Schema](schemas/authoring-inspect.schema.json) | CLI 응답과 선언 모델 조회 결과 |

## 범위

이번 프로파일은 **로컬 월드의 Element 문서만 편집**합니다. 기존 Object·Component·Element의 지원 속성을 변경하고 기존 Component 아래에 새 Element를 추가합니다. 노드 ID 변경·삭제·이동, Object·Component 생성, Script·state·config·Style·매니페스트·이미지 편집은 이 patch 계약에 포함하지 않습니다.

원격 서버 편집·파일 업로드, AI 모델 연결·프롬프트 전송·생성 결과 자동 실행도 제공하지 않습니다. 제작 도구와 AI 클라이언트가 JSON patch를 만드는 데 사용할 수 있는 계약입니다. 기존 문서는 기존 브라우저에서 같은 방식으로 실행되며 LINK TCP 프레임이나 `world.link`의 `version: "0.1"`을 바꾸지 않습니다.

## 1. 실제 문서 조회

LINKAuthor.exe는 **별도 `link-architect` 프로젝트**가 제공합니다. 아래 명령은 그 프로젝트를 빌드한 뒤 `link-architect` 루트의 PowerShell에서 실행합니다. 이 문서를 LINK Browser 저장소에서 읽더라도 실행 기준은 Architect 프로젝트입니다. `--worlds`는 월드 폴더들의 부모이고 `--open`은 그 루트 안의 로컬 LINK 주소입니다.

~~~powershell
.\build\native\Release\LINKAuthor.exe --worlds "$PWD\worlds" --open link://local/first-world inspect
~~~

조회 결과는 실행 중인 Script의 상태가 아니라 **원본 선언을 읽은 편집 모델**입니다. 생략한 transform 등은 기본값을 채워 보여줍니다. 노드 ID·부모·형상·재질과 소스 위치를 함께 읽으면 파일 전체를 다시 생성하지 않고 변경 대상을 지정할 수 있습니다.

CLI의 성공 응답은 다음 공통 외피를 사용합니다.

~~~json
{
  "ok": true,
  "command": "inspect",
  "saved": false,
  "inspection": {
    "profile": "link-authoring-v1",
    "revision": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
    "address": "link://local/first-world",
    "id": "first-world",
    "name": "My first world",
    "version": "0.1",
    "extensions": [],
    "materials": ["cube_material", "floor_material", "pedestal_material"],
    "nodes": [],
    "dirty": false,
    "canUndo": false,
    "canRedo": false
  }
}
~~~

위 JSON은 외피 설명용이며 실제 예제의 노드·이름·revision은 실행 결과를 사용합니다. 0으로 채운 revision을 patch에 복사하면 안 됩니다.

| inspection 필드 | 의미 |
| --- | --- |
| `profile`, `version` | 편집 계약 이름과 원본 월드 문서 버전 |
| `revision` | 현재 편집 문서 묶음을 식별하는 `sha256:` + 소문자 16진수 64자리 |
| `address`, `id`, `name` | 월드 주소와 선언 정보 |
| `extensions`, `materials` | 선언된 월드 확장과 이름 있는 재질 ID 목록 |
| `nodes` | 선언 순서의 노드 목록. `id`, `kind`, `parent`, `name`, `properties`, `source` 포함 |
| `dirty` | 마지막으로 읽거나 저장한 원본과 현재 초안이 다른지 |
| `canUndo`, `canRedo` | 현재 메모리 세션의 편집 이력 상태 |

루트 노드의 `parent`는 null이고 다른 노드는 부모 ID입니다. `kind`는 `object`, `component`, `element`입니다. source는 `{file,line,column}`이며 행·열은 1부터 시작하고 열은 UTF-8 byte 위치입니다. properties에는 이름·로컬 transform·visible과 종류별 선언 정보가 들어갑니다. Element의 image·text와 Object·Component의 state·config·script는 조회할 수 있어도 이번 patch로 수정하지 못하는 정보입니다. 조회 결과 전체를 properties에 되돌려 보내지 마세요.

### revision 계산

소비 도구는 revision을 불투명한 내용 토큰으로 취급하고 inspect가 반환한 값을 그대로 사용하면 됩니다. 독립 구현에서 재계산할 때는 ASCII `LINK authoring-v1` 바이트 뒤에 각 문서의 `uint64 little-endian(상대 경로의 UTF-8 바이트 길이)`, 상대 경로 UTF-8, `uint64 little-endian(본문 바이트 길이)`, 원본 본문 바이트를 이어 SHA-256을 계산합니다. 문서 순서는 `world.link`, 매니페스트의 elements, style, scripts 선언 순서입니다. 현재 Element 초안은 해당 본문에 대신 넣으며 결과는 `sha256:`와 소문자 16진수 64자리입니다. 이미지 본문은 revision에 포함하지 않습니다.

## 2. 현재 revision에 변경 제안

patch의 최상위 키는 정확히 `profile`, `baseRevision`, `operations`입니다. JSON 객체의 키 중복, 주석, 후행 쉼표, NaN·Infinity는 허용하지 않습니다. 문자열은 표준 JSON escape와 Unicode escape를 지원합니다. 실제 Element·Script 소스의 문자열 문법과 JSON 문자열 문법은 구분합니다.

~~~json
{
  "profile": "link-authoring-v1",
  "baseRevision": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
  "operations": [
    {
      "op": "set",
      "id": "sample_cube",
      "properties": {
        "position": [0, 0.3, 0],
        "rotation": [0, 25, 0]
      }
    },
    {
      "op": "add",
      "parent": "room",
      "id": "new_marker",
      "kind": "element",
      "properties": {
        "name": "A new marker",
        "geometry": {"type": "sphere", "radius": 0.25},
        "position": [2, 0.5, 0],
        "style": "cube_material"
      }
    }
  ]
}
~~~

실제로 사용할 때는 baseRevision을 직전 inspect 응답의 값으로 바꿉니다.

`set`은 기존 노드, `add`는 기존 Component를 대상으로 합니다. Object 바로 아래에는 Element를 추가할 수 없습니다. ID는 공간 전체에서 유일한 ASCII 식별자이며 영문자 또는 `_`로 시작하고 이후 영문자·숫자·`_`를 사용합니다. 새 ID로 기존 ID를 재사용할 수 없습니다.

| 편집 속성 | 대상과 값 |
| --- | --- |
| `name` | 모든 노드. 비어 있지 않은 표시 문자열 |
| `position` | 모든 노드. 로컬 좌표 `[x,y,z]`, 각 성분 절댓값 1,000,000 이하 |
| `rotation` | 모든 노드. XYZ Euler 각도 degree `[x,y,z]`, 각 성분 절댓값 1,000,000 이하 |
| `scale` | 모든 노드. 세 성분 모두 0 초과 1,000 이하 |
| `visible` | 모든 노드. bool |
| `geometry` | Element만. 형상별 필수 필드를 모두 포함한 geometry 맵 |
| `classes` | Element만. 중복 없는 ASCII 식별자 배열, 최대 64개 |
| `style` | Element만. 기존의 이름 있는 material ID, 빈 문자열 불가 |

모든 숫자는 유한수입니다. properties에는 최소 한 속성이 필요하고 `add`에는 반드시 geometry를 넣습니다. 생략한 속성은 `set`에서 유지하고 `add`에서는 언어의 기본값을 사용합니다. geometry는 내부 필드 병합이 아닌 전체 교체입니다. 기존 image/text plane을 다른 형상으로 바꾸는 등 원래 선언과 충돌하는 변경은 전체 검증에서 실패합니다.

지원 형상은 box·sphere·plane·cylinder·frame과 선택 확장의 particles입니다. particles를 추가할 월드는 이미 `particles-v1` 확장을 선언하고 있어야 합니다. frame.border와 바깥 치수 관계, material 참조, 노드 계층, 파일·노드·입자 예산도 기존 로더로 검사합니다. JSON Schema 통과만으로 이 문맥 검증을 대신할 수 없습니다.

## 3. 후보를 검증한 뒤 저장

~~~powershell
# 문서·참조·Script 구문을 읽고 기존 로더의 제약을 검사
.\build\native\Release\LINKAuthor.exe --worlds "$PWD\worlds" --open link://local/first-world validate

# patch를 메모리에서 적용·검증. 파일은 저장하지 않음
.\build\native\Release\LINKAuthor.exe --worlds "$PWD\worlds" --open link://local/first-world apply --patch .\patch.json

# 같은 검증에 성공한 후보를 명시적으로 저장
.\build\native\Release\LINKAuthor.exe --worlds "$PWD\worlds" --open link://local/first-world apply --patch .\patch.json --write
~~~

`apply`는 기본으로 dry-run입니다. 성공 응답의 inspection은 후보이며 `saved:false`이면 디스크를 바꾸지 않았습니다. `saved:true`는 명시적 쓰기 요청이 성공 처리되었다는 뜻이며 내용이 같은 no-op에서는 파일을 다시 쓰지 않을 수 있습니다. CLI 실행은 각각 독립 세션이므로, dry-run 결과의 revision을 저장되지 않은 디스크에 대한 다음 patch에 사용하지 마세요.

한 patch에는 최대 128개 작업을 넣고 메모리 후보에 순서대로 적용합니다. 후보 전체를 기존 LINK 로더로 검증하며 실패하면 세션의 이전 초안을 유지합니다. baseRevision이 현재 세션과 다르면 변경을 거절합니다. 편집·저장할 때도 디스크의 문서 바이트를 확인하고 감지한 외부 변경과 충돌하면 덮어쓰기를 거절합니다. 이는 일반적인 파일 변경 검사이며 비협조적인 다른 프로그램의 동시 쓰기와 원자적 compare-and-swap을 보장하지 않습니다. 이때는 새 inspect 결과를 읽고 변경을 다시 검토합니다.

저장 대상은 Element 문서뿐입니다. 수정하지 않은 소스 영역의 주석·공백을 유지하고 변경 속성의 값과 새 Element 부분을 생성합니다. 구조가 같은 값 변경은 잎 토큰을 바꿔 내부 공백·주석도 유지합니다. geometry·classes 등의 구조 변경으로 기존 값 내부의 `//` 주석이 사라질 상황이면 변경을 거절합니다. 이때 더 작은 필드 변경을 사용하거나 원본을 직접 검토합니다. world.link·Style·Script·이미지 자산에는 쓰지 않습니다. Element 저장은 임시 파일에서 파일 단위로 교체하며, 여러 문서의 트랜잭션이나 전원 장애 때의 영속성까지 보장하지 않습니다.

AuthoringSession과 Architect는 한 patch를 한 undo 단위로 다루며 undo/redo 후보도 검증합니다. 이력 스택마다 최대 32개 명령·16 MiB를 보관하며 더 오래된 이력은 제거합니다. 이미 선언된 속성을 같은 값으로 바꾸는 no-op은 revision·dirty·이력을 바꾸지 않습니다. CLI는 실행마다 종료하므로 다른 CLI 호출로 undo 이력을 이어가지 않습니다.

## 진단과 보장 범위

실패 응답은 `{ok:false,diagnostics:[{file,line,column,message}]}`입니다. 종료 코드는 성공 0, 검증·편집 실패 1, CLI 사용법 오류 2입니다. 오류 문장 자체를 고정된 오류 코드로 파싱하지 말고 ok와 진단 위치를 사용하세요.

| 제한 | 값 |
| --- | ---: |
| patch JSON | 1 MiB |
| 한 patch의 작업 | 1~128 |
| patch 입력 JSON 깊이 / 값 수 | 64 / 20,000 |
| JSON 문자열 | 8,192 UTF-8 bytes |
| 원본 문서·노드·실행 예산 | World Standards v0.1 계속 적용 |

validate/inspect/apply는 Script를 실행하지 않으며 GPU 이미지 디코딩이나 렌더링 성공을 확인하지 않습니다. 정적 검증에 통과해도 재생할 때의 Script 평가 예산·조건 분기·상호작용·렌더러 활성화에 문제가 없다고 보장할 수 없습니다. 저장 전후에 Architect나 LINK Browser로 실제 동작을 확인합니다. 강화된 실행 격리, 독립 구현의 적합성 인증, 협업 편집, 원격 저장은 이번 프로파일의 범위 밖입니다.

영어 AI 입력 템플릿과 실행 가능한 patch 생성 예제는 [AI authoring 가이드](guides/ai-authoring.md), 화면 조작은 [Architect 가이드](architect.md)를 참고하세요.
