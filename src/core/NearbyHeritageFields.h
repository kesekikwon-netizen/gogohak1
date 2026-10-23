#pragma once
// 주변유적 참조 레이어의 서지 필드(유산코드·보고서명·사업명·소재지·조사기관 등)를 찾는다.
// 자료 종류마다 컬럼 이름이 다르므로 한 곳에서만 정한다.
#include "core/HeritageStyle.h"

#include <QString>

class QgsFeature;
class QgsVectorLayer;

namespace NearbyHeritageExport {

struct Fields {
  QString label;        // 목록에 보일 이름(발굴은 사업명)
  QString code;         // 유산코드
  QString project;      // 사업명
  QString report;       // 보고서명
  QString address;      // 소재지
  QString institution;  // 조사기관
  QString era;          // 시대
  QString presence;     // 유적유무
  QString permit;       // 허가번호(자료에 있을 때만)
  QString feature;      // 유적명(발굴의 A지구 같은 세부 이름)
};

Fields resolveFields(const QgsVectorLayer* layer, HeritageDataset dataset);
QString attributeText(const QgsFeature& feature, const QString& fieldName);

}  // namespace NearbyHeritageExport
