# Windows 빌드

상세 절차(다른 PC 포함): [`other-pc-setup.md`](./other-pc-setup.md)

## 요약

1. OSGeo4W 설치 — 루트 `C:\OSGeo4W` 권장  
   패키지: `qgis-dev`, `qt6-devel`, `gdal-dev-devel`, `sqlite3-devel`, `zlib-devel`, `pdal-dev`
2. VS 2022 Build Tools (MSVC) — QGIS/OSGeo4W와 **동일 major 툴체인** 권장
3. CMake는 `dev-env.lock.json`의 앞 두 자리
4. `.\scripts\dev-env.ps1` 후 cmake. SDK 경로는 그 스크립트가 찾은 값이다

```powershell
. .\scripts\dev-env.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DOSGEO4W_ROOT=$env:OSGEO4W_ROOT" -DKA_HGIS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

원클릭: `.\scripts\build-all.ps1`

환경변수(스크립트가 설정): `OSGEO4W_ROOT`, `QGIS_PREFIX_PATH`, `GDAL_DATA`, `PROJ_LIB`/`PROJ_DATA`, `QT_PLUGIN_PATH`, `QGIS_PLUGIN_PATH`

성공 시 실제 QGIS 버전은 `VERSION_QGIS_PIN.txt` 참고.
