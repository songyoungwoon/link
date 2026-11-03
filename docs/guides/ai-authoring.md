# AI authoring · 같은 표준을 읽고 작은 변경 제안하기

AI가 LINK 월드를 수정할 때 먼저 현재 선언 모델을 읽고, 필요한 ID와 속성만 patch로 제안하도록 구성할 수 있습니다. `link-authoring-v1`은 이 과정을 위한 기계 판독 계약입니다. AI 서비스나 모델을 연결하는 프로그램 자체는 이번 버전에 포함하지 않습니다.

흐름은 **inspect → patch 제안 → dry-run 검증 → 변경 검토 → 명시적 저장 → 실제 재생 확인**입니다. 기존 월드 전체를 다시 생성하는 대신 지원하는 부분 편집을 사용하면 관계없는 Script·Style·이미지와 원본 소스 영역을 유지할 수 있습니다.

## 도구가 읽어야 할 정보

| 입력 | 용도 |
| --- | --- |
| [언어 명세](../../native/docs/language.md) | Element·Script·Style의 실제 문법·단위·API |
| [편집 계약](../authoring-v1.md) | revision과 허용 작업·검증·저장 규칙 |
| [patch JSON Schema](../schemas/authoring-patch.schema.json) | JSON 응답 구조와 허용 속성 |
| [inspection JSON Schema](../schemas/authoring-inspect.schema.json) | CLI 출력과 조회 모델 |
| 현재 월드의 inspect 결과 | 존재하는 노드 ID·부모·형상·재질·revision |

inspection은 실행 중인 세계의 스냅샷이 아닌 선언 정보입니다. 원래 source에 생략된 기본값도 표시하고, 이번 patch에서 편집할 수 없는 state·config·script·image·text 정보도 포함할 수 있습니다. 조회 결과 전체를 set 속성으로 복사하지 마세요.

## 복사한 예제로 안전하게 실행

다음 PowerShell 명령은 **별도 `link-architect` 프로젝트 루트**에서 실행합니다. 그 저장소의 worlds/first-world를 무시되는 build 폴더에 한 번 복사하므로 포함된 예제 원본은 편집하지 않습니다. 먼저 Architect 프로젝트를 빌드해 LINKAuthor.exe를 만듭니다. LINK Browser 프로젝트를 함께 clone하거나 빌드할 필요는 없습니다.

~~~powershell
$author = Join-Path $PWD 'build\native\Release\LINKAuthor.exe'
$demoRoot = Join-Path $PWD 'build\authoring-example\worlds'
$demoWorld = Join-Path $demoRoot 'first-world'
if (-not (Test-Path -LiteralPath $demoWorld)) {
  New-Item -ItemType Directory -Path $demoRoot -Force | Out-Null
  Copy-Item -LiteralPath '.\worlds\first-world' -Destination $demoWorld -Recurse
}

# 네이티브 CLI의 UTF-8 JSON 응답을 PowerShell 객체로 읽음
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$response = & $author --worlds $demoRoot --open link://local/first-world inspect
if ($LASTEXITCODE -ne 0) { throw 'World inspection failed.' }
$current = ($response -join "`n") | ConvertFrom-Json

# 실제 inspect revision을 사용하며 sample_cube의 로컬 위치만 변경
$patch = [ordered]@{
  profile = 'link-authoring-v1'
  baseRevision = $current.inspection.revision
  operations = @(
    [ordered]@{
      op = 'set'
      id = 'sample_cube'
      properties = [ordered]@{ position = @(0, .3, 0) }
    }
  )
}
$patchPath = Join-Path $PWD 'build\authoring-example\patch.json'
[IO.File]::WriteAllText($patchPath, ($patch | ConvertTo-Json -Depth 16), (New-Object Text.UTF8Encoding($false)))

# 후보만 검증. saved:false이면 파일을 저장하지 않은 상태
& $author --worlds $demoRoot --open link://local/first-world apply --patch $patchPath
~~~

dry-run의 ok와 진단을 확인하고 patch 내용을 검토합니다. 같은 원본에 저장하려면 아래 명령을 별도로 실행합니다. 그동안 문서를 바꿨다면 먼저 inspect부터 다시 진행하세요.

~~~powershell
& $author --worlds $demoRoot --open link://local/first-world apply --patch $patchPath --write
.\build\native\Release\LINKArchitect.exe --worlds $demoRoot --open link://local/first-world
~~~

Windows PowerShell의 기본 파일 출력은 UTF-16 등이 될 수 있어 위 예제는 BOM 없는 UTF-8로 명시해 씁니다. patch는 표준 JSON이고 Element 소스는 별도의 LINK 문법입니다. JSON을 `.element` 파일로 저장하지 마세요.

## AI에 줄 수 있는 입력 지침

현재 inspection과 스키마를 함께 제공하고 다음처럼 요청할 수 있습니다. 이 문장은 프롬프트 예시이며 모델에 실제로 전송하는 기능을 뜻하지 않습니다.

~~~text
You edit LINK World Standards v0.1 through the link-authoring-v1 contract.
Return exactly one JSON patch object without Markdown.
Use the supplied inspection.revision as baseRevision.
Use only existing node IDs and declared material IDs from the inspection.
For add, use a new unique ASCII identifier and an existing Component parent.
Only propose supported set/add operations and properties from the patch schema.
Preserve Script, Style, state, config, image and text declarations.
Coordinates are local to the parent, right-handed, Y-up, and measured in meters.
Rotation uses Euler XYZ degrees; scale has three positive components.
Do not invent APIs, geometry types, imports, shaders, or file/network access.
If the requested change cannot be expressed by this profile, explain that limit
instead of emitting an unsupported patch.
Do not claim a patch passed validation or execution without actual tool results.
~~~

사용자 요청과 실제 API 범위가 맞지 않으면 AI가 지원하지 않는 작업을 만들어내는 대신 제한을 설명하도록 구성합니다. 모델 출력은 신뢰하지 않은 입력으로 취급하고 같은 로더의 검증을 거칩니다.

## 혼동하기 쉬운 규칙

| 주제 | 현재 규칙 |
| --- | --- |
| 계층 | Object 아래 Component, Component 아래 Component·Element, Element는 잎 |
| Script 연결 | Object·Component에 연결. Element에 직접 Script·state를 붙이지 않음 |
| 위치와 배율 | 로컬 미터 좌표, 양수 scale. 부모 transform이 자식에 적용됨 |
| 각도 | transform.rotation은 degree, Script sin·cos는 라디안 |
| geometry | box·sphere·plane·cylinder·frame·선언된 particles 확장. 임의 mesh 필드 없음 |
| 노드 상태 | `ctx.setState`는 최상위 키를 얕게 병합. 조회 값은 복사 |
| 입자 상태 | `ctx.setParticle(...,{state:...})`는 state 전체 교체, 같은 단계에서는 고정 스냅샷 읽기 |
| 반복 | 제한된 for-of만 지원. while·일반 for·재귀·사용자 함수 없음 |
| 실행 환경 | JavaScript·Python·Lua·DOM·Math 객체·fetch·OS API를 제공하지 않음 |
| 이번 patch | 일반 노드 생성·삭제·부모 이동, Script·Style 파일 편집 API 없음. 새 Element 추가만 지원 |

Script 작성과 동작은 [Script 가이드](script.md), 입자는 [입자 가이드](particles.md)를 따릅니다. patch에 Script 코드를 넣는 기능은 제공하지 않습니다. AI가 원래 Script 파일을 직접 작성하는 별도 워크플로를 구성해도 그 소스와 실제 동작은 따로 검증해야 합니다.

## 검증 결과를 다루는 방법

JSON Schema는 형태와 일부 수치 범위를 검사합니다. 존재하지 않는 ID·재질, 잘못된 부모 종류, 오래된 revision, 현재 월드 확장, frame.border의 상대 크기, 전체 예산 등은 LINKAuthor의 실제 후보 검증이 필요합니다. 한 단계라도 실패하면 진단을 기준으로 수정하고 현재 원본을 다시 읽습니다.

정적 검증은 Script를 실행하지 않습니다. 재생 중 분기·연산량·클릭 반응·입자 알고리즘·이미지 디코딩 성공까지 검증했다고 표현하지 마세요. 성공한 patch도 Architect의 Play와 Browser에서 의도한 동작을 확인해야 합니다.

이 방식은 현재 로컬 문서 편집을 재현 가능하게 연결하는 출발점입니다. 모델 품질을 보증하거나 여러 AI가 동시에 편집하는 트랜잭션, 원격 서버에 자동 배포하는 시스템을 제공하는 것은 아닙니다.
