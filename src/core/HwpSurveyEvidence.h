#pragma once
// HwpSurveyReportService 내부에서만 쓰는 서지·증거 문구 helper.
#include "core/HwpSurveyReportService.h"

#include <QString>

namespace HwpSurveyReportService::Detail {

QString detectRegionName(const SurveySnapshot& snapshot);
QString buildDefaultDiscussion(const SurveySnapshot& snapshot);
// GIS 서지(보고서명·기관·연도)를 한 줄로 만든다. 없으면 빈 값.
QString bibliographyLine(const SiteEvidenceRow& row);
// 표의 「원문 대조」 칸. 로컬 원문 상태와 GIS 서지를 함께 보인다.
QString reportSourceLabel(const SiteEvidenceRow& row);

}  // namespace HwpSurveyReportService::Detail
