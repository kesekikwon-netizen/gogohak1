# Strata 작업 참고 (필요할 때만 읽는다)

CLAUDE.md에서 옮긴 내용이다(2026-10-03, docs/intent/2026-10-03-dev-setup-gaps.md). CLAUDE.md가 이 문서의 절을 가리킬 때 읽는다.

## 리눅스 첫 빌드 (평소 개발, 2026-10-23)

저장소 루트(경로에 괄호가 없어야 한다)에서: `cmake --preset linux && cmake --build build-linux --parallel 32 --target ka-hgis`. 시험은 `ctest --preset linux -R "^(이름)$"`. 어떤 시험이 리눅스에서 도는지는 docs/testing-map.md의 「리눅스에서 돌리기」. 바탕화면 바로가기와 smoke는 Windows 마무리 단계에서만 한다.

## 바탕화면 바로가기 (앱 빌드가 성공할 때마다, 사용자의 상시 요청)

Run the smoke, then create or repoint `%USERPROFILE%\Desktop\Strata (개발).lnk` (this PC: `C:\Users\권을\Desktop`; old PC: `C:\Users\kwonyoungin1\Desktop`) with WScript.Shell (`CreateShortcut` + `Save` makes it when missing):
- TargetPath `<worktree>\scripts\start-ka-hgis.vbs`, WorkingDirectory `<worktree>`, IconLocation `<worktree>\build\Release\ka-hgis.exe,0`, Description `Strata 개발 빌드 (워크트리 <name>)`.
- Then call `SHChangeNotify(0x08000000)` so the icon refreshes, and tell the user which worktree it now opens.
- 「고고학 전용 HGIS.lnk」 ran `A:\qgis\build` (exe from 2026-09-28): touch or recreate it only when asked. On the `D:\hgis` PC the user asked (2026-10-11) for it to open `D:\hgis\build\Release`; there the desktop is `OneDrive\바탕 화면` (`[Environment]::GetFolderPath('Desktop')`).
- Explorer keeps showing an old icon cached for the same exe path after a rebuild. Pointing IconLocation at `data\theme\ka-hgis.ico,0` (the file the exe embeds) plus `SHChangeNotify` and `ie4uinit.exe -show` makes the new icon show.
- If the app is open, rename the running exe before rebuilding; close it only if its title has no " *".

## Code map

- `src/app` (UI): MainWindow is split over 16 files (`MainWindow.cpp` + `MainWindow{Align,Cadastral,ContextMenus,Downloads,Editing,Erase,Export,Files,Offline,Overlay,Ribbon,Session,SurveyContour,Topographic,Undo}.cpp`), `KaApplication` (startup, `--smoke-quit`, rendering settings), Ka* widgets and map tools, `KaDrawingStudio` (도면). (단면도 화면은 2026-10-10에 삭제.)
- `src/core` (static lib `ka_core`): LayerOps, BasemapOps, SurveyStorage/SurveySession/SurveyProjectFactory, ExportService, LayoutService, ChecklistEngine (+ data/rules/drawing_checklist.v1.json), ProjectStateBuilder, Heritage*, Topographic*, Cadastral*, PolygonErase, FeaturePick.

## Test baseline history

- Baseline at a6eb712 (main 9f3992c, 71 tests) was 67/71 with 4 known failures: workflow_engine `shapeEditing_livesInsideSelectTool`, cadastral `referenceLayerHasOutlineAndOptionalJibunLabels`, storage_safety `persistWorkspace_writeExceptionKeepsPreviousGeneration`, save_open_portable `oldVersionSurveysStillOpen`. On 2026-10-03 all four passed locally (ctest 4/4) and in CI (146/146), so CLAUDE.md and the commit gate stopped excusing them.
- The 10-01 merge line (`claude/strata-merge-20261001`, de0414f, 124 tests) was measured 117-118/120 at -j4 on 09-30 (closeSave flake, memory `strata-visual-redesign.md`). It was fast-forwarded into main on 2026-10-03. Replace this with a fresh measurement when one is taken.

## Worktrees

- The desktop app runs each session in `<main>\.claude\worktrees\<name>` (`<main>`: CLAUDE.md 「Git」) on a `claude/<session>` branch. Once the desktop reuses a folder, the folder and branch names differ, so read `git branch --show-current` instead of assuming.
- New desktop sessions start from origin/main.

## Stale docs

docs/HANDOFF.md, docs/README.md and TEST_INFRA.md still mention D:\hgis, D:\OSGeo4W, C:\CMake, AGENTS.md, .codex/NOW.md, publish-desktop after every build and a Codex section. Real paths: repo `<main>` (CLAUDE.md 「Git」; old PC A:\qgis), OSGeo4W C:\OSGeo4W on this PC (old PC A:\OSGeo4W) (QGIS 4.3 master, Qt 6.11), CMake C:\Program Files\CMake\bin, VS 2022 BuildTools. CLAUDE.md wins over those docs. docs/COMMIT_STATUS.md is stale; leave it.

## Why some rules exist

- 「코드 고치는 방식」 came from general rules the user pasted on 2026-10-01; 「모르면 멈추고 묻기」 was left out because it conflicts with rule 2.
- The security review context is `.claude/claude-security-guidance.md`. It is read by the end-of-turn diff review only; the commit reviewer ignores it, and on this subscription login neither LLM review runs, so expect only the regex edit warnings.
- `scripts/run-ka-hgis.ps1 --smoke-quit` is not a smoke test: ka-hgis.exe is a GUI exe, PowerShell does not wait for it, and the script returns 0 even if the app never ran.
- Files in docs/superpowers dated 2026-09-13 or earlier are history, not current requirements.
- Removed from CLAUDE.md on 2026-10-03 because it was out of date: "fast-forward main only on 「커밋」" and the note that `claude/strata-merge-20261001` (worktree happy-davinci-a9ee1e) was not on main. That line was merged into main and pushed on 2026-10-03, and since 2026-10-03 every verified commit is pushed.
