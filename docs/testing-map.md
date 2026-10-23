# 시험 지도 (축 A–J)

계획서 `plans/2026-09-19-world-class-gis-plan.md` §1.1 축마다 **자동 시험 1개 이상**.  
실행은 저장소 루트, Release CTest. 원본 조사 GPKG는 쓰지 않는다.

```powershell
$env:PATH = "C:\Program Files\CMake\bin;" + $env:PATH
. .\scripts\dev-env.ps1
ctest --test-dir build -C Release -R '<아래 이름>' --output-on-failure
```

플레이크 5회: `.\scripts\ctest-flake.ps1 -Repeat 5`

| 축 | 자동 시험 | 무엇을 보나 |
| --- | --- | --- |
| A 저장·무결성 | `storage_safety` | 세대 저장, `SurveySession::persistWork`, 동반 QGZ |
| A | `save_open_roundtrip` | 저장→재열기→제출 (슬롯 실패 이력 있음) |
| B CRS·변환 | `export_survey_areas` | 5179 SHP 좌표 ≤1mm |
| B | `topographic_source_crs` | 수치지형 원본 CRS |
| C 편집 | `workflow_engine` | undoStack, 스냅, 위상 편집 |
| C | `save_open_edit` · `save_open_edit_delete` · `save_open_edit_draw` | 정점 Undo·메뉴 (ctrlZ 슬롯 실패 이력), 레이어 Delete 키, 그리기 중 기록 칸 |
| C | `parcel_copy` | 받은 지적도 필지 Ctrl+C·V: 같은 자리, 여러 조각, 원본 줄을 지워도 복사본이 남음, 저장 뒤 다시 열어도 남음 |
| C | `parcel_copy_keys` | 도형선택에서만 Ctrl+C·V, 누르고 있어도 한 번만 붙음 |
| D 성능·안정 | `perf_engine` | 15만 필지·열기·조판 상한 |
| D | `parallel_render` | 병렬 렌더 재현, 제품은 꺼 둠 |
| D | `cadastral` | 지적 준비 cheap key · P3-4 절대 HTTP 마감 |
| D | [network-download-timeout-checklist.md](network-download-timeout-checklist.md) | 내려받기 취소·시간 제한·재시도·오프라인 문구 |
| E UX | `layer_information` | 작은 창 목록 ≥5행 |
| E UX | `layer_list_chrome` | 「지적도」 머리글: 받은 지적도·VWorld 지적은 지적도 구역, 「주변·참조」와 나뉨 |
| E | `theme_qss` | Tab/Enter 새 조사→저장 |
| E | `above_labels_200` | 200% 라벨 |
| E | `ribbon_overflow` · `ribbon_pixels` | 리본은 접지 않고 창 너비에 맞는 가장 큰 크기(타일 56~20)로 모든 칩을 보인다, 글자·타일이 잘리지 않는다 |
| E | `toolbar_fit` | 그리기 보조 줄은 » 없이 글자를 숨긴 뒤 아이콘을 20에서 16으로 줄인다 |
| E | `home_polish` | 세부 디자인 보완(2026-10-11) 홈: 제출 준비 한 줄·칩 숫자 모노, 연결 상태 「설정」·「인터넷 확인」, 표제란 3행, 최근 조사 줄 hover, 폴더 칸·줄 경로 |
| E | `map_polish` | 같은 보완 지도 화면: 오른쪽 판 탭·빈 문장, 되돌리기 한 덮개, 상태줄 「→ 제출 5179」·32 px, 그리기 줄(고른 도구 먹 채움·토글 귀리색·묶음 선), 탭 밑줄 |
| E | `drawing_polish` | 같은 보완 도면 화면: 오른쪽 판 캡션 + 이음선, 왼쪽 머리줄·빈 문장, 용지 설정 창의 읽는 줄 |
| E | `page_polish` | 같은 보완 판 틀: 홈·지도의 바깥 틀 없음, 7 px 분할선 가운데 1 px 선, 지도가 판 사이를 채움 |
| E | `window_polish` | 같은 보완 창: 단추·입력칸 32 px, 검수 항목 삼선표 |
| E | `inspector_polish` | 낮은 창(360·420·460·900 px)에서 오른쪽 판 스타일 탭이 스크롤되고 「배경 지도」 줄이 아래에 붙음 |
| E | `browser_polish` | 파일함: 「SD 카드」 없음, 「상위」는 오른쪽 끝, 조용한 24 px 바로가기 |
| E | `alert_polish` | 실패 알림 29곳이 한 틀(KaUserError)이다(소스 글을 읽어 남은 기본 경고창이 없음을 본다) |
| E | `layout_sheet_rules`·`save_open_drawing` | 범례는 용지 밖에도 놓을 수 있다: 내용에 맞춰 흘러도, 마우스로 놓은 뒤에도, 도면을 다시 열어도 놓은 자리 그대로 |
| F 제출 | `checklist_engine` | 자기교차·빈 도형·0면적 차단 |
| F | `export_survey_areas` | SHP+PDF 패키지 |
| G 배포 | `theme_qss` 슬롯 `versionAndLaunchScripts_exist` | `VERSION`·바로가기·QGIS 핀 파일 |
| H 시험·CI | `scripts/ctest-flake.ps1` | CTest 5회, 5/5 아닌 목록 |
| H | `catch_log` | 시험 하네스가 로그 경로를 씀 |
| I 구조 | `catch_log` 슬롯 `srcCatchHandlers_allLog` | `catch (...)` 로그 누락 검출 |
| J 비밀·문서 | `e2e_opaque_suite` | VWorld 키를 URL에만 주입, 하드코딩 없음 |
| J | `theme_qss` 슬롯 `noUrl` | QSS `url(` 없음 |
| J | [docs/README.md](README.md) | 현행 문서 한 장 (`docs/archive/` 분리) |
| K 보고서·평가서 | `assessment_contract` | 사실 묶음 내보내기, 근거 항목·평가 결과 검사(근거 번호, 잠긴 도메인 수치, 개발 용어, 모식도 표시), 저장소의 샘플 JSON |
| K | `assessment_skeleton` | AI 없이 만든 평가 뼈대가 계약 검사를 통과하고, 근거 없는 곳을 「자료 없음」으로 말하며, 같은 입력이면 같은 결과 |
| K | `report_region` | 보고서 지역 분류: 경로의 도 폴더 우선, 고성·광주·영동은 폴더가 없으면 「미분류」 |
| K | `ui_terms` | 「조판」 금지, 보고서 글자에 개발 용어(RAG·SDK·누름틀 …)와 장식 문자 금지 |
| K | `hwp_survey_report` | 보고서 문서 만들기·열기·고치기, 공식 서지 표(실제 보고서 DB가 없으면 일부 건너뜀) |

## save_open 분할 (7-3)

한 exe, 최장 ≤60초. `RESOURCE_LOCK ka_save_open`.

`save_open_window` · `save_open_topo` · `save_open_drawing` · `save_open_edit` · `save_open_edit_delete` · `save_open_edit_draw` · `save_open_roundtrip` · `save_open_open` · `save_open_open_invalid` · `save_open_commit` · `save_open_saveas` · `save_open_saveas_fail`

## 리눅스에서 돌리기 (2026-10-23 직접 돌려 본 결과)

평소 개발과 시험은 리눅스, 마무리는 Windows다. 폴더 이름에 괄호가 있으면(`hgis (사본)`) CMake가 자동 RCC 단계에서 셸 오류를 내니 괄호 없는 경로에 둔다.

```bash
cmake --preset linux && cmake --build build-linux --parallel 32     # 첫 빌드 약 1분 반(36코어), 결과는 build-linux/
ctest --preset linux -j8 --timeout 180 -R '^(바꾼 시험 이름)$'          # 화면 없이(offscreen) 돈다
```

187개 중 167개가 리눅스에서 통과했다(Qt TIFF 플러그인 `qt6-image-formats-plugins`를 깔면 3개 더). 나머지 20개는 아래처럼 나뉜다(`storage_safety`는 단독 재실행에서 통과해 흔들림으로 분류).

| 구분 | 시험 | 이유 |
| --- | --- | --- |
| Windows 전용(`cad_dwg`는 오류 문구로 확인) | `cad_dwg` · `cad_import` | 묶어 둔 DWG 변환기가 Windows `.exe`라 실행되지 않는다(`execve: Permission denied`, `cad_import`는 시간 초과). |
| | `topographic_settings` | Windows 비밀 저장(DPAPI) 흐름을 시험한다. |
| | `survey_durability` | Windows Restart Manager(열고 있는 프로그램 확인)를 시험한다. |
| Windows 전제로 보임(시험 이름·오류로 짐작, 확정 아님) | `survey_facts` · `home_cards` | Windows 경로 표기(`toNativeSeparators`, 네트워크 경로). |
| | `cad_pending_copies` · `file_cleanup` | 열려 있는 파일을 지우지 못하는 Windows 잠금 동작. |
| | `perf_engine` · `save_open_monitor` · `theme_qss` | 시간 예산(`startup_home 3320ms > 70ms`)·화면 크기·글꼴 기준이 Windows 기준값이다. |
| 부하 때 흔들림 | `storage_safety` | 전체 병렬 실행에서만 실패하고 단독 재실행은 통과했다. |
| 설치하면 풀림 | `georef_engine` · `georef_backup` · `workflow_engine` | `img.save(tif, "TIFF")` 실패: Qt TIFF 플러그인(`qt6-image-formats-plugins`)이 없다. |
| 진짜 오류(5단계에서 고침) | `ui_terms` | 보고서 창 글자 4곳(`KaHwpReportStudio*.cpp`, `KaHwpLivePaperWindow.cpp`)에 금지된 「조판」이 보인다. Windows에서도 같다. |
| 원인 미확인 | `longitude_ruler` · `topographic_merge` · `topographic_import` · `dem_legend` | 리눅스 한계인지 코드 문제인지 아직 모른다. Windows CI 결과로 가른다. |

리눅스에서 통과해도 Windows에서 깨질 수 있다(경로·인코딩·WebEngine). Windows 전용 코드를 건드린 변경은 푸시 뒤 Windows CI 초록까지 확인한다. 커밋 전 검사는 `build-linux/`를 증거로 인정하지만 CI는 볼 수 없다.

## 이 표가 아닌 것

- 사용자 화면 확인(0-4, 5-4 기록).
- 다른 PC·서명·기관 접수(§2 결정).
- `run-ka-hgis.ps1 --smoke-quit` — 자동이나 CTest 등록은 아님.
