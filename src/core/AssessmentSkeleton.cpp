#include "AssessmentSkeleton.h"

#include <QDir>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>

namespace AssessmentSkeleton {

namespace {

using HwpSurveyReportService::SiteEvidenceRow;

QJsonObject claim(const QString& text, const QString& type, const QStringList& ids = {}) {
  return QJsonObject{{"text", text}, {"type", type}, {"evidenceIds", QJsonArray::fromStringList(ids)}};
}

QJsonObject section(const QString& key, const QString& title, const QJsonArray& claims) {
  return QJsonObject{{"key", key}, {"title", title}, {"claims", claims}};
}

QString domainSentence(const QString& domain) {
  if (domain == QLatin1String("soil")) return QStringLiteral("토양");
  if (domain == QLatin1String("paleo_landform")) return QStringLiteral("옛 지형(고지형)");
  if (domain == QLatin1String("dem")) return QStringLiteral("해발(DEM)");
  if (domain == QLatin1String("geology")) return QStringLiteral("지질");
  return domain;
}

QString where(const SiteEvidenceRow& e) {
  return QStringLiteral("%1 %2m").arg(e.direction.isEmpty() ? QStringLiteral("인접") : e.direction).arg(e.distanceMeters);
}

QString joinedSummary(const SiteEvidenceRow& e) {
  QStringList parts;
  for (const QString& s : {e.eraSummary, e.featureSummary, e.strataSummary})
    if (!s.trimmed().isEmpty() && !parts.contains(s.trimmed())) parts << s.trimmed();
  return parts.join(QStringLiteral(" / "));
}

// 쪽 번호와 요약 문장이 있는 ✓·△ 만 보고서 근거가 된다. 쪽 번호를 지어내지 않는다.
bool hasQuotableReport(const SiteEvidenceRow& e) {
  return (e.reportStatus == QStringLiteral("✓") || e.reportStatus == QStringLiteral("△")) && e.pageNo >= 1 &&
         !joinedSummary(e).isEmpty();
}

}  // namespace

Result build(const GisFactHarness::Ledger& ledger, const HwpSurveyReportService::SurveySnapshot& snap) {
  Result r;
  r.facts = AssessmentContract::factsBundle(ledger, snap);
  const auto& sites = snap.siteEntries;
  const int n = sites.size();

  // 근거 항목: 사이트 순서대로 E1, E2 …
  QMap<int, QString> evidenceOfSite;
  for (int i = 0; i < n; ++i) {
    const SiteEvidenceRow& e = sites[i];
    if (!hasQuotableReport(e)) continue;
    const QString id = QStringLiteral("E%1").arg(r.evidence.size() + 1);
    const QString title = !e.reportName.trimmed().isEmpty() ? e.reportName.trimmed()
                          : (!e.bibliography.trimmed().isEmpty() ? e.bibliography.trimmed() : QStringLiteral("보고서명 확인 필요"));
    r.evidence.append(QJsonObject{{"id", id},
                                  {"siteRef", e.siteName},
                                  {"kind", QStringLiteral("report")},
                                  {"source", QJsonObject{{"title", title}, {"year", e.reportYear}, {"institution", e.institution}}},
                                  {"page", e.pageNo},
                                  {"text", joinedSummary(e)},
                                  {"status", e.reportStatus == QStringLiteral("✓") ? QStringLiteral("same_report") : QStringLiteral("reference")}});
    evidenceOfSite.insert(i, id);
  }
  auto siteId = [](int i) { return QStringLiteral("S%1").arg(i + 1); };
  QStringList allSiteIds, allEvidenceIds;
  for (int i = 0; i < n; ++i) allSiteIds << siteId(i);
  for (const QString& id : evidenceOfSite) allEvidenceIds << id;

  const GisFactHarness::Card* areaCard = nullptr;
  for (const auto& c : ledger.cards)
    if (c.kind == QLatin1String("survey_area") && c.attribute == QStringLiteral("면적")) areaCard = &c;
  const int radius = qRound(snap.radiusMeters);

  // 0 요약
  QJsonArray summary;
  if (areaCard) summary.append(claim(QStringLiteral("조사구역 면적은 %1%2이다.").arg(areaCard->value, areaCard->unit), QStringLiteral("fact"), {areaCard->id}));
  else summary.append(claim(QStringLiteral("조사구역 면적 값이 없다."), QStringLiteral("none")));
  if (n == 0) {
    summary.append(claim(QStringLiteral("반경 %1m 안에서 주변유적을 찾지 못했다.").arg(radius), QStringLiteral("none")));
  } else {
    summary.append(claim(QStringLiteral("반경 %1m 안에 주변유적이 %2건 있다.").arg(radius).arg(n), QStringLiteral("fact"), allSiteIds));
    int nearest = 0;
    for (int i = 1; i < n; ++i)
      if (sites[i].distanceMeters < sites[nearest].distanceMeters) nearest = i;
    summary.append(claim(QStringLiteral("가장 가까운 주변유적은 %1(%2)이다.").arg(sites[nearest].siteName, where(sites[nearest])),
                         QStringLiteral("fact"), {siteId(nearest)}));
    if (allEvidenceIds.isEmpty()) summary.append(claim(QStringLiteral("보고서 근거를 확인한 유적이 없다."), QStringLiteral("none")));
    else summary.append(claim(QStringLiteral("보고서 근거를 확인한 유적은 %1건이다.").arg(allEvidenceIds.size()), QStringLiteral("fact"), allEvidenceIds));
  }

  // 1 조사구역 개요
  QJsonArray survey;
  const QString crs = snap.surveyCrs.isEmpty() ? ledger.workCrs : snap.surveyCrs;
  if (areaCard && !crs.isEmpty()) survey.append(claim(QStringLiteral("작업 좌표계는 %1이다.").arg(crs), QStringLiteral("fact"), {areaCard->id}));
  for (const QString& domain : ledger.uncollectedDomains)
    survey.append(claim(QStringLiteral("%1 자료가 없어 확인하지 못했다.").arg(domainSentence(domain)), QStringLiteral("none")));
  if (survey.isEmpty()) survey.append(claim(QStringLiteral("추가로 알려 줄 조사구역 값이 없다."), QStringLiteral("none")));

  // 2 반경 안 주변유적
  QJsonArray siteLines;
  for (int i = 0; i < n; ++i)
    siteLines.append(claim(QStringLiteral("%1은(는) 조사구역에서 %2 떨어져 있다.").arg(sites[i].siteName, where(sites[i])), QStringLiteral("fact"), {siteId(i)}));
  if (n == 0) siteLines.append(claim(QStringLiteral("반경 안에 주변유적이 없다."), QStringLiteral("none")));

  // 3 유적별 근거, 6 부족한 자료
  QJsonArray cards, gapClaims;
  QStringList gaps;
  for (const QString& domain : ledger.uncollectedDomains)
    gaps << QStringLiteral("%1 자료: 현장 또는 기관에서 확인한다.").arg(domainSentence(domain));
  for (int i = 0; i < n; ++i) {
    const SiteEvidenceRow& e = sites[i];
    if (evidenceOfSite.contains(i)) {
      const QJsonObject ev = r.evidence.at(evidenceOfSite.keys().indexOf(i)).toObject();
      cards.append(claim(QStringLiteral("%1: %2 (%3 %4쪽)").arg(e.siteName, ev.value("text").toString(),
                                                                  ev.value("source").toObject().value("title").toString()).arg(e.pageNo),
                         QStringLiteral("fact"), {evidenceOfSite.value(i), siteId(i)}));
    } else if (e.reportStatus == QStringLiteral("◌")) {
      cards.append(claim(QStringLiteral("%1: 스캔본이라 직접 대조가 필요하다.").arg(e.siteName), QStringLiteral("none")));
      gaps << QStringLiteral("%1: 스캔본 보고서를 직접 대조한다.").arg(e.siteName);
    } else if (!e.bibliography.trimmed().isEmpty()) {
      cards.append(claim(QStringLiteral("%1: 이 PC의 보고서에서 근거를 찾지 못했다. 공식 서지: %2").arg(e.siteName, e.bibliography.trimmed()), QStringLiteral("none")));
      gaps << QStringLiteral("%1: 공식 서지의 보고서 원문을 구해 확인한다.").arg(e.siteName);
    } else {
      cards.append(claim(QStringLiteral("%1: 이 PC의 보고서에서 근거를 찾지 못했고 공식 서지도 없다.").arg(e.siteName), QStringLiteral("none")));
      gaps << QStringLiteral("%1: 보고서와 서지를 기관에 문의한다.").arg(e.siteName);
    }
  }
  if (n == 0) cards.append(claim(QStringLiteral("주변유적이 없어 유적별 근거를 쓰지 않았다."), QStringLiteral("none")));
  for (const QString& g : gaps) gapClaims.append(claim(g, QStringLiteral("none")));
  if (gapClaims.isEmpty()) gapClaims.append(claim(QStringLiteral("추가로 확인할 자료가 없다."), QStringLiteral("none")));

  // 7 참고문헌
  QJsonArray refs;
  for (int i = 0; i < n; ++i)
    if (!sites[i].bibliography.trimmed().isEmpty()) refs.append(claim(sites[i].bibliography.trimmed(), QStringLiteral("fact"), {siteId(i)}));
  if (refs.isEmpty()) refs.append(claim(QStringLiteral("공식 서지가 있는 유적이 없다."), QStringLiteral("none")));

  r.assessment = QJsonObject{{"sections", QJsonArray{section(QStringLiteral("summary"), QStringLiteral("요약"), summary),
                                                      section(QStringLiteral("survey"), QStringLiteral("조사구역 개요"), survey),
                                                      section(QStringLiteral("sites"), QStringLiteral("반경 안 주변유적"), siteLines),
                                                      section(QStringLiteral("siteCards"), QStringLiteral("유적별 근거"), cards),
                                                      section(QStringLiteral("gaps"), QStringLiteral("부족한 자료와 확인할 것"), gapClaims),
                                                      section(QStringLiteral("references"), QStringLiteral("참고문헌"), refs)}},
                             {"figures", QJsonArray()},
                             {"gaps", QJsonArray::fromStringList(gaps)}};
  return r;
}

bool writeBundle(const QString& folder, const Result& result, QString* error) {
  auto fail = [&](const QString& why) {
    if (error) *error = why;
    return false;
  };
  if (folder.trimmed().isEmpty()) return fail(QStringLiteral("저장할 폴더가 정해지지 않았습니다"));
  if (!QDir().mkpath(folder)) return fail(QStringLiteral("폴더를 만들지 못했습니다: %1").arg(folder));
  const QDir dir(folder);
  const std::pair<QString, QByteArray> files[] = {
      {QStringLiteral("facts.json"), QJsonDocument(result.facts).toJson(QJsonDocument::Indented)},
      {QStringLiteral("evidence.json"), QJsonDocument(result.evidence).toJson(QJsonDocument::Indented)},
      {QStringLiteral("assessment.json"), QJsonDocument(result.assessment).toJson(QJsonDocument::Indented)}};
  for (const auto& [name, bytes] : files) {
    QSaveFile file(dir.filePath(name));  // 끝까지 쓴 뒤에만 바꿔치기해서 실패해도 기존 파일이 남는다.
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      return fail(QStringLiteral("파일을 쓰지 못했습니다: %1").arg(file.fileName()));
  }
  return true;
}

}  // namespace AssessmentSkeleton
