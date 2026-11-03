# LINK Architect는 독립 프로젝트입니다

시각적 제작 프로그램은 LINK Browser 저장소에서 분리한 **link-architect** 프로젝트에서 제공합니다. LINK Browser·LINK Server와 같은 LINK 생태계의 별도 도구입니다.

| 프로젝트 | 역할 |
| --- | --- |
| LINK Browser (`link`) | 월드를 방문해 렌더링하고 탐색·상호작용 |
| LINK Server (`link-server`) | LINK 프로토콜로 월드 문서 제공 |
| LINK Architect (`link-architect`) | 로컬 월드를 시각적으로 편집하고 검증·저장 |

Architect 프로젝트는 자체 CMake·빌드 도우미·런처·예제와 공통 코어 소스 스냅샷을 포함합니다. Browser 프로젝트가 옆에 없어도 단독으로 빌드할 수 있습니다. Windows의 Visual Studio C++·CMake 빌드 도구는 필요합니다.

Architect의 README를 기준으로 **그 프로젝트 루트**에서 빌드하고 LINK_ARCHITECT_START.cmd를 실행합니다. LINKArchitect.exe와 LINKAuthor.exe도 그 프로젝트에서 생성합니다. 이 Browser 저장소의 실행 파일이나 빌드 경로를 사용하지 마세요.

분리로 Element·Script·Style의 문법이나 LINK 프로토콜 버전을 바꾸지 않습니다. 여기에는 [authoring-v1 편집 계약](authoring-v1.md), [patch 스키마](schemas/authoring-patch.schema.json), [inspection 스키마](schemas/authoring-inspect.schema.json), [AI 제작 가이드](guides/ai-authoring.md)를 공통 표준 문서로 유지합니다. 도구 명령 예제의 실행 기준은 별도 Architect 프로젝트입니다.
