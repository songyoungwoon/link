# LINK Browser · Windows PC VR

LINK Browser의 실험적 OpenXR 모드로 Windows PC에서 월드를 렌더링하고, Meta Quest 3에는 Quest Link 또는 Air Link로 전달합니다. 같은 Element·Script·Style과 기존 로컬 주소·LINK TCP 전송을 사용합니다. LINK World Standards v0.1과 서버 프로토콜은 바꾸지 않습니다. 이번 범위는 Windows PC VR이며 Quest에 설치하는 Android APK를 만들지 않습니다.

2026-10-31 사용자 실기기 확인에서 **Quest 3 + Air Link로 LINK의 VR 모드 진입, 컨트롤러 조작, Hello World·흑룡이 전시회 등 여러 공간 이용이 정상 동작했습니다.** Unknown Sources 허용 이후 확인한 결과이며, 프레임 지연·장시간 착용감의 계측 결과는 아닙니다. 새 멀티플레이어 RPG의 헤드셋 성능과 조작은 별도로 확인해야 합니다. 아래 목록은 후속 회귀 검증에도 사용합니다.

## PC와 헤드셋 준비

Windows 10/11 x64와 LINK의 [기존 빌드 도구](../README.ko.md#link-browser-실행)가 필요합니다. PC의 GPU·메모리·드라이버는 Meta의 현재 지원 조건도 충족해야 합니다. Meta Horizon Link PC 앱과 헤드셋 소프트웨어를 갱신하세요. 이전 버전의 PC 앱에서는 이름이 **Meta Quest Link**로 표시될 수 있습니다. [Meta의 Windows PC 요구 사항](https://www.meta.com/help/quest/140991407990979/)을 확인하세요.

연결 방법은 둘 중 하나입니다.

- **USB:** 데이터 전송을 지원하는 USB 3 연결을 사용합니다. 현재 Meta 설정 안내는 최소 5 Gbps USB-C 3.2 케이블과 PC의 USB 3.2 포트를 지정합니다. PC 앱의 기기 화면에서 USB 연결 테스트를 실행하고, 헤드셋 안에서도 Link를 실행합니다.
- **Air Link:** Meta 앱과 헤드셋을 페어링하고 헤드셋 안에서 Link 세션을 시작합니다. Meta는 Air Link용 Wi-Fi로 5 GHz를 권장합니다.

메뉴 이름과 연결 요구 사항은 앱 버전에 따라 달라질 수 있습니다. 최신 절차는 [Meta의 Link·Air Link 설정](https://www.meta.com/en-gb/help/quest/509273027107091/)과 [연결 테스트 안내](https://developers.meta.com/vr/documentation/unity/unity-link/#test-connection)를 따릅니다. USB로 파일이 보이거나 충전된다는 사실만으로 PC VR 연결이 준비된 것은 아닙니다.

## PC 앱 실행 허용과 OpenXR 런타임

LINK 같은 개발 중인 PC VR 앱을 실행하려면 **PC의 Meta Horizon Link 또는 Meta Quest Link 앱**에서 다음을 설정합니다.

1. **Settings → General**을 엽니다.
2. **Unknown Sources(알 수 없는 출처)**를 **On**으로 켜고 허용 안내를 확인합니다.
3. **OpenXR Runtime → Set Meta Horizon Link as active**를 선택합니다. 기존 앱에서는 Quest Link라는 이름이 표시될 수 있으며, 이미 활성화된 버튼은 비활성 상태로 표시됩니다.

이 설정은 PC 앱에서 합니다. 이번 Windows PC VR 실행에는 APK용 개발자 모드가 필요하지 않습니다. Meta의 [OpenXR runtime considerations](https://developers.meta.com/vr/documentation/unity/unity-haptics-sdk-get-started/#openxr-runtime-considerations)와 [활성 런타임 설정](https://developers.meta.com/vr/documentation/unity/unity-link/#activate-openxr-runtime)을 참고하세요.

LINK는 저장소에 고정한 공식 OpenXR SDK 1.1.65 헤더와 static loader를 사용합니다. 일반 빌드 때 SDK를 다시 다운로드하지 않습니다. 포함된 loader는 헤드셋 드라이버나 Meta 런타임을 설치하지 않습니다. PC 앱이 기기를 인식하고 헤드셋에서 Link 화면을 연 상태에서 LINK의 VR 모드를 시작하세요. native C/C++ 앱에서 Khronos의 헤더와 loader를 사용하는 방식은 [Meta의 OpenXR PC 개발 안내](https://developers.meta.com/vr/documentation/native/pc/dg-openxr/)와 같습니다.

## 빌드와 시작

저장소 루트에서 VR용 빌드 도우미를 실행합니다. 이 도우미는 OpenXR를 켜고 **`build/vr`에 별도로 빌드**하므로 일반 `build/native` 빌드와 경로가 다릅니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-vr.ps1 -Test
.\LINK_VR_START.cmd
```

[LINK_VR_START.cmd](../LINK_VR_START.cmd)는 `build/vr/Release/LINK.exe --vr`를 실행합니다. 먼저 빌드가 필요하며, 도우미의 `-Fresh`로 새 CMake 구성을 요청할 수 있습니다.

최신 기본 빌드를 사용한다면 [LINK_START.cmd](../LINK_START.cmd)로 기존 홈 화면을 열고, 원하는 월드에 접속한 뒤 **F8**로 VR을 시작할 수도 있습니다.

`--vr`로 시작합니다. `--open`을 생략하면 작은 걷기·클릭 예제 `vr-room`을 엽니다.

```powershell
.\build\vr\Release\LINK.exe --vr

# 다른 로컬 월드
.\build\vr\Release\LINK.exe --vr --open link://local/hello-world

# 기존 LINK TCP 주소
.\build\vr\Release\LINK.exe --vr --open link://192.168.0.10:8000/hello-world
```

소스의 월드 폴더를 바로 읽으려면 기존 `--worlds "$PWD\native\worlds"`도 함께 지정합니다. 예시 IP는 실제 LINK 서버 주소로 바꿉니다. [LAN 연결 안내](link-lan-v0.1.md)의 서버 설정은 그대로 적용합니다.

| 옵션 | 의미 |
| --- | --- |
| `--vr` | VR 모드로 시작; 주소 생략 시 `vr-room` |
| `--vr-turn snap` 또는 `smooth` | 회전 방식; 기본 `snap` |
| `--vr-snap-angle 30`, `45`, `90` | snap 한 번의 회전각; 기본 45도 |
| `--vr-turn-speed 90` | smooth 회전 속도(도/초); 0 초과 360 이하, 기본 90 |
| `--vr-check report.json` | OpenXR 시스템·그래픽·세션 준비를 검사하고 JSON 기록; 프레임 제출 없음 |
| `--vr-smoke report.json` | 월드를 열어 짧은 세션·프레임 제출 검사 후 JSON 기록 |

예를 들어 smooth 회전을 선택하고 속도를 낮춰 시작하려면:

```powershell
.\build\vr\Release\LINK.exe --vr --vr-turn smooth --vr-turn-speed 60
```

## 작은 VR 예제

기본 `link://local/vr-room`은 고정된 방과 바닥·벽 표식, 중앙의 빛 구체로 구성한 작은 확인용 월드입니다. 왼쪽 stick으로 천천히 이동하고 중앙 구체를 광선으로 가리킨 뒤 trigger를 누르면 색과 발광이 바뀝니다. 오른쪽 프레임의 포탈은 로컬 `hello-world`로 이어집니다. `walk`와 기존 Script 상호작용을 그대로 사용합니다.

## 조작과 월드 이동

| 입력 | 동작 |
| --- | --- |
| 헤드셋 | 머리 방향과 위치 추적 |
| 왼쪽 thumbstick | 머리가 향하는 수평 방향을 기준으로 전후·좌우 이동 |
| 오른쪽 thumbstick 좌우 | 선택한 snap 또는 smooth 회전 |
| 왼쪽 또는 오른쪽 컨트롤러 광선 + trigger | 가리킨 Element에 기존 Script 상호작용 전달 |
| 오른쪽 **B** | VR 종료 |
| 왼쪽 **X** 또는 오른쪽 **A** | 현재 월드의 `entry`를 기준으로 시점 다시 맞추기 |
| **F8** | VR 시작·종료 |
| **F9** | 현재 월드의 `entry`를 기준으로 시점 다시 맞추기 |
| 데스크톱 메뉴 | VR 시작·종료, 시점 다시 맞추기, Snap·Smooth 선택 |

왼쪽 stick의 인공 이동은 `walk`가 있는 월드에서만 동작합니다. `walk`가 없는 관찰 월드는 실제 머리 위치 이동과 회전을 사용합니다. 회전은 실제 머리 위치를 중심으로 적용합니다. 입력 포커스나 추적을 잃으면 이동·회전·선택을 중지하며 snap은 stick을 중립으로 되돌린 뒤 다시 입력합니다.

최신 OpenXR API 경로에서는 바닥 기준 `STAGE` 공간을 우선 사용하고, 사용할 수 없으면 `LOCAL`로 전환합니다. 런타임이 요청한 API 버전을 지원하지 않아 OpenXR 1.0으로 다시 연결하는 경우에는 `LOCAL`을 사용합니다. 바닥 기준이 없는 `LOCAL`에서는 월드의 `walk.eyeHeight`, 없으면 1.65m를 눈높이로 근사해 머리·양쪽 눈·컨트롤러 pose를 같은 기준으로 보정합니다. 실제 바닥 높이와 재정렬을 기기에서 확인하세요. 물리적으로 움직인 머리 위치를 월드의 `walk` 상자로 강제 이동시키지 않습니다.

주소 입력·홈·F5·개발자 도구·메뉴는 데스크톱 창에서 조작합니다. 주소 변경·포탈·reload는 VR을 유지하면서 새 월드의 `entry`로 기준을 다시 맞춥니다. **홈** 또는 **실행 중지**는 VR을 종료합니다. VR 모드를 종료해도 일반 브라우저로 월드를 계속 다룰 수 있습니다.

### 헤드셋 안내가 가리거나 컨트롤러가 반응하지 않을 때

1. PC Meta 앱의 **Settings → General → Unknown Sources**가 켜져 있는지 확인합니다.
2. LINK에서 VR이 켜져 있다면 **F8**로 종료한 뒤 다시 **F8**로 시작합니다.
3. 헤드셋에서 LINK 앱으로 돌아오고 Meta Dash·Meta 메뉴를 닫아 LINK가 VR 입력 포커스를 받게 합니다. 안내 창이 남아 있으면 확인합니다.
4. stick을 중립으로 놓고 trigger에서 손을 뗀 다음 다시 조작합니다. `vr-room`에서 **왼쪽 stick은 이동**, **오른쪽 stick은 회전**, **빛 구체를 가리킨 trigger는 색 변경**입니다.

데스크톱 하단 상태줄로 대기 원인을 확인합니다.

| 상태 표시 | 확인할 것 |
| --- | --- |
| `VR 입력 포커스 대기` | 헤드셋의 안내를 확인하고 LINK로 돌아와 Meta 메뉴 닫기 |
| `VR 머리 추적 대기` | 헤드셋의 추적 상태와 주변 환경 |
| `VR 컨트롤러 추적 대기` | 컨트롤러를 깨우고 연결·추적 확인 |
| `왼쪽 스틱 대기` 또는 `오른쪽 스틱 대기` | 해당 컨트롤러의 연결·추적과 입력 준비 상태 |

**F12** 실행 로그에는 `OpenXR session: VISIBLE`, `OpenXR session: FOCUSED`, `OpenXR input: waiting-focus`, `OpenXR input: ready` 같은 상태 변경을 기록합니다. `VISIBLE`만으로는 LINK가 VR 입력 포커스를 받은 것이 아닙니다.

## 회전과 편안함

처음에는 snap turn을 사용하고 짧게 확인하세요. Meta는 snap turn을 편안함을 고려한 초기 옵션으로 제공하고, smooth turn은 사용자가 선택하도록 권장합니다. [Turning](https://developers.meta.com/vr/documentation/iwsdk/concepts/locomotion/turn/)과 [Locomotion Best Practices](https://developers.meta.com/vr/design/locomotion-best-practices/)를 참고하세요.

머리 움직임은 헤드셋 추적에서 가져옵니다. 월드의 카메라 애니메이션이나 화면 마우스 회전을 머리 추적에 더해 강제로 시선을 움직이지 않는지 확인하세요. 실제 공간의 경계를 확보하고 불편하면 사용을 멈춥니다. 월드의 걷기 범위는 물리적인 벽이나 실제 방의 장애물을 감지하는 경계가 아닙니다.

## 문제 확인

| 증상 | 확인할 것 |
| --- | --- |
| 알 수 없는 출처 안내가 화면을 가림 | **PC Meta 앱 → Settings → General → Unknown Sources On** 후 F8로 VR 재진입 |
| 런타임을 찾지 못함 | Meta PC 앱 설치와 활성 OpenXR 런타임 설정 |
| `XR_ERROR_FORM_FACTOR_UNAVAILABLE` | 런타임이 준비된 HMD를 제공하지 못함; PC 앱의 기기 상태와 헤드셋 안의 Link 세션 시작 여부 |
| OpenGL 요구 사항 오류 | GPU 드라이버, 런타임의 OpenGL 요구 버전, compatibility context 생성 여부 |
| 그래픽 기기 불일치 | LINK와 Meta 런타임이 같은 GPU를 사용하는지 확인 |
| 화면은 보이지만 컨트롤러 반응이 없음 | 헤드셋에서 LINK 앞으로 돌아와 Meta 메뉴 닫기, 컨트롤러 연결·추적, stick·trigger 중립 후 재시도 |
| 원격 월드 로딩 실패 | 일반 화면에서도 같은 주소가 열리는지와 LINK 서버 상태 |

기기를 분리하거나 런타임이 세션 종료를 요청한 경우에는 일반 브라우저로 돌아와 주소와 월드를 다시 조작할 수 있어야 합니다. 오류 메시지를 남겨 어떤 단계에서 실패했는지 확인합니다. 프로세스가 종료되거나 화면이 멈추는 현상은 정상적인 기기 없음 처리로 보지 않습니다.

## 검증 목록

자동 검사와 헤드셋 검증을 따로 기록합니다. 자동 검사에는 합성 OpenXR 런타임을 사용하는 경로가 있으며 이는 Quest 테스트가 아닙니다. 빌드·수학 검사·합성 런타임 검사가 통과해도 스테레오 영상·추적 지연·실제 컨트롤러·착용감이 검증된 것은 아닙니다.

```powershell
# 런타임과 시스템 기능 조사
.\build\vr\Release\LINK.exe --vr-check ".\build\vr-check.json"

# 연결된 헤드셋의 짧은 세션·프레임 제출 검사
.\build\vr\Release\LINK.exe --vr-smoke ".\build\vr-smoke.json"
```

`--vr-check`는 OpenXR 초기화와 HMD 시스템 조회, 그래픽 요구 사항·세션·swapchain 준비를 시도하고 바로 정리합니다. 준비된 HMD가 필요하지만 월드나 눈별 프레임을 렌더링하지 않으므로 영상 출력의 증거가 아닙니다.

`--vr-smoke`는 `--vr`를 포함하며, 주소를 주지 않으면 `vr-room`을 엽니다. 최대 40초 동안 렌더링 가능한 프레임 120개 제출을 목표로 하며, 성공 판정에는 추적된 머리 pose 표본과 입력 포커스를 받은 프레임(`focusedInputFrames > 0`)도 필요합니다. 두 진단 옵션은 한 번에 하나만 사용합니다. 성공은 종료 코드 0, 검사 실패는 2입니다.

JSON의 `passed`와 `failure`, 제출·추적·입력 포커스 표본 수를 함께 확인하세요. `sessionState`·`inputFocused`·`inputState`로 세션과 입력 포커스를, `headTracked`·`leftStickActive`·`rightStickActive`로 추적과 stick 준비 상태를 확인합니다. `locomotionAvailable`은 월드에 `walk`가 있는지를 나타냅니다. `syntheticRuntime:true`는 합성 런타임 결과입니다. `headsetFramesVerified`는 capability 검사와 합성 런타임에서 `false`이며, `humanComfortVerified`는 항상 `false`입니다. 실제 런타임의 smoke가 성공해도 사람이 헤드셋을 착용해 영상·추적·컨트롤러·편안함을 확인하는 절차가 남습니다.

- 일반 화면에서 기존 월드 로드·상호작용·포탈·F5를 확인합니다.
- OpenXR 런타임이 없거나 헤드셋이 준비되지 않은 경우 오류를 표시하고 일반 화면을 계속 사용할 수 있는지 확인합니다.
- Quest 3에서 양쪽 눈의 영상, 머리를 돌릴 때의 방향, 위치 추적과 실제 크기를 확인합니다.
- 양쪽 컨트롤러의 광선과 trigger 선택이 기존 Script 상호작용에 전달되는지 확인합니다.
- thumbstick 이동과 snap·smooth 회전, 머리 위치를 중심으로 한 회전이 예상대로 동작하는지 확인합니다.
- 포탈·reload에서는 VR 세션을 유지한 채 새 월드가 보이는지, 홈에서는 세션과 월드 자원이 정리되는지 확인합니다.
- Link 해제·입력 포커스 상실·VR 종료 후 일반 화면과 다시 VR 진입을 확인합니다.
- 사용한 헤드셋, 연결 방식, 활성 런타임, GPU·드라이버, 테스트한 월드와 결과를 함께 기록합니다.

## 그래픽 연동 참고

현재 렌더러의 고정 기능 OpenGL 호출을 유지하려면 **compatibility profile**이 필요합니다. core context에서는 기존 고정 기능 호출을 사용할 수 없습니다. 실제 OpenGL 버전과 profile을 확인하며, legacy context의 profile mask가 0인 드라이버에서는 `GL_ARB_compatibility` 확장 지원을 확인합니다. [Khronos의 WGL context 규칙](https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt)을 따릅니다.

`XR_KHR_opengl_enable`을 활성화하고 같은 instance·system에 대해 `xrGetOpenGLGraphicsRequirementsKHR`를 호출한 뒤 세션을 생성합니다. 보고된 최소 OpenGL 버전은 충족해야 하며 최대 값은 런타임이 알고 있는 테스트 버전입니다. [OpenGL 요구 버전 조회](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetOpenGLGraphicsRequirementsKHR.html), [요구 버전 구조체](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsRequirementsOpenGLKHR.html)를 참고하세요.

Windows 세션은 실제 `HDC`·`HGLRC`를 전달합니다. 런타임과 context의 GPU가 다르면 `XR_ERROR_GRAPHICS_DEVICE_INVALID`가 발생할 수 있습니다. [Windows OpenGL binding](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingOpenGLWin32KHR.html)의 장치·동기화 요구를 확인하세요.
