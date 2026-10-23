#pragma once

#include <QString>
#include <QVector>

class QgsProject;
class QgsVectorLayer;

namespace NearbyHeritageExport {

struct Item {
  QString datasetName;      // 예: "매장유산유존지역", "지정유산"
  QString layerName;        // 예: "매장유산유존지역 · 제주시", "국가지정유산"
  QString siteName;         // 예: "광령리 유적"
  QString radiusLabel;      // "조사구역 내", "500m 이내", "1000m 이내"
  QString direction;        // "조사구역 내", "북", "북동", "동", "남동", "남", "남서", "서", "북서"
  int distanceMeters = 0;   // 조사구역 경계로부터 최단 거리(m)
  double azimuthDeg = -1.;  // 북 기준 시계방향 방위각(0~360), 조사구역 내(중첩)면 -1
  QString description;      // 예: "조사구역에서 북동쪽으로 약 125m", "조사구역 내 (0m)"
  // GIS 원자료의 공식 서지. 자료에 없는 항목은 빈 값으로 둔다(지어내지 않는다).
  QString heritageCode;     // 유산코드
  QString reportName;       // 보고서명(기관, 연도, 제목)
  QString projectName;      // 사업명
  QString siteAddress;      // 소재지
  QString institution;      // 조사기관
  QString era;              // 시대
  QString heritagePresence; // 유적유무
  QString permitNumber;     // 허가번호
  QString featureName;      // 유적명(발굴의 "A지구" 같은 세부 이름)
};

struct Result {
  QVector<Item> items;
  double maxRadiusMeters = 1000.0;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// 선택된 면 레이어가 없거나 참조/버퍼 레이어이면 조사구역(survey_area)을 돌려준다.
QgsVectorLayer* resolveSourceLayer(QgsProject* project, QgsVectorLayer* preferred = nullptr);

// 지도에 그려진 주변 경계(500m / 1000m)에 맞춘 최대 반경(m). 없으면 1000m.
double activeMaxRadiusMeters(const QgsProject* project);

// 북(0°) 기준 시계방향 방위각을 8방위("북"~"북서")로 바꾼다.
QString cardinalDirection8(double azimuthDegrees);

// 조사구역 버퍼(500m·1000m) 안에 포함되는 주변유적 도형의 유적명·방향·거리를 모은다.
Result collect(QgsProject* project, QgsVectorLayer* sourceLayer = nullptr,
               double maxRadiusMeters = 0.0);

// 모은 주변유적 목록을 엑셀(.xlsx) 파일로 저장한다.
bool writeXlsx(const QString& path, const QVector<Item>& items, QString* errorOut = nullptr);

// 수집과 엑셀(.xlsx) 저장을 한 번에 실행한다.
Result exportToXlsx(QgsProject* project, const QString& path,
                    QgsVectorLayer* sourceLayer = nullptr, double maxRadiusMeters = 0.0);

}  // namespace NearbyHeritageExport
