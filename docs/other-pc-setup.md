# 다른 PC에서 바로 개발하기 (Windows)

원격: **https://github.com/kesekikwon-netizen/gogohak1** · 브랜치: 지금 작업 중인 브랜치(예: `20260922-1`). `main`은 뒤처져 있을 수 있다.  
DLL 미포함 → 대상 PC에 **OSGeo4W `qgis-dev`** 필요.

---

## 같은 개발 환경 만들기 (PC가 달라도 같은 판)

`qgis-dev`는 매일 새로 빌드된다. `install-deps.ps1`로 설치하면 설치한 날의 판이 들어오므로 PC마다 QGIS·Qt·GDAL 판이 달라지고, 같은 코드도 다르게 동작하거나 검사 결과가 달라진다. 지난 판은 다시 받을 수 없다. 그래서 기준 PC 하나의 판을 잠그고, 다른 PC는 그 폴더를 복사해 맞춘다.

| 무엇 | 어떻게 맞추나 |
|------|------|
| OSGeo4W (qgis-dev, Qt, GDAL, PROJ …) | 기준 PC 폴더를 `osgeo4w-bundle.ps1`로 복사. 패키지 판까지 비교 |
| MSVC 도구 모음 · Windows SDK · CMake | 같은 판을 설치. MSVC·CMake는 `dev-env.lock.json`의 앞 두 자리, SDK는 전체 판 |
| Git · Node · Python · clangd | 경고만 |
| Cursor | 모델 Grok 4.7 |
| 계정·API 키 | PC마다 앱에서 입력(DPAPI). git 에는 절대 넣지 않는다. 배포 번들 기본값도 미포함(아래 개인용 포터블 예외만 다름) |
| 조사 GPKG·SHP | git에 없음. OneDrive/NAS 별도 |

**개인용 포터블 예외:** 사용자가 요청한 개인용 포터블(`make-portable.ps1 -IncludeLocalCredentials`)만 이 PC 의 VWorld 키와 계정 비밀번호를 폴더 `config` 에 **암호화 없이** 담는다(키는 그대로, 비밀번호는 앱과 같은 가림 형식 `password_portable=` 이며 암호화가 아니다). 다른 PC 에서 로그인 없이 쓰려는 사용자 결정이다. 기본값은 미포함이고, 담을 때 스크립트가 경고를 띄우며 `PORTABLE-MANIFEST.json` 의 `credentialsIncluded` 가 `true` 가 된다. 그 폴더는 본인 USB·PC 에만 두고 남에게 주거나 올리지 않는다. 자세한 것은 [portable-desktop.md](portable-desktop.md).

**기준 PC**는 `dev-env.lock.json`을 마지막으로 쓴 컴퓨터다. 폴더 위치는 PC마다 달라도 된다. OSGeo4W나 VS를 업데이트할 때마다 1~2를 다시 한다.

```powershell
.\scripts\dev-env-lock.ps1 -Write                  # 1. dev-env.lock.json 갱신 → 커밋·push
.\scripts\osgeo4w-bundle.ps1 -Export E:\ka-hgis-sdk # 2. 외장 디스크/NAS로 SDK 복사 (수 GB)
```

**다른 PC**

```powershell
git clone https://github.com/kesekikwon-netizen/gogohak1.git
cd hgis
git checkout 20260922-1                               # 지금 작업 브랜치
.\scripts\osgeo4w-bundle.ps1 -Import E:\ka-hgis-sdk  # 잠금의 OSGeo 경로. 없으면 -Root
.\scripts\dev-env-lock.ps1                            # [다름]이 없어야 한다
.\scripts\bootstrap-dev-pc.ps1                        # 같은 비교 후 빌드·ctest·smoke
```

- `-Import`는 잠금에 적힌 OSGeo 경로로 복사한다. 그 드라이브가 없으면 `-Root`로 이 PC의 폴더를 준다. 가져올 자리에 폴더가 있으면 지우지 않고 `<폴더>.before-<시각>`으로 이름만 바꿔 둔다.
- 가져온 폴더를 `dev-env.ps1`이 못 찾으면 스크립트가 `OSGEO4W_ROOT` 설정 방법을 출력한다. 탐색 순서는 `OSGEO4W_ROOT` → `C:\OSGeo4W` → `D:\OSGeo4W` → `A:\OSGeo4W`다.
- `dev-env-lock.ps1` 종료 코드: 0 같음, 1 다름, 2 잠금 파일 없음. `bootstrap-dev-pc.ps1`은 1이면 빌드하지 않는다(`-AllowEnvDrift`로 무시).
- CI의 Windows 빌드(기준 PC runner)도 같은 비교를 한다. 기준 PC를 업데이트하고 잠금을 갱신하지 않으면 CI가 실패한다.

---

## 30초 요약 (기준 PC를 처음 만들 때)

```powershell
git clone https://github.com/kesekikwon-netizen/gogohak1.git
cd hgis
# 최초 1회만 — 관리자 PowerShell 권장 (CMake/VS/OSGeo4W 설치 시도)
# 다른 PC는 설치 대신 위 「같은 개발 환경 만들기」의 -Import 를 쓴다.
# .\scripts\install-deps.ps1

.\scripts\bootstrap-dev-pc.ps1
.\scripts\run-ka-hgis.ps1
```

성공 시: `build\Release\ka-hgis.exe` + ctest + smoke 통과.

---

## A) 개발 환경 요구

| 항목 | 권장 |
|------|------|
| OS | Windows 10/11 x64 |
| Git | 설치 |
| CMake | `dev-env.lock.json`의 앞 두 자리 (`C:\Program Files\CMake\bin`, `C:\CMake\bin` 또는 PATH) |
| 컴파일러 | **VS 2022** (MSVC C++ 워크로드 / Build Tools). 도구 모음 앞 두 자리는 잠금 파일 |
| GIS SDK | **OSGeo4W**. `dev-env.ps1`이 `OSGEO4W_ROOT` → `C:\OSGeo4W` → `D:\OSGeo4W` → `A:\OSGeo4W` |
| C++ 분석 | 설치된 clangd + `.clangd` + 실제 CMake 컴파일 DB |

OSGeo4W 패키지:

- `qgis-dev`
- `qt6-devel`
- `gdal-dev-devel`
- `sqlite3-devel`
- `zlib-devel`
- `pdal-dev`

판 기준: 저장소 `dev-env.lock.json`(패키지별 정확한 판). `VERSION_QGIS_PIN.txt`는 사람이 읽는 메모다.

### 의존성 자동 설치 (관리자 PowerShell)

설치한 날의 최신 `qgis-dev`가 들어온다. 기준 PC를 처음 만들거나 일부러 판을 올릴 때만 쓰고, 그 뒤에는 `dev-env-lock.ps1 -Write`로 잠금을 갱신한다.

```powershell
cd <클론>\hgis
.\scripts\install-deps.ps1
```

수동 OSGeo4W: https://download.osgeo.org/osgeo4w/v2/osgeo4w-setup.exe → 루트 `C:\OSGeo4W`.

---

## B) 클론 → 빌드 → 실행

### 원클릭

```powershell
git clone https://github.com/kesekikwon-netizen/gogohak1.git
cd hgis
git checkout 20260922-1   # 지금 작업 브랜치
git pull
.\scripts\bootstrap-dev-pc.ps1
.\scripts\run-ka-hgis.ps1
```

OSGeo가 다른 경로면:

```powershell
.\scripts\bootstrap-dev-pc.ps1 -OsgeoRoot "D:\OSGeo4W"
```

### 수동 (bootstrap 없이)

```powershell
. .\scripts\dev-env.ps1
cmake --preset vs
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\scripts\run-ka-hgis.ps1 --smoke-quit
.\scripts\run-ka-hgis.ps1
```

또는 `.\scripts\build-all.ps1` (build + test + smoke + e2e). 포터블 생성은 별도 요청 시에만 실행한다.

### 이 PC 경로와 clangd

스크립트는 클론한 폴더에서 실행한다. `dev-env.ps1`이 SDK와 CMake를 찾는다.

```powershell
.\scripts\build-now.ps1
```

일반 앱 빌드는 VS 2022 x64의 `build\Release`를 사용한다. 빌드가 CMake 빌드 정보로 `build\compile_commands.json`을 만들고(`scripts/compile-commands.mjs`), 빌드에 쓰는 MSVC를 `/vctoolsdir`로 적어 두므로 일반 편집기의 clangd도 같은 표준 헤더를 읽는다. 구성이 바뀌면 다음 빌드가 다시 쓴다. 절대 경로가 들어가는 생성 DB는 Git에서 제외한다. `compile_flags.txt`는 최소 C++ fallback이며 전체 GIS 분석에는 생성 DB가 필요하다.

---

## C) 일상 동기화 (두 PC)

```powershell
# 시작 — 지금 작업 브랜치에서
git pull
.\scripts\dev-env-lock.ps1   # 기준 PC가 판을 올렸으면 여기서 [다름]이 보인다
.\scripts\build-all.ps1      # 또는 cmake --build build --config Release

# 끝 (커밋 후)
git push
```

다른 PC에서 push 한 뒤 원래 PC로 돌아오면 작업 전에 `git pull`부터 한다. 먼저 커밋했다면 push가 거절되므로 `git pull` 후 다시 push 한다.

- 조사 파일 `*.gpkg` / 필드 SHP는 **git에 없음** → OneDrive/NAS 별도.
- VWorld 키: **더보기 → API 키 입력** (PC 로컬, 커밋 금지).

---

## D) 에이전트 / 제품 규칙 (개발 시 필수)

| 파일 | 내용 |
|------|------|
| `AGENTS.md` | 에이전트 라우팅 + **QGIS 매뉴얼 연동 규칙** + 불변식 |
| `HANDOFF.md` | 제품 SSOT 요약 (`.codex/NOW.md`와 현재 코드 우선) |
| `docs/vendor/qgis-manual-3.44/` | PyQGIS Cookbook PDF (git) + User Guide 다운로드 스크립트 |
| `docs/domain/data-model.md` | 도메인 레이어/필드 |
| `docs/COMMIT_STATUS.md` | 최근 커밋 장부 |

대용량 Desktop User Guide PDF:

```powershell
.\scripts\download-qgis-manuals.ps1
```

### 현재 제품 동작 (헷갈리지 말 것)

1. **새 조사** → 저장소(GPKG 스키마)만 준비, **범례는 비어 있음** (QGIS: 레이어는 add 할 때만).
2. **그리기 → 면/선/구역/GPS** → 그때 레이어가 생기고 디지타이즈.
3. 좌클릭=점, **우클릭/더블클릭/Enter=완료**(도구 유지), **편집저장**=커밋.
4. 그리기 중 **속성 팝업 없음** (나중에 편집).
5. **도구 → 조판 편집 창** → 별도 창에서 QgsLayoutView 편집 + PDF.
6. 제출: 검수 error 있으면 SHP 패키지 차단 · 업로드 CRS **EPSG:5179**.

---

## E) 실행만 (포터블)

실행만: 개발 PC에서 `.\scripts\make-portable.ps1 -OutDir '<새 출력 폴더>'` 후 해당 폴더를 **통째** 복사 → `ka-hgis.exe` 또는 `start.bat`. 대상 PC에 OSGeo4W 불필요. 기존 폴더는 덮어쓰거나 삭제하지 않으므로 매번 새 출력 경로를 사용한다.

사용자가 개인 계정까지 포함하도록 요청한 경우에만 `-IncludeLocalCredentials`를 추가한다. 현재 앱의 VWorld 키와 수치지형도·국가유산 인트라넷 계정을 포함하며 값은 로그에 출력하지 않는다. `scripts/verify-portable-pack.ps1 -OutDir '<출력 폴더>'`로 주요 런타임 파일을 확인한다. 실제 실행·좌표계·웹 엔진 검증은 이 파일 검사와 별도로 수행한다.

---

## F) 자주 막히는 것

| 증상 | 조치 |
|------|------|
| `OSGEO4W_ROOT not found` | 기준 PC 번들 `-Import` 또는 `$env:OSGEO4W_ROOT` |
| `dev-env-lock.ps1`에 `[다름] OSGeo4W 패키지` | 설치로는 같은 판을 못 받는다. 기준 PC에서 `-Export` → 이 PC에서 `-Import` |
| `[다름] MSVC 도구 모음` / `Windows SDK` | Visual Studio Installer에서 기준 PC와 같은 판으로 수정 |
| `qgis-dev missing` | OSGeo4W에서 `qgis-dev` |
| DLL 없음 / 즉시 종료 | `dev-env.ps1` 후 실행; `pdal-dev\bin` PATH |
| CMake 없음 | `winget install Kitware.CMake` |
| 시작 때 「모듈(wms)이 없습니다」, 위성·지적 배경 안 나옴 | OSGeo4W 경로에 한글이 있다(예: `C:\Users\<한글 이름>\OSGeo4W`). `C:\OSGeo4W`를 그 폴더로 연결(`New-Item -ItemType Junction -Path C:\OSGeo4W -Target <원래 폴더>`)하고 사용자 환경변수 `OSGEO4W_ROOT=C:\OSGeo4W`, 그 뒤 `cmake --preset vs` (docs/ERROR_REGRESSION.md) |
| VS 제너레이터 실패 | VS 2022 Build Tools + C++ 워크로드 |
| VWorld 배경 안 됨 | 더보기 → API 키 입력 (로컬) |
| 조판 창 안 뜸 | Release 빌드 후 `도구 → 조판 편집 창` |

---

## G) 검증 체크리스트 (다른 PC 첫날)

- [ ] `git log -1 --oneline` 가 GitHub 작업 브랜치 tip과 같음
- [ ] `.\scripts\dev-env-lock.ps1` 결과가 「기준 PC와 같다」
- [ ] `.\scripts\bootstrap-dev-pc.ps1` 성공
- [ ] `ctest` 100%
- [ ] 앱 기동 → 새 조사 → 레이어 목록 비어 있음
- [ ] 그리기 → 면 → 우클릭 완료 → 도형 표시
- [ ] 도구 → 조판 편집 창 열림
