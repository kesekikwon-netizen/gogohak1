// 화면에 보이는 글자의 용어 결정(2026-09-23 Strata 이름·용어): 「조판」은 「도면」으로 부른다(2026-10-02 조사 R87).
// 소스의 문자열(주석 제외)과 점검 규칙 파일에 「조판」이 다시 들어오면 실패한다.
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>

namespace {

// 한 줄에서 // 주석을 떼고 "…" 문자열만 모은다(문자열 안의 // 는 남긴다).
QStringList literalsOf(const QString& line) {
  QStringList out;
  bool inString = false;
  QString current;
  for (int i = 0; i < line.size(); ++i) {
    const QChar c = line.at(i);
    if (!inString && c == QLatin1Char('/') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('/')) break;
    if (c == QLatin1Char('"') && (i == 0 || line.at(i - 1) != QLatin1Char('\\'))) {
      if (inString) out << current;
      current.clear();
      inString = !inString;
    } else if (inString) {
      current += c;
    }
  }
  return out;
}

}  // namespace

class TestUiTerms : public QObject {
  Q_OBJECT
 private slots:
  void visibleTextSaysDrawingNotJopan() {
    const QString root = QString::fromUtf8(KA_SOURCE_DIR);
    QStringList hits;
    for (const QString& folder : {QStringLiteral("src/app"), QStringLiteral("src/core"), QStringLiteral("data/rules")}) {
      QDirIterator it(root + QLatin1Char('/') + folder, {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.json")},
                      QDir::Files);
      while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (int n = 0; n < lines.size(); ++n)
          for (const QString& text : literalsOf(lines.at(n)))
            if (text.contains(QStringLiteral("조판"))) hits << QStringLiteral("%1:%2 %3").arg(path.mid(root.size() + 1)).arg(n + 1).arg(text);
      }
    }
    QVERIFY2(hits.isEmpty(), qUtf8Printable(hits.join(QLatin1Char('\n'))));
  }

  // 창 제목은 「<조사 이름> * - Strata」 하나로 만든다(목업 Task 3, refreshWindowTitle). 조사를 연 뒤 다른 곳이
  // 이름만 써 넣으면 「광령리1」처럼 꼬리 없는 제목이 남는다(2026-10-03 검토): 주 창 코드는 이름을 직접 쓰지 않는다.
  void mainWindowTitleComesFromRefreshOnly() {
    const QString root = QString::fromUtf8(KA_SOURCE_DIR);
    // moc 가 R"(...)" 를 읽지 못하므로 보통 문자열로 쓴다.
    const QRegularExpression direct(QStringLiteral("(?<![\\w.>:])setWindowTitle\\((?!QStringLiteral\\()"));
    QStringList hits;
    QDirIterator it(root + QStringLiteral("/src/app"), {QStringLiteral("MainWindow*.cpp")}, QDir::Files);
    while (it.hasNext()) {
      const QString path = it.next();
      QFile file(path);
      QVERIFY(file.open(QIODevice::ReadOnly));
      const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
      for (int n = 0; n < lines.size(); ++n)
        if (direct.match(lines.at(n)).hasMatch()) hits << QStringLiteral("%1:%2").arg(path.mid(root.size() + 1)).arg(n + 1);
    }
    QVERIFY2(hits.isEmpty(), qUtf8Printable(hits.join(QLatin1Char('\n'))));
  }

  // 사용자에게 보이는 보고서 글자에는 개발 용어를 쓰지 않는다(2026-10-23 설계도 10절, 스킬 strata-ui-wording 글자 사전).
  // 영문·숫자·기호만 있는 문자열(필드 식별자, 주소)과 양식 호환 때문에 이름을 못 바꾸는 필드 이름은 건너뛴다.
  void reportTextHasNoDeveloperTerms() {
    const QString root = QString::fromUtf8(KA_SOURCE_DIR);
    const QStringList banned = {QStringLiteral("RAG"), QStringLiteral("SDK"), QStringLiteral("피처"),
                                QStringLiteral("누름틀"), QStringLiteral("Antigravity"), QStringLiteral("슬롯"),
                                QStringLiteral("_SLOT"), QStringLiteral("하네스"), QStringLiteral("┏"), QStringLiteral("┓"),
                                QStringLiteral("┗"), QStringLiteral("┛")};
    const QStringList keepFieldNames = {QStringLiteral("RAG인용슬롯")};
    const QRegularExpression identifierOnly(QStringLiteral("^[A-Za-z0-9_./:?=&%#-]*$"));
    QStringList hits;
    for (const QString& pattern : {QStringLiteral("src/app/KaHwp*.cpp"), QStringLiteral("src/app/KaDbpia*.cpp"),
                                   QStringLiteral("src/core/Hwp*.cpp"), QStringLiteral("src/core/LocalReport*.cpp")}) {
      const int slash = pattern.lastIndexOf(QLatin1Char('/'));
      QDirIterator it(root + QLatin1Char('/') + pattern.left(slash), {pattern.mid(slash + 1)}, QDir::Files);
      while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (int n = 0; n < lines.size(); ++n)
          for (const QString& text : literalsOf(lines.at(n))) {
            if (identifierOnly.match(text).hasMatch() || keepFieldNames.contains(text)) continue;
            for (const QString& word : banned)
              if (text.contains(word)) {
                hits << QStringLiteral("%1:%2 [%3] %4").arg(path.mid(root.size() + 1)).arg(n + 1).arg(word, text.left(60));
                break;
              }
          }
      }
    }
    QVERIFY2(hits.isEmpty(), qUtf8Printable(QStringLiteral("%1건\n").arg(hits.size()) + hits.join(QLatin1Char('\n'))));
  }
};

QTEST_GUILESS_MAIN(TestUiTerms)

#include "test_ui_terms.moc"
