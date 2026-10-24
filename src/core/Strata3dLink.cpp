#include "core/Strata3dLink.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>

namespace Strata3dLink {

QString pipeName() {
  const QString env = qEnvironmentVariable("STRATA_3D_PIPE");
  return env.isEmpty() ? QStringLiteral("strata-3d-bridge-v1") : env;
}

QString surveyGpkgFromProjectFile(const QString& fileName) {
  if (fileName.startsWith(QLatin1String("geopackage:"), Qt::CaseInsensitive))
    return fileName.mid(11).section(QLatin1Char('?'), 0, 0);
  if (fileName.endsWith(QLatin1String(".gpkg"), Qt::CaseInsensitive)) return fileName;
  if (fileName.isEmpty()) return {};
  const QFileInfo info(fileName);
  const QString sibling = info.dir().filePath(info.completeBaseName() + QStringLiteral(".gpkg"));
  return QFileInfo::exists(sibling) ? sibling : QString();
}

QString findExecutable() {
  const QString env = qEnvironmentVariable("STRATA_3D_EXE");
  if (!env.isEmpty()) return QFileInfo::exists(env) ? env : QString();
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  for (const QString& c : {appDir + QStringLiteral("/strata-3d/strata-3d.exe"),
                           docs + QStringLiteral("/hgis-3d-engine/build/Release/strata-3d.exe")})
    if (QFileInfo::exists(c)) return QDir::toNativeSeparators(c);
  return {};
}

QJsonObject request(const QString& pipe, const QJsonObject& req, int timeoutMs, QString* errorOut) {
  QLocalSocket sock;
  QElapsedTimer connectTimer;
  connectTimer.start();
  while (true) {  // 앱이 그리느라 바쁜 사이에는 파이프가 잠깐 안 받는다. 제한시간 안에서 다시 시도
    sock.connectToServer(pipe);
    if (sock.waitForConnected(2000)) break;
    if (connectTimer.elapsed() >= std::min(timeoutMs, 30000)) {
      if (errorOut) *errorOut = QStringLiteral("strata-3d 에 연결하지 못했습니다: ") + sock.errorString();
      return {};
    }
    sock.abort();
    QThread::msleep(200);
  }
  sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
  if (!sock.waitForBytesWritten(5000)) {
    if (errorOut) *errorOut = QStringLiteral("strata-3d 에 요청을 보내지 못했습니다.");
    return {};
  }
  QByteArray buf;
  QElapsedTimer t;
  t.start();
  while (!buf.contains('\n')) {
    const int left = timeoutMs - int(t.elapsed());
    if (left <= 0 || !sock.waitForReadyRead(left)) {
      if (errorOut) *errorOut = QStringLiteral("strata-3d 응답 시간이 지났습니다(%1초).").arg(timeoutMs / 1000);
      return {};
    }
    buf += sock.readAll();
  }
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(buf.left(buf.indexOf('\n')), &pe);
  if (!doc.isObject()) {
    if (errorOut) *errorOut = QStringLiteral("strata-3d 응답을 읽지 못했습니다: ") + pe.errorString();
    return {};
  }
  return doc.object();
}

namespace {

bool answers(const QString& pipe) {
  QString err;
  return request(pipe, {{"cmd", "hello"}}, 1500, &err).value(QStringLiteral("ok")).toBool();
}

// 응답이 ok:false 면 사유를 errorOut 에 담는다.
bool accepted(const QJsonObject& r, const QString& what, QString* errorOut) {
  if (r.isEmpty()) return false;  // request 가 이미 사유를 채웠다
  if (r.value(QStringLiteral("ok")).toBool()) return true;
  if (errorOut) *errorOut = what + QStringLiteral(": ") + r.value(QStringLiteral("error")).toString(QStringLiteral("알 수 없는 오류"));
  return false;
}

}  // namespace

bool ensureRunning(const QString& pipe, const QString& exe, QString* errorOut) {
  if (answers(pipe)) return true;
  if (exe.isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("strata-3d 가 실행 중이 아니고 실행 파일도 찾지 못했습니다(STRATA_3D_EXE 로 지정할 수 있습니다).");
    return false;
  }
  QStringList args{QStringLiteral("--no-initial-scene")};  // 조사를 곧바로 열 것이므로 기본 경주 장면은 받지 않는다
  if (pipe != QLatin1String("strata-3d-bridge-v1")) args << QStringLiteral("--bridge-pipe") << pipe;
  if (!QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath())) {
    if (errorOut) *errorOut = QStringLiteral("strata-3d 를 실행하지 못했습니다: ") + exe;
    return false;
  }
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < 30000) {
    if (answers(pipe)) return true;
    QThread::msleep(300);
  }
  if (errorOut) *errorOut = QStringLiteral("strata-3d 를 실행했지만 30초 안에 연결되지 않았습니다.");
  return false;
}

Outcome openAndEvaluate(const QString& pipe, const QString& exe, const QString& gpkg) {
  Outcome out;
  if (gpkg.isEmpty() || !QFileInfo::exists(gpkg)) {
    out.error = QStringLiteral("열려 있는 조사 파일(.gpkg)을 찾지 못했습니다. 조사를 저장하거나 연 뒤에 눌러 주세요.");
    return out;
  }
  if (!ensureRunning(pipe, exe, &out.error)) return out;
  // 실측 DEM·위성영상·건물·유적을 받아 올리는 일이라 오래 걸릴 수 있다.
  const QJsonObject open = request(pipe, {{"cmd", "openSurvey"}, {"gpkg", QDir::toNativeSeparators(gpkg)}}, 300000, &out.error);
  if (!accepted(open, QStringLiteral("조사 열기 실패"), &out.error)) return out;
  const QJsonObject survey = open.value(QStringLiteral("survey")).toObject();
  out.surveyMessage = QStringLiteral("조사 %1: 레이어 %2개").arg(QFileInfo(gpkg).fileName()).arg(survey.value(QStringLiteral("layers")).toArray().size());
  const QJsonObject ev = request(pipe, {{"cmd", "evaluate"}, {"target", "survey"}}, 120000, &out.error);
  if (!accepted(ev, QStringLiteral("평가 실패"), &out.error)) return out;
  const QJsonObject e = ev.value(QStringLiteral("evaluation")).toObject();
  out.evaluationSummary = e.value(QStringLiteral("summary")).toString();
  out.crs = e.value(QStringLiteral("crs")).toString();
  out.ok = !out.evaluationSummary.isEmpty();
  if (!out.ok) out.error = QStringLiteral("평가 결과에 요약이 없습니다.");
  return out;
}

Outcome fetchSheet(const QString& pipe, const QString& png, const QString& pdf) {
  Outcome out;
  QJsonObject req{{"cmd", "renderSheet"}, {"png", QDir::toNativeSeparators(png)}};
  if (!pdf.isEmpty()) req.insert(QStringLiteral("pdf"), QDir::toNativeSeparators(pdf));
  const QJsonObject r = request(pipe, req, 180000, &out.error);
  if (!accepted(r, QStringLiteral("도면 만들기 실패"), &out.error)) return out;
  const QJsonObject sheet = r.value(QStringLiteral("sheet")).toObject();
  out.png = sheet.value(QStringLiteral("png")).toString();
  out.pdf = sheet.value(QStringLiteral("pdf")).toString();
  out.ok = QFileInfo(out.png).size() > 0;
  if (!out.ok) out.error = QStringLiteral("strata-3d 가 도면 파일을 만들지 못했습니다: ") + out.png;
  return out;
}

}  // namespace Strata3dLink
