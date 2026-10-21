# LINK World Standards · v0.1

LINK의 World Standards는 구조의 **Element**, 동작의 **Script**, 외형의 **Style**로 구성합니다. 이 문서는 로컬 파일과 LAN HTTP에서 공통으로 사용하는 LINK Browser의 실험적 v0.1 문서 형식을 설명하며 확정되거나 공인된 표준을 뜻하지 않습니다. 전체 어휘·EBNF·유효 문서·우선순위·API·예산은 [실제 제작 언어 명세](../native/docs/language.md)에 정의합니다. 실행 문서는 [사진 공간](../native/worlds/heukryong/world.link), [Hello World](../native/worlds/hello-world/world.link), [Room A](../native/worlds/room-a/world.link), [Room B](../native/worlds/room-b/world.link)이며 LINK Protocol의 주소·전송은 [로컬 명세](link-local-v0.1.md)와 [LAN HTTP 계약](link-lan-v0.1.md)을 따릅니다. 전송 방식이 바뀌어도 `version: "0.1"`과 제작 문법은 유지합니다.

## 문서 경계

`world.link`가 `scene.element`, `appearance.style`, `scripts/*.script`를 참조합니다. 선언·토큰·AST는 C++가 직접 해석합니다. JavaScript·Lua·Python 또는 네이티브 플러그인을 공간 코드로 실행하지 않습니다. 확장자와 v0.1 문법은 실험안입니다.

```text
world {
  version: "0.1";
  id: "room-a";
  name: "LINK Room A";
  elements: "scene.element";
  style: "appearance.style";
  scripts: ["scripts/door.script"];
  entry: {position:[7,6,9], target:[0,1,0]};
}
```

핵심 선언의 EBNF는 다음과 같습니다. 세부 value·expression 문법과 연산 우선순위는 위 제작 언어 명세가 기준입니다.

```ebnf
field = identifier, ":", value, ";";
worldDocument = "world", "{", {field}, "}";
sceneDocument = "scene", "{", {object}, "}";
object = "object", identifier, "{", {field | component}, "}";
component = "component", identifier, "{", {field | component | element}, "}";
element = "element", identifier, "{", {field}, "}";
styleDocument = {"defaults", "{", {field}, "}"
  | ("material" | "class" | "id"), identifier, "{", {field}, "}"};
hook = "on", hookName, "(", parameters, ")", block;
```

전체 ID는 공간 안에서 유일합니다. Element는 box·sphere·plane·cylinder·frame의 잎이며 상태·Script를 갖지 않습니다. plane은 선택 `image` 또는 `text` 필드를 지원하고 두 필드는 함께 사용할 수 없습니다. 현재 사진 공간은 28개의 image plane과 액자를 사용하며 문구용 text plane은 배치하지 않습니다. Hello World!의 문자는 box 형상을 묶어 두께를 표현합니다. Object와 Component는 데이터 state, 초기 config, Script 하나를 가집니다. 오른손 Y-up, m·초·degree XYZ이며 변환은 `T × Rx × Ry × Rz × S`, 부모 visible도 자식에 적용됩니다.

진입 문서는 선택 `views`로 이름 있는 카메라 위치 메타데이터를, `environment`로 배경색·격자 표시를 지정합니다. `views`는 내부 검증에 유지하며 현재 사용자 화면에는 시점 선택 콤보 상자·이전/다음 시점 버튼을 제공하지 않습니다. `walk`는 고정 눈높이의 수평 1인칭 이동 범위·속도를, `portals`는 진입 감지 상자와 대상 LINK를 선언합니다. 두 기능을 선언하지 않은 Room A/B는 기존 관찰 카메라와 동작을 유지합니다. 이미지 경로는 각 공간 안으로 제한하며 파일당 16 MiB·고유 참조 128개로 제한합니다. 로컬 파일은 canonical 포함 검사를, 원격 파일은 같은 공간의 HTTP 경로와 임시 스냅샷 검사를 거칩니다. 원격 공간의 다운로드 총합은 64 MiB입니다. 실제 이미지 디코딩과 텍스처 생성은 렌더러가 담당합니다. 자세한 형상·이미지·텍스트·시점·걷기·포탈 문법은 위 언어 명세를 따릅니다.

포탈은 이동한 눈 위치의 선분과 감지 상자의 교차로 진입을 판정합니다. `/hello-world` 같은 대상은 현재 LINK authority를 유지하고 명시적 `link://local/...`은 브라우저의 로컬 루트를 가리킵니다. 로컬 대상 디렉터리는 선언 시 존재해야 하며 원격 대상은 문법만 검증합니다. 진입한 뒤 로컬 읽기 또는 HTTP 다운로드·검증·최신 요청 우선 활성화 순서로 이동합니다. 선언만으로 대상 공간을 가져오거나 실행하지 않습니다.

LINK Browser 홈의 **흑룡이 전시회**는 `link://local/heukryong`을 열며 Hello World 공간은 복도 끝 포탈로 연결합니다. 사진을 더블클릭하면 큰 사진 보기가 열립니다. 홈에는 Hello World 바로 가기를 두지 않습니다.

Style 계산 순서는 실행기 기본값, defaults, 이름 있는 material 참조, class, id, Script override입니다. 같은 우선순위는 파일에서 뒤의 규칙이 이깁니다. color는 `#RRGGBB`, roughness·metalness·opacity는 0~1, wireframe은 bool입니다. override는 원본을 오염시키지 않습니다.

## 공개 Script 계약

정확한 훅 표기는 `on onStart(ctx)`, `on onUpdate(ctx, deltaSeconds)`, `on onInteract(ctx, event)`, `on onDestroy(ctx)`입니다. `store`는 바인딩별 영속 값, `let`은 콜백 지역 값입니다. 상태 읽기는 복사이며 ID와 상태는 렌더러 객체와 분리합니다.

`ctx.id`, `ctx.config`, `ctx.find(id)`, `getState`·`setState`, `getTransform`·`setTransform`, `setStyle`·`clearStyle`, `log`를 제공합니다. 자기 노드와 명시적 ID 조회만 사용하며 렌더러 포인터·OS 핸들·DOM·파일·네트워크 API는 없습니다. 인수·반환·실패·병합 규칙은 [API 표](../native/docs/language.md#ctx-공개-api)를 따릅니다. 런타임 노드 삭제 API는 v0.1에 없습니다.

각 콜백은 4,096 평가 단계와 중첩 호출 16, 인스턴스 저장소·현재 지역 값 합 64 KiB로 제한합니다. 반복문·재귀·import는 없습니다. 이는 평가 비용을 제한하는 방어 장치이고 운영용 완전 격리는 아닙니다.

## 탐색과 자원 소유권

1. 단일 백그라운드 worker가 파일 읽기·파싱·의미 검증·Script AST 준비를 수행합니다. 다음 요청은 하나만 보관하며 가장 최신 요청으로 교체합니다.
2. 취소 토큰과 generation ID를 함께 사용합니다. 늦은 결과는 활성화하지 않습니다. 준비 실패 때 기존 Runtime을 유지합니다.
3. 검증된 후보만 UI 스레드에서 기존 Runtime을 해제한 뒤 활성화합니다. 생성 실패 때 기존 세계는 이미 해제된 상태이며 오류·홈·주소로 복구합니다.
4. onStart를 한 번 실행합니다. Script 오류는 소스·노드·바인딩·훅을 기록하고 해당 바인딩을 중지합니다. 다른 Script와 화면은 유지합니다.
5. 프레임마다 실제 경과 시간을 0~.05초로 제한하여 onUpdate를 호출합니다. Element 클릭은 가장 가까운 바인딩 하나에 전달하고 버블링하지 않습니다. event.targetId에는 실제 클릭 Element ID가 들어갑니다.
6. 이동·홈·중지·reload·종료 때 모든 바인딩의 onDestroy를 역순으로 한 번 시도합니다. 중지된 바인딩도 포함합니다. 실패는 기록하고 나머지를 계속 정리합니다.

Runtime이 노드·상태·Script 인스턴스를 소유하고 공유된 불변 World가 원본 문서·AST를 소유합니다. 원격 World는 다운로드한 파일의 임시 스냅샷도 소유하므로 이미지 사용 중 파일을 유지하고 마지막 소유자가 해제되면 정리합니다. reload는 로컬 소스를 다시 읽거나 원격 소스를 새로 가져와 새 상태·store·override를 만듭니다. Script는 각 브라우저에서 실행하며 상태를 다른 방문자와 공유하지 않습니다. 원본과 기존 Runtime의 상태는 바뀌지 않습니다.

셸은 단일 Win32 메시지 루프, 하나의 timer, 한 쌍의 window procedure, 하나의 WGL context를 소유합니다. 공간 전환 중 이들을 다시 등록하지 않습니다. 기본 형상은 CPU에서 즉시 제출하고 사진은 공간별 OpenGL 텍스처로 보관합니다. 사진 확대 보기는 별도 텍스처 하나를 사용하며 사진을 바꾸거나 닫을 때 해제합니다. 이동·홈·중지·reload·실패 정리에서는 준비 작업과 현재 공간·확대 보기의 텍스처를 해제합니다. WGL context가 유효한 UI 스레드에서 GPU 삭제를 수행하며 종료 시 WGL/DC·폰트·브러시·worker도 정리합니다. 이 소유 구조만으로 드라이버 내부 메모리 누수가 없다고 주장하지 않습니다.

이미지는 Windows Imaging Component로 디코딩합니다. 배치된 사진은 긴 변 최대 512 px로 준비하고 큰 사진 보기는 최대 2,048 px로 준비합니다. 현재 표시용 파생본은 최대 1,280 px이므로 확대 보기에서도 그 해상도를 넘겨 새 세부 정보를 만들지 않습니다. 텍스처 준비 작업은 프레임당 하나씩 진행하며 각 작업 사이에 화면·입력 처리를 이어갑니다. 한 파일의 동기 디코딩 시간을 강제로 선점하는 구조는 아닙니다.

## 렌더러 한계

OpenGL 고정 기능 조명에서 roughness는 shininess, metalness는 specular 강도로 근사합니다. opacity<1은 blending을 켜고 깊이 쓰기를 끈 후 요소 중심의 카메라 깊이순으로 정렬합니다. 복잡하게 교차하는 투명 형상의 완전한 정렬은 보장하지 않습니다. opacity=0은 렌더·클릭 대상에서 제외합니다. plane은 양면입니다.

창 크기·DPI 변경 시 viewport와 글꼴 배치를 갱신하고 최소화·0 크기 타깃에서는 렌더링을 건너뜁니다. 현재 구현은 Windows 네이티브 셸을 대상으로 하며, 다른 OS·다중 모니터 사이의 DPI 이동·장시간 성능은 지원 범위를 확장할 때 별도 확인이 필요합니다.
