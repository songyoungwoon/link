# lighting-v1 · 재질과 빛으로 공간 만들기

`lighting-v1`은 LINK 월드에 **태양광, 환경광, 방향광 그림자, 점광원과 노출**을 설정하는 선택 확장입니다. `models-v1`의 GLB 모델과 함께 사용하면 돌의 요철, 금속의 반사, 어두운 틈과 빛이 들어오는 공간을 표현할 수 있습니다. Windows LINK Browser의 PC 화면과 OpenXR 렌더링에서 같은 설정을 사용합니다.

## world.link에서 조명 설정

확장은 `world.extensions`, 값은 **`world.environment.lighting`** 안에 선언합니다. `lighting`은 world 최상위 필드가 아닙니다.

```text
world {
  version: "0.1";
  id: "stone-courtyard";
  name: "Stone Courtyard";
  elements: "scene.element";
  style: "appearance.style";
  scripts: [];
  extensions: ["models-v1", "lighting-v1"];
  entry: {position: [0, 1.65, 4], target: [0, 1.6, -5]};
  environment: {
    background: "#9DAFC0",
    grid: false,
    lighting: {
      sunDirection: [-.55, .78, .30],
      sunColor: "#FFE3BE",
      sunIntensity: 3.2,
      skyColor: "#9BBBD6",
      groundColor: "#5E4B36",
      ambientIntensity: .35,
      exposure: 1,
      environmentMap: "lighting/courtyard.hdr",
      environmentIntensity: .65,
      shadow: {
        enabled: true,
        resolution: 2048,
        center: [0, 2, -5],
        extent: 16,
        bias: .0008,
        normalBias: .015
      },
      points: [
        {position: [-2, 2.5, -4], color: "#FFBB77", intensity: 10, range: 5}
      ]
    }
  };
}
```

`lighting`은 확장을 선언한 뒤 사용하는 선택 맵입니다. 값을 생략한 필드는 기본값을 사용하고, `{}`는 전체 기본 설정을 선택합니다. 철자가 틀렸거나 지원하지 않는 필드는 준비 단계에서 거부합니다. `environmentMap`은 실제로 월드 안에 있는 파일을 가리켜야 하므로, 예제를 사용할 때 해당 `.hdr`를 준비하거나 이 필드를 생략하세요.

## 태양광·주변광·노출

| 필드 | 기본값 | 범위·의미 |
| --- | --- | --- |
| `sunDirection` | `[-.55,.78,.30]` | 광원을 향하는 월드 방향. 0이 아닌 벡터를 정규화 |
| `sunColor` | `"#FFE3BE"` | 태양광 색, `#RRGGBB` |
| `sunIntensity` | `3.2` | 0~20, 방향광 세기 |
| `skyColor` | `"#9BBBD6"` | 하늘 쪽 주변광 색 |
| `groundColor` | `"#5E4B36"` | 지면 쪽 주변광 색 |
| `ambientIntensity` | `.35` | 0~4, 주변광 세기 |
| `exposure` | `1` | .1~8, ACES 톤 매핑 전의 선형 노출 배율 |
| `environmentMap` | 생략 | 월드 내부 상대 `.hdr` 리소스 경로 |
| `environmentIntensity` | `.65` | 0~8, HDR 환경 조명 배율 |

좌표는 Element와 같은 오른손 Y-up·미터 규칙입니다. 방향 벡터와 위치는 유한수 3개여야 하며 각 성분의 절댓값은 1,000,000 이하입니다. 색은 여섯 자리 16진 문자열이고 숫자로 대신할 수 없습니다. 노출은 카메라의 위치나 이동 속도를 바꾸지 않습니다.

태양의 방향과 모델의 노멀, 재질의 거칠기·금속성에 따라 표면 명암과 반사가 달라집니다. 주변광을 지나치게 올리면 그림자가 밝아져 깊이감이 약해질 수 있습니다. 먼저 태양광과 그림자를 정한 뒤 주변광과 노출을 조정하는 순서가 유용합니다.

## HDR 환경광

`environmentMap`은 equirectangular Radiance `.hdr` 이미지입니다. 기본색 이미지가 아니라 **빛의 밝기와 방향**을 표현합니다. 로더가 HDR을 읽고 diffuse irradiance와 거칠기별 GGX specular prefilter를 준비해 재질 조명에 사용합니다. 원본은 보존하며, 구면 면적에 맞춘 mip과 샘플의 분포·면적을 고려한 필터로 매우 밝은 태양 픽셀이 거친 표면에 점처럼 튀는 현상을 줄입니다.

- 파일은 월드 폴더 안에 두고 경로를 상대 경로로 지정합니다. HTTP URL이나 절대 경로는 허용하지 않습니다.
- 소스 파일은 최대 8 MiB, 각 축 최대 2,048픽셀·총 2,097,152픽셀입니다. 선형 RGB는 0~1,000,000의 유한값을 허용하고 GPU에는 RGB32F로 올려 밝은 태양 값도 보존합니다.
- 로컬에서도 실제 경로의 월드 경계를 검사하며, 원격에서는 다른 문서·모델과 같은 LINK 세션과 전체 64 MiB 전송 예산을 공유합니다.
- 경로 표기의 부모 이동과 정규화 뒤의 디렉터리 탈출은 거부합니다. 파일 해석과 디코드는 월드 로더가 수행합니다.
- HDR은 환경 조명과 원거리 하늘 배경에 함께 사용하며 모델과 같은 노출을 적용합니다. 환경맵을 생략하면 기존 `environment.background`가 배경색입니다.

## 방향광 그림자

현재 그림자는 태양광에 대한 **단일 방향광 shadow map**과 PCF 필터를 사용합니다. 필요한 공간에 그림자 영역을 맞춰 작은 조형의 디테일을 보존하세요.

| `shadow` 필드 | 기본값 | 범위·의미 |
| --- | --- | --- |
| `enabled` | `true` | bool. 방향광 그림자 사용 |
| `resolution` | `2048` | `512`, `1024`, `2048` 중 하나 |
| `center` | `[0,2,-5]` | 그림자 영역의 중심, 월드 좌표 |
| `extent` | `16` | 1~100m, 직교 그림자 영역의 반너비 |
| `bias` | `.0008` | 0~.01, 깊이 비교의 오프셋 |
| `normalBias` | `.015` | 0~.1, 노멀 방향 오프셋 |

한 맵의 해상도로 영역 전체를 표현하므로 `extent`를 크게 늘리면 작은 그림자의 세부가 줄어듭니다. `bias`와 `normalBias`를 너무 작게 하면 표면의 자기 그림자 무늬가 생길 수 있고, 크게 하면 그림자가 물체에서 떨어져 보일 수 있습니다. 모델의 실제 크기와 그림자 영역에 맞춰 조정합니다.

## 점광원

`points`에는 최대 4개 광원을 넣습니다. 각 필드는 선택 사항이며 비어 있는 `{}`도 아래 기본값을 사용합니다.

| 점광원 필드 | 기본값 | 범위·의미 |
| --- | --- | --- |
| `position` | `[0,2,0]` | 월드 위치 |
| `color` | `"#FFBB77"` | `#RRGGBB` |
| `intensity` | `10` | 0~100 |
| `range` | `5` | .1~50m |

점광원은 횃불이나 따뜻한 실내 조명의 색과 거리별 명암을 표현합니다. 현재 점광원에는 별도의 shadow map이 없으므로 벽으로 가려지는 점광원 그림자는 제공하지 않습니다.

## GLB의 PBR 재질 맵

현재 `models-v1` 로더는 다음 내장 PNG/JPEG 맵을 읽습니다. 각 참조는 GLB 안의 texture 인덱스를 거쳐 image 인덱스로 변환됩니다.

| glTF 재질 슬롯 | 해석 |
| --- | --- |
| `baseColorTexture` | RGB는 sRGB 기본색, A는 coverage |
| `normalTexture` | 선형 RGB tangent-space 노멀, `scale`은 X·Y에 적용 |
| `metallicRoughnessTexture` | 선형 G=거칠기, B=금속성 |
| `occlusionTexture` | 선형 R=주변 차폐, `strength`로 혼합 |
| `emissiveTexture` | sRGB 발광색 × `emissiveFactor` |

맵은 UV0와 repeat wrapping을 사용합니다. 모든 슬롯에서 다른 UV 세트와 `KHR_texture_transform`은 현재 지원하지 않습니다. `normalTexture.scale`은 -16~16의 유한수, `occlusionTexture.strength`는 0~1입니다. normal scale과 AO strength의 기본값은 1입니다. 어떤 재질 맵이든 사용하는 삼각형에는 실제 TEXCOORD_0가 있어야 합니다.

Style에서 `roughness`·`metalness`를 명시하면 GLB의 재질 인자를 덮어쓰고 해당 재질 맵 채널과 곱하므로 원래 PBR 재질을 보존하려면 이 속성을 생략하세요. `color: "#FFFFFF";`는 원래 기본색을 유지합니다. [모델 프로필](models-v1.md)의 파일·메시·이미지 검증도 함께 적용됩니다.

## 범위와 실행 확인

`lighting-v1` 세계는 환경 조명·모델 텍스처·그림자 자원을 합해 모델 렌더링용 GPU 예산 최대 192 MiB를 사용합니다. 기존 확장 없는 월드의 예산과 렌더링 경로는 유지합니다. 예산을 늘리는 것만으로 모델이나 조명의 품질이 자동으로 좋아지지는 않으며, 고품질 원본 에셋과 적절한 재질 맵을 함께 준비해야 합니다.

이번 단계는 단일 방향광 그림자·HDR 기반 조명·점광원·노출·PBR 재질 표현입니다. 실시간 전역 조명, 화면 공간 반사, Nanite 방식의 가상 지오메트리, LOD/자원 스트리밍, 지형 충돌, Quest 단독 APK는 포함하지 않습니다. 현재 LINK의 이동은 평면 보행 규칙을 따릅니다. PC 화면과 OpenXR의 양안에서 확인한 뒤 실제 Quest에서 프레임 시간과 시각적 안정성을 별도로 측정하세요.

`lighting-v1`의 그림자에는 깊이 텍스처 FBO, HDR에는 RGB32F, 환경 반사에는 셰이더 texture LOD를 지원하는 Windows OpenGL 호환 드라이버가 필요합니다. 기존 기본 도형·사진 세계는 해당 모델 조명 기능을 초기화하지 않습니다.
