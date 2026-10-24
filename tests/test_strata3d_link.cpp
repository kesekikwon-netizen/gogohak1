// Strata ↔ strata-3d 연동(core/Strata3dLink) 시험.
// 앞의 셋은 프로토콜 v1 을 흉내 내는 서버로, 마지막은 실제 strata-3d.exe 를 띄워 왕복을 확인한다.
#include <QtTest>
#include <atomic>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>

#include <qgsproject.h>

#include "app/KaStrata3dLink.h"
#include "core/Strata3dLink.h"

namespace {

QByteArray fileHash(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

// strata-3d 의 파이프 응답 모양(프로토콜 v1). 받은 요청 이름을 순서대로 적어 둔다.
struct FakeBridge {
  QLocalServer server;
  QStringList cmds;
  QString gpkg;
  explicit FakeBridge(const QString& name) {
    QLocalServer::removeServer(name);
    server.listen(name);
    QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
      QLocalSocket* s = server.nextPendingConnection();
      QObject::connect(s, &QLocalSocket::readyRead, s, [this, s] {
        while (s->canReadLine()) {
          const QJsonObject req = QJsonDocument::fromJson(s->readLine()).object();
          const QString cmd = req.value("cmd").toString();
          cmds << cmd;
          QJsonObject r{{"ok", true}, {"protocol", 1}};
          if (cmd == QLatin1String("openSurvey")) {
            gpkg = req.value("gpkg").toString();
            r["survey"] = QJsonObject{{"layers", QJsonArray{"survey_area", "trial_trench"}}};
          } else if (cmd == QLatin1String("evaluate")) {
            r["evaluation"] = QJsonObject{{"summary", "반경 500 m 안 유산 3건"}, {"crs", "EPSG:5186"}};
          } else if (cmd == QLatin1String("renderSheet")) {
            const QString png = req.value("png").toString();
            QFile out(png);
            if (out.open(QIODevice::WriteOnly)) out.write("png-bytes");
            r["sheet"] = QJsonObject{{"png", png}, {"pdf", req.value("pdf").toString()}};
          }
          s->write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
        }
      });
    });
  }
};

// 블로킹 연동 호출을 워커에서 돌리고, 서버가 있는 이 스레드는 이벤트 루프를 돌린다.
template <typename F>
Strata3dLink::Outcome runBlocking(F fn) {
  Strata3dLink::Outcome out;
  std::atomic<bool> done{false};
  QThread* th = QThread::create([&] {
    out = fn();
    done = true;
  });
  th->start();
  QElapsedTimer t;
  t.start();
  while (!done && t.elapsed() < 60000) QTest::qWait(20);
  th->wait();
  delete th;
  return out;
}

}  // namespace

class TestStrata3dLink : public QObject {
  Q_OBJECT
private slots:
  void findsSurveyFromProjectFile();
  void speaksProtocolToBridge();
  void reportsMissingBridge();
  void studioButtonsRoundTripThroughBridge();
  void realAppRoundTrip();
};

void TestStrata3dLink::findsSurveyFromProjectFile() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QCOMPARE(Strata3dLink::surveyGpkgFromProjectFile(QStringLiteral("geopackage:C:/a/b.gpkg?projectName=x")), QStringLiteral("C:/a/b.gpkg"));
  QCOMPARE(Strata3dLink::surveyGpkgFromProjectFile(QStringLiteral("C:/a/b.GPKG")), QStringLiteral("C:/a/b.GPKG"));
  QCOMPARE(Strata3dLink::surveyGpkgFromProjectFile(QString()), QString());
  const QString qgz = dir.filePath(QStringLiteral("현장.qgz"));
  QVERIFY(QFile(qgz).open(QIODevice::WriteOnly));
  QCOMPARE(Strata3dLink::surveyGpkgFromProjectFile(qgz), QString());  // 옆에 같은 이름 gpkg 가 없으면 빈 값
  const QString side = dir.filePath(QStringLiteral("현장.gpkg"));
  QVERIFY(QFile(side).open(QIODevice::WriteOnly));
  QCOMPARE(Strata3dLink::surveyGpkgFromProjectFile(qgz), side);
}

void TestStrata3dLink::speaksProtocolToBridge() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString gpkg = dir.filePath(QStringLiteral("survey.gpkg"));
  QVERIFY(QFile::copy(QStringLiteral("samples/demo_survey/demo.gpkg"), gpkg));
  const QByteArray before = fileHash(gpkg);
  const QString pipe = QStringLiteral("ka-hgis-test-bridge-%1").arg(QCoreApplication::applicationPid());
  FakeBridge bridge(pipe);
  QVERIFY(bridge.server.isListening());
  const auto r = runBlocking([&] { return Strata3dLink::openAndEvaluate(pipe, QString(), gpkg); });
  QVERIFY2(r.ok, qPrintable(r.error));
  QCOMPARE(bridge.cmds, QStringList({"hello", "openSurvey", "evaluate"}));
  QCOMPARE(QDir::fromNativeSeparators(bridge.gpkg), QDir::fromNativeSeparators(gpkg));
  QCOMPARE(r.evaluationSummary, QStringLiteral("반경 500 m 안 유산 3건"));
  QCOMPARE(r.crs, QStringLiteral("EPSG:5186"));
  QVERIFY(r.surveyMessage.contains(QStringLiteral("레이어 2개")));
  const QString png = dir.filePath(QStringLiteral("도면.png"));
  const auto s = runBlocking([&] { return Strata3dLink::fetchSheet(pipe, png, dir.filePath(QStringLiteral("도면.pdf"))); });
  QVERIFY2(s.ok, qPrintable(s.error));
  QVERIFY(QFileInfo(png).size() > 0);
  QCOMPARE(fileHash(gpkg), before);  // 조사 원본은 건드리지 않는다
}

void TestStrata3dLink::reportsMissingBridge() {
  const QString pipe = QStringLiteral("ka-hgis-test-nobridge-%1").arg(QCoreApplication::applicationPid());
  const auto r = Strata3dLink::openAndEvaluate(pipe, QString(), QStringLiteral("samples/demo_survey/demo.gpkg"));
  QVERIFY(!r.ok);
  QVERIFY2(r.error.contains(QStringLiteral("strata-3d")), qPrintable(r.error));
  const auto none = Strata3dLink::openAndEvaluate(pipe, QString(), QStringLiteral("없는파일.gpkg"));
  QVERIFY(!none.ok);
  QVERIFY(none.error.contains(QStringLiteral(".gpkg")));
}

void TestStrata3dLink::studioButtonsRoundTripThroughBridge() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString gpkg = dir.filePath(QStringLiteral("survey.gpkg"));
  QVERIFY(QFile::copy(QStringLiteral("samples/demo_survey/demo.gpkg"), gpkg));
  const QString pipe = QStringLiteral("ka-hgis-test-ui-%1").arg(QCoreApplication::applicationPid());
  qputenv("STRATA_3D_PIPE", pipe.toUtf8());
  FakeBridge bridge(pipe);
  QVERIFY(bridge.server.isListening());
  // 입체지형 탭 패널과 같은 모양: 위에서부터 위젯, 맨 끝 늘림칸.
  QWidget panel;
  QVBoxLayout vl(&panel);
  vl.addWidget(new QLabel(QStringLiteral("입체지형"), &panel));
  QLabel status(QStringLiteral("대기"), &panel);
  status.setWordWrap(true);
  vl.addWidget(&status);
  vl.addStretch(1);
  QgsProject project;
  project.setFileName(gpkg);
  KaStrata3dLink link(&vl, &status, &project, &panel);
  auto* open = panel.findChild<QPushButton*>(QStringLiteral("strata3dOpenBtn"));
  auto* sheet = panel.findChild<QPushButton*>(QStringLiteral("strata3dSheetBtn"));
  QVERIFY(open && sheet);
  QVERIFY(vl.indexOf(open) < vl.count() - 1);  // 늘림칸 앞에 들어간다
  panel.resize(260, 360);
  panel.show();
  open->click();
  QVERIFY(!open->isEnabled());  // 호출 중에는 눌리지 않는다
  QTRY_VERIFY_WITH_TIMEOUT(status.text().contains(QStringLiteral("반경 500 m 안 유산 3건")), 20000);
  QVERIFY(open->isEnabled() && sheet->isEnabled());
  QVERIFY2(status.text().contains(QStringLiteral("EPSG:5186")), qPrintable(status.text()));
  QCOMPARE(bridge.cmds, QStringList({"hello", "openSurvey", "evaluate"}));
  const QString shot = qEnvironmentVariable("STRATA_3D_SHOT");
  if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));  // 눈으로 볼 캡처(지정했을 때만)
  qunsetenv("STRATA_3D_PIPE");
}

// Strata → strata-3d(조사 열기·평가) → Strata(도면 받기) 왕복. 실제 네트워크 자료(DEM·위성·OSM·국가유산청)를 쓴다.
void TestStrata3dLink::realAppRoundTrip() {
  const QString exe = qEnvironmentVariable("STRATA_3D_EXE");
  if (exe.isEmpty()) QSKIP("STRATA_3D_EXE 가 없어 실제 strata-3d 왕복은 이 PC에서 건너뜁니다(프로토콜은 위 가짜 서버 시험이 확인).");
  QVERIFY2(QFileInfo::exists(exe), "STRATA_3D_EXE 경로가 없습니다");
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString gpkg = dir.filePath(QStringLiteral("survey.gpkg"));
  QVERIFY(QFile::copy(QStringLiteral("samples/demo_survey/demo.gpkg"), gpkg));
  const QByteArray before = fileHash(gpkg);
  const QString pipe = QStringLiteral("ka-hgis-real-%1").arg(QCoreApplication::applicationPid());
  const auto r = Strata3dLink::openAndEvaluate(pipe, exe, gpkg);
  QVERIFY2(r.ok, qPrintable(r.error));
  QVERIFY2(r.evaluationSummary.contains(QStringLiteral("반경 500 m")), qPrintable(r.evaluationSummary));
  QVERIFY2(r.crs.startsWith(QStringLiteral("EPSG:518")), qPrintable(r.crs));
  const QString png = dir.filePath(QStringLiteral("도면.png")), pdf = dir.filePath(QStringLiteral("도면.pdf"));
  const auto s = Strata3dLink::fetchSheet(pipe, png, pdf);
  QVERIFY2(s.ok, qPrintable(s.error));
  QVERIFY2(QFileInfo(png).size() > 200000, "도면 PNG 가 너무 작습니다");
  QVERIFY2(QFileInfo(pdf).size() > 20000, "도면 PDF 가 너무 작습니다");
  QString err;
  Strata3dLink::request(pipe, {{"cmd", "quit"}}, 10000, &err);
  QCOMPARE(fileHash(gpkg), before);  // strata-3d 는 조사 파일을 읽기 전용으로만 연다
  QVERIFY(!QFileInfo::exists(gpkg + QStringLiteral("-wal")));
}

QTEST_MAIN(TestStrata3dLink)
#include "test_strata3d_link.moc"
