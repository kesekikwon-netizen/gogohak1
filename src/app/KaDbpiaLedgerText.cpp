// DBpia 수집 원장을 표·인용문으로 옮기는 텍스트 생성기.
// 네트워크·SDK 호출은 KaGeminiBridgeClient.cpp가 맡고, 여기서는 실제 수집분만 서술한다.
#include "KaGeminiBridgeClient.h"

#include <QRegularExpression>

QString KaGeminiBridgeClient::buildPaperGroundedFallbackAnswer(const QString& question, int turnIndex) const {
  QStringList out{QStringLiteral("#### [DBpia 수집 문헌 원장 발췌 — AI 생성 아님]"),
                  QStringLiteral("- **질의 Q%1**: %2").arg(turnIndex).arg(question)};
  if (m_collectedPapers.isEmpty()) {
    out << QStringLiteral(
        "- **수집 문헌 없음**: Antigravity SDK/DBpia 응답을 받지 못했습니다. "
        "수집되지 않은 문헌·요약을 임의로 만들지 않았습니다.");
    return out.join(QLatin1Char('\n'));
  }
  const QStringList qToks =
      question.split(QRegularExpression(QStringLiteral(R"([\s,·()\[\]{}'"]+)")), Qt::SkipEmptyParts);
  int matched = 0;
  for (int i = 0; i < m_collectedPapers.size() && matched < 8; ++i) {
    const QJsonObject r = m_collectedPapers[i].toObject();
    const QString title = r.value(QStringLiteral("title")).toString().trimmed();
    const QString abs = r.value(QStringLiteral("abstract")).toString().trimmed();
    if (title.isEmpty()) continue;
    bool hit = qToks.isEmpty();
    for (const QString& tk : qToks)
      if (tk.size() >= 2 && (title.contains(tk) || abs.contains(tk))) {
        hit = true;
        break;
      }
    if (!hit) continue;
    ++matched;
    const QString authors = r.value(QStringLiteral("authors")).toString().trimmed();
    const QString year = r.value(QStringLiteral("year")).toString().trimmed();
    const QString journal = r.value(QStringLiteral("journal")).toString().trimmed();
    out << QStringLiteral("- **%1 (%2), 「%3」, 『%4』** ⟦L%5⟧%6")
               .arg(authors.isEmpty() ? QStringLiteral("저자 미수집") : authors,
                    year.isEmpty() ? QStringLiteral("연도 미수집") : year, title,
                    journal.isEmpty() ? QStringLiteral("학술지 미수집") : journal)
               .arg(i + 1)
               .arg(abs.isEmpty() ? QStringLiteral(" — 초록 미수집")
                                  : QStringLiteral(" — %1").arg(abs.simplified().left(220)));
  }
  if (matched == 0)
    out << QStringLiteral(
               "- **질의와 직접 일치하는 수집 문헌 없음**: 수집 원장 %1편에 질의 토큰이 포함된 문헌이 없습니다.")
               .arg(m_collectedPapers.size());
  return out.join(QLatin1Char('\n'));
}

QString KaGeminiBridgeClient::buildStructuredDataSynthesis(const QJsonArray& dbpiaAiTurns) const {
  QStringList out;
  out << QStringLiteral("**<표 6> DBpia 수집 문헌 원장 (실제 수집분만)**")
      << QStringLiteral("| 구분 | 질의 | 기록된 응답 | 근거 |") << QStringLiteral("|---|---|---|---|");
  for (int i = 0; i < dbpiaAiTurns.size(); ++i) {
    const QJsonObject t = dbpiaAiTurns[i].toObject();
    const QString q = t.value(QStringLiteral("question")).toString().simplified();
    const QString a = t.value(QStringLiteral("answer")).toString().simplified();
    out << QStringLiteral("| Q%1 | %2 | %3 | L1~L%4 |")
               .arg(i + 1)
               .arg(q.left(70).replace(QLatin1Char('|'), QLatin1Char('/')),
                    a.isEmpty() ? QStringLiteral("응답 없음") : a.left(90).replace(QLatin1Char('|'), QLatin1Char('/')))
               .arg(qMax(1, int(m_collectedPapers.size())));
  }
  if (m_collectedPapers.isEmpty()) {
    out << QString()
        << QStringLiteral(
               "- **DBpia 수집 문헌 없음**: Antigravity SDK/DBpia 응답을 받지 못한 상태입니다. "
               "표와 고찰을 수집되지 않은 문헌으로 채우지 않았습니다.");
    return out.join(QLatin1Char('\n'));
  }
  out << QString() << QStringLiteral("| 기호 | 저자(연도) | 논문명 | 학술지 | DBpia nodeId |")
      << QStringLiteral("|---|---|---|---|---|");
  for (int i = 0; i < m_collectedPapers.size() && i < 40; ++i) {
    const QJsonObject p = m_collectedPapers[i].toObject();
    auto field = [&](const char* key, const QString& empty) {
      QString v = p.value(QLatin1String(key)).toString().trimmed();
      if (v.isEmpty()) v = empty;
      return v.replace(QLatin1Char('|'), QLatin1Char('/'));
    };
    out << QStringLiteral("| L%1 | %2 | %3 | %4 | %5 |")
               .arg(i + 1)
               .arg(field("authors", QStringLiteral("미수집")), field("year", QStringLiteral("미수집")),
                    field("title", QStringLiteral("제목 미수집")), field("journal", QStringLiteral("미수집")))
               .arg(field("nodeId", QStringLiteral("-")));
  }
  out << QString()
      << QStringLiteral("- **수집 원장 집계**: 문헌 %1편 · 기록된 DBpia AI 응답 %2회.")
             .arg(m_collectedPapers.size())
             .arg(dbpiaAiTurns.size());
  out << QStringLiteral(
      "- 위 표는 수집 원장의 서지·초록만 옮긴 것이다. 유구·층위·편년 해석은 원문 대조 후 집필해야 하며, "
      "수집되지 않은 값은 빈칸(미수집)으로 둔다.");
  return out.join(QLatin1Char('\n'));
}
