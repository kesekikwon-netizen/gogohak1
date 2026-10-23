#include "HwpSurveyEvidence.h"
#include "LocalReportMatch.h"
#include "LocalReportRag.h"

#include <cmath>
#include <QFileInfo>
#include <QLocale>

namespace HwpSurveyReportService {
namespace {

QString localReportLabel(const SiteEvidenceRow& row) {
  const QString name = row.reportName.isEmpty() ? QStringLiteral("로컬 원문") : row.reportName;
  if (row.reportStatus == QStringLiteral("✓"))
    return QStringLiteral("✓ 『%1』 p.%2").arg(name).arg(row.pageNo > 0 ? row.pageNo : 1);
  if (row.reportStatus == QStringLiteral("◌"))
    return row.reportName.isEmpty() ? QStringLiteral("◌ 스캔본")
                                    : QStringLiteral("◌ 스캔본 『%1』").arg(row.reportName);
  if (row.hasLocalPdf) return QStringLiteral("○ 로컬 PDF 색인 전");
  return QStringLiteral("○ 원문 미확보");
}

}  // namespace

namespace Detail {

QString detectRegionName(const SurveySnapshot& snap) {
  const QString s = snap.surveyTitle + QLatin1Char(' ') +
                    (snap.nearbySites.isEmpty() ? QString() : snap.nearbySites.first().siteName);
  const QString q = QString::fromStdString(strata::core::LocalReportRag::extractRegionScope(s.toStdString(), "경주시"))
                        .section(QLatin1Char(','), 0, 0);
  return q == QStringLiteral("경주") ? QStringLiteral("경주시") : q;
}

QString buildDefaultDiscussion(const SurveySnapshot& snap) {
  const QString base =
      QStringLiteral("본 조사는 %1 일원(면적 %2㎡, 좌표계 %3)을 대상으로 수행되었다.")
          .arg(snap.surveyTitle, QLocale(QLocale::Korean).toString(std::round(snap.totalAreaSqm)), snap.surveyCrs);
  if (snap.nearbySites.isEmpty())
    return base + QStringLiteral(" 로드된 주변유적 레이어가 없으며 지역 보고서 RAG(%1 %2권)를 기준으로 검토한다.")
                      .arg(snap.regionName)
                      .arg(snap.regionTotalReports);
  const auto& n = snap.nearbySites.first();
  return base + QStringLiteral(
                    " GIS 최단거리 측정 결과, 반경 %1m 내 총 %2개소 유적이 분포하며 최단거리 유적은 "
                    "%3(경계 기준 %4 %5m)이다.")
                    .arg(qRound(snap.radiusMeters))
                    .arg(snap.nearbySites.size())
                    .arg(n.siteName, n.direction)
                    .arg(n.distanceMeters);
}

QString bibliographyLine(const SiteEvidenceRow& row) {
  if (row.bibliography.trimmed().isEmpty()) return {};
  return QStringLiteral("『%1』").arg(row.bibliography.trimmed());
}

QString reportSourceLabel(const SiteEvidenceRow& row) {
  const QString bib = bibliographyLine(row);
  const QString local = localReportLabel(row);
  if (row.reportStatus == QStringLiteral("✓") && !row.reportName.isEmpty())
    return bib.isEmpty() ? local : QStringLiteral("%1 · GIS %2").arg(local, bib);
  if (row.reportStatus == QStringLiteral("◌"))
    return bib.isEmpty() ? local : QStringLiteral("%1 · GIS %2").arg(local, bib);
  if (bib.isEmpty()) return QStringLiteral("○ 서지 없음");
  return QStringLiteral("○ 원문 미확보 · GIS %1").arg(bib);
}

}  // namespace Detail

void ReportDocumentSession::populateRagEvidence(SurveySnapshot* snap, bool queryRagDb) {
  if (!snap) return;
  if (snap->regionName.isEmpty()) snap->regionName = Detail::detectRegionName(*snap);
  if (queryRagDb) {
    const auto rs = strata::core::LocalReportRag::queryRegionStats(snap->regionName.toStdString());
    snap->regionTotalReports = rs.total_reports, snap->regionTextReports = rs.text_reports,
    snap->regionScanReports = rs.scan_reports;
  }
  snap->siteEntries.clear();
  for (const auto& s : snap->nearbySites) {
    SiteEvidenceRow row;
    row.datasetName = s.datasetName;
    row.siteName = s.siteName;
    row.distanceMeters = s.distanceMeters;
    row.direction = s.direction.isEmpty() ? QStringLiteral("인접") : s.direction;
    row.heritageCode = s.heritageCode;
    row.bibliography = s.reportName;
    row.projectName = s.projectName;
    row.siteAddress = s.siteAddress;
    row.eraField = s.era;
    row.heritagePresence = s.heritagePresence;
    row.permitNumber = s.permitNumber;
    row.institution = s.institution;
    row.reportYear = strata::core::LocalReportMatch::parseYear(s.reportName.toStdString());
    if (queryRagDb) {
      strata::core::LocalReportMatchQuery q;
      q.report_name = s.reportName.toStdString();
      q.institution = s.institution.toStdString();
      q.address = s.siteAddress.toStdString();
      q.site_name = s.siteName.toStdString();
      q.project_name = s.projectName.toStdString();
      q.region = snap->regionName.toStdString();
      q.year = row.reportYear;
      auto match = strata::core::LocalReportMatch::find(q);
      if (!match.matched && !match.indexed && !strata::core::LocalReportMatch::hasAddressNumber(q.site_name))
        match = strata::core::LocalReportMatch::localityDocument(q.site_name, q.region);
      if (match.matched || match.indexed) {
        const QString hint = QStringLiteral("%1 %2 %3").arg(s.siteName, s.projectName, s.featureName);
        const auto ev = strata::core::LocalReportRag::inspectMatchedReport(match, hint.toStdString(),
                                                                           snap->regionName.toStdString());
        row.hasLocalPdf = ev.has_local_pdf;
        row.indexed = ev.indexed;
        row.reportName = QString::fromStdString(ev.report_name);
        row.reportStatus = QString::fromStdString(ev.status);
        row.eraStatus = QString::fromStdString(ev.era_status);
        row.featureStatus = QString::fromStdString(ev.feature_status);
        row.strataStatus = QString::fromStdString(ev.strata_status);
        row.pageNo = ev.page_no;
        row.strataPageNo = ev.strata_page_no > 0 ? ev.strata_page_no : ev.page_no;
        row.eraSummary = QString::fromStdString(ev.era_summary);
        row.featureSummary = QString::fromStdString(ev.feature_summary);
        row.strataSummary = QString::fromStdString(ev.strata_summary);
        if (row.reportYear <= 0) row.reportYear = ev.report_year;
        if (row.institution.isEmpty()) row.institution = QString::fromStdString(ev.institution);
      }
    }
    if (row.reportStatus == QStringLiteral("○")) {
      row.eraStatus = row.featureStatus = row.strataStatus = QStringLiteral("○");
      row.eraSummary = QStringLiteral("근거 없음·확인 필요");
      row.featureSummary = QStringLiteral("원문 미확보");
      row.strataSummary = QStringLiteral("토층 미확보");
    } else if (row.reportStatus == QStringLiteral("◌")) {
      row.eraSummary = QStringLiteral("스캔본·수동 대조 필요");
      row.featureSummary = QStringLiteral("스캔본·수동 대조 필요");
      row.strataSummary = QStringLiteral("스캔본·수동 대조 필요");
    } else {
      if (row.eraSummary.isEmpty()) row.eraSummary = QStringLiteral("근거 없음·확인 필요");
      if (row.featureSummary.isEmpty()) row.featureSummary = QStringLiteral("원문 미기재·확인 필요");
      if (row.strataSummary.isEmpty()) row.strataSummary = QStringLiteral("토층 미기재·확인 필요");
    }
    const QString relationBase =
        QStringLiteral("조사구역 경계 기준 %1 %2m(GIS 최단거리) 이격.").arg(row.direction).arg(row.distanceMeters);
    const QString bib = Detail::bibliographyLine(row);
    if (row.reportStatus == QStringLiteral("✓"))
      row.relationToSurvey =
          relationBase + QStringLiteral(" 『%1』(p.%2) 확인 유구·문화층 동일 지형면 연장 가능성 검토 대상.")
                             .arg(row.reportName)
                             .arg(row.pageNo);
    else if (row.reportStatus == QStringLiteral("◌"))
      row.relationToSurvey =
          relationBase + QStringLiteral(" 스캔본(『%1』) 도면·토층 수동 대조 필요%2")
                             .arg(row.reportName, bib.isEmpty() ? QString() : QStringLiteral(" (GIS %1)").arg(bib));
    else if (!bib.isEmpty())
      row.relationToSurvey =
          relationBase +
          QStringLiteral(" 로컬 원문 미확보. GIS 공식 서지 %1를 기준으로 유구 연장 여부 검토(확인 필요).").arg(bib);
    else
      row.relationToSurvey = relationBase + QStringLiteral(" 로컬·GIS 서지 미확보로 유구 연장 단정 유보(확인 필요).");
    snap->siteEntries.push_back(std::move(row));
  }
  if (snap->siteEntries.isEmpty() && queryRagDb) {
    for (const auto& h : strata::core::LocalReportRag::search("주거지 수혈 건물지 고분 석곽묘 토기 기와",
                                                              snap->regionName.toStdString(), 10, 1)) {
      const QString rpt = QString::fromStdString(h.report_name);
      const QString snip = QString::fromStdString(h.snippet.empty() ? h.content : h.snippet).simplified().left(95);
      SiteEvidenceRow row;
      row.datasetName = QStringLiteral("지역 보고서 RAG");
      row.siteName =
          QStringLiteral("[%1] %2").arg(QString::fromStdString(h.sigungu), QFileInfo(rpt).completeBaseName());
      row.direction = QStringLiteral("권역");
      row.eraStatus = row.featureStatus = row.strataStatus = row.reportStatus = QStringLiteral("✓");
      row.reportName = rpt;
      row.pageNo = row.strataPageNo = h.page_no;
      row.eraSummary = QStringLiteral("원문 수록");
      row.featureSummary = snip;
      row.strataSummary = snip;
      row.relationToSurvey =
          QStringLiteral("동일 권역(%1) 발굴조사보고서 원문 대조 대상.").arg(QString::fromStdString(h.sigungu));
      snap->siteEntries.push_back(std::move(row));
    }
  }
  snap->discussionDraft = Detail::buildDefaultDiscussion(*snap);
  if (queryRagDb && snap->regionTotalReports > 0)
    snap->ragCitationSlotText = QStringLiteral("[로컬 발굴조사보고서 RAG 연동] %1 총 %2권(본문 %3권 · 스캔 %4권).")
                                    .arg(snap->regionName)
                                    .arg(snap->regionTotalReports)
                                    .arg(snap->regionTextReports)
                                    .arg(snap->regionScanReports);
}
}  // namespace HwpSurveyReportService
