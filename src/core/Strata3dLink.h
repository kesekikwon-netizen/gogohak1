#pragma once

#include <QJsonObject>
#include <QString>

// Strata 와 별도 앱 strata-3d(정밀 3D·고고학 평가)를 잇는 로컬 파이프 연동. 창 코드 없음.
// 프로토콜 v1: 한 줄 JSON 요청·응답(strata-3d 저장소 docs/bridge-protocol.md). 조사 파일은 strata-3d 가 읽기 전용으로만 연다.
namespace Strata3dLink {

struct Outcome {
  bool ok = false;
  QString error;               // 실패 사유(한국어, 그대로 보여 줄 수 있다)
  QString surveyMessage;       // 조사를 연 결과 한 줄
  QString evaluationSummary;   // 조사구역 500 m·1000 m 평가 요약(계산값만)
  QString crs;                 // 평가 좌표계
  QString png, pdf;            // 받은 도면 경로
};

QString pipeName();  // 환경변수 STRATA_3D_PIPE 가 있으면 그것, 없으면 strata-3d-bridge-v1
// 프로젝트 파일 이름("geopackage:…", .gpkg, 같은 이름 .gpkg 가 옆에 있는 .qgz)에서 조사 파일을 찾는다. 없으면 빈 문자열.
QString surveyGpkgFromProjectFile(const QString& projectFileName);
// STRATA_3D_EXE → 설치 옆 strata-3d\ → 문서 폴더의 hgis-3d-engine 빌드. 못 찾으면 빈 문자열.
QString findExecutable();
// 블로킹 한 번 호출(워커 스레드에서). 연결·응답 시간이 지나면 빈 객체와 사유.
QJsonObject request(const QString& pipe, const QJsonObject& req, int timeoutMs, QString* errorOut = nullptr);
// 파이프가 응답하면 true. 아니면 exe 를 따로 띄우고(--no-initial-scene) 응답할 때까지 기다린다.
bool ensureRunning(const QString& pipe, const QString& exe, QString* errorOut);

// 조사 파일을 strata-3d 로 열고(실측 DEM·위성·건물·유적을 받아 올린다) 조사구역을 평가해 요약을 돌려받는다.
Outcome openAndEvaluate(const QString& pipe, const QString& exe, const QString& gpkg);
// 열려 있는 장면의 입체지형 도면(PNG, pdf 가 비어 있지 않으면 PDF 도)을 지정한 경로에 받는다.
Outcome fetchSheet(const QString& pipe, const QString& png, const QString& pdf = QString());

}  // namespace Strata3dLink
