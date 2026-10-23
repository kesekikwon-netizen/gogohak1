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
// <표 1-1>(가까운 20건)과 부록 <표 1-2>(전체)의 행. maxRows<=0이면 전체 행을 만든다.
QVector<QStringList> buildSiteTableRows(const SurveySnapshot& snapshot, int maxRows = 0);
// 범례·상태 집계·미수록 보고서 검색 안내.
QString siteStatusSummary(const SurveySnapshot& snapshot);

}  // namespace HwpSurveyReportService::Detail
