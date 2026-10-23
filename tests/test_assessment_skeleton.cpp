// AI 없이 만드는 시굴 전 평가 뼈대. 계약 검사기를 통과해야 하고, 근거가 없는 곳은 「자료 없음」으로 말해야 한다.
#include "core/AssessmentSkeleton.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace AssessmentSkeleton;
using HwpSurveyReportService::SiteEvidenceRow;
using HwpSurveyReportService::SurveySnapshot;

namespace {

GisFactHarness::Card card(const QString& id, const QString& kind, const QString& attribute, const QString& value,
                          const QString& unit = QString(), const QString& site = QString()) {
  GisFactHarness::Card c;
  c.id = id;
  c.kind = kind;
  c.subject = site.isEmpty() ? QStringLiteral("조사구역") : site;
  c.siteName = site;
  c.attribute = attribute;
  c.value = value;
  c.unit = unit;
  return c;
}

SiteEvidenceRow site(const QString& name, int distance, const QString& status) {
  SiteEvidenceRow e;
  e.siteName = name;
  e.distanceMeters = distance;
  e.direction = QStringLiteral("북서");
  e.reportStatus = status;
  return e;
}

struct Fixture {
  GisFactHarness::Ledger ledger;
  SurveySnapshot snap;
  Fixture() {
    ledger.workCrs = QStringLiteral("EPSG:5187");
    ledger.surveyAreaM2 = 1840.0;
    ledger.uncollectedDomains = {QStringLiteral("soil"), QStringLiteral("dem")};
    ledger.cards = {card(QStringLiteral("G1"), QStringLiteral("survey_area"), QStringLiteral("면적"), QStringLiteral("1840"), QStringLiteral("㎡")),
                    card(QStringLiteral("G2"), QStringLiteral("nearby_heritage"), QStringLiteral("이격거리"), QStringLiteral("142"), QStringLiteral("m"),
                         QStringLiteral("황남동 유적"))};
    snap.surveyTitle = QStringLiteral("성동동 시굴");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    snap.regionName = QStringLiteral("경주시");
    snap.radiusMeters = 500.0;
    snap.totalAreaSqm = 1840.0;

    SiteEvidenceRow a = site(QStringLiteral("황남동 유적"), 142, QStringLiteral("✓"));
    a.reportName = QStringLiteral("황남동 발굴조사보고서");
    a.pageNo = 78;
    a.eraSummary = QStringLiteral("삼국시대 주거 유구");
    a.featureSummary = QStringLiteral("구덩이 3기");
    a.bibliography = QStringLiteral("동국문화재연구원, 2019, 황남동 발굴조사보고서");
    a.reportYear = 2019;
    SiteEvidenceRow b = site(QStringLiteral("구황동 유적"), 380, QStringLiteral("○"));
    b.bibliography = QStringLiteral("연구원, 2015, 구황동 발굴조사보고서");
    SiteEvidenceRow c = site(QStringLiteral("월성 유적"), 640, QStringLiteral("○"));
    SiteEvidenceRow d = site(QStringLiteral("인왕동 유적"), 710, QStringLiteral("◌"));
    d.reportName = QStringLiteral("인왕동 스캔 보고서");
    snap.siteEntries = {a, b, c, d};
  }
};

QStringList claimTexts(const QJsonObject& assessment, const QString& sectionKey) {
  QStringList out;
  for (const QJsonValue& sv : assessment.value("sections").toArray()) {
    if (sv.toObject().value("key").toString() != sectionKey) continue;
    for (const QJsonValue& cv : sv.toObject().value("claims").toArray()) out << cv.toObject().value("text").toString();
  }
  return out;
}

bool anyContains(const QStringList& list, const QString& part) {
  for (const QString& s : list)
    if (s.contains(part)) return true;
  return false;
}

}  // namespace

class TestAssessmentSkeleton : public QObject {
  Q_OBJECT
 private slots:
  void passesTheContractWithoutAnyAi() {
    Fixture f;
    const Result r = build(f.ledger, f.snap);
    const QStringList problems = AssessmentContract::validateAssessment(r.assessment, r.evidence, f.ledger.uncollectedDomains,
                                                                        AssessmentContract::gisIdsOf(r.facts));
    QVERIFY2(problems.isEmpty(), qUtf8Printable(problems.join(QLatin1Char('\n'))));
    for (const QJsonValue& e : r.evidence) {
      const QStringList bad = AssessmentContract::validateEvidenceItem(e.toObject());
      QVERIFY2(bad.isEmpty(), qUtf8Printable(bad.join(QLatin1Char('\n'))));
    }
  }

  void writesTheSixChaptersInOrder() {
    Fixture f;
    const Result r = build(f.ledger, f.snap);
    QStringList keys;
    for (const QJsonValue& sv : r.assessment.value("sections").toArray()) keys << sv.toObject().value("key").toString();
    QCOMPARE(keys, (QStringList{QStringLiteral("summary"), QStringLiteral("survey"), QStringLiteral("sites"),
                                QStringLiteral("siteCards"), QStringLiteral("gaps"), QStringLiteral("references")}));
  }

  void summaryStatesOnlyMeasuredFacts() {
    Fixture f;
    const QStringList s = claimTexts(build(f.ledger, f.snap).assessment, QStringLiteral("summary"));
    QVERIFY(anyContains(s, QStringLiteral("1840")));
    QVERIFY(anyContains(s, QStringLiteral("반경 500m")));
    QVERIFY(anyContains(s, QStringLiteral("4건")));
    QVERIFY(anyContains(s, QStringLiteral("가장 가까운 주변유적은 황남동 유적")));
    QVERIFY(anyContains(s, QStringLiteral("보고서 근거를 확인한 유적은 1건")));
  }

  void evidenceOnlyForSitesWithReportPages() {
    Fixture f;
    const Result r = build(f.ledger, f.snap);
    QCOMPARE(r.evidence.size(), 1);
    const QJsonObject e = r.evidence.at(0).toObject();
    QCOMPARE(e.value("id").toString(), QStringLiteral("E1"));
    QCOMPARE(e.value("status").toString(), QStringLiteral("same_report"));
    QCOMPARE(e.value("page").toInt(), 78);
    QCOMPARE(e.value("siteRef").toString(), QStringLiteral("황남동 유적"));
    QVERIFY(e.value("text").toString().contains(QStringLiteral("구덩이 3기")));
  }

  void missingReportsAreSaidPlainly() {
    Fixture f;
    const QStringList cards = claimTexts(build(f.ledger, f.snap).assessment, QStringLiteral("siteCards"));
    QVERIFY(anyContains(cards, QStringLiteral("구황동 유적: 이 PC의 보고서에서 근거를 찾지 못했다")));
    QVERIFY(anyContains(cards, QStringLiteral("공식 서지도 없다")));  // 월성: 서지도 없음
    QVERIFY(anyContains(cards, QStringLiteral("인왕동 유적: 스캔본이라 직접 대조가 필요하다")));
  }

  void lockedDomainsBecomeGapsNotNumbers() {
    Fixture f;
    const Result r = build(f.ledger, f.snap);
    const QStringList survey = claimTexts(r.assessment, QStringLiteral("survey"));
    QVERIFY(anyContains(survey, QStringLiteral("토양 자료가 없어 확인하지 못했다")));
    QVERIFY(anyContains(survey, QStringLiteral("해발")));
    const QJsonArray gaps = r.assessment.value("gaps").toArray();
    QVERIFY(gaps.size() >= 3);  // 잠긴 도메인 2 + 보고서를 못 찾은 유적들
  }

  void emptyRadiusStillProducesAValidAssessment() {
    Fixture f;
    f.snap.siteEntries.clear();
    f.ledger.cards = {f.ledger.cards.first()};
    const Result r = build(f.ledger, f.snap);
    const QStringList problems = AssessmentContract::validateAssessment(r.assessment, r.evidence, f.ledger.uncollectedDomains,
                                                                        AssessmentContract::gisIdsOf(r.facts));
    QVERIFY2(problems.isEmpty(), qUtf8Printable(problems.join(QLatin1Char('\n'))));
    QVERIFY(anyContains(claimTexts(r.assessment, QStringLiteral("summary")), QStringLiteral("주변유적을 찾지 못했다")));
  }

  void sameInputGivesSameOutput() {
    Fixture f;
    const Result a = build(f.ledger, f.snap);
    const Result b = build(f.ledger, f.snap);
    QCOMPARE(QJsonDocument(a.assessment).toJson(), QJsonDocument(b.assessment).toJson());
    QCOMPARE(QJsonDocument(a.evidence).toJson(), QJsonDocument(b.evidence).toJson());
  }
};

QTEST_GUILESS_MAIN(TestAssessmentSkeleton)

#include "test_assessment_skeleton.moc"
