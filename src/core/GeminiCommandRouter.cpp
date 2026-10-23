#include "GeminiCommandRouter.h"

#include <QRegularExpression>
#include <QStringBuilder>
#include <algorithm>

namespace GeminiCommandRouter {
namespace {

int extractRadiusMeters(const QString& text, int fallbackMeters) {
  static const QRegularExpression reKm(QStringLiteral(R"((\d+(?:\.\d+)?)\s*(?:km|킬로미터|키로))"),
                                       QRegularExpression::CaseInsensitiveOption);
  const auto mKm = reKm.match(text);
  if (mKm.hasMatch()) {
    const int val = qRound(mKm.captured(1).toDouble() * 1000.0);
    return std::clamp(val, int(GisFactHarness::kMinRadiusMeters), int(GisFactHarness::kMaxRadiusMeters));
  }
  static const QRegularExpression reM(
      QStringLiteral(R"((?:(\d{3,4})\s*(?:m|미터)(?!\s*2|²)|(?:반경|거리)\s*(\d{3,4})))"),
      QRegularExpression::CaseInsensitiveOption);
  const auto mM = reM.match(text);
  if (mM.hasMatch()) {
    const QString cap = !mM.captured(1).isEmpty() ? mM.captured(1) : mM.captured(2);
    const int val = cap.toInt();
    return std::clamp(val, int(GisFactHarness::kMinRadiusMeters), int(GisFactHarness::kMaxRadiusMeters));
  }
  return std::clamp(fallbackMeters, int(GisFactHarness::kMinRadiusMeters), int(GisFactHarness::kMaxRadiusMeters));
}

bool hasAny(const QString& text, std::initializer_list<const char16_t*> words) {
  for (const char16_t* w : words) {
    if (text.contains(QStringView(w), Qt::CaseInsensitive)) return true;
  }
  return false;
}

}  // namespace

ParsedCommand parse(const QString& userInput, int defaultRadiusMeters) {
  ParsedCommand out;
  out.rawText = userInput.trimmed();
  const QString t = out.rawText;
  out.radiusMeters = extractRadiusMeters(t, defaultRadiusMeters);
  out.docType = t.contains(QStringLiteral("논문")) ? QStringLiteral("paper") : QStringLiteral("report");
  out.autoExportDocx = hasAny(t, {u"고찰", u"논문", u"보고서", u"워드", u"docx"}) &&
                       hasAny(t, {u"만들", u"작성", u"써", u"내보내", u"출력", u"저장"});

  const bool mentionsResearch =
      hasAny(t, {u"비교", u"생각", u"평가", u"디비피아", u"dbpia", u"논문", u"고찰", u"데이터를 조사",
                 u"데이터 조사", u"유적에 대해", u"유적을 조사", u"학술", u"문헌"});

  const auto setCmd = [&](Intent intent, const char* action, bool withRadius = false) -> ParsedCommand& {
    out.intent = intent;
    QJsonObject params;
    if (withRadius) params.insert(QStringLiteral("radiusMeters"), out.radiusMeters);
    if (action && *action)
      out.harnessCommand = QJsonObject{{QStringLiteral("action"), QString::fromLatin1(action)},
                                       {QStringLiteral("params"), params}};
    return out;
  };
  if (hasAny(t, {u"엑셀", u"xlsx"}) ||
      (hasAny(t, {u"주변유적", u"주변 유적"}) && hasAny(t, {u"표로", u"목록", u"리스트", u"현황표"}) &&
       hasAny(t, {u"만들", u"저장", u"정리", u"출력", u"뽑아"})))
    return setCmd(Intent::ExportNearbyExcel, "collect_nearby_sites", true);
  if (mentionsResearch) return setCmd(Intent::ResearchAndEvaluate, "collect_nearby_sites", true);
  if (hasAny(t, {u"버퍼", u"경계", u"반경"}) && hasAny(t, {u"그려", u"표시", u"추가", u"만들", u"500", u"1000", u"1km"}))
    return setCmd(Intent::DrawBuffer, "add_distance_ring", true);
  if (hasAny(t, {u"유산"}) && hasAny(t, {u"받", u"다운", u"불러", u"가져", u"올려"}))
    return setCmd(Intent::FetchHeritage, "");
  if (hasAny(t, {u"지적도", u"지적"}) && hasAny(t, {u"받", u"다운", u"불러", u"5km", u"켜"}))
    return setCmd(Intent::DownloadCadastral, "");

  struct EnvRule {
    const char16_t* kw1;
    const char16_t* kw2;
    const char16_t* key;
  };
  static constexpr EnvRule kEnvRules[] = {
      {u"토양", u"흙토람", u"soil"},     {u"지질", u"kigam", u"geology"},
      {u"수계", u"하천", u"river"},      {u"dem", u"수치표고", u"dem"},
      {u"고지형", u"구하도", u"paleo"},  {u"수치지형", u"국토지리", u"topo"},
      {u"등고선", u"등고", u"contour"},  {u"지형도", u"음영", u"terrain"},
  };
  for (const auto& r : kEnvRules) {
    if (t.contains(QStringView(r.kw1), Qt::CaseInsensitive) ||
        t.contains(QStringView(r.kw2), Qt::CaseInsensitive)) {
      out.envLayerKey = QString::fromUtf16(r.key);
      return setCmd(Intent::ToggleEnvironmentLayer,
                    out.envLayerKey == QLatin1String("soil") ? "download_soil_map" : "");
    }
  }
  if (hasAny(t, {u"시굴격자", u"트렌치 격자", u"격자 놓"}))
    return setCmd(Intent::PlaceTrenchGrid, "generate_trench_grid");
  if (hasAny(t, {u"도면"})) return setCmd(Intent::OpenLayout, "");
  if (hasAny(t, {u"검수", u"제출 꾸러미"})) return setCmd(Intent::OpenSubmitReview, "run_checklist");
  if (hasAny(t, {u"워드크래프트", u"wordcraft", u"docx"})) return setCmd(Intent::ExportWordCraft, "");
  return setCmd(Intent::ResearchAndEvaluate, "collect_nearby_sites", true);
}

QString formatLedgerSummary(const GisFactHarness::Ledger& ledger, int radiusMeters) {
  if (!ledger.ok()) return QStringLiteral("⚠ GIS 실측 원장을 만들지 못했습니다: %1").arg(ledger.error);
  QStringList lines{QStringLiteral("### 📍 GIS 실측 사실원장 (반경 %1m 기준, 현상변경 규제구역 제외)").arg(radiusMeters)};
  int nearbyCount = 0;
  for (const auto& c : ledger.cards) {
    if (c.kind == QLatin1String("survey_area"))
      lines << QStringLiteral("- **[%1] 조사구역**: 면적 **%2%3** (좌표계 `%4`)").arg(c.id, c.value, c.unit, ledger.workCrs);
    else if (c.kind == QLatin1String("nearby_heritage") && ++nearbyCount <= 30)
      lines << QStringLiteral("- **[%1] %2** (%3): 조사구역에서 **%4** 방향 **%5m** (%6)")
                   .arg(c.id, c.siteName, c.datasetName, c.direction, c.value, c.radiusLabel);
    else if (c.kind != QLatin1String("nearby_heritage"))
      lines << QStringLiteral("- **[%1] %2**: %3").arg(c.id, c.subject, c.quote);
  }
  if (nearbyCount > 30) lines << QStringLiteral("- *(외 %1곳의 주변유적 G-카드도 실측 원장에 모두 포함됨)*").arg(nearbyCount - 30);
  lines << (nearbyCount == 0 ? QStringLiteral("- 반경 %1m 이내 주변유적 도형이 없습니다 (「유산」 버튼을 눌러 자료를 받아 주세요).").arg(radiusMeters)
                             : QStringLiteral("- **반경 %1m 이내 고유 주변유적 합계**: 총 **%2곳**").arg(radiusMeters).arg(nearbyCount));
  if (!ledger.dbpiaQueries.isEmpty())
    lines << QStringLiteral("- **합성된 학술 검색어 (%1건)**: `%2`").arg(ledger.dbpiaQueries.size()).arg(ledger.dbpiaQueries.join(QStringLiteral("`, `")));
  if (!ledger.uncollectedDomains.isEmpty())
    lines << QStringLiteral("- **미수집 환경 도메인(날조 잠금)**: `%1`").arg(ledger.uncollectedDomains.join(QStringLiteral(", ")));
  return lines.join(QLatin1Char('\n'));
}

QStringList buildDbpiaAiQuestions(const QString&, const GisFactHarness::Ledger& ledger, int radiusMeters) {
  QStringList allSites;
  for (const auto& c : ledger.cards) {
    if (c.kind != QLatin1String("nearby_heritage")) continue;
    const QString s = c.siteName.trimmed();
    if (!s.isEmpty() && !allSites.contains(s) && allSites.size() < 24) allSites << s;
  }
  if (allSites.isEmpty()) allSites = ledger.dbpiaQueries.mid(0, 6);
  const QString coreStr = allSites.isEmpty() ? QStringLiteral("조사지역 일원 유적") : allSites.mid(0, 3).join(QStringLiteral(", "));
  QStringList qs;
  qs << QStringLiteral("%1의 발굴조사 연혁, 확인된 시대별 유구 종류·규모(실측 제원) 및 출토유물 데이터를 발굴보고서와 학술논문 근거로 상세히 정리해 줘.")
            .arg(coreStr);
  for (int i = 3; i < allSites.size() && qs.size() < 6; i += 4) {
    const QString batch = allSites.mid(i, 4).join(QStringLiteral(", "));
    qs << QStringLiteral("조사구역 반경 %1m 내 위치한 %2 각각의 발굴조사 성과(확인 유구·규모·출토유물·시대 편년 데이터)를 학술논문과 보고서 근거로 정리해 줘.")
              .arg(radiusMeters).arg(batch);
  }
  qs << QStringLiteral("%1 일원 발굴조사에서 확인된 층위 구조와 주요 유구(건물지·도로·방리·담장·고분·우물 등)의 실측 제원 및 배치 데이터를 논문 근거로 정리해 줘.")
            .arg(coreStr);
  qs << QStringLiteral("%1 및 반경 %2m 주변유적 출토 유물(토기·와전·금속기·명문자료 등)의 형식 분류와 편년(연대) 데이터를 학술논문 근거로 정리해 줘.")
            .arg(coreStr).arg(radiusMeters);
  qs << QStringLiteral("%1 일원 주변유적군의 시대별 공간 분포 데이터와 발굴보고서·학술논문에 수록된 핵심 실측 데이터를 종합 정리해 줘.")
            .arg(coreStr);
  return qs;
}

QString buildGeminiLoopPrompt(const QString& userRequest, const GisFactHarness::Ledger& ledger,
                              const QJsonArray& dbpiaAiTurns, const QJsonArray& dbpiaPackets,
                              int radiusMeters, int roundNumber, const QString& mapSnapshotPath) {
  QStringList p;
  p << buildEvaluationPrompt(userRequest, ledger, dbpiaPackets, radiusMeters, mapSnapshotPath);
  p << QStringLiteral("\n## [5] 지금까지 DBpia AI 에이전트(ai.dbpia.co.kr)에서 취합한 주변유적 학술 데이터 원장 (총 %1건)")
           .arg(dbpiaAiTurns.size());
  int idx = 1;
  for (const QJsonValue& v : dbpiaAiTurns) {
    const QJsonObject t = v.toObject();
    p << QStringLiteral("\n### [데이터 취합 Q%1] %2\n- **DBpia AI 문헌 데이터**: %3")
             .arg(idx++).arg(t.value("question").toString(), t.value("answer").toString().left(1600));
  }
  if (dbpiaAiTurns.isEmpty()) {
    p << QStringLiteral(
        "\n## [6] 현재 단계 지시 (Round 1 — DBpia AI 전수 데이터 취합 질문 설계)\n"
        "- 제미나이의 주관적 의견이나 추측은 일절 쓰지 마십시오. 오직 데이터로만 이야기해야 합니다.\n"
        "- 위 [2] GIS 실측 원장의 주변유적 전체와 [3] 수집 참고문헌을 빠짐없이 취합하기 위해 **DBpia AI 에이전트에게 물어볼 사실 확인용 질문**을 출력하십시오.\n"
        "- 첫 줄에 `[STATUS: NEED_MORE]`를 쓰고, 각 질문은 `[ASK_DBPIA: 구체적인 데이터 확인 질문]` 형식으로 한 줄에 하나씩 작성하십시오.");
  } else if (roundNumber < 3 && dbpiaAiTurns.size() < 8) {
    p << QStringLiteral(
        "\n## [6] 현재 단계 지시 (Round %1 — 미취합 주변유적 데이터 추가 질의 또는 최종 데이터 종합)\n"
        "- 아직 데이터가 취합되지 않은 주변유적이나 참고문헌 핵심 실측치(유구 제원·출토유물·편년)가 남아 있다면 첫 줄에 `[STATUS: NEED_MORE]`를 쓰고 `[ASK_DBPIA: 추가 데이터 확인 질문]`을 출력하십시오.\n"
        "- 충분한 데이터가 모였다면 첫 줄에 `[STATUS: SUFFICIENT]`를 쓰고, **제미나이의 주관적 의견을 배제하고 오직 [2] GIS 실측 데이터(⟦G#⟧)와 [3]·[5] DBpia AI 취합 데이터·참고문헌(⟦L#⟧)의 객관적 사실과 수치만** 학술 보고서 형식으로 정리하십시오.")
             .arg(roundNumber);
  } else {
    p << QStringLiteral(
        "\n## [6] 현재 단계 지시 (최종 데이터 종합 정리)\n"
        "- 첫 줄에 `[STATUS: SUFFICIENT]`를 쓰고, **제미나이의 주관적 의견·추측 없이 오직 [2] GIS 실측 데이터(⟦G#⟧)와 [3]·[5] DBpia AI 취합 데이터·참고문헌(⟦L#⟧)의 사실과 수치만** 종합하여 학술 보고서 본문을 완성하십시오.");
  }
  return p.join(QLatin1Char('\n'));
}

LoopStepDecision parseGeminiLoopReply(const QString& geminiReply, const GisFactHarness::Ledger& ledger,
                                      const QSet<QString>& alreadyAsked, int roundNumber, int radiusMeters) {
  LoopStepDecision dec;
  const QString trimmed = geminiReply.trimmed();
  static const QRegularExpression reAsk(QStringLiteral(R"(\[ASK_DBPIA\s*:\s*([^\]]+)\])"));
  auto it = reAsk.globalMatch(trimmed);
  while (it.hasNext()) {
    const QString q = it.next().captured(1).simplified();
    if (q.size() >= 8 && !alreadyAsked.contains(q) && !dec.questionsToAskDbpia.contains(q) &&
        dec.questionsToAskDbpia.size() < 6) {
      dec.questionsToAskDbpia.append(q);
    }
  }
  const bool explicitNeedMore = trimmed.contains(QLatin1String("NEED_MORE"), Qt::CaseInsensitive);
  const bool explicitSufficient = trimmed.contains(QLatin1String("SUFFICIENT"), Qt::CaseInsensitive);
  if ((roundNumber == 1 || explicitNeedMore) && dec.questionsToAskDbpia.isEmpty() && !explicitSufficient) {
    for (const QString& seed : buildDbpiaAiQuestions(QString(), ledger, radiusMeters)) {
      if (!alreadyAsked.contains(seed)) dec.questionsToAskDbpia.append(seed);
    }
  }
  if ((explicitNeedMore || roundNumber == 1) && !dec.questionsToAskDbpia.isEmpty() && roundNumber < 5) {
    dec.sufficient = false;
    return dec;
  }
  dec.sufficient = true;
  QString cleanMd = trimmed;
  cleanMd.remove(QRegularExpression(QStringLiteral(R"(\[STATUS\s*:\s*[^\]]+\]\s*)")));
  cleanMd.remove(reAsk);
  dec.finalEvaluationMarkdown = cleanMd.trimmed();
  return dec;
}

QString buildEvaluationPrompt(const QString& userRequest, const GisFactHarness::Ledger& ledger,
                              const QJsonArray& dbpiaPackets, int radiusMeters,
                              const QString& mapSnapshotPath) {
  QStringList p;
  p << QStringLiteral("당신은 한국 고고학 발굴조사 및 공간분석(GIS) 전문 학술 AI입니다.");
  p << QStringLiteral("사용자가 Strata(ka-hgis) 조사지역과 반경 %1m 주변유적 전체를 바탕으로 요청했습니다: %2")
           .arg(radiusMeters).arg(userRequest.trimmed());
  if (!mapSnapshotPath.isEmpty()) p << QStringLiteral("[현재 GIS 화면 캡처]: %1").arg(mapSnapshotPath);
  p << QStringLiteral("\n## [2] Strata GIS 실측 사실원장 (G-카드 — 절대 변경·날조 금지)");
  QStringList extraSites;
  int shownCards = 0;
  for (const auto& c : ledger.cards) {
    if (shownCards < 25 || c.kind != QLatin1String("nearby_heritage")) {
      p << QStringLiteral("- ⟦%1⟧ [%2] %3 | %4=%5%6 | 원문: %7")
               .arg(c.id, c.kind, c.subject, c.attribute, c.value, c.unit, c.quote);
      ++shownCards;
    } else if (extraSites.size() < 40) {
      extraSites << QStringLiteral("⟦%1⟧ %2(%3 %4m)").arg(c.id, c.siteName, c.direction, c.value);
    }
  }
  if (!extraSites.isEmpty()) p << QStringLiteral("- [추가 주변유적]: %1").arg(extraSites.join(QStringLiteral(", ")));
  if (!ledger.uncollectedDomains.isEmpty())
    p << QStringLiteral("- [잠금 도메인(미수집)]: %1 -> 수치 날조 절대 금지").arg(ledger.uncollectedDomains.join(QStringLiteral(", ")));
  p << QStringLiteral("\n## [3] DBpia 학술지·보고서·학위논문 검색 원장 (L-카드)");
  int lIdx = 1;
  for (const QJsonValue& v : dbpiaPackets) {
    for (const QJsonValue& rv : v.toObject().value("references").toArray()) {
      if (lIdx > 20) break;
      const QJsonObject r = rv.toObject();
      const QString abs = r.value("abstract").toString().trimmed();
      p << QStringLiteral("  * ⟦L%1⟧ [%2] %3 (%4), 「%5」, 『%6』%7")
               .arg(lIdx++).arg(r.value("nodeType").toString(QStringLiteral("논문")), r.value("authors").toString(),
                    r.value("year").toString(), r.value("title").toString(), r.value("journal").toString(),
                    abs.isEmpty() ? QString() : QStringLiteral(" | 요지: %1").arg(abs.left(100)));
    }
  }
  p << QStringLiteral(
      "\n## [4] 필수 작성 규칙 (폐쇄 세계 사실 하네스)\n"
      "1. 반드시 위 GIS 실측 원장(`⟦G1⟧`~)과 DBpia AI 답변·문헌(`⟦L1⟧`~)만 근거로 서술하고 문장 끝에 근거 마커를 붙이십시오.\n"
      "2. 원장에 없는 유적명, 없는 유구 번호, 실측과 다른 방위·거리, 잠금 도메인을 절대 지어내지 마십시오.\n"
      "3. Ⅰ. GIS 공간 분포 분석 / Ⅱ. DBpia 학술 연구로 본 주변유적 성격·성과 / Ⅲ. 조사지역 종합 평가 및 비교·고찰 순서로 작성하십시오.");
  return p.join(QLatin1Char('\n'));
}

QString normalizeDbpiaAnswer(const QString& rawAnswer) {
  static const QRegularExpression reCiteNum(QStringLiteral(R"(([가-힣\)\]\.])\s+\d{1,3}(?=\s|$))"));
  static const QRegularExpression reChatty(
      QStringLiteral(R"((?:요청하신[^.\n]*정리해\s*드립니다\.?|어려운 용어 대신[^.\n]*설명해\s*드릴게요\.?|한\s*줄\s*요약\s*:?))"));
  QString text = rawAnswer;
  text.remove(QLatin1Char('`'));
  text.replace(reChatty, QString());
  text.replace(reCiteNum, QStringLiteral("\\1"));
  QStringList out;
  bool inTabTable = false;
  for (const QString& rawLn : text.split(QLatin1Char('\n'))) {
    const QString ln = rawLn.trimmed();
    if (ln.contains(QLatin1Char('\t'))) {
      QStringList cols = ln.split(QLatin1Char('\t'));
      for (QString& c : cols) {
        c = c.simplified();
        if (c.isEmpty()) c = QStringLiteral("—");
      }
      out << QStringLiteral("| ") + cols.join(QStringLiteral(" | ")) + QStringLiteral(" |");
      if (!inTabTable) {
        QStringList sep;
        for (int i = 0; i < cols.size(); ++i) sep << QStringLiteral("---");
        out << QStringLiteral("|") + sep.join(QLatin1Char('|')) + QStringLiteral("|");
        inTabTable = true;
      }
    } else {
      inTabTable = false;
      out << ln;
    }
  }
  return out.join(QLatin1Char('\n')).trimmed();
}

}  // namespace GeminiCommandRouter
