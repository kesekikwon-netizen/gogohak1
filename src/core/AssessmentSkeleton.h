#pragma once
// AI 없이 만드는 시굴 전 평가 뼈대(설계도 11절 M3). GIS 값과 보고서 근거만으로 요약·조사구역 개요·주변유적·
// 유적별 근거·부족한 자료·참고문헌을 쓴다. 판단(연계성, 시굴 참고)은 SDK 몫이라 여기서 쓰지 않는다.
#include "AssessmentContract.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace AssessmentSkeleton {

struct Result {
  QJsonObject facts;       // 사실 묶음
  QJsonArray evidence;     // 근거 항목(E1…)
  QJsonObject assessment;  // 평가 결과(AssessmentContract::validateAssessment 를 통과해야 한다)
};

Result build(const GisFactHarness::Ledger& ledger, const HwpSurveyReportService::SurveySnapshot& snapshot);

// 세 자료를 폴더에 facts.json, evidence.json, assessment.json 으로 쓴다(SDK 에 넘기는 파일). 폴더가 없으면 만들고,
// 같은 이름은 덮어쓴다. 쓰는 도중 실패해도 기존 파일은 남는다.
bool writeBundle(const QString& folder, const Result& result, QString* error = nullptr);

// 평가 결과를 읽기 쉬운 HTML 로 그린다. 문장마다 종류(사실·추정·자료 없음)와 근거 번호를 보인다.
// problems 가 있으면 맨 위에 「확인할 점」 칸을 둔다. 모든 글자는 HTML 로 이스케이프한다.
QString renderHtml(const Result& result, const QStringList& problems);

}  // namespace AssessmentSkeleton
