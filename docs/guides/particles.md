# Particle Systems · 작은 규칙으로 움직이는 세계

`particles-v1`은 LINK World Standards v0.1의 **실험적 선택 확장**입니다. Element는 입자 집합의 공간과 최대 크기를 선언하고, Style은 초기 외형을 정하며, Script는 각 입자의 상태와 행동을 계산합니다. 입자마다 Object나 Component를 생성하지 않습니다.

실행 예제는 [PARTICLE LIFE](../particle-life.md)입니다. 이 기능은 군집·인공생명 실험의 기반이며 생물학적 세포, 의식, 실제 생명 또는 정밀 물리 시뮬레이션을 제공한다는 뜻은 아닙니다. 현재 입자는 카메라를 향하는 작은 원판으로 일괄 렌더링합니다. 사용자 정의 메시·분자 모델·입체 구체 인스턴싱은 별도 기능입니다.

## 확장을 명시하기

`world.link`의 기존 `version`은 유지하고 필요한 확장을 등록합니다.

~~~text
world {
  version: "0.1";
  extensions: ["particles-v1"];
  id: "my-particles";
  name: "My particle field";
  elements: "scene.element";
  style: "appearance.style";
  scripts: ["scripts/field.script"];
  entry: {position:[4,3,6], target:[0,1,0]};
  environment: {background:"#08121B", grid:false};
}
~~~

확장을 생략한 기존 공간에는 기존 실행 예산을 그대로 적용합니다. 알 수 없는 확장·중복 확장은 준비 단계에서 거절하며, `particles` 형상을 사용하면서 확장을 선언하지 않은 문서도 거절합니다. 이 변경은 LINK 전송 프레임을 바꾸지 않습니다. 서버는 문서를 제공하고 브라우저가 기능을 검증·실행합니다. 이전 브라우저는 이 확장을 지원하지 않으므로 최신 브라우저가 필요합니다.

## Element: 입자를 담는 공간

~~~text
scene {
  object field {
    component behavior {
      script: "scripts/field.script";
      element cells {
        geometry: {type:"particles", capacity:128, radius:.055, cellSize:.6};
        position: [0,1,0];
        style: "living";
      }
    }
  }
}
~~~

~~~text
defaults { color:"#D8E5D4"; roughness:.8; }
material living { color:"#BEE8AD"; emission:1.1; }
~~~

`capacity`는 이 시스템이 담을 수 있는 최대 입자 수입니다. `radius`는 생성 시 생략한 반지름의 기본값이고 `cellSize`는 이웃 검색용 공간 격자의 크기입니다. 세 필드는 모두 필수입니다. capacity는 1~2,048 정수이고 radius·cellSize는 0 초과 1,000 이하의 유한수입니다. 좌표와 반지름은 미터, 속도는 m/s, 시간은 초입니다.

입자의 위치·속도는 해당 Element의 **로컬 좌표계**에 있습니다. Element와 부모의 transform·visible을 렌더링에 적용합니다. 다른 시스템의 좌표를 비교하려면 같은 좌표계를 사용하거나 제작자가 변환해야 합니다. 서로 다른 시스템 사이의 이웃 검색·충돌·자동 좌표 변환은 없습니다.

입자는 노드가 아닙니다. 노드 500개 제한과 별도로 시스템당 최대 2,048개, 공간 전체 선언 capacity 합계 최대 8,192개를 적용합니다. `ctx.find()`·노드 state·노드 transform·일반 Element 클릭 API로 개별 입자를 찾거나 제어하지 않습니다. 입자는 전용 ID와 아래 API로 다룹니다. 입자 집합을 클릭하면 기존 방식으로 소유 Script에 시스템 Element ID를 전달하며, 개별 입자 ID는 클릭 이벤트에 넣지 않습니다.

## Script: 생성하고 움직이기

다음은 실제로 실행할 수 있는 `scripts/field.script` 예제입니다. 초기 입자는 다음 생명주기에서 조회할 수 있습니다.

~~~text
on onStart(ctx) {
  for (let index of range(24)) {
    let angle = index * .261799388;
    ctx.spawnParticle("cells", {
      position: [cos(angle), 0, sin(angle)],
      velocity: [-sin(angle) * .2, 0, cos(angle) * .2],
      state: {energy: 1}
    });
  }
}
on onUpdate(ctx, deltaSeconds) {
  for (let id of ctx.particleIds("cells")) {
    let particle = ctx.getParticle("cells", id);
    let attraction = vmul(particle.position, -.1);
    ctx.setParticle("cells", id, {
      velocity: vadd(particle.velocity, vmul(attraction, deltaSeconds))
    });
  }
}
~~~

브라우저가 매 갱신에서 Script 실행 뒤 `position += velocity * deltaSeconds`와 `age += deltaSeconds`를 계산합니다. 따라서 속도를 지정했다면 위치를 직접 적분하지 않아도 됩니다. 직접 `position`을 지정하면서 속도도 남겨 두면 지정 위치에서 그 속도로 다시 이동합니다. 위치를 고정하려면 속도를 `[0,0,0]`으로 지정합니다. 중력·반발·질량·충돌·에너지·증식은 자동 기능이 아니며 Script로 정의합니다.

`deltaSeconds`는 기존과 같이 0~.05초로 제한합니다. 현재 구현은 고정 시간 간격의 과학 계산기가 아닙니다. 난수 발생기는 재현 가능한 초기 시드를 사용하지만, 다른 프레임 시간·입력 순서·콜백 순서는 다른 결과를 만들 수 있습니다. 방문자마다 독립적으로 계산하고 서버에 입자 상태를 전송하지 않습니다. F5로 새 인스턴스를 만들면 입자·ID·난수 상태도 초기화합니다.

## 입자 API

모든 `systemId`는 같은 공간에 있는 `particles` Element ID 문자열입니다. API는 `particles-v1` 확장을 선언한 공간에서만 사용할 수 있습니다. 입자 ID는 Runtime 전체에서 1부터 증가하는 1~1,000,000 정수이며, 삭제 후에도 같은 Runtime에서 재사용하지 않습니다. API에는 소유 시스템 ID와 함께 전달합니다.

| API | 계약 |
| --- | --- |
| `ctx.spawnParticle(systemId, data)` | 입자를 예약 생성하고 숫자 ID를 반환합니다. capacity가 가득 차면 `null`입니다. `position`이 필수이며 `velocity`, `radius`, `color`, `state`는 선택입니다. |
| `ctx.particleIds(systemId)` | 이번 단계의 스냅샷에 있는 숫자 ID를 오름차순 배열로 반환합니다. |
| `ctx.getParticle(systemId, id)` | `{id,position,velocity,radius,color,age,state}` 사본을 반환하며 없는 ID는 `null`입니다. |
| `ctx.setParticle(systemId, id, patch)` | `position`·`velocity`·`radius`·`color`·`state` 중 전달한 필드의 변경을 예약합니다. `id`·`age`는 읽기 전용입니다. |
| `ctx.removeParticle(systemId, id)` | 해당 입자의 삭제를 예약합니다. |
| `ctx.neighbors(systemId, id, radius, limit)` | 자신을 제외하고 거리 안에 있는 입자 ID를 가까운 거리순으로 최대 `limit`개 반환합니다. 같은 거리는 ID 오름차순입니다. |
| `ctx.particleCount(systemId)` | 현재 스냅샷에 예약 생성·삭제까지 반영한 개수를 반환합니다. |
| `ctx.random()` | 해당 Runtime의 시드가 있는 난수열에서 0 이상 1 미만 숫자를 반환합니다. |

생성 시 `velocity` 기본값은 `[0,0,0]`, `radius` 기본값은 geometry의 radius, `color` 기본값은 생성 시 시스템의 계산된 Style 색, `state` 기본값은 빈 맵입니다. 색은 `"#RRGGBB"`입니다. 입자마다 Style 재질을 만드는 대신 입자 색을 전용 상태로 저장합니다. 시스템의 `opacity`, `emission`, `wireframe`은 해당 Element의 Style을 따릅니다. opacity는 입자 집합의 투명도, emission은 가산 광륜의 강도, wireframe은 원판을 구성하는 삼각형의 선 표시를 제어합니다. wireframe에서는 광륜을 그리지 않습니다. 입자 원판은 장면 조명을 받지 않으며 `roughness`·`metalness`·`textBackground`는 원판 외형에 적용하지 않습니다. emission 광륜도 주변 물체를 실제로 비추지는 않습니다.

`setParticle`의 `state`는 **얕은 병합이 아니라 전체 교체**입니다. 예를 들어 기존 상태가 `{energy:1, kind:"a"}`일 때 `{state:{energy:.8}}`을 쓰면 `kind`는 사라집니다. 필요한 필드를 새 맵에 함께 넣어야 합니다. `ctx.setState`의 노드 상태 병합 규칙과 다릅니다.

입자의 state 맵은 추정 1,024 bytes 이하이고, 루트 맵·중첩 컨테이너·각 스칼라·null을 모두 포함한 재귀 값 개수가 64개 이하입니다. 값의 중첩 깊이는 루트 맵을 0으로 세어 최대 16입니다. 작은 값이 많은 배열이나 깊은 맵이 실제 메모리를 과도하게 늘리지 않도록 세 제한을 함께 검사합니다. 위치·속도는 정확히 세 성분이며 각 성분이 유한수이고 절댓값 1,000,000 이하입니다. 입자 반지름은 0 초과 1,000 이하이고 나이는 생성 시 0초입니다. 검색 인수도 유한수와 범위 검사를 거칩니다. 잘못된 시스템 ID·타입·필드·범위와 예산 초과는 Script 오류이며 해당 바인딩을 중지합니다. capacity 부족으로 반환하는 `null`은 오류가 아니므로 증식 알고리즘에서 처리할 수 있습니다.

### 같은 시점 읽기, 변경 후 반영

입자 API는 단일 콜백이 아니라 **같은 생명주기 단계 전체**에 대해 읽기 스냅샷을 고정합니다.

1. 단계 시작 때 모든 입자의 읽기 상태를 고정합니다.
2. 모든 해당 콜백이 같은 스냅샷을 읽고 변경을 예약합니다.
3. 콜백 실행 뒤 예약한 생성·수정·삭제를 반영합니다.
4. 갱신 단계에서는 속도 적분과 나이 증가를 수행합니다.

따라서 첫 입자를 수정해도 뒤의 입자가 이번 단계에서 읽는 값은 바뀌지 않습니다. 생성한 입자는 다음 단계부터 `particleIds`·`getParticle`·`neighbors`에 나타납니다. 삭제 예약한 입자도 이번 읽기 스냅샷에는 남아 있습니다. `particleCount`만 예약된 생성·삭제를 포함한 수를 반환하여 capacity와 개체 수를 제어할 수 있습니다. 여러 콜백이 같은 입자를 변경할 때는 예약 순서대로 적용합니다. Script 오류 전에 유효하게 예약한 변경은 단계 종료 때 반영되며 전체 콜백을 롤백하지 않습니다. 속도 적분 결과가 좌표 범위를 벗어나면 해당 입자 단계의 반영을 중단하고 reload까지 입자 API를 중지합니다.

이 규칙은 입자 상태에 대한 계약이며 기존 노드의 `ctx.setState`·`setTransform`을 지연 처리한다는 뜻은 아닙니다.

### 이웃만 관찰하기

~~~text
for (let neighborId of ctx.neighbors("cells", id, .6, 4)) {
  let neighbor = ctx.getParticle("cells", neighborId);
  let offset = vsub(particle.position, neighbor.position);
  let separation = vmul(normalize(offset), .1);
}
~~~

검색 반지름은 0 초과 해당 geometry의 `cellSize` 이하이고, `limit`은 1~64의 정수입니다. 검색 거리는 로컬 좌표의 중심 간 유클리드 거리이며 입자 반지름을 합산하지 않습니다. 거리순 정렬이므로 반환값의 첫 항목이 가장 가까운 이웃입니다. 공간 격자를 사용하지만 지나치게 빽빽한 군집은 후보 검사 예산에 도달할 수 있습니다. cellSize·입자 수·limit을 함께 조절하세요.

## 제한된 반복과 벡터 연산

~~~text
for (let index of range(12)) { }
for (let id of ctx.particleIds("cells")) { }
~~~

`for (let 이름 of 배열) { ... }`만 지원합니다. 일반 `for(초기식;조건;증가식)`, `while`, `break`, `continue`, 사용자 함수와 재귀는 지원하지 않습니다. 배열은 반복 시작 때 평가하고 최대 2,048개 원소를 순서대로 순회합니다. 반복 변수와 본문의 `let`은 매 반복마다 별도 범위에 생기며 끝나면 제거됩니다. 바깥 변수를 대입하면 바깥 값이 유지됩니다. 제한된 반복 자체와 아래 수학 함수는 확장 선언이 없는 일반 Script에서도 사용할 수 있으며 기존 평가 예산을 적용합니다.

| 함수 | 의미 |
| --- | --- |
| `range(n)` | 0~n-1의 숫자 배열. n은 0~2,048 정수 |
| `len(value)` | 배열 원소 수 또는 문자열의 UTF-8 byte 수 |
| `sqrt(x)` | 0 이상 숫자의 제곱근 |
| `sin(x)` / `cos(x)` | 라디안 각도의 삼각 함수 |
| `vadd(a,b)` / `vsub(a,b)` | 세 성분 벡터의 덧셈·뺄셈 |
| `vmul(vector, scalar)` | 세 성분 벡터의 숫자 배율 |
| `dot(a,b)` | 세 성분 벡터의 내적 |
| `length(vector)` | 벡터의 유클리드 길이 |
| `normalize(vector)` | 단위 벡터. 영벡터는 `[0,0,0]` |

기존 `min`, `max`, `abs`, `clamp`, `mix`도 그대로 지원합니다. 배열에 임의 메서드를 호출하거나 배열·맵 멤버에 직접 대입하는 기능은 추가하지 않습니다.

## 실행 예산과 현재 범위

| 항목 | 상한 |
| --- | ---: |
| 시스템당 선언 capacity | 2,048 |
| 공간 전체 선언 capacity 합 | 8,192 |
| 입자별 state | 추정 1,024 bytes |
| 입자 state의 재귀 값 개수 / 중첩 깊이 | 64 / 16 (루트 깊이 0) |
| 한 단계 생성 | 256회 |
| 한 단계 생성·변경·삭제 | 합계 4,096회 |
| 한 단계 이웃 후보 검사 | 100,000회 |
| 확장 공간의 바인딩 콜백 평가 | 65,536단계 |
| 확장 공간의 전체 단계 평가 | 262,144단계 |
| 반복 하나의 원소 수 | 2,048 |
| 이웃 검색 반환 수 | 1~64 |

단계는 onStart·onUpdate·onInteract·onDestroy의 실행 경계입니다. 예산은 해당 단계에서 공유하며 기존 파일·노드·변수 저장소 예산도 함께 적용합니다. API 호출 수와 평가 단계 수는 서로 다른 제한입니다. 단순 입자 수만 늘리면 이웃 검색과 Script 비용이 먼저 한계에 닿을 수 있습니다.

현재 입자 데이터는 방문자의 Runtime 안에만 존재합니다. OS·파일·네트워크 API, 무제한 개체 생성, 동적 일반 노드 생성은 제공하지 않습니다. 프로세스 격리나 실제 생물 안전성을 검증한 체계라는 뜻도 아닙니다. 확장을 추가해도 기존 LINK TCP의 암호화·인증 범위는 바뀌지 않습니다.
