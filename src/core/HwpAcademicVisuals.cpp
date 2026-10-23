#include "HwpAcademicVisuals.h"
#include "HwpSurveyEvidence.h"
#include "LocalReportRag.h"

#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <cmath>

namespace HwpAcademicVisuals {
namespace {

struct ParsedFeatureRecord {
  QString macroRegion;
  QString sigungu;
  QString period;
  QString featureType;
  QString planShape;
  QString scaleText;
  double lengthM = 0.0;
  double widthM = 0.0;
  double areaSqm = 0.0;
  QString facilityAndArtifact;
  QString reportName;
  int pageNo = 0;
};

QString matchFirst(const QString& txt, std::initializer_list<const char*> words, const QString& fallback) {
  for (const char* w : words) {
    const QString qw = QString::fromUtf8(w);
    if (txt.contains(qw)) return qw;
  }
  return fallback;
}

ParsedFeatureRecord parseHitToRecord(const strata::core::LocalReportHit& h) {
  ParsedFeatureRecord r;
  r.sigungu = QString::fromStdString(h.sigungu);
  r.macroRegion = QString::fromStdString(h.macro_region);
  r.reportName = QString::fromStdString(h.report_name);
  r.pageNo = h.page_no;
  const QString txt = QString::fromStdString(h.content + " " + h.report_name);
  r.period = matchFirst(txt, {"후기구석기", "중기구석기", "전기구석기", "구석기", "신석기", "청동기", "원삼국", "통일신라", "신라", "백제", "가야", "고구려", "삼국", "고려", "조선"}, QStringLiteral("시대미상"));
  r.featureType = matchFirst(txt, {"적석목곽묘", "굴립주건물지", "적심건물지", "초석건물지", "건물지", "수혈주거지", "주거지", "수혈유구", "수혈", "구상유구", "기와가마", "토기가마", "가마", "석곽묘", "석실묘", "옹관묘", "목관묘", "목곽묘", "토광묘", "구석기문화층", "문화층"}, QStringLiteral("기타유구"));
  r.planShape = matchFirst(txt, {"세장방형", "장방형", "말각방형", "방형", "타원형", "원형", "부정형"}, QStringLiteral("-"));
  static const QRegularExpression dimRx(QStringLiteral("(?:길이|장축)?\\s*(\\d{2,4}(?:\\.\\d+)?)\\s*(?:[×xX*]|(?:cm|㎝|m)?[^0-9]{1,10}(?:너비|폭|단축)\\s*)(\\d{2,4}(?:\\.\\d+)?)\\s*(cm|㎝|m|M)?"));
  static const QRegularExpression diaRx(QStringLiteral("직경\\s*(\\d{2,4}(?:\\.\\d+)?)\\s*(cm|㎝|m|M)?"));
  static const QRegularExpression kanRx(QStringLiteral("정면\\s*(\\d+)칸\\s*[×xX·,]*\\s*측면\\s*(\\d+)칸"));
  static const QRegularExpression axisRx(QStringLiteral("(N-\\d{1,2}°-[EW])"));
  if (const auto m = dimRx.match(txt); m.hasMatch()) {
    double v1 = m.captured(1).toDouble(), v2 = m.captured(2).toDouble();
    const QString unit = m.captured(3);
    if (unit.contains(QLatin1Char('c')) || unit == QStringLiteral("㎝") || v1 >= 40.0) { v1 /= 100.0; v2 /= 100.0; }
    r.lengthM = qMax(v1, v2); r.widthM = qMin(v1, v2); r.areaSqm = r.lengthM * r.widthM;
    r.scaleText = QStringLiteral("%1×%2m (%3㎡)").arg(QString::number(r.lengthM, 'f', 1), QString::number(r.widthM, 'f', 1), QString::number(r.areaSqm, 'f', 1));
  } else if (const auto dm = diaRx.match(txt); dm.hasMatch()) {
    double d = dm.captured(1).toDouble();
    if (d >= 40.0) d /= 100.0;
    r.lengthM = r.widthM = d; r.areaSqm = 3.14159 * (d * 0.5) * (d * 0.5);
    r.scaleText = QStringLiteral("직경 %1m (%2㎡)").arg(QString::number(d, 'f', 1), QString::number(r.areaSqm, 'f', 1));
  }
  if (const auto km = kanRx.match(txt); km.hasMatch()) {
    const QString kan = QStringLiteral("정면%1×측면%2칸").arg(km.captured(1), km.captured(2));
    r.scaleText = r.scaleText.isEmpty() ? kan : (kan + QStringLiteral(" · ") + r.scaleText);
  }
  if (const auto am = axisRx.match(txt); am.hasMatch())
    r.scaleText = r.scaleText.isEmpty() ? am.captured(1) : (r.scaleText + QStringLiteral(" · ") + am.captured(1));
  if (r.scaleText.isEmpty()) r.scaleText = QStringLiteral("본문참조");
  QStringList fac;
  for (const char* f : {"노지", "부뚜막", "구들", "벽구", "배수", "주공", "저장공", "시상대", "슴베찌르개", "좀돌날", "찍개", "AT화산재", "토양쐐기", "무문토기", "타날문", "수막새"})
    if (txt.contains(QString::fromUtf8(f))) fac << QString::fromUtf8(f);
  r.facilityAndArtifact = fac.isEmpty() ? QString::fromStdString(h.snippet).simplified().left(55) : fac.join(QStringLiteral(", "));
  return r;
}

QString renderStrataColumnCell(const QString& title, const QString& distDir, const QString& strataText,
                               const QString& featText, const QString& citeText, bool isExpectedTarget = false) {
  const QString border = isExpectedTarget ? QStringLiteral("2px dashed #B5573A") : QStringLiteral("1px solid #A8A196");
  const QString headBg = isExpectedTarget ? QStringLiteral("#B5573A") : QStringLiteral("#E3DACC");
  const QString headFg = isExpectedTarget ? QStringLiteral("#FAF8F5") : QStringLiteral("#1F1E1B");
  return QStringLiteral(
      "<td width='25%' valign='top' style='border:%1;background:#FAF8F5;padding:0;'>"
      "<div style='background:%2;color:%3;padding:6px;font-size:8.5pt;font-weight:bold;text-align:center;'>%4<br/>"
      "<span style='font-size:7.8pt;font-weight:normal;'>%5</span></div>"
      "<div style='padding:6px;font-size:8pt;background:#EFECE6;border-bottom:1px solid #D5CEC2;'><b>Ⅰ. 표토·복토층</b></div>"
      "<div style='padding:8px 6px;font-size:8pt;background:#E6DEC8;border-bottom:1px solid #D5CEC2;'><b>Ⅱ. 퇴적·토층 양상</b><br/>%6</div>"
      "<div style='padding:10px 6px;font-size:8pt;background:#F3ECE2;border-bottom:1px solid #B5573A;color:#1F1E1B;'>"
      "<b style='color:#B5573A;'>Ⅲ. 문화층·확인유구</b><br/>%7</div>"
      "<div style='padding:6px;font-size:7.8pt;background:#DDD6C9;color:#4B4842;'><b>Ⅳ. 기반풍화토(생토)</b><br/>출처: %8</div></td>")
      .arg(border, headBg, headFg, title.toHtmlEscaped(), distDir.toHtmlEscaped(),
           strataText.toHtmlEscaped(), featText.toHtmlEscaped(), citeText.toHtmlEscaped());
}
}  // namespace

QString renderVisualsAndDbpiaSectionHtml(const HwpSurveyReportService::SurveySnapshot& snap, const DbpiaSummary& dbpia) {
  QString html = QStringLiteral(
      "<div style='margin:16px 0;padding:12px;border:1px solid #A8A196;background:#FAF8F5;'>"
      "<div style='font-size:9.8pt;font-weight:bold;color:#1F1E1B;text-align:center;margin-bottom:8px;'>"
      "&lt;그림 1&gt; 주변유적 층위 대비도 및 조사구역 예상 양상 (동국문화재연구원 해발·토층 기준)</div>"
      "<table width='100%' cellspacing='6' cellpadding='0' style='border-collapse:separate;'><tr>");
  int colCount = 0;
  for (const auto& e : snap.siteEntries) {
    if (colCount >= 3) break;
    const QString cite = HwpSurveyReportService::Detail::reportSourceLabel(e);
    html += renderStrataColumnCell(e.siteName.left(14), QStringLiteral("경계 기준 %1 %2m").arg(e.direction).arg(e.distanceMeters),
                                   e.strataSummary.left(56), QStringLiteral("[%1] %2").arg(e.eraSummary, e.featureSummary.left(50)), cite, false);
    ++colCount;
  }
  while (colCount < 3) {
    html += renderStrataColumnCell(QStringLiteral("인접 대조유적 미상"), QStringLiteral("추가 레이어 로드 필요"),
                                   QStringLiteral("원문 미확보(확인 필요)"), QStringLiteral("원문 미확보"), QStringLiteral("○ 없음"), false);
    ++colCount;
  }
  const QString expStrata =
      !snap.siteEntries.isEmpty() && snap.siteEntries.front().reportStatus == QStringLiteral("✓")
          ? QStringLiteral("최단거리 유적(%1m) 토층 기준 동일 지형면 연장 예상").arg(snap.siteEntries.front().distanceMeters)
          : (!snap.siteEntries.isEmpty() && snap.siteEntries.front().reportStatus == QStringLiteral("△")
                 ? QStringLiteral("참고 보고서(%1m) 기준으로 동일 지형면 연장 여부 확인 필요").arg(snap.siteEntries.front().distanceMeters)
                 : QStringLiteral("시굴 트렌치 굴착을 통한 기준 토층 확인 필요"));
  const QString expFeat = !snap.siteEntries.isEmpty()
      ? QStringLiteral("인접 %1(%2 %3m) 연관 유구 분포 가능성 검토").arg(snap.siteEntries.front().siteName.left(10), snap.siteEntries.front().direction).arg(snap.siteEntries.front().distanceMeters)
      : QStringLiteral("조사구역(%1㎡) 내 유구 분포 확인 예정").arg(qRound(snap.totalAreaSqm));
  html += renderStrataColumnCell(QStringLiteral("조사구역 (예상)"), QStringLiteral("%1 (%2㎡)").arg(snap.surveyTitle.left(12)).arg(qRound(snap.totalAreaSqm)),
                                 expStrata, expFeat, QStringLiteral("[G] GIS 측정 + [R] 원문 대조"), true);
  html += QStringLiteral("</tr></table></div><div style='margin:14px 0;padding:12px;border:1px solid #A8A196;background:#FAF8F5;'>"
                         "<div style='font-size:9.8pt;font-weight:bold;color:#1F1E1B;margin-bottom:8px;'>"
                         "&lt;그림 2&gt; 조사구역 경계 기준 주변유적 실제 최단거리(m) 및 원문 검증 현황 (반경 %1m 내 %2개소)</div>"
                         "<table width='100%' cellspacing='0' cellpadding='3' style='border-top:2px solid #1F1E1B;border-bottom:2px solid #1F1E1B;font-size:8.5pt;'>")
              .arg(qRound(snap.radiusMeters)).arg(snap.siteEntries.size());
  const int maxDist = qMax(100, qRound(snap.radiusMeters));
  for (int i = 0; i < snap.siteEntries.size() && i < 10; ++i) {
    const auto& e = snap.siteEntries[i];
    const int barPct = qBound(4, e.distanceMeters * 100 / maxDist, 100);
    const QString barColor = e.reportStatus == QStringLiteral("✓") ? QStringLiteral("#B5573A")
                             : (e.reportStatus == QStringLiteral("△") ? QStringLiteral("#C08A2E")
                             : (e.reportStatus == QStringLiteral("◌") ? QStringLiteral("#8C7A6B") : QStringLiteral("#B8B2A6")));
    html += QStringLiteral("<tr><td width='165' style='padding:3px 5px;'><b>%1. %2</b></td><td width='65' align='center'>%3 %4m</td>"
                           "<td style='padding:3px 6px;background:#F3EFE8;'><table width='%5%' cellspacing='0' cellpadding='0'><tr><td style='background:%6;height:12px;'>&nbsp;</td></tr></table></td>"
                           "<td width='140' align='right' style='padding:3px 5px;'>%7 %8</td></tr>")
                .arg(i + 1).arg(e.siteName.left(14).toHtmlEscaped(), e.direction.toHtmlEscaped()).arg(e.distanceMeters).arg(barPct)
                .arg(barColor, e.reportStatus, e.pageNo > 0 ? QStringLiteral("p.%1").arg(e.pageNo) : (e.reportStatus == QStringLiteral("◌") ? QStringLiteral("스캔본") : QStringLiteral("미확보")));
  }
  html += QStringLiteral("</table></div>");
  if (!dbpia.collectedPapers.isEmpty()) {
    html += QStringLiteral("<div style='font-size:10pt;font-weight:bold;margin-top:14px;'>Ⅵ. 참고 학술문헌 (&lt;표 3&gt; · %1편)</div>"
                           "<table border='1' cellspacing='0' cellpadding='4' style='border-collapse:collapse;width:100%;margin:8px 0;font-size:8.4pt;border-top:2px solid #1F1E1B;border-bottom:2px solid #1F1E1B;border-color:#A8A196;'>"
                           "<tr style='background:#E3DACC;font-weight:bold;text-align:center;'><td width='40'>기호</td><td width='75'>구분</td><td width='100'>저자(연도)</td><td>논문명 / 학술지</td></tr>")
                .arg(dbpia.collectedPapers.size());
    for (int i = 0; i < qMin<int>(12, dbpia.collectedPapers.size()); ++i) {
      const QJsonObject p = dbpia.collectedPapers[i].toObject();
      html += QStringLiteral("<tr><td align='center'>L%1</td><td align='center'>%2</td><td>%3 (%4)</td><td>%5, 『%6』</td></tr>")
                  .arg(i + 1).arg(p.value(QStringLiteral("nodeType")).toString().toHtmlEscaped(), p.value(QStringLiteral("authors")).toString().toHtmlEscaped(),
                                  p.value(QStringLiteral("year")).toString().toHtmlEscaped(), p.value(QStringLiteral("title")).toString().toHtmlEscaped(), p.value(QStringLiteral("journal")).toString().toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
  }
  return html;
}

QString renderVerificationReportHtml(const HwpSurveyReportService::SurveySnapshot& snap, const DbpiaSummary& dbpia) {
  int cVerified = 0, cReference = 0, cScan = 0, cMissing = 0;
  for (const auto& e : snap.siteEntries) {
    if (e.reportStatus == QStringLiteral("✓")) ++cVerified;
    else if (e.reportStatus == QStringLiteral("△")) ++cReference;
    else if (e.reportStatus == QStringLiteral("◌")) ++cScan;
    else ++cMissing;
  }
  QString html = QStringLiteral(
      "<html><body style='background:#FAF8F5;margin:0;padding:18px;font-family:\"Pretendard\",\"Noto Sans CJK KR\",sans-serif;color:#1F1E1B;'>"
      "<div style='padding:10px 14px;background:#E3DACC;border:1px solid #1F1E1B;margin-bottom:14px;font-size:9.5pt;'>"
      "<b>[검수 보고 · 사실근거 대조표]</b> &nbsp; GIS 측정(G): <b>%1건</b> &nbsp;|&nbsp; 원문 확인(✓): <b>%2건</b> &nbsp;|&nbsp; 참고(△): <b>%3건</b> &nbsp;|&nbsp; 스캔본(◌): <b>%4건</b> &nbsp;|&nbsp; 미수록(○): <b>%5건</b> &nbsp;|&nbsp; 문헌(L): <b>%6편</b></div>"
      "<table border='1' cellspacing='0' cellpadding='6' style='border-collapse:collapse;width:100%;font-size:8.8pt;border-top:2px solid #1F1E1B;border-bottom:2px solid #1F1E1B;border-color:#A8A196;'>"
      "<tr style='background:#E3DACC;font-weight:bold;text-align:center;'><td width='70'>장·절</td><td>본문 서술 및 수치 항목</td><td width='55'>구분</td><td width='210'>검증 출처</td><td width='65'>판정</td></tr>")
      .arg(snap.siteEntries.size() + 1).arg(cVerified).arg(cReference).arg(cScan).arg(cMissing).arg(dbpia.collectedPapers.size());
  html += QStringLiteral("<tr><td align='center'>Ⅰ. 머리말</td><td>조사면적 %1㎡ (%2개 구역, 좌표계 %3)</td><td align='center'>G</td><td>QGIS 도메인 레이어(survey_area) 실측</td><td align='center'><b>✓ 확인</b></td></tr>")
              .arg(qRound(snap.totalAreaSqm)).arg(snap.surveyPolygonCount).arg(snap.surveyCrs.toHtmlEscaped());
  for (int i = 0; i < snap.siteEntries.size(); ++i) {
    const auto& e = snap.siteEntries[i];
    const QString cite = HwpSurveyReportService::Detail::reportSourceLabel(e);
    const QString badge = e.reportStatus == QStringLiteral("✓") ? QStringLiteral("<b>✓ 확인</b>")
                            : (e.reportStatus == QStringLiteral("△") ? QStringLiteral("△ 참고")
                            : (e.reportStatus == QStringLiteral("◌") ? QStringLiteral("◌ 스캔본")
                                                                     : QStringLiteral("○ 미수록")));
    html += QStringLiteral("<tr><td align='center'>Ⅱ-%1</td><td><b>%2</b> (경계 기준 %3 %4m) · %5</td><td align='center'>G+R</td><td>%6</td><td align='center'>%7</td></tr>")
                .arg(i + 1).arg(e.siteName.toHtmlEscaped(), e.direction.toHtmlEscaped()).arg(e.distanceMeters).arg(e.featureSummary.left(65).toHtmlEscaped(), cite.toHtmlEscaped(), badge);
  }
  return html + QStringLiteral("</table></body></html>");
}

QString renderFeatureComparisonHtml(const HwpSurveyReportService::SurveySnapshot& snap, const QString& featureKeyword) {
  const QString kw = featureKeyword.trimmed().isEmpty() ? QStringLiteral("주거지 건물지 가마 고분 유구 일람표") : featureKeyword.trimmed();
  const QString regionExpr = QString::fromStdString(strata::core::LocalReportRag::extractRegionScope(kw.toStdString(), snap.regionName.toStdString()));
  const auto hits = strata::core::LocalReportRag::search(kw.toStdString(), regionExpr.toStdString(), 18, 2);

  QVector<ParsedFeatureRecord> recs;
  QMap<QString, int> byRegion, byPeriodPlan;
  QMap<QString, QPair<double, int>> areaByRegion, areaByPeriodPlan;
  double sumArea = 0.0; int areaCnt = 0;
  for (const auto& h : hits) {
    ParsedFeatureRecord r = parseHitToRecord(h);
    const QString regKey = r.macroRegion + QStringLiteral(" · ") + r.sigungu;
    const QString ppKey = r.period + QStringLiteral(" | ") + r.featureType + (r.planShape != QLatin1String("-") ? (QStringLiteral("(") + r.planShape + QStringLiteral(")")) : QString());
    byRegion[regKey]++;
    byPeriodPlan[ppKey]++;
    if (r.areaSqm > 0.1) {
      sumArea += r.areaSqm; ++areaCnt;
      areaByRegion[regKey].first += r.areaSqm; areaByRegion[regKey].second++;
      areaByPeriodPlan[ppKey].first += r.areaSqm; areaByPeriodPlan[ppKey].second++;
    }
    recs.push_back(std::move(r));
  }
  QString html = QStringLiteral(
      "<html><body style='background:#FAF8F5;margin:0;padding:18px;font-family:\"Pretendard\",\"Noto Sans CJK KR\",sans-serif;color:#1F1E1B;'>"
      "<div style='padding:10px 14px;background:#E3DACC;border:1px solid #1F1E1B;margin-bottom:12px;font-size:9.4pt;'>"
      "<b>[단일 유구 비교 및 다차원(지역×시대×유구×평면×규모) 분류·집계]</b> &nbsp; 지역 변수: <b>%1</b> &nbsp;|&nbsp; 주제: <b>%2</b> (추출 %3건%4)</div>")
      .arg(regionExpr.toHtmlEscaped(), kw.toHtmlEscaped()).arg(recs.size())
      .arg(areaCnt > 0 ? QStringLiteral(" · 평균 면적 %1㎡").arg(QString::number(sumArea / areaCnt, 'f', 1)) : QString());

  html += QStringLiteral("<table width='100%' cellspacing='8' cellpadding='0'><tr><td width='48%' valign='top'>"
                         "<div style='font-size:9pt;font-weight:bold;margin-bottom:4px;'>1. 지역별(권역·시군구) 분포 및 규모 집계</div>"
                         "<table border='1' cellspacing='0' cellpadding='4' style='border-collapse:collapse;width:100%;font-size:8.4pt;border-color:#A8A196;'>"
                         "<tr style='background:#E3DACC;font-weight:bold;'><td>권역 · 시군구</td><td width='45' align='center'>건수</td><td width='75' align='center'>평균 면적</td></tr>");
  for (auto it = byRegion.begin(); it != byRegion.end(); ++it) {
    const auto ap = areaByRegion.value(it.key());
    const QString avgStr = ap.second > 0 ? (QString::number(ap.first / ap.second, 'f', 1) + QStringLiteral("㎡")) : QStringLiteral("-");
    html += QStringLiteral("<tr><td>%1</td><td align='center'><b>%2</b></td><td align='center'>%3</td></tr>").arg(it.key().toHtmlEscaped()).arg(it.value()).arg(avgStr);
  }
  html += QStringLiteral("</table></td><td width='52%' valign='top'>"
                         "<div style='font-size:9pt;font-weight:bold;margin-bottom:4px;'>2. 시대별 × 유구·평면형태별 교차 집계 (크기 변화)</div>"
                         "<table border='1' cellspacing='0' cellpadding='4' style='border-collapse:collapse;width:100%;font-size:8.4pt;border-color:#A8A196;'>"
                         "<tr style='background:#E3DACC;font-weight:bold;'><td>시대 | 유구 (평면형태)</td><td width='45' align='center'>건수</td><td width='75' align='center'>평균 면적</td></tr>");
  for (auto it = byPeriodPlan.begin(); it != byPeriodPlan.end(); ++it) {
    const auto ap = areaByPeriodPlan.value(it.key());
    const QString avgStr = ap.second > 0 ? (QString::number(ap.first / ap.second, 'f', 1) + QStringLiteral("㎡")) : QStringLiteral("-");
    html += QStringLiteral("<tr><td>%1</td><td align='center'><b>%2</b></td><td align='center'>%3</td></tr>").arg(it.key().toHtmlEscaped()).arg(it.value()).arg(avgStr);
  }
  html += QStringLiteral("</table></td></tr></table>");

  html += QStringLiteral("<div style='font-size:9.2pt;font-weight:bold;margin:10px 0 5px 0;'>3. 고고학 변수별(지역·시대·유구·평면·규모·내부시설) 원문 대조 일람표 (R)</div>"
                         "<table border='1' cellspacing='0' cellpadding='5' style='border-collapse:collapse;width:100%;font-size:8.4pt;border-top:2px solid #1F1E1B;border-bottom:2px solid #1F1E1B;border-color:#A8A196;'>"
                         "<tr style='background:#E3DACC;font-weight:bold;text-align:center;'>"
                         "<td width='38'>기호</td><td width='105'>권역·시군구</td><td width='65'>시대</td><td width='95'>유구(평면)</td><td width='105'>규모·칸수·장축</td><td>내부시설·유물·원문</td><td width='165'>보고서명·쪽</td></tr>");
  for (int i = 0; i < recs.size(); ++i) {
    const auto& r = recs[i];
    html += QStringLiteral("<tr><td align='center'><b>R%1</b></td><td>%2<br/><span style='color:#6E6A63;'>%3</span></td><td align='center'>%4</td>"
                           "<td align='center'><b>%5</b><br/>(%6)</td><td align='center'>%7</td><td>%8</td><td>%9 (p.%10)</td></tr>")
                .arg(i + 1).arg(r.sigungu.toHtmlEscaped(), r.macroRegion.toHtmlEscaped(), r.period.toHtmlEscaped(),
                                r.featureType.toHtmlEscaped(), r.planShape.toHtmlEscaped(), r.scaleText.toHtmlEscaped(),
                                r.facilityAndArtifact.toHtmlEscaped(), r.reportName.left(22).toHtmlEscaped()).arg(r.pageNo);
  }
  return html + QStringLiteral("</table></body></html>");
}

void appendDbpiaAndVisualSummaryToDoc(const HwpSurveyReportService::SurveySnapshot& snap, const DbpiaSummary& dbpia,
                                      HwpSurveyReportService::ReportDocumentSession* session) {
  if (!session) return;
  int cVerified = 0, cReference = 0;
  for (const auto& e : snap.siteEntries) {
    if (e.reportStatus == QStringLiteral("✓")) ++cVerified;
    else if (e.reportStatus == QStringLiteral("△")) ++cReference;
  }
  const QString summary = QStringLiteral("\n[동국문화재연구원 양식 · 사실근거 종합]\n• <그림 1~2> 주변유적(%1개소, 동일 보고서 %2개소 · 참고 %3개소) 층위 대비도 및 최단거리 분포\n• 학술문헌(DBpia): %4편\n")
                              .arg(snap.siteEntries.size()).arg(cVerified).arg(cReference).arg(dbpia.collectedPapers.size());
  session->setFieldValue(QStringLiteral("DISCUSSION_BODY"), snap.discussionDraft + summary + dbpia.synthesizedDiscussion);
}
}  // namespace HwpAcademicVisuals
