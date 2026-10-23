#include "core/HwpAcademicVisuals.h"
#include "core/HwpSurveyReportService.h"
#include "core/LocalReportRag.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class TestHwpSurveyReport : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  void buildReportAndHtmlPreviewWithoutRagBlocking() {
    HwpSurveyReportService::SurveySnapshot snap =
        HwpSurveyReportService::ReportDocumentSession::collectFromProject(nullptr, false, {});
    snap.surveyTitle = QStringLiteral("경주 황남동 120번지 유적 시굴조사");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    snap.totalAreaSqm = 2450.5;
    snap.surveyPolygonCount = 1;
    snap.domainCounts = {
        {QStringLiteral("survey_area"), QStringLiteral("조사구역"), 1},
        {QStringLiteral("trial_trench"), QStringLiteral("트렌치"), 3},
        {QStringLiteral("feature_poly"), QStringLiteral("유구(면)"), 4},
        {QStringLiteral("artifact_point"), QStringLiteral("유물(점)"), 11},
    };

    NearbyHeritageExport::Item site;
    site.datasetName = QStringLiteral("국가유산 공간정보");
    site.siteName = QStringLiteral("경주 황남대총");
    site.distanceMeters = 185;
    site.direction = QStringLiteral("남동");
    site.radiusLabel = QStringLiteral("500m 이내");
    snap.nearbySites.push_back(site);

    HwpSurveyReportService::ReportDocumentSession session;
    QString err;
    QVERIFY2(session.buildStandardReport(snap, &err), qPrintable(err));

    const auto fields = session.fields();
    QVERIFY(fields.size() >= 5);

    const QString html = session.renderPreviewHtml();
    QVERIFY(html.contains(QStringLiteral("경주 황남동 120번지 유적 시굴조사")));
    QVERIFY(html.contains(QStringLiteral("경주 황남대총")));
    QVERIFY(html.contains(QStringLiteral("RAG 연결 슬롯 대기 중")));
  }

  void writeAndReloadHwpxAndHwp5() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    HwpSurveyReportService::SurveySnapshot snap =
        HwpSurveyReportService::ReportDocumentSession::collectFromProject(nullptr, false, {});
    snap.surveyTitle = QStringLiteral("부여 관북리 유적 발굴조사");
    snap.surveyCrs = QStringLiteral("EPSG:5186");
    snap.totalAreaSqm = 1280.0;
    snap.surveyPolygonCount = 1;

    HwpSurveyReportService::ReportDocumentSession session;
    QString err;
    QVERIFY2(session.buildStandardReport(snap, &err), qPrintable(err));

    const QString hwpxPath = dir.filePath(QStringLiteral("survey_report.hwpx"));
    QVERIFY2(session.saveAs(std::filesystem::path(hwpxPath.toStdString()), QStringLiteral("HWPX"), &err),
             qPrintable(err));
    QVERIFY(QFileInfo::exists(hwpxPath));
    QVERIFY(QFileInfo(hwpxPath).size() > 256);

    const QString hwpPath = dir.filePath(QStringLiteral("survey_report.hwp"));
    QVERIFY2(session.saveAs(std::filesystem::path(hwpPath.toStdString()), QStringLiteral("HWP"), &err),
             qPrintable(err));
    QVERIFY(QFileInfo::exists(hwpPath));
    QVERIFY(QFileInfo(hwpPath).size() > 512);

    HwpSurveyReportService::SurveySnapshot updated = snap;
    updated.surveyTitle = QStringLiteral("부여 관북리 유적 정밀발굴조사 (수정본)");
    HwpSurveyReportService::ReportDocumentSession tplSession;
    QVERIFY2(tplSession.openTemplateAndFill(std::filesystem::path(hwpxPath.toStdString()), updated, &err),
             qPrintable(err));
    QVERIFY(tplSession.renderPreviewHtml().contains(QStringLiteral("부여 관북리 유적 정밀발굴조사 (수정본)")));

    QString reply;
    QVERIFY(tplSession.applyUserRequest(
        QStringLiteral("고찰에 백제 사비기 왕궁지와의 공간적 위계 관계를 추가해줘"), updated, &reply));
    QVERIFY(tplSession.renderPreviewHtml().contains(QStringLiteral("백제 사비기 왕궁지")));
    QVERIFY(tplSession.applyUserRequest(
        QStringLiteral("'왕궁지'를 '핵심 유구군'으로 바꿔줘"), updated, &reply));
    QVERIFY(tplSession.renderPreviewHtml().contains(QStringLiteral("핵심 유구군")));
  }

  void gisBibliographyFillsRowsWithoutLocalText() {
    HwpSurveyReportService::SurveySnapshot snap;
    snap.surveyTitle = QStringLiteral("경주 성동동 150-3번지 유적 시굴조사");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    snap.regionName = QStringLiteral("경주시");
    NearbyHeritageExport::Item a;
    a.datasetName = QStringLiteral("발굴조사구역");
    a.siteName = QStringLiteral("경주 성동동 150-3번지 유적");
    a.direction = QStringLiteral("동");
    a.distanceMeters = 320;
    a.heritageCode = QStringLiteral("EX1815950001");
    a.reportName = QStringLiteral("화랑문화유산연구원, 2019, 경주 성동동 150-3번지 유적 발굴조사 보고서");
    a.institution = QStringLiteral("화랑문화유산연구원");
    snap.nearbySites.push_back(a);
    NearbyHeritageExport::Item b;
    b.datasetName = QStringLiteral("매장유산유존지역");
    b.siteName = QStringLiteral("경주 황남동 유적");
    b.direction = QStringLiteral("북");
    b.distanceMeters = 80;
    b.heritageCode = QStringLiteral("RH0000015824");
    b.reportName = QStringLiteral("영남문화재연구원, 2002, 경주 황남동 유적 발굴조사 보고서");
    b.institution = QStringLiteral("영남문화재연구원");
    snap.nearbySites.push_back(b);

    HwpSurveyReportService::ReportDocumentSession::populateRagEvidence(&snap, false);
    QCOMPARE(snap.siteEntries.size(), 2);
    for (const auto& row : snap.siteEntries) {
      QCOMPARE(row.reportStatus, QStringLiteral("○"));
      QVERIFY(!row.bibliography.isEmpty());
      QVERIFY(row.relationToSurvey.contains(QStringLiteral("GIS 공식 서지")));
      QVERIFY(row.reportYear > 0);
    }
    QCOMPARE(snap.siteEntries[0].reportYear, 2019);
    QCOMPARE(snap.siteEntries[1].reportYear, 2002);

    HwpSurveyReportService::ReportDocumentSession session;
    QString err;
    QVERIFY2(session.buildStandardReport(snap, &err), qPrintable(err));
    const QString html = session.renderPreviewHtml();
    QVERIFY(html.contains(QStringLiteral("GIS 보고서명(공식 서지)")));
    QVERIFY(html.contains(QStringLiteral("화랑문화유산연구원, 2019")));
    QVERIFY(html.contains(QStringLiteral("영남문화재연구원, 2002")));
  }

  void scanReportKeepsBibliographyAndDoesNotBorrowAnotherYear() {
    if (!std::filesystem::exists(strata::core::LocalReportRag::ragDbPath()))
      QSKIP("Local RAG DB not mounted on this machine");
    HwpSurveyReportService::SurveySnapshot snap;
    snap.surveyTitle = QStringLiteral("경주 성동동 150-3번지 유적 정밀발굴조사");
    snap.regionName = QStringLiteral("경주시");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    NearbyHeritageExport::Item a;
    a.datasetName = QStringLiteral("발굴조사구역");
    a.siteName = QStringLiteral("경주 성동동 150-3번지 유적");
    a.direction = QStringLiteral("조사구역 내");
    a.heritageCode = QStringLiteral("EX1815950001");
    a.projectName = QStringLiteral("경주 성동동 150-3번지 유적");
    a.siteAddress = QStringLiteral("경상북도 경주시 성동동 150-3번지 일원");
    a.reportName = QStringLiteral("화랑문화유산연구원, 2019, 경주 성동동 150-3번지 유적 발굴조사 보고서");
    a.institution = QStringLiteral("화랑문화유산연구원");
    snap.nearbySites.push_back(a);
    NearbyHeritageExport::Item b = a;
    b.siteName = QStringLiteral("경주 성동동 150-3번지 유적(2002)");
    b.heritageCode = QStringLiteral("RH0000015824");
    b.reportName = QStringLiteral("영남문화재연구원, 2002, 경주 성동동 150-3번지 유적 발굴조사 보고서");
    b.institution = QStringLiteral("영남문화재연구원");
    snap.nearbySites.push_back(b);

    HwpSurveyReportService::ReportDocumentSession::populateRagEvidence(&snap, true);
    QCOMPARE(snap.siteEntries.size(), 2);
    const auto& scan = snap.siteEntries[0];
    // 스캔본은 ◌, OCR로 색인된 원문은 ✓다. 어느 쪽이든 2019 서지를 유지해야 한다.
    QVERIFY(scan.reportStatus == QStringLiteral("◌") || scan.reportStatus == QStringLiteral("✓"));
    QVERIFY(scan.reportName.contains(QStringLiteral("성동동 150-3")));
    QVERIFY(scan.bibliography.contains(QStringLiteral("2019")));
    QVERIFY(scan.relationToSurvey.contains(QStringLiteral("성동동 150-3")));
    QVERIFY(!scan.relationToSurvey.contains(QStringLiteral("2002")));
    const auto& missed = snap.siteEntries[1];
    QCOMPARE(missed.reportStatus, QStringLiteral("○"));
    QVERIFY(missed.reportName.isEmpty());  // 2002 보고서를 2019 원문에 붙이지 않는다.
    QVERIFY(missed.bibliography.contains(QStringLiteral("2002")));
  }

  void gyeongjuRagEvidenceAndStratigraphyVisuals() {
    if (!std::filesystem::exists(strata::core::LocalReportRag::ragDbPath()))
      QSKIP("Local RAG DB not mounted on this machine");

    HwpSurveyReportService::SurveySnapshot snap;
    snap.surveyTitle = QStringLiteral("경주 황남동 120번지 유적 정밀발굴조사");
    snap.surveyCrs = QStringLiteral("EPSG:5187");
    snap.regionName = QStringLiteral("경주시");
    snap.radiusMeters = 500.0;
    snap.totalAreaSqm = 1840.0;
    snap.surveyPolygonCount = 1;
    snap.domainCounts = {
        {QStringLiteral("survey_area"), QStringLiteral("조사구역"), 1},
        {QStringLiteral("feature_poly"), QStringLiteral("유구(면)"), 6},
    };

    NearbyHeritageExport::Item s1;
    s1.datasetName = QStringLiteral("국가유산 공간정보");
    s1.siteName = QStringLiteral("경주 황남동 유적");
    s1.distanceMeters = 142;
    s1.direction = QStringLiteral("북서");
    snap.nearbySites.push_back(s1);

    NearbyHeritageExport::Item s2;
    s2.datasetName = QStringLiteral("국가유산 공간정보");
    s2.siteName = QStringLiteral("경주 구황동 867-4번지 유적");
    s2.distanceMeters = 380;
    s2.direction = QStringLiteral("동");
    snap.nearbySites.push_back(s2);

    HwpSurveyReportService::ReportDocumentSession::populateRagEvidence(&snap, true);
    QVERIFY2(snap.regionTotalReports >= 800, qPrintable(QString::number(snap.regionTotalReports)));
    QCOMPARE(snap.siteEntries.size(), 2);
    QCOMPARE(snap.siteEntries[0].reportStatus, QStringLiteral("✓"));
    QVERIFY(snap.siteEntries[0].pageNo > 0);
    QVERIFY(snap.siteEntries[0].relationToSurvey.contains(QStringLiteral("142m")));
    QCOMPARE(snap.siteEntries[1].reportStatus, QStringLiteral("◌"));

    HwpSurveyReportService::ReportDocumentSession session;
    QString err;
    QVERIFY2(session.buildStandardReport(snap, &err), qPrintable(err));

    HwpAcademicVisuals::DbpiaSummary dbpia;
    const QString visuals = HwpAcademicVisuals::renderVisualsAndDbpiaSectionHtml(snap, dbpia);
    QVERIFY(visuals.contains(QStringLiteral("주변유적 층위 대비도")));
    QVERIFY(visuals.contains(QStringLiteral("조사구역 (예상)")));

    const QString verifyHtml = HwpAcademicVisuals::renderVerificationReportHtml(snap, dbpia);
    QVERIFY(verifyHtml.contains(QStringLiteral("검수 보고")));
    QVERIFY(verifyHtml.contains(QStringLiteral("◌ 스캔본")));

    const QString compareHtml =
        HwpAcademicVisuals::renderFeatureComparisonHtml(snap, QStringLiteral("적석목곽묘 주거지 가마"));
    QVERIFY(compareHtml.contains(QStringLiteral("단일 유구 비교")));
    QVERIFY(compareHtml.contains(QStringLiteral("경주시")));
    QVERIFY(session.renderPreviewHtml().contains(QStringLiteral("동국문화재연구원 보고서 작성 기준")));

    const QString jeonnamHtml = HwpAcademicVisuals::renderFeatureComparisonHtml(
        snap, QStringLiteral("전남지역 원형주거지와 방형주거지의 현황"));
    QVERIFY(jeonnamHtml.contains(QStringLiteral("전남")));
    QVERIFY(jeonnamHtml.contains(QStringLiteral("주거지")));

    const QString paleoHtml = HwpAcademicVisuals::renderFeatureComparisonHtml(
        snap, QStringLiteral("전국의 후기구석기의 분포"));
    QVERIFY(paleoHtml.contains(QStringLiteral("전국")));
    QVERIFY(paleoHtml.contains(QStringLiteral("구석기")));

    const QString buildingHtml = HwpAcademicVisuals::renderFeatureComparisonHtml(
        snap, QStringLiteral("지역별 시대별 건물지의 크기 변화"));
    QVERIFY(buildingHtml.contains(QStringLiteral("전국")));
    QVERIFY(buildingHtml.contains(QStringLiteral("건물지")));

    QString reply;
    QVERIFY(session.applyUserRequest(QStringLiteral("전남지역 원형주거지와 방형주거지의 현황"), snap, &reply));
    QVERIFY(reply.contains(QStringLiteral("전남")));

    // 6대 도별(경상도·전라도·경기도·충청도·강원도·제주도) 및 하위 시·군(나주시·수원시) 2단계 폴더 검증
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("경상도").total_reports, 4272);
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("경기도").total_reports, 2845);
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("충청도").total_reports, 2013);
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("전라도").total_reports, 1437);
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("강원도").total_reports, 683);
    QCOMPARE(strata::core::LocalReportRag::queryRegionStats("제주도").total_reports, 373);
    const auto najuHits = strata::core::LocalReportRag::search("주거지", "나주시", 4, 2);
    QVERIFY(!najuHits.empty());
    QCOMPARE(QString::fromStdString(najuHits.front().sigungu), QStringLiteral("나주시"));
    QVERIFY(QString::fromStdString(najuHits.front().txt_path.string()).contains(QStringLiteral("전라도/나주시/")));
  }
};

QTEST_MAIN(TestHwpSurveyReport)
#include "test_hwp_survey_report.moc"
