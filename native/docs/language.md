# LINK World Standards · 제작 언어 v0.1

LINK Browser의 실제 C++ lexer·parser·AST 인터프리터 계약입니다. **Element**, **Script**, **Style**은 LINK World Standards를 구성하는 실험적 v0.1 문법이며 확정·공인된 표준이 아닙니다. JSON 장면 파일이나 JavaScript/Lua 실행기를 사용하지 않습니다. 아래 데이터 리터럴의 일부 표기는 익숙한 배열·맵 표기를 따르지만 선언·문서 구조와 실행 의미는 이 규격으로 파싱합니다.

## 공통 어휘와 데이터

소스는 BOM 없는 UTF-8입니다. 식별자는 ASCII 영문자 또는 `_`로 시작하고 이후 영문자·숫자·`_`를 사용합니다. `door_hinge`는 식별자이고 `room-a`는 따옴표가 필요한 문자열입니다. 공백·줄바꿈과 `//`부터 줄 끝까지의 주석을 무시합니다. 블록 주석은 없습니다.

문자열은 큰따옴표로 감싸며 `\n`, `\r`, `\t`, `\"`, `\\`만 escape로 지원합니다. 한글 등 UTF-8 문자를 직접 넣을 수 있습니다. 작은따옴표, `\u` escape, raw 제어 문자는 거절합니다. 따옴표 안의 `"on"`, `"["`, `"-"`는 문법 토큰이 아닌 문자열 값입니다.

```ebnf
identifier = (letter | "_"), {letter | digit | "_"};
number     = (digits, [".", {digit}] | ".", digits),
             [("e" | "E"), ["+" | "-"], digits];
string     = '"', {utf8Character | escape}, '"';
value      = ["+" | "-"], number | string | "true" | "false" | "null"
           | "[", [value, {",", value}], "]"
           | "{", [key, ":", value, {",", key, ":", value}], "}";
key        = identifier | string;
field      = identifier, ":", value, ";";
```

`letter`는 ASCII A–Z/a–z, `digits`는 하나 이상의 숫자입니다. 숫자는 표현 가능한 유한 double이어야 합니다. `.15`, `1.`, `1e-3`, `-2`를 사용할 수 있습니다. 부호는 lexer에서 별도 토큰입니다. 배열·맵 구분자는 쉼표, 선언 필드 끝은 세미콜론이며 후행 쉼표는 허용하지 않습니다. 맵의 중복 키도 오류입니다. null·bool·문자열·숫자·배열·맵 외 데이터 타입은 없습니다.

오류는 `파일:행:열: 메시지`로 표시합니다. 행과 열은 1부터 시작하며 열은 UTF-8 **byte 위치**입니다. 필드·구문 오류는 해당 위치, 누락 필드는 선언 시작 위치를 사용합니다. 런타임 Script 오류에는 바인딩 노드 ID·파일·생명주기도 추가합니다.

## 진입 문서와 Element

```ebnf
worldDocument = "world", "{", {field}, "}";
sceneDocument = "scene", "{", {object}, "}";
object        = "object", identifier, "{", {field | component}, "}";
component     = "component", identifier, "{", {field | component | element}, "}";
element       = "element", identifier, "{", {field}, "}";
```

`world.link`의 모든 필수 필드는 아래 예시에 있습니다. 선택 필드 `views`로 이름 있는 카메라 위치를, `environment`로 배경색과 격자 표시를, `walk`와 `portals`로 1인칭 이동과 포탈을 지정할 수 있습니다. 미지원 필드·중복 필드·`version`이 `"0.1"`이 아닌 문서는 거절합니다. `scripts`는 비어 있어도 되며 파일 참조 문자열과 해석된 실제 파일 모두 중복을 거절합니다. 상대 참조는 `world.link`가 위치한 공간 디렉터리 기준입니다. LINK의 허용 주소와 경로 제한은 [로컬 LINK 명세](../../docs/link-local-v0.1.md)를 따릅니다.

```text
world {
  version: "0.1";
  id: "room-a";
  name: "Mint Studio";
  elements: "scene.element";
  style: "appearance.style";
  scripts: ["scripts/door.script"];
  entry: {position: [7, 6, 9], target: [0, 1, 0]};
  environment: {background: "#E9E5DD", grid: false};
  views: [
    {name: "입구", position: [7, 6, 9], target: [0, 1, 0]},
    {name: "작품 정면", position: [0, 1.6, 4], target: [0, 1.6, 0]}
  ];
}
```

`views`는 최대 128개 맵의 배열입니다. 각 맵에는 비어 있지 않은 문자열 `name`, 세 성분 벡터 `position`, `target`만 들어가며 세 필드는 모두 필수입니다. 좌표의 각 성분은 유한수이고 절댓값이 1,000,000 이하입니다. 목록 순서를 유지하며 진입 카메라 `entry`를 바꾸지 않습니다. `name`은 표시 문자열이고 노드 ID가 아닙니다. 이 기능은 저장된 관찰 위치이며 카메라 경로·보간 애니메이션을 선언하지 않습니다. 현재 셸은 이 메타데이터를 내부 검증에 사용하며 사용자 화면에 시점 선택 콤보 상자·이전/다음 시점 버튼을 표시하지 않습니다.

`environment`는 선택 맵이며 허용 필드는 `background`, `grid`, `fog`입니다. `background`는 `#RRGGBB` 문자열, `grid`는 bool입니다. 기본값은 배경 `#111c21`, 격자 표시 `true`, 거리 안개 없음입니다. `grid:false`는 공간의 관찰용 격자를 숨깁니다. 잘못된 타입·색상·알 수 없는 필드·중복 키는 준비 단계 오류입니다.

`fog`를 지정하면 `{color:"#08171F",start:85,end:280}`처럼 선형 거리 안개를 선언합니다. `start`와 `end`는 필수 유한수이며 0~500 범위, start < end입니다. 선택 `color`는 `#RRGGBB`이고 생략하면 배경색을 사용합니다. 시작 거리까지 원래 색을 유지하고 끝 거리에서 안개색에 도달하며, 공간 전환 때 새 환경 설정을 적용합니다.

### 1인칭 걷기와 포탈

`walk`를 선언하면 셸은 WASD·방향키로 수평 이동하고 마우스 드래그로 시선을 돌리는 걷기 카메라를 사용합니다. `walk`가 없는 공간은 기존 관찰 카메라를 사용합니다. 아래 필드는 world 블록 안에 넣습니다.

```text
walk: {
  min: [-3,0,-52], max: [3,4,5],
  eyeHeight: 1.65, speed: 3.4
};
portals: [{
  id: "hello_portal", address: "link://local/hello-world",
  position: [0,1.65,-51.6], size: [2.4,3.3,1]
}];
```

walk 맵의 `min`·`max`는 필수 세 성분 벡터이며 각 수치는 유한하고 절댓값 1,000,000 이하입니다. 모든 축에서 min < max여야 합니다. 선택 `eyeHeight`는 기본 1.65, 범위 0.25~10이고 minY~maxY 안이어야 합니다. 선택 `speed`는 기본 3.4m/s, 범위 0.1~20입니다. 알 수 없는 필드·잘못된 타입·중복 키·범위 오류를 거절합니다.

이동은 시선의 전방 벡터를 XZ 평면에 투영하고 대각선 입력을 정규화합니다. Y는 eyeHeight로 고정하며 XZ 위치를 선언 범위 안으로 제한합니다. 한 갱신의 이동 시간은 최대 .1초로 제한합니다. 이 범위 검사는 일반 형상 충돌·계단·중력 계산이 아닙니다. 사진 확대 보기 중 방향키는 사진 넘기기에 사용합니다.

`portals`는 선택 배열이며 최대 16개입니다. 각 맵에는 `id`, `address`, `position`, `size`가 모두 필요하고 그 외 필드는 허용하지 않습니다. id는 128 bytes 이하의 native 식별자이며 포탈 목록 안에서 유일해야 합니다. position은 카메라 눈 위치를 감지하는 축 정렬 상자의 중심, size는 전체 폭·높이·깊이입니다. position에는 기존 유한 Vec3 좌표 상한을 적용하고 size의 각 성분은 0 초과 1,000 이하입니다. 포탈의 시각적 테두리는 별도 Element 형상으로 선언합니다.

address는 `resolveLink`의 동일한 로컬 authority·경로·인코딩·canonical 포함 규칙을 지켜야 하고 대상 디렉터리는 존재해야 합니다. 선언을 검증할 때 대상 공간 문서를 실행하거나 네트워크 요청을 하지 않습니다. 실제 포탈 진입 때 대상 주소를 탐색 요청으로 보내고 기존 파일 준비·검증·generation·취소·활성화 규칙을 따릅니다.

진입 판정은 이전 눈 위치에서 새 눈 위치까지의 선분과 상자를 비교하여 가장 먼저 만나는 포탈을 선택합니다. 한 프레임에 얇은 상자를 통과하는 경우도 감지하며, 시작점이 이미 그 포탈 안이면 반복 진입으로 처리하지 않습니다. walk·portals를 선언하지 않은 기존 Room A/B는 변경하지 않습니다.

공간 최상위는 Object이며 Object 자식은 Component, Component 자식은 Component·Element, Element는 잎입니다. ID는 모든 kind를 통틀어 공간 내에서 유일합니다. 모든 노드는 `name`, `position`, `rotation`, `scale`, `visible`을 선택 필드로 가집니다.

| kind | 전용 필드 |
| --- | --- |
| Object·Component | 선택 `state` 맵, 단일 `script` 문자열, `config` 맵. config에는 script가 필요 |
| Element | 필수 `geometry` 맵, 선택 `style` 재질 이름 문자열, `classes` 식별자 문자열 배열. plane에만 선택 `image` 또는 `text` |

Element에 state·script·자식을 넣을 수 없습니다. Component 템플릿·상속은 없습니다. script는 매니페스트 `scripts`의 문자열과 정확히 일치해야 합니다. 아래는 문짝을 한쪽 모서리에서 회전시키는 최소 구성입니다.

```text
scene {
  object door {
    state: {isOpen: false};
    component door_hinge {
      state: {isOpen: false};
      script: "scripts/door.script";
      config: {owner: "door", openAngle: -100, speed: 165};
      element door_panel {
        position: [.75, 1.4, 0];
        geometry: {type: "box", size: [1.5, 2.8, .12]};
        style: "mint";
        classes: ["door"];
      }
    }
  }
}
```

오른손 좌표계, Y-up, 길이 m, 시간 초입니다. position·rotation·scale은 `[x,y,z]`, 회전은 degree Euler XYZ입니다. 변환 행렬은 `T × Rx × Ry × Rz × S`이고 부모 변환을 자식 앞에 곱합니다. 기본값은 위치·회전 `[0,0,0]`, 배율 `[1,1,1]`, visible true입니다. 부모가 숨겨지면 자식도 렌더링·클릭 대상에서 숨겨집니다.

위치·회전 벡터의 각 성분 절댓값은 1,000,000 이하입니다. 배율은 각 성분이 0 초과 1,000 이하입니다. 지원하는 geometry 맵은 다음과 같습니다. 표의 모든 필드는 필수이고 다른 필드는 거절합니다.

| 형상 | geometry 예시 | 범위·의미 |
| --- | --- | --- |
| box | `{type:"box",size:[1,2,.12]}` | 중심을 원점으로 하는 세 크기, 각 0 초과 1,000 이하 |
| sphere | `{type:"sphere",radius:.5}` | 원점 중심 구체, 반지름 0 초과 1,000 이하 |
| plane | `{type:"plane",size:[3,2]}` | 로컬 XY 평면, 법선 +Z, 가로·세로 각 0 초과 1,000 이하 |
| cylinder | `{type:"cylinder",radius:1,radiusTop:.7,height:3}` | Y축 방향, 중심 원점. 아래쪽 반지름 `radius`, 위쪽 반지름 `radiusTop`, 높이 `height` |
| frame | `{type:"frame",size:[3,2],border:.06,depth:.08}` | XY 방향 직사각 테두리. size는 바깥 가로·세로, border는 테두리 너비, depth는 Z방향 두께 |

cylinder의 아래 반지름과 높이는 0 초과 1,000 이하이며 위 반지름만 0 이상 1,000 이하입니다. `radiusTop:0`으로 원뿔 끝을 표현합니다. frame의 크기·border·depth는 모두 0 초과 1,000 이하이며 border는 더 작은 바깥 크기의 절반보다 작아야 합니다. frame은 안쪽이 빈 테두리이고 image가 들어가는 plane과 별도 Element로 배치합니다. 바닥 plane은 보통 `rotation:[-90,0,0]`을 사용합니다.

### 이미지와 제목 표지

plane Element에는 공간 내부 비트맵 경로 `image` 또는 UTF-8 문자열 `text`를 넣을 수 있습니다. 두 필드는 함께 사용할 수 없으며 Object·Component와 plane 이외 형상에는 허용하지 않습니다. 기존 style·class·transform도 사용할 수 있습니다.

```text
scene {
  object exhibit {
    component display {
      element photo {
        geometry: {type:"plane",size:[2.8,1.8]};
        image: "assets/photo01.jpg";
        position: [0,1.8,0];
      }
      element frame {
        geometry: {type:"frame",size:[2.94,1.94],border:.06,depth:.08};
        position: [0,1.8,0];
      }
      element title {
        geometry: {type:"plane",size:[2.8,.22]};
        text: "사진 01 · 고양이";
        position: [0,.7,.03];
      }
    }
  }
}
```

`image`는 비어 있지 않은 상대 파일 참조이며 `world.link`의 공간 디렉터리를 기준으로 해석합니다. 확장자는 대소문자를 구분하지 않는 jpg·jpeg·png·bmp만 지원합니다. 파일의 canonical 경로가 해당 공간 안의 일반 파일이어야 하므로 절대 경로·`..`·링크를 통한 공간 밖 참조도 거절합니다. 고유 참조 문자열 기준 최대 128개이고 파일별 실제 크기는 최대 16 MiB입니다. 같은 문자열을 여러 plane에서 사용하면 준비된 파일 항목을 공유합니다. `World.images`는 원래 image 문자열을 키로 하며 해석한 파일 경로를 보관합니다.

코어 로더는 원본 이미지를 수정하거나 디코딩하지 않습니다. 경로·확장자·크기·존재 여부를 활성화 전에 검사하고 렌더러가 파일을 읽어 텍스처로 디코딩합니다. 확장자가 맞아도 이미지 내용이 손상되었으면 렌더러 활성화 단계에서 실패할 수 있습니다. 기본 제공 예제의 파생 자산 생성과 원본 사진의 보관은 로더와 별개입니다. 새 이미지 파일 누락 같은 준비 실패는 이미 실행 중인 공간을 유지합니다.

`text`는 최대 1,024 UTF-8 bytes의 일반 문자열입니다. 한글은 byte 수로 계산하며 HTML·마크업·폰트 명령을 해석하지 않습니다. 빈 문자열도 허용합니다. 렌더러의 네이티브 텍스트 래스터화로 plane용 텍스처를 만들며 문서에는 OS 핸들·폰트 객체를 노출하지 않습니다. 이미지·텍스트 plane은 일반 부모 transform과 visible을 따릅니다.

Hello World 공간의 큰 입체 제목은 이 text plane 기능을 사용하지 않습니다. 제작 생성기가 7행 블록 글리프의 가로로 이어진 칸을 box Element로 병합하고 깊이 .74m를 부여합니다. 따라서 제목의 앞면·옆면·두께는 일반 3D 형상이며 텍스트 텍스처나 외부 글꼴 모델을 필요로 하지 않습니다. 공간 안의 안내 문장은 별도의 text plane을 사용할 수 있습니다.

## Style

```ebnf
styleDocument = {styleDeclaration};
styleDeclaration = "defaults", "{", {field}, "}"
                 | ("material" | "class" | "id"), identifier, "{", {field}, "}";
```

```text
defaults { color: "#D8E7DF"; roughness: .8; }
material mint { color: "#A8D6BF"; }
class door { roughness: .65; }
id door_panel { color: "#83B79D"; }
```

지원 속성은 `color` (`#RRGGBB`), `roughness`·`metalness`·`opacity` (0~1의 유한수), `emission` (0~4의 유한수), `wireframe`·`textBackground` (bool)입니다. 기본 재질은 색 `#c4d9d0`, roughness .65, metalness 0, opacity 1, emission 0, wireframe false, textBackground true입니다. defaults는 한 번, material 이름은 한 번만 선언합니다. 같은 class·id 규칙은 여러 번 선언할 수 있습니다.

| textBackground | text plane 표현 | color의 의미 |
| --- | --- | --- |
| `true` (기본값) | 불투명 배경 위에 명암 대비를 자동 선택한 문자 | 배경색 |
| `false` | 배경이 투명한 문자, 글자 가장자리는 coverage alpha로 부드럽게 처리 | 문자색 |

```text
material hologram { color: "#A3F9F2"; textBackground: false; opacity: .94; }
```

이 재질을 `text`가 있는 plane의 `style`로 지정하면 청록색 문자가 공간 위에 떠 있는 것처럼 보입니다. `textBackground:false`인 텍스트는 opacity가 1이어도 투명 pass에서 정렬하고 깊이 쓰기를 꺼 투명 배경이 뒤의 형상을 가리지 않게 합니다. 텍스트가 없는 형상에는 textBackground가 외형을 바꾸지 않습니다.

텍스트 plane의 color·textBackground는 공간을 로드할 때 정적 Style로 확정됩니다. 이 두 속성의 Script override는 텍스처를 다시 만들지 않으므로, 보이는 문자색·배경을 바꾸려면 Style 파일을 고친 뒤 reload합니다. opacity와 부모 transform·visible은 실행 중에도 화면에 반영됩니다. 투명 pass는 변경되지 않은 텍스처의 배경 모드와 현재 opacity를 기준으로 결정합니다.

`emission`은 자체 색을 밝히고 부드러운 가산 광륜을 더합니다. 0이면 기존 표면 조명만 사용합니다. 현재 렌더러는 강도를 `emission / (1 + emission)`으로 압축해 색상을 유지하며, 주변 물체의 조명은 장면 조명으로 계산합니다. 값은 material·선택자·Script override의 동일한 우선순위를 따릅니다.

계산 순서는 **실행기 기본값 < defaults < Element의 style이 가리키는 material < class < id < Script override**입니다. 같은 우선순위는 파일에서 뒤에 오는 규칙이 속성별로 이깁니다. 여러 class의 충돌도 `classes` 배열 순서가 아닌 규칙의 파일 순서입니다. 생략한 속성은 앞선 계층의 값을 유지합니다. 없는 material 참조·알 수 없는 속성·범위 오류는 활성화 전에 거절합니다.

class·id에는 식별자 하나만 사용합니다. 상속·복합 선택자·레이아웃·사용자 shader는 없습니다. Script override는 원본 Style을 변경하지 않으며 해제하면 계산된 원래 재질로 돌아갑니다. opacity는 렌더러의 blending·깊이 처리에 반영합니다. 파일 수정 뒤 reload하면 재컴파일 없이 반영합니다.

## Script 문법

```ebnf
script       = hook, {hook};
hook         = "on", hookName, "(", parameters, ")", block;
block        = "{", {statement}, "}";
statement    = ("store" | "let"), identifier, "=", expression, ";"
             | identifier, "=", expression, ";"
             | "if", "(", expression, ")", block, ["else", block]
             | expression, ";";
expression   = primary, {binaryOperator, primary};
primary      = literal | identifier | "(", expression, ")"
             | ("!" | "-" | "+"), primary
             | primary, ".", identifier
             | primary, "[", expression, "]"
             | "[", [expression, {",", expression}], "]"
             | "{", [key, ":", expression, {",", key, ":", expression}], "}"
             | builtin, "(", [arguments], ")"
             | "ctx", ".", apiName, "(", [arguments], ")";
arguments    = expression, {",", expression};
```

위 expression EBNF는 아래 연산자 우선순위와 결합 규칙으로 해석합니다. `literal`은 숫자·문자열·bool·null입니다. 호출 가능한 대상은 열거한 builtin과 `ctx` API뿐입니다. 임의 함수 선언·호출, 반환문, 반복문, 재귀, import, 동적 코드 실행은 없습니다. API를 변수로 얻거나 `ctx["method"](...)`로 호출할 수 없습니다.

선언 가능한 훅은 아래 네 가지뿐이며 **이름·매개변수 철자·개수까지 일치**해야 합니다. 각 훅은 파일당 한 번이며 하나 이상의 훅이 필요합니다. 생략한 훅은 아무 동작도 하지 않습니다.

```text
on onStart(ctx) { store clicks = 0; }
on onUpdate(ctx, deltaSeconds) { }
on onInteract(ctx, event) { clicks = clicks + 1; }
on onDestroy(ctx) { ctx.log("destroyed"); }
```

전체 실행 예제는 [door.script](../worlds/room-a/scripts/door.script), [spin.script](../worlds/room-a/scripts/spin.script), [stone.script](../worlds/room-b/scripts/stone.script)를 참고합니다.

`store`는 바인딩별 영속 저장소를 선언합니다. 일반적으로 onStart에서 초기화합니다. 같은 store 이름을 다시 선언하면 오류입니다. `let`은 이번 콜백에만 존재하며 종료 시 제거합니다. 블록별 별도 scope·shadowing은 없고 같은 콜백에서 이름을 재선언할 수 없습니다. 선언된 변수만 대입할 수 있습니다. `ctx`, `deltaSeconds`, `event`와 문법 키워드는 변수명으로 사용할 수 없으며 훅 인수는 읽기 전용입니다. 맵·배열 멤버에 직접 대입하는 구문은 없으며 새 값을 만들거나 API로 상태를 변경합니다.

| 우선순위 (높은 순) | 연산 |
| --- | --- |
| 1 | `.field`, `[index]`, 허용된 호출 |
| 2 | 단항 `!`, `-`, `+` |
| 3 | `*`, `/`, `%` |
| 4 | `+`, `-` |
| 5 | `<`, `<=`, `>`, `>=` |
| 6 | `==`, `!=` |
| 7 | `&&` |
| 8 | `||` |

이항 연산은 같은 우선순위에서 왼쪽 결합입니다. `&&`·`||`는 단락 평가하며 bool만 받습니다. if 조건·단항 `!`도 bool만 받습니다. 산술·관계 연산은 숫자이며 `+`는 문자열 두 개의 연결도 지원합니다. 암묵적 문자열·숫자·bool 변환은 없습니다. `/`·`%`의 0 제수와 비유한 결과는 오류입니다. `%`는 실수 나머지이며 음수 피연산자의 부호를 따릅니다. 동등 비교는 타입을 포함하고 배열·맵의 내용을 비교합니다.

배열 index는 범위 안의 0 이상 정수입니다. 맵은 `.field` 또는 `["key"]`로 읽습니다. 없는 필드·키, 잘못된 타입·범위는 오류입니다. 배열 길이·추가·삭제 메서드는 없습니다.

숫자 builtin은 `min(a,b)`, `max(a,b)`, `abs(a)`, `clamp(value,min,max)`, `mix(a,b,t)`입니다. clamp에서 min > max는 오류입니다. mix는 `a + (b-a)*t`이며 t를 자동으로 0~1에 제한하지 않습니다.

## ctx 공개 API

`ctx.id`는 바인딩 노드 ID, `ctx.config`는 Element 문서의 해당 바인딩 config 맵입니다. 같은 파일의 파싱 결과는 불변 상태로 공유하지만 `store`, 콜백 지역 값, ctx는 바인딩마다 분리합니다. 노드당 바인딩은 하나입니다. state·transform 읽기는 값의 사본입니다.

| API | 인수·반환·실패 규칙 |
| --- | --- |
| `ctx.find(id)` | 현재 공간에 존재하는 노드 ID 문자열 반환; 없으면 오류 |
| `ctx.getState()` / `ctx.getState(id)` | 자기 또는 지정 노드의 state 맵 사본 |
| `ctx.setState(patch)` / `ctx.setState(id,patch)` | Object·Component state에 최상위 키를 얕게 병합. patch는 맵, 중첩 키는 전체 교체 |
| `ctx.getTransform()` / `ctx.getTransform(id)` | position·rotation·scale·visible 맵 사본 |
| `ctx.setTransform(patch)` / `ctx.setTransform(id,patch)` | 전달한 transform 필드 교체. 벡터 전체 필요, 문서와 같은 범위 검증 |
| `ctx.setStyle(id,patch)` | Element에만 검증된 재질 override 속성별 병합 |
| `ctx.clearStyle(id)` | Element의 override 전체 제거 |
| `ctx.log(value)` | 값을 표시 문자열로 기록. 바인딩 ID 포함 |

읽기·조회 API 외 호출은 null을 반환합니다. 노드 ID와 style 색은 문자열입니다. 인수 개수·타입·노드 kind·없는 ID·알 수 없는 필드·예산 초과는 Script 오류입니다. `find()`는 포인터나 API 객체가 아닌 검증된 ID 문자열을 반환합니다. 파일·네트워크·OS·렌더러·셸 UI 접근 API와 런타임 노드 삭제 API는 없습니다.

Room A의 문은 힌지 Component의 `isOpen`을 갱신하고 owner Object에 같은 값을 명시적으로 기록합니다. Room B의 두 돌은 Object의 `strength`·`broken`만 소유 상태로 사용합니다. 공통 stone Script는 `ctx.find(ctx.config.owner)`로 자기 owner만 변경하고 Component의 `hits` 및 인스턴스 store의 hits를 기록합니다. 두 돌은 같은 구조·외형이며 Object 강도는 10·100입니다. 앞의 1칸·10칸은 초기 강도의 정적 표식입니다.

클릭은 Element에서 가장 가까운 상위 **바인딩 하나**로 전달하고 버블링하지 않습니다. 중지된 바인딩도 경계를 유지하므로 더 위로 전달하지 않습니다. event는 `{type:"click",targetId:"실제 Element",bindingId:"바인딩 노드"}`입니다. UI 클릭·카메라 드래그는 공간 상호작용에서 제외합니다.

onUpdate의 deltaSeconds는 실제 경과 초를 0~.05로 제한한 값입니다. 긴 중단 시간을 한 프레임에 따라잡지 않습니다. reload는 파일·AST를 다시 읽고 모든 store·상태·transform·override를 초기화한 새 인스턴스를 만듭니다. 예외 시 해당 바인딩을 중지하며 다른 바인딩은 계속 실행합니다. 이미 수행한 API 변경은 롤백하지 않습니다. 중지된 바인딩도 공간 종료 때 onDestroy를 한 번 시도하며 종료 중 오류는 기록하고 나머지 정리를 계속합니다.

## 입력·실행 예산과 보안 경계

| 대상 | 상한 |
| --- | ---: |
| world.link / Element·Style 각 문서 / Script 각 파일 | 64 KiB / 1 MiB / 128 KiB |
| 소스별 토큰 / Script AST 노드 | 40,000 / 20,000 |
| 파서·식 AST 깊이 | 64 |
| 전체 노드 / 노드 깊이 (루트=1) / script 파일 | 500 / 16 / 16 |
| Element classes | 64 |
| image 고유 참조 / 이미지 파일 각각 | 128 / 16 MiB |
| text 문자열 | 1,024 UTF-8 bytes |
| 이름 있는 카메라 views | 128 |
| 포탈 목록 / 포탈 ID | 16개 / 128 bytes |
| 문자열·맵 키 | 8,192 UTF-8 bytes |
| 노드 state / config 각각 | 65,536 추정 bytes |
| 콜백별 문장·식 평가 단계 / 중첩 호출 깊이 | 4,096 / 16 |
| 인스턴스 store + 현재 콜백 지역 값·인수 | 65,536 추정 bytes |

KiB는 1,024 bytes입니다. 논리 데이터 크기는 숫자 8 bytes, bool·null 1 byte, 문자열 길이+1, 배열은 1+자식 합, 맵은 1+키 길이+1+값 합으로 계산하며 C++ allocator·컨테이너 전체 heap 비용을 뜻하지 않습니다. store·지역 변수 변경은 변경 후보를 먼저 검증하므로 초과한 값은 저장하지 않습니다. 임시 배열·맵과 문자열도 제한합니다. 단계·호출 예산은 콜백마다 초기화합니다. 수치·API·예산 오류도 소스 위치를 유지합니다.

파서·검증·파일 예산 초과는 후보 활성화 전에 실패합니다. 콜백 평가·인스턴스 저장소 예산은 실행 중 해당 바인딩만 중지합니다. 평가 한도는 AST 인터프리터가 직접 확인하며 wall-clock timeout으로 실행 중인 C++ 코드를 선점한다고 주장하지 않습니다.

제한 API와 평가 예산은 방어 장치이며 완전한 운영용 샌드박스가 아닙니다. 네이티브 파서·렌더러 오류에 대한 프로세스 격리, 공개 원격 문서 검증, 서버 권한·공유 상태는 없습니다. 신뢰하는 로컬 제작 문서만 대상으로 합니다.
