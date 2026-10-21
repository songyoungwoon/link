# Script — 움직임과 상호작용

Script는 공간이 시작될 때, 갱신될 때, 클릭될 때, 종료될 때 실행할 동작을 적는 언어입니다. 이번 예제는 **초당 45도 회전하는 큐브**와 **클릭할 때마다 색이 전환되는 반응**을 만듭니다.

[가이드 시작](README.md) · [Element](element.md) · [Style](style.md) · [실제 Script 파일](examples/first-world/scripts/pulse.script)

## 1. Component에 연결하기

먼저 `world.link`에 파일을 등록합니다.

```text
scripts: ["scripts/pulse.script"];
```

그리고 Element 문서의 `spinner` Component에 같은 문자열로 바인딩합니다.

```text
script: "scripts/pulse.script";
config: {speed: 45, visual: "sample_cube", activeColor: "#F1AE66"};
state: {selected: false, clicks: 0, angle: 0};
```

이 세 줄은 Component 안의 필드입니다. Script는 Object나 Component에 연결할 수 있고 노드당 하나입니다. 실제 큐브 형상인 Element에는 직접 연결하지 않습니다. 큐브의 클릭은 가장 가까운 상위 바인딩인 `spinner`로 전달됩니다.

파일은 같은 공간 디렉터리를 기준으로 참조합니다. `scripts` 목록과 `script` 문자열이 정확히 일치해야 하며, 목록에 적은 파일은 모두 존재하고 파싱되어야 합니다.

## 2. 예제 파일 전체

`pulse.script`라는 이름은 이 예제의 파일 이름입니다. 동작은 아래 네 훅이 정의합니다.

```text
on onStart(ctx) {
  store angle = 0;
  store clicks = 0;
  ctx.find(ctx.config.visual);
  ctx.setState({selected: false, clicks: 0, angle: 0});
  ctx.log("first-world ready");
}

on onUpdate(ctx, deltaSeconds) {
  angle = (angle + ctx.config.speed * deltaSeconds) % 360;
  ctx.setState({angle: angle});
  ctx.setTransform({rotation: [0, angle, 0]});
}

on onInteract(ctx, event) {
  let selected = !ctx.getState().selected;
  clicks = clicks + 1;
  ctx.setState({selected: selected, clicks: clicks});
  let visual = ctx.find(ctx.config.visual);
  if (selected) {
    ctx.setStyle(visual, {color: ctx.config.activeColor});
  } else {
    ctx.clearStyle(visual);
  }
  ctx.log(event);
}

on onDestroy(ctx) {
  ctx.log("first-world binding destroyed");
}
```

## 3. 네 생명주기 훅

| 훅 | 실행 시점 | 예제에서 하는 일 |
| --- | --- | --- |
| `on onStart(ctx)` | 공간 인스턴스 시작 시 한 번 | 바인딩 저장소 초기화, 대상 ID 확인 |
| `on onUpdate(ctx, deltaSeconds)` | 활성 바인딩의 매 갱신 | 시간에 비례하여 회전 |
| `on onInteract(ctx, event)` | 이 바인딩에 전달된 클릭 | 선택 상태·횟수 갱신, 색 전환 |
| `on onDestroy(ctx)` | 공간 인스턴스 종료 시 한 번 시도 | 정리 로그 기록 |

`on` 다음의 훅 이름과 매개변수 이름·개수까지 위 표와 같아야 합니다. `onUpdate(ctx, dt)`처럼 임의로 이름을 바꾸면 파싱 오류입니다. 한 파일에 같은 훅을 두 번 선언할 수 없습니다. 필요 없는 훅은 생략할 수 있지만 최소 하나는 있어야 합니다.

새로 고침·다른 공간으로 이동·홈·실행 중지·정상 창 종료는 기존 인스턴스의 수명을 끝냅니다. 새 인스턴스는 파일의 원래 state·transform·Style과 새 저장소로 시작합니다. 이 저장소는 디스크에 저장되지 않습니다.

## 4. config, state, store, let 구분하기

| 종류 | 읽는 방법 | 수명과 용도 |
| --- | --- | --- |
| `config` | `ctx.config.speed` | Element 문서에 적은 바인딩별 설정 |
| 노드 `state` | `ctx.getState()` | Object·Component의 실행 중 상태 |
| `store` 변수 | `angle`, `clicks` | 같은 바인딩의 다음 콜백까지 유지되는 저장소 |
| `let` 변수 | `selected`, `visual` | 이번 콜백 안에서만 쓰는 임시 값 |

예제는 `store angle`로 계산 값을 유지하고 `state.angle`에도 복사해 노드 상태를 조회할 수 있게 합니다. `clicks`도 저장소와 state에 함께 기록합니다. 필수 패턴은 아니며, 바깥에서 조회할 필요가 없는 계산은 store만 써도 됩니다.

같은 Script 파일을 두 Component에 연결하면 파싱된 코드는 공유해도 store·config·콜백 지역 값은 바인딩마다 분리됩니다. 예를 들어 한쪽 config의 speed를 90으로 정하면 그쪽만 빠르게 회전합니다.

`store`는 보통 onStart에서 한 번 선언합니다. onUpdate에서 같은 store를 반복 선언하면 다음 갱신에서 중복 선언 오류가 납니다. `let`도 같은 콜백 안에서 이름을 중복 선언할 수 없으며 `if` 블록마다 별도 scope가 생기지 않습니다. `ctx`, `deltaSeconds`, `event`는 읽기 전용 예약 이름입니다.

state·transform 조회는 **값의 사본**을 돌려줍니다. `state.selected = true;` 같은 멤버 대입은 지원하지 않습니다. 원래 노드를 변경하려면 `ctx.setState({selected:true});`를 사용합니다. state 패치는 최상위 키를 얕게 병합하고 중첩 맵은 값 전체를 교체합니다.

## 5. 시간에 맞춰 회전시키기

핵심 계산은 다음 줄입니다.

```text
angle = (angle + ctx.config.speed * deltaSeconds) % 360;
```

speed는 degree/second, deltaSeconds는 초입니다. speed가 45라면 콜백의 delta 합계가 1초가 되었을 때 45도 회전합니다. `% 360`은 누적 각도를 한 바퀴 범위로 되돌립니다. 각 프레임에 고정 각도를 더하는 방식보다 갱신 간격 변화에 대응하기 쉽습니다.

현재 런타임은 onUpdate의 deltaSeconds를 **0~.05초로 제한**합니다. 긴 중단을 한 번에 따라잡지 않으므로 매우 느린 프레임이나 중단 뒤에는 실제 벽시계 시간만큼 이동하지 않을 수 있습니다. 음수·비유한 입력은 유효한 갱신 값으로 제한됩니다.

다음 호출은 현재 Component의 로컬 rotation만 바꿉니다.

```text
ctx.setTransform({rotation: [0, angle, 0]});
```

`position:[0,1.55,0]`은 유지됩니다. Component의 자식 큐브도 함께 회전합니다. `ctx.setTransform("다른_ID", patch)` 형식으로 다른 노드의 변환도 바꿀 수 있습니다.

## 6. 클릭이 전달되는 방식

큐브를 클릭하면 다음 형태의 event를 받습니다.

```text
{type: "click", targetId: "sample_cube", bindingId: "spinner"}
```

`targetId`는 실제로 맞힌 Element, `bindingId`는 훅이 연결된 노드입니다. 가장 가까운 상위 바인딩 하나만 호출하고 위쪽 Object로 다시 버블링하지 않습니다. 중지된 바인딩도 전달 경계를 유지합니다. UI 버튼 클릭이나 카메라를 움직인 드래그는 공간 클릭 동작에 포함하지 않습니다.

예제는 `!ctx.getState().selected`로 bool을 반전하고 첫 클릭에는 `ctx.setStyle`로 color만 덮어씁니다. 다음 클릭에는 `ctx.clearStyle`로 override를 제거합니다. 단순히 이전 색을 하드코딩하지 않고 [Style 우선순위](style.md#3-속성마다-계산하는-우선순위)로 돌아가기 때문에 Style 파일을 나중에 바꿔도 복원이 맞습니다.

`ctx.log(event)`는 맵을 로그로 표시합니다. 실행 로그는 **F12**로 개발자 도구를 열어 확인합니다. 문자열과 숫자를 자동으로 섞는 변환은 없으므로 `"clicks: " + clicks` 대신 `ctx.log(clicks)` 또는 `ctx.log({clicks:clicks})`를 사용합니다.

## 7. 사용할 수 있는 ctx API

`ctx.id`는 현재 바인딩 노드 ID 문자열입니다. ID 인수를 생략할 수 있는 API는 자기 바인딩 노드를 대상으로 합니다.

| API | 사용법과 결과 |
| --- | --- |
| `ctx.find(id)` | 현재 공간에 있는 ID 문자열인지 확인하고 같은 ID 반환. 없으면 오류 |
| `ctx.getState()` / `ctx.getState(id)` | 노드 state 맵의 사본 반환 |
| `ctx.setState(patch)` / `ctx.setState(id,patch)` | Object·Component state에 맵을 얕게 병합 |
| `ctx.getTransform()` / `ctx.getTransform(id)` | position·rotation·scale·visible 맵의 사본 반환 |
| `ctx.setTransform(patch)` / `ctx.setTransform(id,patch)` | 지정한 로컬 변환 필드 교체 |
| `ctx.setStyle(id,patch)` | Element의 Style override를 속성별 병합 |
| `ctx.clearStyle(id)` | 해당 Element의 Style override 전체 제거 |
| `ctx.log(value)` | 바인딩 ID와 값을 로그로 출력 |

조회 API 이외 호출의 반환값은 null입니다. `find`는 객체나 포인터를 돌려주지 않으며 `ctx.find("sample_cube").setStyle(...)` 같은 메서드 연결은 지원하지 않습니다.

Transform 패치에는 `position`, `rotation`, `scale`, `visible`만 넣습니다. 벡터를 바꿀 때는 세 성분 전체가 필요합니다. Style 패치에는 [지원 Style 속성](style.md#4-지원-속성과-범위)만 넣으며 Element만 대상으로 합니다. 존재하지 않는 ID, 잘못된 인수 개수·타입, Element에 대한 setState는 실행 오류입니다.

이 API는 현재 공간 안의 동작을 다룹니다. 파일·네트워크·OS·렌더러·셸 UI 접근이나 런타임 노드 생성·삭제 API는 없습니다. 다른 공간으로 이동시키려면 Script 호출을 만들어내지 말고 [매니페스트 포탈](element.md#7-걷기와-포탈은-필요할-때-추가하기)을 사용합니다.

## 8. 지원하는 식과 문장

데이터는 숫자, 문자열, bool, null, 배열, 맵입니다. `.field`나 `[index]`로 읽고, 선언한 변수에 `=`로 대입합니다. 배열 인덱스는 범위 안의 0 이상 정수여야 합니다. 없는 맵 키를 읽으면 오류입니다.

| 분류 | 지원 문법 |
| --- | --- |
| 선언·대입 | `store angle = 0;`, `let speed = 45;`, `angle = angle + 1;` |
| 분기 | `if (조건) { ... } else { ... }` |
| 산술 | `+`, `-`, `*`, `/`, `%`, 단항 `+`·`-` |
| 비교 | `<`, `<=`, `>`, `>=`, `==`, `!=` |
| 논리 | `!`, `&&`, `||` — bool만 사용, 단락 평가 |
| 수치 builtin | `min(a,b)`, `max(a,b)`, `abs(a)`, `clamp(value,min,max)`, `mix(a,b,t)` |

`* / %`가 `+ -`보다 먼저, 비교가 논리식보다 먼저 계산됩니다. 조건은 bool이어야 하고 숫자 0을 false처럼 자동 변환하지 않습니다. 문자열끼리는 `+`로 이어 붙일 수 있습니다. `mix`는 `a + (b-a)*t`이며 t를 자동 제한하지 않습니다.

Script는 이 전용 문법의 AST 인터프리터입니다. JavaScript나 Lua의 코드, `function`, `return`, `for`, `while`, 임의 함수 호출, import, `Math.sin`, DOM API는 지원하지 않습니다. 콜백 밖 전역 문장을 적는 대신 onStart 안에서 초기화합니다. 문자열은 큰따옴표를 쓰고 문장 끝에는 세미콜론을 붙입니다.

## 9. 오류와 실행 제한

파일의 문법 오류는 공간 활성화 전에 발견됩니다. 콜백 실행 오류는 해당 바인딩을 중지하지만 다른 바인딩은 계속 실행합니다. 오류 전에 이미 적용한 API 변경은 되돌리지 않습니다. 중지된 바인딩도 공간이 종료될 때 onDestroy를 한 번 시도하고 종료 오류는 로그로 남깁니다.

각 콜백의 문장·식 평가는 최대 4,096단계, 호출 깊이는 16입니다. 바인딩의 store와 현재 콜백 값·인수 합계는 추정 64KiB, 노드 state와 config는 각각 추정 64KiB입니다. Script 파일은 최대 128KiB, 공간당 등록 파일은 최대 16개입니다. 문자열·맵 키는 각각 최대 8,192 UTF-8 bytes입니다. 자세한 예산 계산과 문법은 [제작 언어 명세](../../native/docs/language.md)를 참고합니다.

첫 수정으로 config의 `speed:45`를 `speed:90`으로 바꿔 봅니다. 다음에는 `activeColor`를 바꾸고 클릭을 두 번 해 봅니다. 매번 저장 후 새로 고침하여 초기 상태부터 확인하면 설정·상태·외형의 관계를 익히기 쉽습니다.
