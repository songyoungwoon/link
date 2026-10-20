# Style — 재질과 선택자

Style 문서는 형상의 **색·표면·투명도·표시 방식**을 정합니다. 공통 재질을 이름으로 만들고, class·id 규칙으로 필요한 Element만 조정합니다.

[가이드 시작](README.md) · [Element](element.md) · [Script](script.md) · [실제 Style 파일](examples/first-world/appearance.style)

## 1. 예제 파일 전체

다음은 `first-world/appearance.style`의 전체 내용입니다.

```text
defaults {
  color: "#CBD8D1";
  roughness: .8;
  metalness: 0;
  opacity: 1;
  wireframe: false;
}

material floor_material { color: "#DCD8CD"; roughness: .95; }
material pedestal_material { color: "#31484A"; roughness: .85; }
material cube_material { color: "#4E9C93"; roughness: .6; metalness: .15; }

class interactive { roughness: .35; }
id sample_cube { color: "#6CCFC1"; }
```

매니페스트에는 `style:"appearance.style";`로 파일을 등록합니다. 이 파일은 `world`나 `scene` 블록 없이 선언들을 바로 나열합니다. 선언 안의 속성은 세미콜론으로 끝냅니다.

## 2. 네 가지 선언

| 선언 | 적용 대상 | 예제에서의 역할 |
| --- | --- | --- |
| `defaults { ... }` | 모든 Element의 공통 출발값 | 불투명·단색 재질 기본값 |
| `material 이름 { ... }` | `style:"이름"`을 지정한 Element | 바닥·받침대·큐브의 기본 재질 |
| `class 이름 { ... }` | `classes`에 해당 이름이 있는 Element | 상호작용 가능한 형상의 roughness 조정 |
| `id 이름 { ... }` | ID가 정확히 일치하는 Element | sample_cube의 최종 기본색 |

예제의 큐브에는 다음 연결이 있습니다.

```text
element sample_cube {
  geometry: {type: "box", size: [1.8, 1.8, 1.8]};
  style: "cube_material";
  classes: ["interactive"];
}
```

이 Element는 `cube_material`, `class interactive`, `id sample_cube`를 모두 적용받습니다. `name` 표시 문자열은 선택자와 관계없습니다. Node ID를 바꾸면 해당 `id` 규칙과 Script의 대상 ID도 함께 확인합니다.

`defaults`는 파일에 한 번만, 같은 `material` 이름도 한 번만 선언할 수 있습니다. `class`와 `id` 규칙은 같은 이름으로 여러 번 선언할 수 있습니다. 재질·선택자 이름은 영문자 또는 `_`로 시작하는 식별자입니다.

## 3. 속성마다 계산하는 우선순위

낮은 순서부터 다음과 같이 적용됩니다.

```text
실행기 기본값 → defaults → material → class → id → Script override
```

뒤 단계가 **자기가 지정한 속성만** 바꿉니다. 색만 지정한 id 규칙이 앞에서 정한 roughness까지 없애지 않습니다.

| 속성 | defaults | cube_material | class interactive | id sample_cube | 클릭 전 결과 |
| --- | --- | --- | --- | --- | --- |
| color | `#CBD8D1` | `#4E9C93` | 유지 | `#6CCFC1` | `#6CCFC1` |
| roughness | .8 | .6 | .35 | 유지 | .35 |
| metalness | 0 | .15 | 유지 | 유지 | .15 |
| opacity | 1 | 유지 | 유지 | 유지 | 1 |
| wireframe | false | 유지 | 유지 | 유지 | false |

같은 우선순위에서 충돌하면 **Style 파일에 나중에 쓴 규칙**이 속성별로 이깁니다. `classes:["a","b"]`의 배열 순서를 바꿔도 class 충돌 결과는 변하지 않습니다. class보다 높은 id 규칙은 파일 앞부분에 있어도 class보다 우선합니다.

## 4. 지원 속성과 범위

| 속성 | 값 | 의미 |
| --- | --- | --- |
| `color` | `"#RRGGBB"` 문자열 | 기본색, 대·소문자 hex 허용 |
| `roughness` | 0~1 유한수 | 표면 거칠기 재질 값 |
| `metalness` | 0~1 유한수 | 금속성 재질 값 |
| `opacity` | 0~1 유한수 | 1은 불투명, 작은 값은 더 투명 |
| `emission` | 0~4 유한수 | 자체 발광색과 부드러운 광륜의 강도 |
| `wireframe` | `true` 또는 `false` | 선으로 형상 표시 |
| `textBackground` | `true` 또는 `false` | text plane의 배경 표시 여부. false이면 color는 문자색 |

아무 규칙도 지정하지 않았을 때 실행기 기본값은 color `#c4d9d0`, roughness .65, metalness 0, opacity 1, emission 0, wireframe false, textBackground true입니다. 실제 조명과 외형은 현재 네이티브 렌더러의 표현을 따릅니다.

빛나는 유도등이나 별은 `material beacon {color:"#8DEBDD";emission:1.8;}`처럼 정의합니다. emission은 재질 자신의 색과 광륜을 밝히며, 주변 형상은 장면 조명으로 빛을 받습니다. 먼 풍경을 배경색에 섞으려면 `world.link`의 `environment.fog`를 사용합니다. [환경 설정](../../native/docs/language.md#진입-문서와-element)에서 거리 범위를 확인하세요.

투명 배경 안내 문자는 다음 재질을 `text`가 있는 plane에 연결합니다.

```text
material hologram_text {
  color: "#A3F9F2";
  textBackground: false;
  opacity: .94;
}
```

문자만 청록색으로 표시하고 나머지 부분은 투명하게 처리합니다. 부드러운 글자 가장자리에는 coverage alpha를 사용하며, opacity가 1이어도 뒤의 풍경을 볼 수 있습니다. 기본값 true는 color를 배경색으로 쓰고 그 위에 명암 대비를 자동 선택한 문자를 표시합니다. textBackground는 text plane에만 영향을 줍니다.

텍스트의 color·textBackground는 공간 로딩 때 정적 Style로 래스터화합니다. Script로 이 두 값을 override해도 기존 텍스처를 다시 만들지 않으므로, 보이는 문자색·배경을 바꾸려면 Style 문서를 수정하고 새로 고침합니다. opacity와 부모 transform·visible은 실행 중에 반영되므로, 배경 없는 안내판을 천천히 움직이거나 흐리게 할 수 있습니다.

`"red"`, `"#fff"`, `rgb(...)`, `"50%"` 같은 표기는 지원하지 않습니다. `opacity:1.5`, `wireframe:1`, `textBackground:"false"`도 오류입니다. 다른 속성 이름, CSS 복합 선택자, 배치 규칙, 상속, 사용자 shader 선언은 없습니다.

## 5. 클릭 색상과 복원

예제 Script의 첫 클릭은 다음 API로 색상만 덮어씁니다.

```text
ctx.setStyle(visual, {color: ctx.config.activeColor});
```

이 코드에서 `visual`은 `sample_cube` ID 문자열이고 `activeColor`는 Component config의 `"#F1AE66"`입니다. Script override가 가장 우선하므로 id 규칙의 민트색 대신 주황색이 보입니다. roughness와 metalness는 계속 앞서 계산한 값을 사용합니다.

두 번째 클릭은 다음 API를 실행합니다.

```text
ctx.clearStyle(visual);
```

`clearStyle`은 해당 Element의 **모든 Script override**를 제거합니다. 계산된 기본 재질로 돌아가기 때문에 큐브는 `id sample_cube`의 민트색으로 복원됩니다. Style 파일 자체를 수정하는 API가 아닙니다.

`setStyle`은 Element ID와 지원 속성 맵을 받습니다. `material` 이름을 전달하거나 `style` 속성을 바꾸는 호출은 지원하지 않습니다. `setStyle`을 여러 번 호출하면 지정 속성을 병합하고, `clearStyle`을 호출하거나 새 공간 인스턴스를 만들 때까지 유지합니다.

## 6. 값 하나씩 바꿔 보기

먼저 `id sample_cube`의 색을 바꾸고 저장한 뒤 새로 고침합니다.

```text
id sample_cube { color: "#729BE0"; }
```

큐브가 파란색으로 시작합니다. 한 번 클릭하면 config의 주황색이 되고, 두 번째 클릭하면 방금 정한 파란색으로 돌아옵니다. 이를 통해 정적 Style과 실행 중 override의 관계를 확인할 수 있습니다.

다음에는 class 규칙 뒤에 아래 규칙을 추가합니다.

```text
class interactive { wireframe: true; }
```

같은 class 선언을 반복해도 되므로 기존 roughness .35를 유지하며 wireframe만 켭니다. 시험을 마치면 추가한 규칙을 지우고 새로 고침합니다.

알 수 없는 material을 `style`로 참조하면 공간을 열기 전에 실패합니다. 문서 로드 후 기대와 다른 색이 보인다면 먼저 id 규칙과 활성 Script override를 확인합니다. 전체 우선순위를 다시 보려면 [제작 언어 명세의 Style 계약](../../native/docs/language.md#style)을 참고합니다.
