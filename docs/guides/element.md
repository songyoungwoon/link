# Element — 구조와 배치

Element 문서는 **무엇을 어디에 놓을지** 정의합니다. 렌더링되는 형상은 Element, 여러 형상을 함께 이동·회전시키는 단위는 Component, 공간의 큰 의미 단위는 Object로 만듭니다.

[가이드 시작](README.md) · [Style](style.md) · [Script](script.md) · [실제 예제 폴더](examples/first-world)

## 1. world.link로 문서 연결하기

공간 폴더에는 이름이 `world.link`인 진입 문서가 필요합니다. 아래 코드는 [예제 파일](examples/first-world/world.link)의 전체 내용입니다.

```text
world {
  version: "0.1";
  id: "first-world";
  name: "나의 첫 번째 LINK 공간";
  elements: "scene.element";
  style: "appearance.style";
  scripts: ["scripts/pulse.script"];
  entry: {position: [5, 3.8, 7], target: [0, 1.1, 0]};
  environment: {background: "#E9E5DD", grid: false};
}
```

`version`, `id`, `name`, `elements`, `style`, `scripts`, `entry`는 모두 필수입니다. Script가 없는 공간도 `scripts: [];`를 적습니다. `entry.position`은 시작 카메라 위치이고 `entry.target`은 바라볼 지점입니다. 서로 다른 두 지점을 지정합니다. `environment`는 선택 사항으로 배경색과 관찰용 격자를 정합니다. 생략하면 배경은 `#111c21`, 격자는 켜져 있습니다.

파일 경로는 모두 이 `world.link`가 들어 있는 공간 폴더 기준입니다. Script가 `scripts/` 안에 있어도 그 바인딩 문자열이나 이미지 경로의 기준 디렉터리가 바뀌지 않습니다.

## 2. Object → Component → Element

허용되는 부모·자식 관계는 다음과 같습니다.

```text
scene
└─ object first_world
   ├─ component room
   │  ├─ element floor
   │  └─ element pedestal
   └─ component spinner       ← Script를 여기에 연결
      └─ element sample_cube ← 실제 형상과 클릭 대상
```

Object 아래에는 Component만, Component 아래에는 Component나 Element를 넣습니다. Element는 자식을 가질 수 없습니다. `scene` 최상위에는 Object만 놓습니다. Object를 여러 개 선언할 수 있고 Component를 중첩해 복잡한 묶음도 구성할 수 있습니다.

`spinner`를 회전하면 그 안의 `sample_cube`도 함께 회전합니다. `room`은 형제 Component이므로 영향을 받지 않습니다. 이 관계가 회전하는 부품과 고정된 받침대를 나누는 이유입니다.

모든 노드 ID는 한 공간 안에서 종류와 관계없이 유일해야 합니다. `sample_cube` 같은 ID는 Script·Style에서 대상을 찾는 고정 이름입니다. `name`은 선택 표시 이름이며 생략하면 ID를 사용합니다.

## 3. 실행되는 장면 전체

[scene.element](examples/first-world/scene.element)의 전체 내용입니다.

```text
scene {
  object first_world {
    name: "나의 첫 번째 공간";

    component room {
      element floor {
        position: [0, -.08, 0];
        geometry: {type: "box", size: [10, .16, 10]};
        style: "floor_material";
      }
      element pedestal {
        position: [0, .325, 0];
        geometry: {type: "cylinder", radius: 1.3, radiusTop: 1.3, height: .65};
        style: "pedestal_material";
      }
    }

    component spinner {
      name: "회전하는 큐브";
      position: [0, 1.55, 0];
      state: {selected: false, clicks: 0, angle: 0};
      script: "scripts/pulse.script";
      config: {speed: 45, visual: "sample_cube", activeColor: "#F1AE66"};

      element sample_cube {
        name: "클릭하면 색이 바뀝니다";
        geometry: {type: "box", size: [1.8, 1.8, 1.8]};
        style: "cube_material";
        classes: ["interactive"];
      }
    }
  }
}
```

`geometry`는 Element에 필수입니다. `style`은 Style 문서의 `material` 이름, `classes`는 class 선택자가 매칭할 이름 목록입니다. Object·Component는 선택 `state`, `script`, `config`를 가질 수 있습니다. Element에는 이 세 필드를 붙이지 않습니다. `config`를 사용하려면 같은 노드에 `script`가 있어야 합니다.

예제의 `speed`, `visual`, `activeColor`는 언어에 내장된 필드가 아닙니다. 이 Script가 `ctx.config`에서 읽도록 정한 설정 이름입니다. 반대로 `position`이나 `geometry`는 로더가 의미를 정한 문법 필드입니다.

## 4. 좌표와 부모 변환 이해하기

길이 단위는 m, Y축은 위쪽입니다. `[x, y, z]` 순서를 사용하고 회전은 degree입니다. 기본 변환은 다음과 같습니다.

```text
position: [0, 0, 0];
rotation: [0, 0, 0];
scale: [1, 1, 1];
visible: true;
```

변환은 각 노드의 로컬 좌표에 적용됩니다. 계산은 `T × Rx × Ry × Rz × S`이고 그 앞에 부모 변환을 곱합니다. 이 순서는 여러 회전축이나 부모 배율을 함께 사용할 때 결과에 영향을 줍니다.

예제에서 큐브의 로컬 중심은 `[0,0,0]`이고 부모 `spinner`의 위치가 `[0,1.55,0]`이므로 큐브 중심은 1.55m 높이에 놓입니다. 한 변이 1.8m라서 아랫면은 .65m 높이이며, 높이 .65m 받침대 위에 맞닿습니다. 바닥은 중심이 -.08m, 두께가 .16m라서 윗면이 Y=0입니다.

부모 `visible:false`는 자식까지 숨기며 렌더링과 클릭 대상에서 제외합니다. 배율은 각 성분이 0 초과 1,000 이하이고, 위치·회전은 각 성분 절댓값 1,000,000 이하의 유한수입니다.

## 5. 지원 형상

아래 각 행의 `geometry` 맵은 그대로 쓸 수 있습니다. 지정한 형상에는 표에 있는 필드가 모두 필요합니다.

| 형상 | geometry | 기준 |
| --- | --- | --- |
| Box | `{type:"box",size:[1,2,.2]}` | 원점 중심의 가로·세로·깊이 |
| Sphere | `{type:"sphere",radius:.5}` | 원점 중심의 반지름 |
| Plane | `{type:"plane",size:[3,2]}` | 로컬 XY 평면, 앞쪽 법선 +Z |
| Cylinder | `{type:"cylinder",radius:1,radiusTop:.7,height:2}` | Y축 방향, 아래·위 반지름과 전체 높이 |
| Frame | `{type:"frame",size:[3,2],border:.06,depth:.08}` | XY 방향의 속이 빈 직사각 테두리 |

치수는 모두 0 초과 1,000 이하입니다. Cylinder의 `radiusTop`만 0을 허용하여 원뿔 꼭짓점을 만들 수 있습니다. Frame의 `border`는 더 작은 바깥 치수의 절반보다 작아야 합니다. Plane을 수평 바닥처럼 놓으려면 보통 `rotation:[-90,0,0];`을 씁니다.

직접 만든 형상을 추가해 보려면 `component room` 안에 아래 Element를 넣고 새로 고침합니다.

```text
element marker {
  position: [3, .5, 0];
  geometry: {type: "sphere", radius: .5};
  style: "cube_material";
}
```

## 6. 사진과 텍스트 plane

사진은 plane의 `image`로 참조합니다. 예를 들어 자기 공간 안에 `assets/photo.jpg`를 넣었다면 다음처럼 선언합니다. 이 조각은 해당 이미지 파일을 준비한 뒤 Component 안에 넣는 확장 예제입니다.

```text
element photo {
  position: [-3, 1.8, 0];
  geometry: {type: "plane", size: [2.4, 1.6]};
  image: "assets/photo.jpg";
}
```

원본 사진의 가로·세로 비율에 맞춰 `size`를 정하면 찌그러짐을 피할 수 있습니다. 테두리는 별도 `frame` Element로 배치합니다. 기본 앱에서 사진 plane을 더블클릭하면 확대해서 볼 수 있습니다.

같은 plane에 `image` 대신 `text:"Hello World!";`를 쓰면 일반 문자열을 그린 평면이 됩니다. 이는 두께가 있는 3D 글자 형상이 아닙니다. `image`와 `text`를 동시에 넣을 수 없으며 둘 다 plane에서만 지원합니다.

이미지는 jpg·jpeg·png·bmp를 지원하며 확장자는 대소문자를 구분하지 않습니다. 공간마다 고유 참조 최대 128개, 파일당 최대 16MiB입니다. 실제 이미지의 디코딩은 렌더러가 수행하므로 확장자가 맞아도 손상된 파일은 실행 준비에서 실패할 수 있습니다. `text`는 최대 1,024 UTF-8 bytes의 일반 문자열입니다.

## 7. 걷기와 포탈은 필요할 때 추가하기

기본 예제는 관찰 카메라를 사용합니다. 걷기를 넣으려면 `world.link`의 `world` 블록에 다음 선택 필드를 추가합니다.

```text
walk: {min: [-3,0,-8], max: [3,4,8], eyeHeight: 1.65, speed: 3.4};
```

시작 카메라도 `entry:{position:[0,1.65,4],target:[0,1.65,0]};`처럼 걷기 높이에 맞춥니다. 화면을 클릭한 뒤 WASD·방향키로 수평 이동하고 드래그로 시선을 돌립니다. `min`·`max`는 필수이며 모든 축에서 min < max여야 합니다. `eyeHeight`는 기본 1.65, 범위 .25~10이고 Y 범위 안이어야 합니다. `speed`는 기본 3.4m/s, 범위 .1~20입니다. 이동은 XZ 범위로 제한되며 일반 형상 충돌·중력은 계산하지 않습니다.

포탈은 대상 디렉터리가 실제로 있을 때만 선언합니다. 다음은 같은 `--worlds` 루트에 **완성된 `next-world` 공간을 먼저 만든 뒤** 추가하는 형식입니다. 제공된 first-world 예제에는 이 대상이 없으므로 그대로 추가하면 로드 오류가 납니다.

```text
portals: [{
  id: "next_portal", address: "link://local/next-world",
  position: [0,1.65,-6], size: [2.4,3.3,1]
}];
```

`position`은 카메라 눈을 감지하는 상자의 중심, `size`는 전체 크기입니다. 포탈의 외형은 별도 Element로 만듭니다. `size`의 각 성분은 0 초과 1,000 이하, 포탈은 최대 16개이며 ID는 128 bytes 이하의 유일한 식별자입니다. 대상은 현재 등록된 루트 안의 `link://local/...` 주소만 허용합니다. 이동 선분이 포탈 상자에 진입하면 해당 공간으로 탐색하고, 이미 안에 있는 상태에서는 반복 진입하지 않습니다.

`views`도 선택 매니페스트 메타데이터로 저장할 수 있지만 첫 공간 제작에는 필요하지 않습니다. 모든 선택 필드의 자세한 규칙은 [제작 언어 명세](../../native/docs/language.md)에 있습니다.

## 8. 작은 공간부터 확장하기

전체 노드는 최대 500개, 부모 깊이는 루트를 1로 세어 최대 16입니다. `world.link`는 64KiB, Element·Style 문서는 각각 1MiB, Script는 파일당 128KiB·최대 16개입니다. 큰 공간을 만들기 전에 작은 장면에서 문서 로드와 상호작용을 확인하면 오류 위치를 찾기 쉽습니다.

다음으로 [Style 가이드](style.md)에서 `material`·`class`·`id`가 같은 큐브에 어떻게 적용되는지 확인합니다.
