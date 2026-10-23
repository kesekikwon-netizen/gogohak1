#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include "core/BufferAnalysis.h"
#include "core/HeritageFetchPlan.h"
#include "core/HeritageLayoutNumbers.h"
#include "core/HeritagePledge.h"
#include "core/HeritageRecentDownloads.h"
#include "core/HeritageStyle.h"
#include "core/LayerOps.h"
#include "core/NearbyHeritageExport.h"
#include <qgscoordinatereferencesystem.h>
#include <qgsfeature.h>
#include <qgsfield.h>
#include <qgsgeometry.h>
#include <qgsrectangle.h>

// F120 / F169 / F174 core rules, without the intranet or WebEngine.
class HeritageFetchPlanTest : public QObject {
  Q_OBJECT
  static QString writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) return {};
    return QFileInfo(path).absoluteFilePath();
  }

private slots:
  void followUpsKeepOrderAndNeverRepeatTheFirstCity() {
    const HeritageCity first{QStringLiteral("경상북도"), QStringLiteral("안동시")};
    const QList<HeritageCity> picked = {
        {QStringLiteral("경북"), QStringLiteral("예천군")},
        {QStringLiteral("경상북도"), QStringLiteral("안동시")},
        {QStringLiteral("경상북도"), QStringLiteral("의성군")},
        {QStringLiteral("경상북도"), QStringLiteral("예천군")},
        {QStringLiteral("경상북도"), QString()},
    };
    const QList<HeritageCity> out = HeritageFetchPlanning::normalizedFollowUps(first, picked);
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).display(), QStringLiteral("경상북도 예천군"));
    QCOMPARE(out.at(1).display(), QStringLiteral("경상북도 의성군"));
    const HeritageCity sejong{QStringLiteral("세종특별자치시"), QStringLiteral("세종특별자치시")};
    QCOMPARE(sejong.display(), QStringLiteral("세종특별자치시"));
  }

  void nextCityGoesNextToTheCurrentOne() {
    const HeritageCity next{QStringLiteral("경상북도"), QStringLiteral("예천군")};
    QCOMPARE(HeritageFetchPlanning::siblingRoot(QStringLiteral("C:/data/주변유적/경상북도 안동시/원본"), next),
             QStringLiteral("C:/data/주변유적/경상북도 예천군/원본"));
    QCOMPARE(HeritageFetchPlanning::siblingRoot(QStringLiteral("C:/data/downloads"), next),
             QStringLiteral("C:/data/downloads/경상북도 예천군"));
  }

  void recentDownloadIsReusedOnlyWhileFreshAndComplete() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString root = temp.filePath(QStringLiteral("주변유적/경상북도 안동시/원본"));
    const QString zip = writeFile(QDir(root).filePath(QStringLiteral("a/지정유산.zip")), "PK-fixture");
    QVERIFY(!zip.isEmpty());
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(!HeritageRecentDownloads::latest(root, 30, now).isValid());
    QVERIFY(HeritageRecentDownloads::record(root, HeritageDataset::DesignatedHeritage, {zip},
                                            now.addDays(-2)));
    QCOMPARE(HeritageRecentDownloads::find(root, HeritageDataset::DesignatedHeritage, 30, now),
             QStringList{zip});
    QVERIFY(HeritageRecentDownloads::find(root, HeritageDataset::SurfaceSurveyArea, 30, now).isEmpty());
    QVERIFY(HeritageRecentDownloads::latest(root, 30, now).isValid());
    // Too old: not offered.
    QVERIFY(HeritageRecentDownloads::find(root, HeritageDataset::DesignatedHeritage, 1, now).isEmpty());
    // A file that disappeared or became empty is never reused.
    QFile(zip).resize(0);
    QVERIFY(HeritageRecentDownloads::find(root, HeritageDataset::DesignatedHeritage, 30, now).isEmpty());
    QVERIFY(!HeritageRecentDownloads::latest(root, 30, now).isValid());
    // The default root follows the download layout of the confirm flow.
    QVERIFY(HeritageRecentDownloads::defaultRoot({QStringLiteral("경상북도"), QStringLiteral("안동시")})
                .endsWith(QStringLiteral("주변유적/경상북도 안동시/원본")));
  }

  void pledgeHighlightsQuoteOnlyTheText() {
    const QString terms = QStringLiteral(
        "국가유산 공간정보 원본자료 사용 서약서\n"
        "본인은 제공받은 자료를 신청한 목적 외에는 사용하지 않겠습니다. "
        "자료를 제3자에게 제공하거나 외부로 반출하지 않겠습니다.\n"
        "위 사항을 위반할 경우 관련 법령에 따른 책임을 지겠습니다.");
    const QStringList lines = HeritagePledge::highlights(terms);
    QCOMPARE(lines.size(), 2);
    QVERIFY(terms.contains(lines.at(0)));
    QVERIFY(terms.contains(lines.at(1)));
    QVERIFY(lines.at(1).contains(QStringLiteral("제3자")));
    const QString notice = HeritagePledge::noticeAfterAgreement(terms);
    QVERIFY(notice.contains(QStringLiteral("receipts")));
    QVERIFY(notice.contains(lines.at(0)));
    QVERIFY(HeritagePledge::noticeAfterAgreement(QString()).contains(QStringLiteral("원문을 확인")));
    // The site's page text can arrive without a single space; the notice must still wrap inside the
    // 680 px download window instead of stretching it (2026-10-03 「창이 너무 크지않나」): no run of more
    // than 20 characters without a break point, and the visible characters stay verbatim.
    const QString packed = QStringLiteral("국가유산공간정보원본자료를신청한목적외에는사용하지않고외부로반출하지않을것을서약합니다.");
    const QString wrapped = HeritagePledge::noticeAfterAgreement(packed);
    for (const QString& run : wrapped.split(QRegularExpression(QStringLiteral("[\\s\\x{200B}]")))) QVERIFY2(run.size() <= 20, qPrintable(run));
    QVERIFY(QString(wrapped).remove(QChar(0x200B)).contains(packed));
    QVERIFY(HeritagePledge::preDisclosure().contains(QStringLiteral("자동으로 동의")));
  }

  void checklistGuidanceFollowsIntranetLayers() {
    QgsProject project;
    QVERIFY(HeritagePledge::checklistPasses(&project));
    auto* plain = new QgsVectorLayer(QStringLiteral("Point?crs=EPSG:5186"), QStringLiteral("참조"),
                                     QStringLiteral("memory"));
    project.addMapLayer(plain);
    QVERIFY(HeritagePledge::checklistPasses(&project));
    // Projects saved before the layer property: a layer inside a heritage kind group.
    auto* legacy = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5186"),
                                      QStringLiteral("국가지정유산"), QStringLiteral("memory"));
    project.addMapLayer(legacy, false);
    auto* group = project.layerTreeRoot()->addGroup(QStringLiteral("참조 지도"))->addGroup(QStringLiteral("지정유산"));
    group->addLayer(legacy);
    QVERIFY(!HeritagePledge::checklistPasses(&project));
    // The legacy layer itself carries no tag: only its group says it is intranet data.
    QVERIFY(!HeritagePledge::isIntranetLayer(legacy));
    QCOMPARE(HeritagePledge::checklistStateKey(), QStringLiteral("HERITAGE_PLEDGE_SCOPE"));
  }

  void nearbyHeritageExcelExport_writesSiteNameDirectionAndDistance() {
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5187")));
    auto* area = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"), QStringLiteral("조사구역"),
                                    QStringLiteral("memory"));
    LayerOps::markSurveyLayer(area, QStringLiteral("survey_area"));
    QgsFeature af(area->fields());
    af.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200020, 450020)));
    area->dataProvider()->addFeature(af);
    project.addMapLayer(area);

    auto* buried = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"),
                                      QStringLiteral("매장유산유존지역"), QStringLiteral("memory"));
    buried->dataProvider()->addAttributes({QgsField(QStringLiteral("유적명"), QMetaType::Type::QString)});
    buried->updateFields();
    HeritageLayoutNumbers::tagDataset(buried, HeritageDataset::BuriedHeritageArea);
    auto addSite = [&](const QString& name, const QgsRectangle& rect) {
      QgsFeature f(buried->fields());
      f.setGeometry(QgsGeometry::fromRect(rect));
      f.setAttribute(0, name);
      buried->dataProvider()->addFeature(f);
    };
    addSite(QStringLiteral("광령리 중첩유적"), QgsRectangle(200005, 450005, 200015, 450015));
    addSite(QStringLiteral("광령리 북쪽유적"), QgsRectangle(200005, 450270, 200015, 450280));
    addSite(QStringLiteral("광령리 동쪽유적"), QgsRectangle(200820, 450005, 200830, 450015));
    addSite(QStringLiteral("광령리 원거리유적"), QgsRectangle(200005, 448400, 200015, 448410));
    project.addMapLayer(buried);

    QString err;
    QVERIFY2(BufferAnalysis::addDistanceRing(&project, nullptr, nullptr, 500.0, &err) ||
                 BufferAnalysis::addDistanceRing(&project, nullptr, area, 500.0, &err),
             qPrintable(err));
    QCOMPARE(NearbyHeritageExport::activeMaxRadiusMeters(&project), 500.0);
    const auto r500 = NearbyHeritageExport::collect(&project);
    QVERIFY2(r500.ok(), qPrintable(r500.error));
    QCOMPARE(r500.items.size(), 2);
    QCOMPARE(r500.items.at(0).siteName, QStringLiteral("광령리 중첩유적"));
    QCOMPARE(r500.items.at(0).distanceMeters, 0);
    QCOMPARE(r500.items.at(0).direction, QStringLiteral("조사구역 내"));
    QCOMPARE(r500.items.at(1).siteName, QStringLiteral("광령리 북쪽유적"));
    QCOMPARE(r500.items.at(1).direction, QStringLiteral("북"));
    QCOMPARE(r500.items.at(1).distanceMeters, 250);

    QVERIFY2(BufferAnalysis::addDistanceRing(&project, nullptr, area, 1000.0, &err), qPrintable(err));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString xlsx = dir.filePath(QStringLiteral("주변유적_현황_1000m.xlsx"));
    const auto r1000 = NearbyHeritageExport::exportToXlsx(&project, xlsx);
    QVERIFY2(r1000.ok(), qPrintable(r1000.error));
    QCOMPARE(r1000.items.size(), 3);
    QCOMPARE(r1000.items.at(2).siteName, QStringLiteral("광령리 동쪽유적"));
    QCOMPARE(r1000.items.at(2).direction, QStringLiteral("동"));
    QCOMPARE(r1000.items.at(2).distanceMeters, 800);
    QCOMPARE(r1000.items.at(2).radiusLabel, QStringLiteral("1000m 이내"));

    QgsVectorLayer sheet(xlsx + QStringLiteral("|layername=주변유적_현황"), QStringLiteral("xlsx"),
                         QStringLiteral("ogr"));
    QVERIFY(sheet.isValid());
    QCOMPARE(int(sheet.featureCount()), 3);
    QVERIFY(sheet.fields().indexOf(QStringLiteral("보고서명(서지)")) >= 0);
    QVERIFY(sheet.fields().indexOf(QStringLiteral("유산코드")) >= 0);

    auto* area2 = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"), QStringLiteral("조사구역 2"),
                                     QStringLiteral("memory"));
    LayerOps::markSurveyLayer(area2, QStringLiteral("survey_area_2"));
    area2->dataProvider()->addFeature(af);
    project.addMapLayer(area2);
    if (auto* n1 = project.layerTreeRoot()->findLayer(area->id())) n1->setItemVisibilityChecked(false);
    QCOMPARE(NearbyHeritageExport::resolveSourceLayer(&project, nullptr), area2);
  }

  void nearbyHeritageKeepsSameNameSeparateAndCarriesGisBibliography() {
    QgsProject project;
    project.setCrs(QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5187")));
    auto* area = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"), QStringLiteral("조사구역"),
                                    QStringLiteral("memory"));
    LayerOps::markSurveyLayer(area, QStringLiteral("survey_area"));
    QgsFeature af(area->fields());
    af.setGeometry(QgsGeometry::fromRect(QgsRectangle(200000, 450000, 200100, 450100)));
    area->dataProvider()->addFeature(af);
    project.addMapLayer(area);

    auto* buried = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"),
                                      QStringLiteral("매장유산유존지역"), QStringLiteral("memory"));
    buried->dataProvider()->addAttributes({QgsField(QStringLiteral("유산코드"), QMetaType::Type::QString),
                                           QgsField(QStringLiteral("유적명"), QMetaType::Type::QString)});
    buried->updateFields();
    HeritageLayoutNumbers::tagDataset(buried, HeritageDataset::BuriedHeritageArea);
    auto addBuried = [&](const QString& code, const QString& name, const QgsRectangle& rect) {
      QgsFeature f(buried->fields());
      f.setGeometry(QgsGeometry::fromRect(rect));
      f.setAttribute(0, code);
      f.setAttribute(1, name);
      buried->dataProvider()->addFeature(f);
    };
    addBuried(QStringLiteral("RH1"), QStringLiteral("조사구역내 유적"), QgsRectangle(200010, 450010, 200020, 450020));
    addBuried(QStringLiteral("RH2"), QStringLiteral("조사구역내 유적"), QgsRectangle(200030, 450030, 200040, 450040));
    project.addMapLayer(buried);

    auto* excavation = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:5187"),
                                          QStringLiteral("발굴조사구역"), QStringLiteral("memory"));
    excavation->dataProvider()->addAttributes(
        {QgsField(QStringLiteral("유산코드"), QMetaType::Type::QString),
         QgsField(QStringLiteral("사업명"), QMetaType::Type::QString),
         QgsField(QStringLiteral("유적명"), QMetaType::Type::QString),
         QgsField(QStringLiteral("보고서명"), QMetaType::Type::QString),
         QgsField(QStringLiteral("조사기관"), QMetaType::Type::QString),
         QgsField(QStringLiteral("소재지"), QMetaType::Type::QString)});
    excavation->updateFields();
    HeritageLayoutNumbers::tagDataset(excavation, HeritageDataset::ExcavationSurveyArea);
    QgsFeature ef(excavation->fields());
    ef.setGeometry(QgsGeometry::fromRect(QgsRectangle(200050, 450050, 200060, 450060)));
    ef.setAttribute(0, QStringLiteral("EX1"));
    ef.setAttribute(1, QStringLiteral("경주 성동동 150-3번지 유적"));
    ef.setAttribute(2, QStringLiteral("A지구"));
    ef.setAttribute(3, QStringLiteral("화랑문화유산연구원, 2019, 경주 성동동 150-3번지 유적 발굴조사 보고서"));
    ef.setAttribute(4, QStringLiteral("화랑문화유산연구원"));
    ef.setAttribute(5, QStringLiteral("경상북도 경주시 성동동 150-3번지 일원"));
    excavation->dataProvider()->addFeature(ef);
    project.addMapLayer(excavation);

    const auto result = NearbyHeritageExport::collect(&project, nullptr, 500.0);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.items.size(), 3);  // 같은 이름이어도 유산코드가 다르면 합치지 않는다.
    int buriedSameName = 0, excavationSeen = 0;
    for (const auto& item : result.items) {
      if (item.datasetName == HeritageStyle::layerName(HeritageDataset::BuriedHeritageArea)) {
        QCOMPARE(item.siteName, QStringLiteral("조사구역내 유적"));
        ++buriedSameName;
      }
      if (item.heritageCode == QStringLiteral("EX1")) {
        ++excavationSeen;
        QCOMPARE(item.siteName, QStringLiteral("경주 성동동 150-3번지 유적"));  // 유적명(A지구)이 아니라 사업명
        QCOMPARE(item.featureName, QStringLiteral("A지구"));
        QCOMPARE(item.institution, QStringLiteral("화랑문화유산연구원"));
        QVERIFY(item.reportName.contains(QStringLiteral("2019")));
      }
    }
    QCOMPARE(buriedSameName, 2);
    QCOMPARE(excavationSeen, 1);
  }
};

int main(int argc, char** argv) {
  QgsApplication app(argc, argv, false);
  QgsApplication::setPrefixPath(
      qEnvironmentVariable("QGIS_PREFIX_PATH", "D:/OSGeo4W/apps/qgis-dev"), true);
  QgsApplication::initQgis();
  QStandardPaths::setTestModeEnabled(true);
  HeritageFetchPlanTest test;
  const int result = QTest::qExec(&test, argc, argv);
  QgsApplication::exitQgis();
  return result;
}
#include "test_heritage_fetch_plan.moc"

