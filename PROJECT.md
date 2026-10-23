# Project: ka-hgis (Korean Field Archaeology Desktop HGIS)

> 현재 상태의 짧은 요약이다. 제품 규칙은 `CLAUDE.md`, 제품 현황 정본은 `docs/HANDOFF.md`. 이 파일은 2026-09-29 에 코드와 맞췄다(F032).

## Architecture
- Architecture B: Standalone C++20/Qt6 desktop application linked to OSGeo4W `qgis_core` and `qgis_gui`. No QGIS fork; do not reimplement PROJ/GDAL/renderer.
- Domain Layers: `survey_area`, `feature_poly`, `feature_line`, `section_line`, `control_points`, `artifact_point`, `trial_trench`. Logic property `ka_hgis/layer_key`. Schema: `data/schemas/ka_hgis_layers.yaml` (checked against `SurveyProjectFactory` by `workflow_engine`).
- Legend Groups: `조사 데이터` (Domain data) vs `참조 지도` (Basemap & references). Downloaded cadastral and VWorld cadastral pictures sit at the layer-tree root.
- CRS Policy: Working CRS EPSG:5186 / EPSG:5187 (new survey default 5187); Submission package export CRS strictly EPSG:5179.
- UI: one-row ribbon (조사 · 기록 · 자료 받기 · 배경 지도 · 정합 · 내보내기 · 기타). `MainWindow` is one class split over `MainWindow*.cpp` files; there are no controller classes.

## Feature Inventory (what the code has)
| # | Feature | Where |
|---|---------|-------|
| 1 | Layout composition validation (`layout_blank`/uncomposed sheets fail) | `LayoutService::isComposedStudioSheet` |
| 2 | Checklist rules and project state (error blocks submit) | `data/rules/drawing_checklist.v1.json`, `ChecklistEngine`, `ProjectStateBuilder`, `ChecklistState*` |
| 3 | Submission package: `user_sheet` → `조사도면.pdf`, 5179 SHP, README, encoding | `ExportService::exportSubmissionPackage` |
| 4 | Streamed SHA-256 `MANIFEST.sha256` | `ExportService::writeSha256Manifest` |
| 7–9 | VWorld key handling and refresh in saved layers | `LayerOps::withVworldApiKey`, `LayerOps::refreshVworldApiKeyInLayers` (in `BasemapOps.cpp`), `MainWindow::configureVworldKey` |
| 11 | Mercator satellite quad regression test | `TestWorkflow::zoomToKorea_5186StaysInsideMercatorSatelliteQuad` |
| — | Old-version survey compatibility | `tests/data/compat`, `save_open_portable` |

## Planned but not done (kept as a record)
These came from the 2026-08 survey rounds (R1–R3) and are **not** in the code. Do not assume them.
- Geometry auto-repair pipeline (`sanitizeAndRepairGeometry`) and the digitizing guard built on it (items 5–6).
- Sub-controllers `ProjectLifecycleController`, `DigitizingStateController`, `LayerStateController`; `src/app/controllers/` does not exist (item 10).
- Asynchronous submission export: `exportSubmissionPackage` runs on the calling thread (item 12).
- Detaching `applyCanvasScreenDpi` and `m_tileHealCount.clear()` from `extentsChanged` (items 13–14); tile cache fixed at 256 (item 15).
- The M4 target "14 suites, 0 /W4 warnings, forensic audit" (items 16–17); the suite count is no longer 14.

## Interface Contracts
### `LayoutService` ↔ `ProjectStateBuilder` ↔ `ExportService`
- `bool LayoutService::isComposedStudioSheet(QgsProject* project, const QString& layoutName = QStringLiteral("user_sheet"))`: true only if a map frame exists with positive scale, finite extent and non-empty layers (excluding `layout_blank` and reference-only layers).
- `QString ExportService::exportSubmissionPackage(QgsProject* project, const QString& outDir, const QString& encoding, const QString& checklistSummary, bool blockOnError, bool hasChecklistErrors, QString* errorOut = nullptr, const SubmitPackageInfo& info = {})`: `outDir` must be absent or empty. With `blockOnError && hasChecklistErrors` nothing is written. Writes `조사도면.pdf` from the composed `user_sheet`, EPSG:5179 SHPs, `README_submit.txt`, `encoding.txt` and a streamed `MANIFEST.sha256` into a staging folder and publishes it only when complete. Returns `outDir` on success, an empty string on failure (a previous package is never overwritten). `SubmitPackageInfo` carries the survey name/path for the README and an optional cancelable progress callback.

### `LayerOps` ↔ `MainWindow`
- `int LayerOps::refreshVworldApiKeyInLayers(QgsProject* project, const QString& currentKey, QStringList* changed)`: updates WMTS/WMS URLs and cadastral GDAL XML files and reloads providers. Declared in `LayerOps.h`, implemented in `BasemapOps.cpp`.
- `LayerOps::ensureDomainLayer` is the only path that adds domain layers to the project/legend.

## Code Layout
- `src/core/ExportService.*`: submission package, shapefile reprojection, SHA-256 manifest.
- `src/core/LayoutService.*`: layout templates, studio sheet validation, PDF export.
- `src/core/ChecklistEngine.*`, `ChecklistState*.cpp`, `ProjectStateBuilder.*`: rule evaluation against project state.
- `src/core/SurveyProjectFactory.*`, `SurveyStorage.*`, `SurveyBundle.*`: survey creation, embedded workspace save/open, moving surveys between PCs.
- `src/core/LayerOps.*`, `BasemapOps.cpp`: layer keys, legend groups, basemaps, VWorld keys.
- `src/app/MainWindow.h` and `MainWindow*.cpp` (Ribbon, Session, Editing, Export, Cadastral, Downloads, Topographic, Undo, ContextMenus, …): one `MainWindow` class.
- `src/app/KaCaptureMapTool.*`: drawing new shapes; saved shapes are edited with `KaFeatureSelectTool`.
- `tests/`: QtTest suites registered with `ka_add_qtest` in `CMakeLists.txt` (see `TEST_INFRA.md`).
