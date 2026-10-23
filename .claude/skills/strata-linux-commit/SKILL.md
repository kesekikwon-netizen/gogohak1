---
name: strata-linux-commit
description: Build, test and commit Strata (ka-hgis) changes on Linux and push to gogohak1 main. Use whenever you change src/, tests/, CMakeLists.txt or data/ on this Linux machine, hit a commit-gate block, or need to push without wasting CI runs. Covers the paren-free clone, the preset, the gate's checks, git identity, and the CI queue behavior.
---

# 리눅스에서 고치고 검증하고 올리기

사용자는 현장 전문가다. 보고는 한국어로, 단계 이름(시험 먼저, 끝나기 전 확인)을 쓴다.

## 어디서 일하나
- 작업 폴더 `/home/kwon/hgis (사본)` 는 이름에 괄호가 있어 리눅스 자동 RCC가 깨진다. **빌드와 커밋은 `/home/kwon/gogohak1-linux`** (괄호 없는 clone)에서 한다.
- 파일을 사본 폴더에서 고쳤다면 같은 경로로 clone에 복사하거나 clone에서 바로 고친다. 마지막에 사본 폴더는 `git pull --ff-only origin main`으로 맞춘다. 사본 쪽에 남은 같은 수정은 `git checkout -- <경로>`로 버린다.
- 다른 터미널이 수집 중이다. `scripts/gyeongju_rag/`, `/mnt/data-drawing/gyeongju_rag/`, `/home/kwon/hgis`(정본)의 수집 파일은 건드리지 않는다.

## 빌드와 시험 (clone에서)
1. 처음이면 `cmake --preset linux` (이미 `build-linux/` 있음).
2. 빌드: `cmake --build build-linux --parallel --target ka-hgis <시험대상>` — 게이트가 `ka-hgis`가 바뀐 파일보다 새것이길 요구하므로 `ka-hgis`를 함께 빌드한다.
3. 시험: `ctest --test-dir build-linux -R '^(이름1|이름2)$' --output-on-failure`. 검색어 `rag`는 `storage_safety`에도 걸리니 `^…$`로 고정한다. 평범한 `ctest` 전체는 돌리지 않는다.
4. 스모크: `QT_QPA_PLATFORM=offscreen timeout 60 build-linux/ka-hgis --smoke-quit` → 종료 0.
5. 시험은 실제 보고서 DB(`ragDbPath`)를 읽지 않게 쓴다. `tests/test_local_report_rag.cpp`는 CMake에 등록되어 있지 않고 실제 DB를 조회하다 멈춘다. 새 시험은 따로 파일을 만든다(예: `tests/test_report_region.cpp`).
6. `storage_safety`는 이 PC에서 단독 실행해도 간헐적으로 실패한다(4번 중 3번). 내 변경과 상관이 없으면 보고에 한 줄 적는다.

## 커밋 전 검사(commit-gate)가 막는 것
- `src/`, `tests/`, `cmake/`, `data/`, `CMakeLists.txt`를 건드린 커밋: 빌드가 바뀐 파일보다 새로워야 하고 그 뒤에 ctest가 돌았어야 한다. 파일을 고친 뒤에는 **다시 빌드하고 다시 ctest** 한다.
- C++ 파일이 300줄을 넘으면 막는다(기준 파일에 있는 큰 파일은 그 줄 수까지). 기준선을 올리려면 `길이 기준 변경:` 줄이 필요하다. 주석 한 줄도 300줄 파일에서는 넘는다. 줄이거나 합친다.
- 원래 있던 시험의 기댓값·검사문을 바꾸거나 지우거나 건너뛰게 하면 커밋 메시지에 `시험 변경: <이유>` 줄을 쓰고 보고 맨 위에 「바꾼 시험」으로 알린다.
- **게이트는 Bash 명령 전체를 실행 전에 본다.** 같은 명령 안에서 파일을 고치고 커밋하면 고치기 전 상태로 판정된다. 고치는 호출과 커밋 호출을 나눈다.
- 문서(`.md`)·`.claude/`·`scripts/`만 바꾼 커밋은 빌드·시험을 요구하지 않는다.

## 커밋과 올리기
- 메시지: `type(scope): 한국어 요약`, 한국어 본문, `시험:` 줄, 마지막에 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- clone에 작성자 정보가 없으면(`Author identity unknown`) 사본 폴더의 `git config user.name/user.email` 값을 clone에만 설정한다. `--global`은 쓰지 않는다.
- 올리기 전 `git pull --rebase origin main` (다른 터미널도 main에 올린다). 되돌릴 수 없는 `--force`는 쓰지 않는다. 올린 뒤 `git -C "/home/kwon/hgis (사본)" pull --ff-only origin main`.

## CI는 이렇게 돈다
- `origin`은 `kesekikwon-netizen/gogohak1`. Windows 실행이 약 11~25분 걸리고 한 번에 하나만 돈다.
- 같은 그룹에서 **대기 중인 실행은 더 새 커밋이 오면 취소**되고, 이미 도는 실행은 계속된다. 그래서 푸시가 겹치면 중간 커밋의 실행은 `cancelled`로 보이는 것이 정상이다. 마지막 커밋의 실행이 그 전 변경을 모두 포함한다.
- 결과 확인: `gh run list --repo kesekikwon-netizen/gogohak1 --limit 4`, 실패 이유: `gh run view <id> --repo … --log-failed | grep -E "error|FAIL"`. 기다릴 때는 백그라운드로 `until [ "$(gh run view <id> --json status -q .status)" = completed ]; do sleep 30; done`.
- Windows에서만 나는 오류가 있다. 리눅스 통과가 Windows 통과는 아니다. 마무리는 CI 초록으로 확인한다. 2026-10-23에 만난 것:
  - `HWND` 중복 정의(C2371): `SdkDef.h`가 Windows에서는 `<windows.h>`를 쓴다.
  - `std::max`/`std::min` 깨짐(C2589): `windows.h` 앞에 `NOMINMAX`.
  - `hwpsdk.lib` 못 찾음(LNK1104): `HwpSDKExport.h`가 MSVC에서 DLL 가져오기를 요구한다. SDK 소스를 `ka_core`에 직접 넣으므로 `HWPSDK_STATIC`을 준다.
  - 한 번의 Windows 실행은 대기까지 합쳐 25~50분 걸린다. 오류는 한 번에 한 층씩 드러나므로, 실패 로그의 오류 종류(`error C`, `LNK`)를 처음부터 끝까지 보고 같은 종류를 한꺼번에 고친다.
