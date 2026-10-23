#pragma once
// AI 없이 만드는 시굴 전 평가 뼈대(설계도 11절 M3). GIS 값과 보고서 근거만으로 요약·조사구역 개요·주변유적·
// 유적별 근거·부족한 자료·참고문헌을 쓴다. 판단(연계성, 시굴 참고)은 SDK 몫이라 여기서 쓰지 않는다.
#include "AssessmentContract.h"

#include <QJsonArray>
#include <QJsonObject>

namespace AssessmentSkeleton {

struct Result {
  QJsonObject facts;       // 사실 묶음
  QJsonArray evidence;     // 근거 항목(E1…)
  QJsonObject assessment;  // 평가 결과(AssessmentContract::validateAssessment 를 통과해야 한다)
};

Result build(const GisFactHarness::Ledger& ledger, const HwpSurveyReportService::SurveySnapshot& snapshot);

}  // namespace AssessmentSkeleton
