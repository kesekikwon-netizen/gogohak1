#include "AssessmentContract.h"

#include <QRegularExpression>
#include <QSet>
#include <QtMath>

namespace AssessmentContract {

namespace {

const QStringList kKinds = {QStringLiteral("report"), QStringLiteral("paper"), QStringLiteral("dbpia_ai")};
const QStringList kStatuses = {QStringLiteral("same_report"), QStringLiteral("reference"), QStringLiteral("scan"),
                               QStringLiteral("missing")};
const QStringList kClaimTypes = {QStringLiteral("fact"), QStringLiteral("inference"), QStringLiteral("none")};

// 사용자가 보는 평가서에 나오면 안 되는 개발 용어(스킬 strata-ui-wording, tests/test_ui_terms.cpp 와 같은 목록).
const QStringList kDeveloperTerms = {QStringLiteral("RAG"),      QStringLiteral("SDK"),         QStringLiteral("피처"),
                                     QStringLiteral("누름틀"),   QStringLiteral("Antigravity"), QStringLiteral("슬롯"),
                                     QStringLiteral("_SLOT"),    QStringLiteral("하네스")};

// 잠긴(미수집) 도메인을 글에서 알아보는 낱말. 이 낱말 옆에 숫자가 있으면 수치를 지어낸 것으로 본다.
QStringList lockWords(const QString& domain) {
  if (domain == QLatin1String("soil")) return {QStringLiteral("토양")};
  if (domain == QLatin1String("paleo_landform")) return {QStringLiteral("고지형")};
  if (domain == QLatin1String("dem")) return {QStringLiteral("해발"), QStringLiteral("표고"), QStringLiteral("DEM")};
  if (domain == QLatin1String("geology")) return {QStringLiteral("지질")};
  return {};
}

QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString().trimmed(); }

QJsonObject siteToJson(int number, const HwpSurveyReportService::SiteEvidenceRow& e) {
  return QJsonObject{{"id", QStringLiteral("S%1").arg(number)},
                     {"name", e.siteName},
                     {"dataset", e.datasetName},
                     {"distanceM", e.distanceMeters},
                     {"direction", e.direction},
                     {"heritageCode", e.heritageCode},
                     {"featureKey", e.featureKey},
                     {"permitNo", e.permitNumber},
                     {"institution", e.institution},
                     {"projectName", e.projectName},
                     {"address", e.siteAddress},
                     {"era", e.eraField},
                     {"presence", e.heritagePresence},
                     {"bibliography", QJsonObject{{"title", e.bibliography}, {"year", e.reportYear}}}};
}

void checkClaim(const QJsonObject& claim, const QString& where, const QSet<QString>& usable, const QSet<QString>& known,
                const QStringList& uncollected, QStringList* problems) {
  const QString text = str(claim, "text");
  const QString type = str(claim, "type");
  if (text.isEmpty()) {
    *problems << QStringLiteral("%1: 문장이 비어 있습니다").arg(where);
    return;
  }
  if (!kClaimTypes.contains(type)) {
    *problems << QStringLiteral("%1: 문장 종류가 올바르지 않습니다(fact, inference, none 중 하나)").arg(where);
    return;
  }
  QStringList found;
  for (const QString& term : kDeveloperTerms)
    if (text.contains(term)) found << term;
  if (!found.isEmpty()) *problems << QStringLiteral("%1: 개발 용어(%2)가 들어 있습니다").arg(where, found.join(QStringLiteral(", ")));
  if (type == QLatin1String("none")) return;

  const QJsonArray ids = claim.value(QStringLiteral("evidenceIds")).toArray();
  if (ids.isEmpty()) {
    *problems << QStringLiteral("%1: 근거 번호가 없습니다").arg(where);
  } else {
    bool anyUsable = false;
    for (const QJsonValue& v : ids) {
      const QString id = v.toString();
      if (!known.contains(id)) *problems << QStringLiteral("%1: 없는 근거 번호 %2").arg(where, id);
      else if (usable.contains(id)) anyUsable = true;
    }
    if (type == QLatin1String("fact") && !anyUsable && !ids.isEmpty())
      *problems << QStringLiteral("%1: 미수록 근거만으로 사실을 말할 수 없습니다").arg(where);
  }
  static const QRegularExpression digit(QStringLiteral("[0-9]"));
  if (digit.match(text).hasMatch())
    for (const QString& domain : uncollected)
      for (const QString& word : lockWords(domain))
        if (text.contains(word)) *problems << QStringLiteral("%1: 자료가 없는 도메인(%2)의 수치를 썼습니다").arg(where, word);
}

}  // namespace

QStringList scanExportText(const QString& plainText) {
  QStringList found;
  auto report = [&](const QString& word, const QString& label) {
    const int at = plainText.indexOf(word);
    if (at < 0) return;
    const QString around = plainText.mid(qMax(0, at - 12), word.size() + 24).simplified();
    found << QStringLiteral("%1 「%2」").arg(label, around);
  };
  for (const QString& term : kDeveloperTerms) report(term, QStringLiteral("개발 용어(%1)").arg(term));
  for (const QString& mark : {QStringLiteral("┏"), QStringLiteral("┓"), QStringLiteral("┗"), QStringLiteral("┛")})
    report(mark, QStringLiteral("장식 문자(%1)").arg(mark));
  report(QString(QChar(0xFFFD)), QStringLiteral("깨진 글자"));
  return found;
}

QString figureLabel() { return QStringLiteral("모식도(실측 아님)"); }

QJsonObject factsBundle(const GisFactHarness::Ledger& ledger, const HwpSurveyReportService::SurveySnapshot& snapshot) {
  const QJsonObject base = GisFactHarness::ledgerToJson(ledger);
  QJsonArray sites;
  int number = 0;
  for (const auto& e : snapshot.siteEntries) sites.append(siteToJson(++number, e));
  const QString crs = snapshot.surveyCrs.isEmpty() ? ledger.workCrs : snapshot.surveyCrs;
  const double area = snapshot.totalAreaSqm > 0.0 ? snapshot.totalAreaSqm : ledger.surveyAreaM2;
  return QJsonObject{{"schemaVersion", QStringLiteral("1.0")},
                     {"survey", QJsonObject{{"title", snapshot.surveyTitle},
                                            {"crs", crs},
                                            {"areaM2", area},
                                            {"radiusM", qRound(snapshot.radiusMeters)},
                                            {"region", snapshot.regionName}}},
                     {"cards", base.value(QStringLiteral("cards"))},
                     {"sites", sites},
                     {"uncollected", QJsonArray::fromStringList(ledger.uncollectedDomains)},
                     {"searchQueries", QJsonArray::fromStringList(ledger.dbpiaQueries)}};
}

QStringList validateEvidenceItem(const QJsonObject& item) {
  QStringList problems;
  static const QRegularExpression idPattern(QStringLiteral("^E[0-9]+$"));
  const QString id = str(item, "id");
  const QString label = id.isEmpty() ? QStringLiteral("(번호 없음)") : id;
  if (!idPattern.match(id).hasMatch()) problems << QStringLiteral("%1: 근거 번호는 E1, E2 … 모양이어야 합니다").arg(label);
  if (str(item, "siteRef").isEmpty()) problems << QStringLiteral("%1: 어느 유적의 근거인지(siteRef) 없습니다").arg(label);
  const QString kind = str(item, "kind");
  if (!kKinds.contains(kind)) problems << QStringLiteral("%1: 근거 종류가 올바르지 않습니다(report, paper, dbpia_ai)").arg(label);
  const QString status = str(item, "status");
  if (!kStatuses.contains(status))
    problems << QStringLiteral("%1: 근거 상태가 올바르지 않습니다(same_report, reference, scan, missing)").arg(label);
  if (str(item.value(QStringLiteral("source")).toObject(), "title").isEmpty())
    problems << QStringLiteral("%1: 출처(제목)가 없습니다").arg(label);
  if (status != QLatin1String("missing") && str(item, "text").isEmpty())
    problems << QStringLiteral("%1: 원문이 비어 있습니다").arg(label);
  if (kind == QLatin1String("report") && status != QLatin1String("missing") && item.value(QStringLiteral("page")).toInt() < 1)
    problems << QStringLiteral("%1: 보고서 근거는 쪽 번호가 필요합니다").arg(label);
  return problems;
}

QStringList gisIdsOf(const QJsonObject& facts) {
  QStringList ids;
  for (const QJsonValue& v : facts.value(QStringLiteral("cards")).toArray()) ids << str(v.toObject(), "id");
  for (const QJsonValue& v : facts.value(QStringLiteral("sites")).toArray()) ids << str(v.toObject(), "id");
  ids.removeAll(QString());
  return ids;
}

QStringList validateAssessment(const QJsonObject& result, const QJsonArray& evidence, const QStringList& uncollectedDomains,
                               const QStringList& gisIds) {
  QStringList problems;
  QSet<QString> known, usable;
  for (const QString& id : gisIds) known.insert(id), usable.insert(id);  // GIS 실측은 그 자체로 쓸 수 있는 근거다.
  for (const QJsonValue& v : evidence) {
    const QJsonObject e = v.toObject();
    known.insert(str(e, "id"));
    if (str(e, "status") != QLatin1String("missing")) usable.insert(str(e, "id"));
  }
  const QJsonArray sections = result.value(QStringLiteral("sections")).toArray();
  if (sections.isEmpty()) problems << QStringLiteral("평가에 장이 하나도 없습니다");
  for (const QJsonValue& sv : sections) {
    const QJsonObject section = sv.toObject();
    const QString title = str(section, "title");
    const QString where = title.isEmpty() ? str(section, "key") : title;
    if (title.isEmpty()) problems << QStringLiteral("%1: 장 제목이 없습니다").arg(where);
    const QJsonArray claims = section.value(QStringLiteral("claims")).toArray();
    if (claims.isEmpty()) problems << QStringLiteral("%1: 문장이 하나도 없습니다(자료가 없으면 「자료 없음」 문장을 쓰세요)").arg(where);
    for (const QJsonValue& cv : claims) checkClaim(cv.toObject(), where, usable, known, uncollectedDomains, &problems);
  }
  for (const QJsonValue& fv : result.value(QStringLiteral("figures")).toArray()) {
    const QJsonObject fig = fv.toObject();
    const QString name = str(fig, "file");
    if (!str(fig, "label").contains(figureLabel()))
      problems << QStringLiteral("그림 %1: 「%2」 표시가 없습니다").arg(name, figureLabel());
    const QJsonArray ids = fig.value(QStringLiteral("evidenceIds")).toArray();
    if (ids.isEmpty()) problems << QStringLiteral("그림 %1: 근거 번호가 없습니다").arg(name);
    for (const QJsonValue& v : ids)
      if (!known.contains(v.toString())) problems << QStringLiteral("그림 %1: 없는 근거 번호 %2").arg(name, v.toString());
  }
  return problems;
}

}  // namespace AssessmentContract
