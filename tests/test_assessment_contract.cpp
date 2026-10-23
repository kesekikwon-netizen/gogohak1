// 시굴 전 종합 평가 계약(설계도 6절): 사실 묶음 내보내기, 근거 항목 검사, 평가 결과 검사.
// SDK 없이도 앱이 약속을 지키는지 본다. 실제 보고서 DB나 사용자 자료는 읽지 않는다.
#include "core/AssessmentContract.h"
#include "core/GisFactHarness.h"
#include "core/HwpSurveyReportService.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace AssessmentContract;

namespace {

QJsonObject evidence(const QString& id, const QString& status = QStringLiteral("same_report"),
                     const QString& kind = QStringLiteral("report"), int page = 78) {
  QJsonObject o{{"id", id},
                {"siteRef", QStringLiteral("경주 황남동 유적")},
                {"kind", kind},
                {"source", QJsonObject{{"title", QStringLiteral("황남동 발굴조사보고서")}, {"year", 2019}}},
                {"text", QStringLiteral("1호 주구석곽묘 내부에서 토기가 출토되었다.")},
                {"status", status}};
  if (page > 0) o.insert("page", page);
  return o;
}

QJsonObject claim(const QString& text, const QString& type, const QStringList& ids) {
  return QJsonObject{{"text", text}, {"type", type}, {"evidenceIds", QJsonArray::fromStringList(ids)}};
}

QJsonObject result(const QJsonArray& claims) {
  return QJsonObject{{"sections", QJsonArray{QJsonObject{{"key", "link"}, {"title", QStringLiteral("연계성")}, {"claims", claims}}}}};
}

bool anyContains(const QStringList& list, const QString& part) {
  for (const QString& s : list)
    if (s.contains(part)) return true;
  return false;
}

}  // namespace

class TestAssessmentContract : public QObject {
  Q_OBJECT
 private slots:
  void factsBundleCarriesSurveySitesAndLocks() {
    GisFactHarness::Ledger ledger;
    ledger.workCrs = QStringLiteral("EPSG:5187");
    ledger.surveyAreaM2 = 1840.0;
    ledger.uncollectedDomains = {QStringLiteral("soil"), QStringLiteral("dem")};
    GisFactHarness::Card card;
    card.id = QStringLiteral("G1");
    card.kind = QStringLiteral("nearby_heritage");
    card.siteName = QStringLiteral("경주 황남동 유적");
    card.attribute = QStringLiteral("이격거리");
    card.value = QStringLiteral("142");
    card.unit = QStringLiteral("m");
    ledger.cards = {card};

    HwpSurveyReportService::SurveySnapshot snap;
    snap.surveyTitle = QStringLiteral("경주 성동동 시굴");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    snap.regionName = QStringLiteral("경주시");
    snap.radiusMeters = 500.0;
    snap.totalAreaSqm = 1840.0;
    HwpSurveyReportService::SiteEvidenceRow site;
    site.siteName = QStringLiteral("경주 황남동 유적");
    site.heritageCode = QStringLiteral("EX1");
    site.permitNumber = QStringLiteral("2019-12");
    site.institution = QStringLiteral("동국문화재연구원");
    site.bibliography = QStringLiteral("동국문화재연구원, 2019, 황남동 발굴조사보고서");
    site.reportYear = 2019;
    site.distanceMeters = 142;
    site.direction = QStringLiteral("북서");
    snap.siteEntries = {site};

    const QJsonObject facts = factsBundle(ledger, snap);
    QCOMPARE(facts.value("schemaVersion").toString(), QStringLiteral("1.0"));
    const QJsonObject survey = facts.value("survey").toObject();
    QCOMPARE(survey.value("title").toString(), QStringLiteral("경주 성동동 시굴"));
    QCOMPARE(survey.value("crs").toString(), QStringLiteral("EPSG:5187"));
    QCOMPARE(survey.value("areaM2").toDouble(), 1840.0);
    QCOMPARE(survey.value("radiusM").toInt(), 500);
    QCOMPARE(survey.value("region").toString(), QStringLiteral("경주시"));
    QCOMPARE(facts.value("cards").toArray().size(), 1);
    QCOMPARE(facts.value("cards").toArray().at(0).toObject().value("id").toString(), QStringLiteral("G1"));
    const QJsonObject s0 = facts.value("sites").toArray().at(0).toObject();
    QCOMPARE(s0.value("id").toString(), QStringLiteral("S1"));
    QCOMPARE(s0.value("name").toString(), QStringLiteral("경주 황남동 유적"));
    QCOMPARE(s0.value("heritageCode").toString(), QStringLiteral("EX1"));
    QCOMPARE(s0.value("permitNo").toString(), QStringLiteral("2019-12"));
    QCOMPARE(s0.value("distanceM").toInt(), 142);
    QCOMPARE(s0.value("bibliography").toObject().value("year").toInt(), 2019);
    const QJsonArray locked = facts.value("uncollected").toArray();
    QCOMPARE(locked.size(), 2);
    QCOMPARE(locked.at(0).toString(), QStringLiteral("soil"));
    // GIS 번호(G 카드, S 유적)는 평가서가 인용할 수 있는 번호다.
    QCOMPARE(gisIdsOf(facts), (QStringList{QStringLiteral("G1"), QStringLiteral("S1")}));
    // 같은 입력이면 같은 JSON이다(기록·비교용).
    QCOMPARE(QJsonDocument(facts).toJson(), QJsonDocument(factsBundle(ledger, snap)).toJson());
  }

  void evidenceItemRules() {
    QVERIFY(validateEvidenceItem(evidence(QStringLiteral("E1"))).isEmpty());
    QVERIFY(validateEvidenceItem(evidence(QStringLiteral("E2"), QStringLiteral("reference"))).isEmpty());
    QVERIFY(validateEvidenceItem(evidence(QStringLiteral("E3"), QStringLiteral("same_report"), QStringLiteral("paper"), 0)).isEmpty());

    QVERIFY(anyContains(validateEvidenceItem(evidence(QStringLiteral("X1"))), QStringLiteral("근거 번호")));
    QVERIFY(anyContains(validateEvidenceItem(evidence(QStringLiteral("E4"), QStringLiteral("maybe"))), QStringLiteral("상태")));
    QVERIFY(anyContains(validateEvidenceItem(evidence(QStringLiteral("E5"), QStringLiteral("same_report"), QStringLiteral("blog"))), QStringLiteral("종류")));
    QVERIFY(anyContains(validateEvidenceItem(evidence(QStringLiteral("E6"), QStringLiteral("scan"), QStringLiteral("report"), 0)), QStringLiteral("쪽")));

    QJsonObject noText = evidence(QStringLiteral("E7"));
    noText.insert("text", QString());
    QVERIFY(anyContains(validateEvidenceItem(noText), QStringLiteral("원문")));
    // 미수록은 원문이 없어도 된다(없다는 사실 자체가 근거 상태다).
    noText.insert("status", QStringLiteral("missing"));
    noText.remove("page");
    QVERIFY(validateEvidenceItem(noText).isEmpty());

    QJsonObject noSource = evidence(QStringLiteral("E8"));
    noSource.remove("source");
    QVERIFY(anyContains(validateEvidenceItem(noSource), QStringLiteral("출처")));
  }

  void assessmentNeedsEvidenceForEveryFact() {
    const QJsonArray ev{evidence(QStringLiteral("E1")), evidence(QStringLiteral("E2"), QStringLiteral("missing"), QStringLiteral("report"), 0)};
    const QStringList none;

    QVERIFY(validateAssessment(result({claim(QStringLiteral("주구석곽묘에서 토기가 출토되었다."), QStringLiteral("fact"), {QStringLiteral("E1")}),
                                       claim(QStringLiteral("조사구역에도 같은 시기 유구가 있을 가능성이 있다."), QStringLiteral("inference"), {QStringLiteral("E1")}),
                                       claim(QStringLiteral("토양 자료가 없어 확인하지 못했다."), QStringLiteral("none"), {})}),
                               ev, none)
                .isEmpty());

    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("근거 없는 사실"), QStringLiteral("fact"), {})}), ev, none), QStringLiteral("근거 번호")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("없는 번호 인용"), QStringLiteral("fact"), {QStringLiteral("E99")})}), ev, none), QStringLiteral("E99")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("미수록 근거로 사실 주장"), QStringLiteral("fact"), {QStringLiteral("E2")})}), ev, none), QStringLiteral("미수록")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("종류가 이상함"), QStringLiteral("guess"), {QStringLiteral("E1")})}), ev, none), QStringLiteral("종류")));
    QVERIFY(anyContains(validateAssessment(result({claim(QString(), QStringLiteral("fact"), {QStringLiteral("E1")})}), ev, none), QStringLiteral("문장")));
    QVERIFY(anyContains(validateAssessment(QJsonObject{}, ev, none), QStringLiteral("장")));
  }

  void assessmentKeepsDeveloperTermsAndLockedNumbersOut() {
    const QJsonArray ev{evidence(QStringLiteral("E1"))};
    const QStringList locked{QStringLiteral("soil"), QStringLiteral("dem")};
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("RAG에서 찾은 내용이다."), QStringLiteral("fact"), {QStringLiteral("E1")})}), ev, {}), QStringLiteral("RAG")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("자료는 [RAG_CITATION_SLOT] 에 있다."), QStringLiteral("fact"), {QStringLiteral("E1")})}), ev, {}), QStringLiteral("_SLOT")));
    // 잠긴 도메인의 수치는 쓸 수 없다. 숫자 없이 「없다」고 말하는 것은 된다.
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("토양 산성도는 pH 5.2이다."), QStringLiteral("fact"), {QStringLiteral("E1")})}), ev, locked), QStringLiteral("토양")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("해발 42m 평탄지다."), QStringLiteral("inference"), {QStringLiteral("E1")})}), ev, locked), QStringLiteral("해발")));
    QVERIFY(validateAssessment(result({claim(QStringLiteral("토양 자료가 없다."), QStringLiteral("none"), {})}), ev, locked).isEmpty());
    // 잠기지 않았으면 같은 문장도 통과한다.
    QVERIFY(validateAssessment(result({claim(QStringLiteral("해발 42m 평탄지다."), QStringLiteral("inference"), {QStringLiteral("E1")})}), ev, {}).isEmpty());
  }

  void gisNumbersCanBeCited() {
    const QJsonArray ev{evidence(QStringLiteral("E1"))};
    const QStringList gis{QStringLiteral("G1"), QStringLiteral("S1")};
    const QJsonObject ok = result({claim(QStringLiteral("조사구역 면적은 1840㎡이다."), QStringLiteral("fact"), {QStringLiteral("G1")}),
                                   claim(QStringLiteral("공식 서지가 있다."), QStringLiteral("fact"), {QStringLiteral("S1"), QStringLiteral("E1")})});
    QVERIFY(validateAssessment(ok, ev, {}, gis).isEmpty());
    // GIS 번호를 알려 주지 않으면 인용할 수 없다.
    QVERIFY(anyContains(validateAssessment(ok, ev, {}), QStringLiteral("G1")));
    QVERIFY(anyContains(validateAssessment(result({claim(QStringLiteral("없는 GIS 번호"), QStringLiteral("fact"), {QStringLiteral("G9")})}), ev, {}, gis), QStringLiteral("G9")));
  }

  void exportScanFindsLeftoversInFinishedText() {
    QVERIFY(scanExportText(QStringLiteral("Ⅰ. 머리말\n조사면적 1,840 ㎡ (1개 구역)\n✓ 동일 보고서 『황남동』 78쪽")).isEmpty());
    const QStringList found = scanExportText(QStringLiteral("보고서 RAG_CITATION_SLOT 자리\n┏ 장식 ┓\n깨짐 \uFFFD 글자\n피처 12"));
    QVERIFY(anyContains(found, QStringLiteral("RAG")));
    QVERIFY(anyContains(found, QStringLiteral("_SLOT")));
    QVERIFY(anyContains(found, QStringLiteral("┏")));
    QVERIFY(anyContains(found, QStringLiteral("깨진 글자")));
    QVERIFY(anyContains(found, QStringLiteral("피처")));
    // 같은 낱말은 한 번만 알린다(줄 수가 많아도 목록이 길어지지 않게).
    QCOMPARE(scanExportText(QStringLiteral("RAG RAG RAG")).size(), 1);
    // 문제 낱말 둘레 글자를 보여 줘 어디인지 알 수 있다.
    QVERIFY(anyContains(scanExportText(QStringLiteral("앞글자 RAG 뒷글자")), QStringLiteral("앞글자")));
  }

  void figuresMustBeLabeledAndGrounded() {
    const QJsonArray ev{evidence(QStringLiteral("E1"))};
    QJsonObject r = result({claim(QStringLiteral("층위 순서를 모식도로 보인다."), QStringLiteral("inference"), {QStringLiteral("E1")})});
    r.insert("figures", QJsonArray{QJsonObject{{"file", "strata_1.png"}, {"label", QStringLiteral("모식도(실측 아님)")},
                                               {"evidenceIds", QJsonArray{QStringLiteral("E1")}}}});
    QVERIFY(validateAssessment(r, ev, {}).isEmpty());

    r.insert("figures", QJsonArray{QJsonObject{{"file", "strata_1.png"}, {"label", QStringLiteral("층위도")},
                                               {"evidenceIds", QJsonArray{QStringLiteral("E1")}}}});
    QVERIFY(anyContains(validateAssessment(r, ev, {}), QStringLiteral("실측 아님")));
    r.insert("figures", QJsonArray{QJsonObject{{"file", "strata_1.png"}, {"label", QStringLiteral("모식도(실측 아님)")}}});
    QVERIFY(anyContains(validateAssessment(r, ev, {}), QStringLiteral("그림")));
  }

  // 저장소에 둔 샘플이 검사기를 계속 통과해야 한다(샘플이 낡는 것을 막는다).
  void samplesStayValid() {
    const QString dir = QString::fromUtf8(KA_SOURCE_DIR) + QStringLiteral("/docs/design/2026-10-23-survey-assessment/samples/");
    auto load = [&](const QString& name) {
      QFile file(dir + name);
      return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
    };
    const QJsonDocument evidenceDoc = load(QStringLiteral("evidence.sample.json"));
    const QJsonDocument assessmentDoc = load(QStringLiteral("assessment.sample.json"));
    const QJsonDocument factsDoc = load(QStringLiteral("facts.sample.json"));
    QVERIFY(evidenceDoc.isArray());
    QVERIFY(assessmentDoc.isObject());
    QVERIFY(factsDoc.isObject());
    for (const QJsonValue& v : evidenceDoc.array()) QVERIFY2(validateEvidenceItem(v.toObject()).isEmpty(), qUtf8Printable(v.toObject().value("id").toString()));
    const QStringList locked = [&] {
      QStringList out;
      for (const QJsonValue& v : factsDoc.object().value("uncollected").toArray()) out << v.toString();
      return out;
    }();
    const QStringList problems = validateAssessment(assessmentDoc.object(), evidenceDoc.array(), locked, gisIdsOf(factsDoc.object()));
    QVERIFY2(problems.isEmpty(), qUtf8Printable(problems.join(QLatin1Char('\n'))));
    for (const char* key : {"schemaVersion", "survey", "cards", "sites", "uncollected"}) QVERIFY(factsDoc.object().contains(QLatin1String(key)));
  }
};

QTEST_GUILESS_MAIN(TestAssessmentContract)

#include "test_assessment_contract.moc"
