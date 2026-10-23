// GDAL failures must reach the session log, because a desktop launch has no
// console. Repeated failures fold into a count so the log does not overflow.
#include "app/KaCrashGuard.h"
#include "app/KaGdalErrorLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cpl_error.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#define _dup dup
#define _dup2 dup2
#define _close close
#define _fileno fileno
#endif

namespace {

std::atomic<int> g_spyHits{0};
std::atomic<int> g_nestedHits{0};

QString sessionLog() {
  QFile log(QDir(KaCrashGuard::logDir()).filePath(QStringLiteral("session.log")));
  if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  return QString::fromUtf8(log.readAll());
}

CPLErrorHandler currentHandler() { return CPLGetErrorHandler(nullptr); }

void CPL_STDCALL spyPrevious(CPLErr, CPLErrorNum, const char*) { g_spyHits.fetch_add(1); }

void CPL_STDCALL spyNested(CPLErr, CPLErrorNum, const char* message) {
  g_nestedHits.fetch_add(1);
  if (message && std::strcmp(message, "nested-from-spy") != 0)
    CPLError(CE_Failure, CPLE_AppDefined, "%s", "nested-from-spy");
}

}  // namespace

class GdalErrorLogTest : public QObject {
  Q_OBJECT
private slots:
  void dumpHint_namesCrashDump() {
    const QString hint = KaCrashGuard::dumpHint();
    QVERIFY2(hint.contains(QStringLiteral(".dmp")), qPrintable(hint));
    QVERIFY2(hint.contains(QStringLiteral("session.log")), qPrintable(hint));
    QVERIFY2(hint.contains(QStringLiteral("10MB")), qPrintable(hint));
  }

  void onlyFailuresAreRecorded() {
    QVERIFY(KaGdalErrorLog::describe(CE_Warning, 1, QStringLiteral("Field width truncated")).isEmpty());
    QVERIFY(KaGdalErrorLog::describe(CE_Debug, 0, QStringLiteral("debug")).isEmpty());
    QCOMPARE(KaGdalErrorLog::describe(CE_Failure, 1,
                                      QStringLiteral("In GetNextRawFeature(): unable to open database file")),
             QStringLiteral("[gdal] ERROR 1: In GetNextRawFeature(): unable to open database file"));
    QVERIFY(KaGdalErrorLog::describe(CE_Fatal, 3, QStringLiteral("x")).startsWith(QStringLiteral("[gdal] FATAL 3")));
    QCOMPARE(KaGdalErrorLog::describe(CE_Failure, 1, QStringLiteral("a\r\nb")),
             QStringLiteral("[gdal] ERROR 1: a b"));
  }

  void failuresReachTheSessionLogAndRepeatsFold() {
    KaGdalErrorLog::install();
    KaGdalErrorLog::install();  // a second install must not chain the handler to itself
    const char* flood = "In GetNextRawFeature(): sqlite3_step() : unable to open database file";
    CPLError(CE_Warning, CPLE_AppDefined, "%s", "warning noise");
    for (int i = 0; i < 120; ++i) CPLError(CE_Failure, CPLE_AppDefined, "%s", flood);
    CPLError(CE_Failure, CPLE_AppDefined, "%s", "a different failure");

    const QString text = sessionLog();
    QVERIFY2(text.contains(QStringLiteral("[gdal] ERROR 1: In GetNextRawFeature()")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("a different failure")));
    QVERIFY(!text.contains(QStringLiteral("warning noise")));
    // 120 identical failures become one line plus repeat counts, not 120 lines.
    QCOMPARE(text.count(QStringLiteral("unable to open database file")), 1);
    QVERIFY(text.contains(QStringLiteral("반복")));
    KaGdalErrorLog::uninstall();
  }

  // 기본 처리기(터미널 stderr)로는 다시 넘기지 않는다: 세션 기록에 이미 남겼다. 앱을 터미널에서 켜면 같은 오류
  // 1000개가 터미널에 쏟아졌다(2026-09-18 신고, R30).
  void failuresAreNotRepeatedOnTheTerminal() {
    const QString capture = QDir::temp().filePath(QStringLiteral("ka-gdal-stderr.txt"));
    fflush(stderr);
    const int saved = _dup(_fileno(stderr));
    QVERIFY(freopen(qPrintable(capture), "w", stderr));
    CPLSetErrorHandler(CPLDefaultErrorHandler);
    KaGdalErrorLog::install();
    CPLError(CE_Failure, CPLE_AppDefined, "%s", "terminal-noise-check");
    KaGdalErrorLog::uninstall();
    fflush(stderr);
    _dup2(saved, _fileno(stderr));
    _close(saved);
    QFile printed(capture);
    QVERIFY(printed.open(QIODevice::ReadOnly));
    QVERIFY2(!printed.readAll().contains("terminal-noise-check"), "the default handler printed the error again");
    QVERIFY(sessionLog().contains(QStringLiteral("terminal-noise-check")));
  }

  void uninstallRestoresPreviousAndInstallWorksAgain() {
    g_spyHits.store(0);
    CPLSetErrorHandler(spyPrevious);
    KaGdalErrorLog::install();
    const CPLErrorHandler ours = currentHandler();
    QVERIFY(ours != spyPrevious);

    KaGdalErrorLog::uninstall();
    QVERIFY(currentHandler() != ours);
    QCOMPARE(reinterpret_cast<void*>(currentHandler()), reinterpret_cast<void*>(spyPrevious));

    CPLError(CE_Failure, CPLE_AppDefined, "%s", "after-uninstall");
    QVERIFY(g_spyHits.load() >= 1);

    KaGdalErrorLog::install();
    QCOMPARE(reinterpret_cast<void*>(currentHandler()), reinterpret_cast<void*>(ours));
    CPLError(CE_Failure, CPLE_AppDefined, "%s", "after-reinstall");
    QVERIFY(sessionLog().contains(QStringLiteral("after-reinstall")));
    KaGdalErrorLog::uninstall();
  }

  void nestedCplErrorFromPreviousDoesNotRecurse() {
    g_nestedHits.store(0);
    CPLSetErrorHandler(spyNested);
    KaGdalErrorLog::install();
    CPLError(CE_Failure, CPLE_AppDefined, "%s", "outer-from-test");
    QCOMPARE(g_nestedHits.load(), 1);
    QVERIFY(sessionLog().contains(QStringLiteral("outer-from-test")));
    KaGdalErrorLog::uninstall();
  }
};

int main(int argc, char** argv) {
  const QString logs = QStringLiteral("build/test-logs/gdal-fixed");
  QDir().mkpath(logs);
  QFile::remove(logs + QStringLiteral("/session.log"));
  qputenv("KA_HGIS_LOG_DIR", logs.toUtf8());
  QCoreApplication app(argc, argv);
  GdalErrorLogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_gdal_error_log.moc"
