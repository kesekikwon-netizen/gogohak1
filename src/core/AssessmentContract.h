#pragma once
// 시굴 전 종합 평가 계약(설계도 6절). 앱이 SDK에 주는 사실 묶음과, SDK에서 받는 근거 항목·평가 결과를 검사한다.
// 지도·레이어·파일을 바꾸지 않는다. AI 와 통신하지 않는다.
#include "GisFactHarness.h"
#include "HwpSurveyReportService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace AssessmentContract {

// 사실 묶음(앱 → SDK): GIS 실측 원장 + 조사 정보 + 주변유적. 앱이 계산한 값만 담는다.
QJsonObject factsBundle(const GisFactHarness::Ledger& ledger, const HwpSurveyReportService::SurveySnapshot& snapshot);

// 근거 항목 한 개를 검사한다. 문제 설명(한국어) 목록을 돌려주고, 비어 있으면 통과.
QStringList validateEvidenceItem(const QJsonObject& item);

// 평가 결과(SDK → 앱)를 검사한다. 모든 사실·추정 문장은 존재하는 근거 번호를 가져야 하고,
// 잠긴(미수집) 도메인의 수치와 개발 용어, 표시 없는 그림은 막는다. 비어 있으면 통과.
QStringList validateAssessment(const QJsonObject& result, const QJsonArray& evidence, const QStringList& uncollectedDomains);

// 모식도에 반드시 붙이는 표시.
QString figureLabel();

}  // namespace AssessmentContract
