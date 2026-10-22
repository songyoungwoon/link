<p align="center">
  <img src="docs/assets/link-banner.svg" alt="LINK — Linked Interactive Network. Every World. One Link." width="100%">
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>한국어</strong>
</p>

# LINK

**Every World. One Link.**

세계를 만들고, 주소를 부여하고, 누군가 그 안으로 들어오게 하세요.

**LINK — Linked Interactive Network**는 공통 프로토콜, 네이티브 브라우저, 세계 제작 규격으로 가상세계를 연결하는 프로젝트입니다. 제작자는 공간의 구조·동작·외형을 정의합니다. 방문자는 `link://` 주소를 열어 세계에 들어갑니다. 포탈은 또 다른 세계로 이어집니다.

**우리의 비전: An open standard for connected virtual worlds.**

제작자가 세계를 만들고 사람들이 하나의 브라우저로 세계 사이를 이동하는 생태계를 지향합니다. 현재 구현은 Windows C++20 네이티브 브라우저와 **실험적 v0.1**이며, 로컬 공간과 **직접 연결하는 LINK TCP v0.1 전송**을 지원합니다. 서버 컴퓨터의 실제 IPv4 주소를 사용해 `link://192.168.0.10:8000/hello-world`처럼 다른 컴퓨터의 세계를 엽니다.

**[첫 세계 만들기 →](docs/guides/README.md)** · [Element](docs/guides/element.md) · [Script](docs/guides/script.md) · [Style](docs/guides/style.md)

## 하나의 생태계, 세 가지 기반

| 기반 | 역할 | 현재 프로토타입 |
| --- | --- | --- |
| **LINK Protocol** | 세계의 주소·탐색과 브라우저 ↔ 공간/서버의 통신 계약을 정의합니다. | 로컬 `link://local/...`·IPv4 서버 연결, 바이너리 핸드셰이크와 리소스 교환, 포탈 이동. |
| **LINK Browser** | 세계를 방문하고 렌더링하며 탐색·상호작용합니다. | 3D 렌더링·걷기·상호작용·reload·공간 수명 관리를 갖춘 Windows 네이티브 앱. |
| **World Standards** | 제작자가 세계를 표현할 공통 규격입니다. | 구조의 **Element**, 동작의 **Script**, 외형의 **Style**을 C++ 런타임이 해석하고 실행. |

프로토콜은 주소와 세계 문서의 전송 방법을, 제작 규격은 그곳에 무엇이 있는지를 정의합니다. 브라우저는 그 세계를 실행합니다. [LINK TCP v0.1 계약](docs/link-protocol-v0.1.md)은 공간을 불러올 때 하나의 TCP 연결에서 수행하는 바이너리 핸드셰이크와 크기가 제한된 리소스 메시지를 정의합니다. 세계 문서 형식은 v0.1을 유지하며 HTTP를 사용하지 않습니다.

## 세계 만들기

세계는 편집할 수 있는 작은 문서 묶음입니다.

```text
first-world/
├── world.link           # 세계 정보, 진입 카메라, 문서 참조
├── scene.element        # Object·Component·시각적 Element
├── appearance.style     # 재질과 외형 규칙
└── scripts/
    └── pulse.script     # LINK Script로 작성한 동작
```

- **[Element](docs/guides/element.md)**는 Object·Component·Element를 계층으로 구성합니다. 부모 변환을 따라 부품이 함께 움직입니다.
- **[Script](docs/guides/script.md)**는 작은 공개 API와 생명주기 훅으로 로컬 상태·프레임 갱신·클릭 동작을 더합니다.
- **[Style](docs/guides/style.md)**은 재질과 class·ID 규칙을 정의합니다. 문서를 바꾸고 다시 불러오면 외형이 달라집니다.

브라우저는 실행 중 세계 문서를 읽습니다. 세계의 구조·동작·외형을 편집할 때 브라우저를 재컴파일하지 않아도 됩니다.

**[제작 가이드](docs/guides/README.md)**와 **[실행 가능한 first-world 예제](docs/guides/examples/first-world/world.link)**로 시작하세요. 전체 문법·API·검증·예산은 [언어 명세](native/docs/language.md)에 기록합니다.

## LINK Browser 실행

**준비 환경:** Windows 10/11 x64, Visual Studio 2022의 **C++를 사용한 데스크톱 개발**, Windows SDK, CMake 도구가 필요합니다. 네이티브 구현은 C++20·Win32·WGL/OpenGL을 사용합니다.

저장소 루트에서 PowerShell 도우미로 빌드·검사할 수 있습니다. Visual Studio의 CMake가 `PATH` 밖에 있어도 찾아 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native.ps1 -Test
.\build\native\Release\LINK.exe
```

개발자 터미널에서 CMake를 사용할 수 있다면 다음과 같습니다.

```powershell
cmake --preset windows
cmake --build --preset release --parallel
ctest --preset release
.\build\native\Release\LINK.exe
```

빌드 후 **[LINK_START.cmd](LINK_START.cmd)**로 홈 화면을 엽니다. 공간 문서는 실행 파일 옆 `worlds/`로 복사합니다. 빌드를 옮길 때 이 폴더도 함께 옮기며 다른 컴퓨터에는 호환되는 MSVC 런타임이 필요합니다. 최초 도구 설치에는 다운로드가 필요할 수 있습니다. 기본 로컬 공간은 오프라인으로 실행하며, 원격 공간을 열면 선택한 서버에서 문서·Script·이미지를 다운로드합니다.

![LINK Browser 홈 화면](docs/assets/browser-home.png)

가이드 예제를 소스 폴더에서 직접 엽니다.

```powershell
.\build\native\Release\LINK.exe --worlds "$PWD\docs\guides\examples" --open link://local/first-world
```

저장소의 데모 공간을 열려면 다음과 같습니다.

```powershell
.\build\native\Release\LINK.exe --worlds "$PWD\native\worlds" --open link://local/room-a
```

`--worlds`는 명시적인 로컬 루트를 선택하고 각 문서의 상대 참조는 해당 공간 내부에서 해석합니다. [LINK Protocol 로컬 주소 명세](docs/link-local-v0.1.md)를 참고하세요. 저장소 위치가 바뀌면 빌드 도우미가 이전 CMake 경로를 다시 구성하며, `-Fresh -Test`로 새 구성을 요청할 수도 있습니다.

## LAN으로 다른 컴퓨터의 세계 열기

서버는 별도 형제 저장소 `link-server`에 있으며 Python 3.10 이상과 표준 라이브러리를 사용합니다. **서버와 브라우저를 모두 갱신**하세요. 현재 브라우저는 직접 LINK TCP로 연결하므로 이전 HTTP 방식 브라우저·서버와 전송 방식이 다릅니다.

서버 PC의 `link-server` 폴더에서 실행합니다.

```powershell
.\LINK_SERVER_START.cmd
# 별도 공간 루트를 지정하려면:
python .\link_server.py --host 0.0.0.0 --port 8000 --worlds "C:\LINK\worlds"
```

서버가 출력하는 주소 중 실제 Ethernet·Wi-Fi 어댑터의 IPv4 주소를 사용합니다. `0.0.0.0`은 수신 설정이며 `127.0.0.1`은 같은 PC에서만 접속할 수 있습니다. 같은 신뢰하는 네트워크의 다른 PC에서 LINK 주소창에 서버 주소를 입력하거나 브라우저 저장소에서 실행합니다.

```powershell
.\build\native\Release\LINK.exe --open link://192.168.0.10:8000/hello-world
```

`192.168.0.10`은 예시이므로 서버의 실제 IPv4 주소로 바꿉니다. 포트를 생략하면 `8000`입니다. 서버의 `hello-world`·`second-world` 예제에는 이미지·클라이언트 Script·같은 서버로 이어지는 포탈이 있습니다. **F5**는 문서를 새로 가져오고 준비 실패·취소 때는 현재 공간을 유지합니다.

신뢰하는 네트워크용 평문 LINK TCP이며, 자체 프로토콜만으로 암호화나 서버 인증을 제공하지 않습니다. TLS·DNS/IPv6·인증·멀티플레이어 상태·OS `link://` 핸들러 등록은 후속 범위입니다. HTTP로 자동 전환하지 않으며 서버의 `--transport http`는 이전 브라우저용 명시적 호환 모드입니다. 서버가 Windows 방화벽을 자동 변경하지 않습니다. 리소스 제한·기존 공간 호스팅·접속 확인 방법은 [LAN 계약과 설정 안내](docs/link-lan-v0.1.md)를 따릅니다.

## 탐색과 상호작용

홈 중앙에는 **LINK**와 주소 입력창, **흑룡이 전시회·Room A·Room B** 바로가기가 있습니다. 주소를 입력하고 **Enter**를 누르거나 바로가기를 선택합니다. 세계에 들어가면 간결한 상단 주소바 아래로 공간이 펼쳐집니다.

**Ctrl+L**로 주소를 선택하고 **F5**로 문서를 다시 읽어 실행 상태를 초기화합니다. **F12**는 기본으로 숨겨진 개발자 도구를 열고 닫으며, 검사기와 실행 로그를 확인할 수 있습니다. **Esc**는 로딩을 취소하거나 큰 사진을 닫습니다.

| 상황 | 조작 |
| --- | --- |
| 걷기 공간 | 화면을 클릭한 뒤 **WASD/방향키**로 이동하고 **마우스 드래그**로 둘러봅니다. |
| 관찰 공간 | **왼쪽 드래그**로 회전, **오른쪽 드래그**로 이동, **휠**로 확대합니다. |
| 상호작용 형상 | Element를 클릭하면 가장 가까운 동작 소유 Script 바인딩에 전달합니다. |
| 사진 | **더블클릭**으로 크게 열고 **←/→**로 넘기며 **Esc**로 닫습니다. |
| 포탈 | 감지 영역 안으로 걸어 들어가면 목적지 LINK 주소를 준비하고 엽니다. |

## 현재 구현

- LINK 자체 제작 언어의 C++ lexer·문서 parser·Script AST 인터프리터.
- 계층·변환·ID·상태·인스턴스별 Script 저장소를 갖춘 렌더러 독립 객체 모델.
- Style 계산, 기본 형상, 로컬·다운로드 이미지 리소스와 네이티브 렌더링.
- 실험 확장 `particles-v1`: 제한된 입자 시스템, 이웃 검색, 벡터 연산, Script로 작성하는 인공생명 실험. [제작 가이드](docs/guides/particles.md).
- 걷기·로컬 및 원격 포탈, 크기가 제한된 LINK 리소스 전송·취소 가능한 로딩·새로 가져오는 reload·검증 오류 처리·공간 전환 정리.
- 핵심 로직·이미지·경로 제한 자동 검사와 네이티브 입력 smoke. 빌드 후 `ctest --preset release`로 포함된 자동 검사를 실행할 수 있습니다.

현재 대상은 **신뢰하는 로컬 문서와 LAN 서버**입니다. 다운로드한 Script는 각 브라우저의 공간 인스턴스에서 동일한 제한 API로 실행합니다. 입력·인터프리터 예산을 적용하며 프로세스 격리와 강화된 권한은 후속입니다. 문법과 API는 실험 단계로 변경될 수 있습니다.

## 실행해 볼 세계

아래 세계는 같은 브라우저와 런타임을 사용합니다.

| 세계 | 주소 | 살펴볼 동작 |
| --- | --- | --- |
| **흑룡이 전시회** | `link://local/heukryong` | 사진 복도를 걷고 더블클릭으로 사진을 연 뒤 끝의 포탈에 들어갑니다. |
| Hello World | `link://local/hello-world` | 복도 끝 포탈을 넘어 우주 광장에 발을 내딛습니다. 행성과 별 아래, 거대한 고리와 입체 글자를 향해 걸으며 첫 지평선을 탐험합니다. |
| [AFTERLIGHT · 오후의 방](docs/afterlight.md) | `link://local/afterlight` | 미리 계산한 오후빛이 드는 작은 방을 걷고 책을 열거나 스탠드를 켭니다. |
| [PARTICLE LIFE](docs/particle-life.md) | `link://local/particle-life` | 이미지 없이 이웃 반응·에너지·제한된 증식과 원천 이동을 관찰하는 입자 실험실입니다. |
| Room A | `link://local/room-a` | 힌지 문을 열고 시간에 따라 회전하는 조형물을 봅니다. |
| Room B | `link://local/room-b` | 두 돌의 독립 상태와 파괴 동작을 확인합니다. |

사진 원본은 읽기 입력이며 데모는 별도 표시용 사본을 사용합니다. 준비 방법은 [데모·자산 안내](docs/exhibition.md)를 따릅니다. 사진 공간은 세계가 담을 수 있는 콘텐츠의 한 예제입니다.

## LINK가 향하는 곳

장기 목표는 제작 규격의 상호운용성과 여러 구현을 갖춘 연결된 세계의 생태계입니다. 다음 과제가 남아 있습니다.

- 버전별 세계 계약과 상호운용성 요구 구체화.
- DNS·서버 발견, TLS 전송, 인증, 상호운용 가능한 프로토콜 프로파일.
- 공간 권한·강화된 실행 격리·공유 상태.
- 다른 네이티브 플랫폼과 이후 몰입형 입력·렌더링.

이는 현재 로컬·LAN 개발본 이후의 프로젝트 방향입니다. 공개 표준은 비전이며 v0.1은 그 아이디어를 실제 브라우저에서 시험하는 출발점입니다.

**세계를 만들고, 경계를 탐색하고, 세계가 연결되는 방식을 함께 다듬어 주세요.**

[제작 가이드](docs/guides/README.md) · [World Standards](docs/link-world-v0.1.md) · [로컬 프로토콜](docs/link-local-v0.1.md) · [LINK TCP 프로토콜](docs/link-protocol-v0.1.md) · [LAN 연결 안내](docs/link-lan-v0.1.md) · [LINK Browser 실행](#link-browser-실행)

## 라이선스

소스 코드·표준 문서·제작 가이드·예제와 사진을 제외한 프로젝트 자산은 **[Apache License 2.0](LICENSE)**으로 공개합니다.

`image/**`의 개인 사진 원본과 `native/worlds/heukryong/assets/**`의 표시용 사본은 **Apache 2.0 적용 대상에서 제외**하며, 별도의 [사진 자산 이용 조건](ASSET_LICENSE.md)을 따릅니다. 해당 조건은 포함된 LINK 데모의 빌드·실행·시험·관람을 위해 사진을 함께 제공하는 것을 허용하며, 사진의 일반적인 재사용을 허락하는 것은 아닙니다.

Apache 2.0은 LINK 명칭·로고의 일반적인 상표 사용 권한을 부여하지 않습니다. [LICENSE](LICENSE) 제6조를 따릅니다.
