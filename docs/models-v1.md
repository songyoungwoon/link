# models-v1 · 움직이는 3D 모델

`models-v1`은 기존 Element·Script·Style에 **자체 포함 GLB 2.0 모델, 뼈대 애니메이션, 모델 재질 표현**을 더하는 선택 확장입니다. 기존 형상과 월드는 그대로 실행됩니다. 이 문서는 현재 Windows LINK Browser 구현 범위를 설명합니다. 별도 LINK Architect와 LINKAuthor의 편집·미리보기 및 JSON patch 프로필은 아직 이 모델 확장을 지원하지 않습니다.

## Element: 모델을 세계에 놓기

`world.link`에 확장을 선언하고 모델을 참조합니다. 경로는 월드 폴더 기준이며 원격 서버에서도 같은 LINK 리소스 요청으로 전달합니다.

```text
world {
  version: "0.1";
  id: "model-example";
  name: "A moving world";
  elements: "scene.element";
  style: "appearance.style";
  scripts: ["scripts/hero.script"];
  extensions: ["models-v1"];
  entry: {position: [0, 1.65, 4], target: [0, 1, 0]};
}
```

```text
scene {
  object garden {
    component hero_controller {
      script: "scripts/hero.script";
      element hero_model {
        geometry: {type: "model", source: "models/hero.glb"};
        style: "hero";
      }
    }
  }
}
```

모델은 Y축 위쪽·미터 단위의 glTF 좌표를 따릅니다. Element의 position·rotation·scale과 부모 Component 변환이 모델 전체에 적용됩니다. 모델 안의 노드·재질·뼈대 계층은 GLB에 보관합니다. 선택 광선은 현재 **기본 자세의 모델 경계 상자**를 사용하므로 삼각형 단위 충돌이나 움직이는 팔다리의 정밀 판정은 제공하지 않습니다. 복잡한 모델의 선택을 끄려면 `raycast: false;`를 사용합니다.

## Script: 이름으로 애니메이션 재생

```text
on onStart(ctx) {
  ctx.playAnimation("hero_model", "Idle", true, 1);
}
on onInteract(ctx, event) {
  ctx.playAnimation("hero_model", "Attack", false, 1);
}
```

| 호출 | 동작 |
| --- | --- |
| `ctx.playAnimation(id, clip, loop=true, speed=1)` | GLB의 이름 있는 클립을 재생. 속도는 0 초과 4 이하 |
| `ctx.stopAnimation(id)` | 재생을 종료하고 GLB의 기본 자세로 복귀 |

클립을 바꾸면 처음부터 재생합니다. 같은 클립·반복 여부·속도를 재생 중에 다시 지정하면 시간이 초기화되지 않습니다. 반복하지 않는 클립은 끝 자세를 유지하며, 끝난 뒤 같은 호출을 하면 다시 재생합니다. 시간은 초 단위이고 기존 Script 프레임 시간 제한을 따릅니다. 두 호출은 성공 시 `true`를 반환하며, 확장 미선언·모델이 아닌 대상·없는 클립·잘못된 인자는 해당 Script 바인딩 오류로 처리합니다.

현재 한 Element당 한 클립을 재생하며 전환 혼합·가산 애니메이션·IK·표정 morph·루트 모션 추출은 없습니다. 이동은 Script의 `ctx.setTransform()`으로, 팔다리 동작은 GLB 클립으로 제어합니다. 선형 이동/스케일, 쿼터니언 구면 보간 회전과 STEP을 지원합니다. CPU 스키닝 결과를 VBO에 올리며 같은 시각의 양안 렌더링에서는 변형 결과를 재사용합니다.

## Style: 셀 셰이딩과 PBR 재질

```text
material hero {
  color: "#FFFFFF";
  shading: "toon";
  outline: .012;
}
material stone {
  color: "#FFFFFF";
  shading: "pbr";
}
```

`shading`은 `pbr` 또는 `toon`이며 모델에 적용됩니다. `outline`은 모델 로컬 미터 단위의 외곽선 폭으로 0~0.04입니다. `color`는 GLB 기본색과 곱해지므로 원래 색을 보존하려면 흰색을 지정합니다. glTF baseColorFactor·내장 base-color PNG/JPEG·정점 색을 함께 사용합니다. normal·metallic-roughness·occlusion·emissive 맵도 지원합니다. roughness·metalness를 Style에서 명시하면 GLB의 재질 인자를 덮어쓰며, 명시하지 않으면 GLB 값을 유지합니다. 재질 인자는 해당 텍스처 채널과 함께 계산합니다. 공통 `defaults`에 값을 적은 경우도 명시적인 덮어쓰기입니다.

모델 렌더러는 셀 명암 단계/외곽선 또는 Cook–Torrance 기반의 직접광, 반구 주변광, 발광·안개·양면·OPAQUE/MASK/BLEND를 표현합니다. 모델 내부 투명 배치는 깊이순으로 정렬하지만 모델과 기존 투명 Element가 교차하는 모든 상황의 정확한 합성까지 보장하지 않습니다. 뷰포트와 OpenXR 양안은 같은 모델 경로를 사용합니다.

### PBR 텍스처 맵

| GLB 재질 슬롯 | 채널과 해석 |
| --- | --- |
| `baseColorTexture` | sRGB RGB 기본색, A는 coverage |
| `normalTexture` | 선형 RGB tangent-space 노멀. `scale` 기본 1, 유한수 -16~16 |
| `metallicRoughnessTexture` | 선형 G=거칠기, B=금속성 |
| `occlusionTexture` | 선형 R=주변 차폐. `strength` 기본 1, 0~1 |
| `emissiveTexture` | sRGB RGB 발광색 × `emissiveFactor` |

모든 맵은 GLB 안의 PNG/JPEG로 포함해야 합니다. UV0와 repeat wrapping을 사용하며, 맵이 하나라도 있는 primitive에는 TEXCOORD_0가 필요합니다. 다른 UV 세트와 `KHR_texture_transform`은 지원하지 않습니다. ORM 이미지를 occlusion과 metallic-roughness 슬롯에서 함께 참조하면 R·G·B 채널을 공유할 수 있습니다.

### 선택 조명 확장

`extensions: ["models-v1", "lighting-v1"];`와 `environment.lighting`을 함께 선언하면 **모델**에 방향광 PCF 그림자, HDR 환경광, 최대 4개의 그림자 없는 점광원, ACES 노출을 적용할 수 있습니다. 확장을 사용하지 않는 기존 월드의 조명과 기본 box·sphere 등의 렌더링은 유지합니다. 필드와 제작 예제는 [lighting-v1 가이드](lighting-v1.md)를 참고하세요.

HDR 소스는 Radiance `.hdr`, 최대 8 MiB·각 축 2,048픽셀·총 2,097,152픽셀입니다. 환경광 준비 결과는 64×32 diffuse irradiance와 256×128부터 1×1까지 9단계 GGX specular mip입니다. 그림자 해상도는 512·1024·2048 중 선택합니다. 이 확장의 조명 설정은 Script나 Style 필드가 아닌 `world.environment.lighting`에 둡니다.

## 가져오기 프로필과 예산

Blender 등에서 **glTF Binary (.glb)**, 삼각형, Apply/검증된 변환, 내장 PNG/JPEG, 이름 있는 애니메이션으로 내보내세요. 현재 프로필에 맞춰야 하며 일반 glTF의 모든 기능을 읽는 임포터는 아닙니다.

- GLB 2.0 안의 단일 BIN 버퍼. 외부 이미지/버퍼·data URI·텍스트 `.gltf`는 거부합니다.
- POSITION과 선택 NORMAL·TEXCOORD_0·COLOR_0·JOINTS_0·WEIGHTS_0. 삼각형 모드만 지원하며, 노멀이 없으면 생성합니다.
- TRS 또는 정적 행렬, 스킨의 정점당 최대 4개 가중치, LINEAR/STEP translation·rotation·scale 클립.
- `KHR_materials_unlit`과 내장 base-color·normal·metallic-roughness·occlusion·emissive 맵을 지원합니다. 미지원 필수 확장은 거부합니다. Draco/meshopt·sparse accessor·morph·CUBICSPLINE은 지원하지 않습니다.

| 예산 | 상한 |
| --- | ---: |
| GLB 파일 | 16 MiB |
| 모델당 정점 / 삼각형 | 각각 65,536 |
| 모델당 노드 / 스킨당 관절 | 256 / 64 |
| 모델당 재질 / 이미지 / 클립 | 32 / 8 / 32 |
| 내장 압축 이미지 총량 / 애니메이션 키 총량 | 8 MiB / 10,000 |
| 월드의 서로 다른 모델 | 16 |
| 월드 모델 자산의 정점 / 삼각형 합계 | 각각 200,000 |
| Element 복제를 포함한 정점 / 삼각형 예산 | 각각 300,000 |
| 기본 모델 VBO·인덱스·텍스처 GPU 예산 | 합계 32 MiB |
| `lighting-v1` 조명 사용 시 모델·환경광·그림자 GPU 예산 | 합계 192 MiB |

GLB 내부 노드의 메시 재사용도 실제 표시량으로 합산하며 모델당 최대 262,144 표시 정점·200,000 표시 삼각형으로 제한합니다. 이미지 디코드 원본은 축당 8,192픽셀까지 허용하고 GPU 업로드용으로 축당 최대 2,048픽셀로 축소합니다. 파서 메모리·오프셋·인덱스·유한값·계층 순환과 모델 경로의 월드 경계를 검사합니다. 원격 월드 전송은 기존 총 64 MiB 제한을 공유합니다. Windows 모델 표시에는 OpenGL 2.1 호환 이상과 셰이더를 지원하는 GPU 드라이버가 필요합니다.

## 예제와 지원 범위

모델·애니메이션 예제는 별도 `link-game`의 `moon-garden`입니다. 원본 GLB 생성기, 정원, Idle·Walk·Attack 탐험가를 함께 제공합니다. `LINK_GAME_GARDEN.cmd` 또는 `link://서버주소:8002/moon-garden`으로 접속합니다.

석조 재질·태양광 그림자·HDR 환경광 예제는 같은 별도 프로젝트의 `stone-sanctum`입니다. 서버를 실행한 뒤 `LINK_GAME_SANCTUM.cmd` 또는 `link://서버주소:8002/stone-sanctum`으로 접속합니다. 이 세계는 `models-v1`과 `lighting-v1`을 함께 사용합니다.

방향광 그림자와 HDR 기반 조명을 제공하며 실시간 전역 조명, 실시간 화면 공간 반사, LOD/스트리밍, VRM, 지형·일반 메시 충돌, 멀티플레이어 아바타 동기화, Quest 단독 APK는 후속 범위입니다. 이 기능은 LINK 네이티브 모델 셰이더 경로에서 구현합니다. PC OpenXR 경로가 준비되어 있지만 데모의 실제 헤드셋 체감 성능은 별도 확인해야 합니다.
