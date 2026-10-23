#include "HwpSurveyReportService.h"
#include "HwpSurveyEvidence.h"
#include "LayerFeatures.h"
#include "LayerOps.h"
#include "LocalReportRag.h"
#include "core/document_engine.hpp"
#include "core/utf_conv.hpp"
#include "hwpsdk_include.h"
#include <cmath>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

namespace HwpSurveyReportService {
namespace {
std::u16string toU16(const QString& s) { return openhwp::core::Utf8ToUtf16(s.toStdString()); }
void bindField(HWPSDK::Document* doc, const char16_t* label, const char16_t* name, const QString& val) {
  auto* impl = doc ? dynamic_cast<openhwp::core::DocumentImpl*>(doc->GetImpl()) : nullptr;
  if (!impl) return;
  const std::string nameUtf8 = openhwp::core::Utf16ToUtf8(name ? name : u""), lblUtf8 = openhwp::core::Utf16ToUtf8(label ? label : u"");
  for (auto& f : impl->Model().fields)
    if (f.name_utf8 == nameUtf8) { f.text_utf8 = val.toStdString(); return; }
  openhwp::core::FieldNode fn;
  fn.name_utf8 = nameUtf8, fn.direction_utf8 = lblUtf8, fn.memo_utf8 = lblUtf8, fn.text_utf8 = val.toStdString();
  fn.para_index = static_cast<int>(impl->Model().paragraphs.size()), impl->Model().fields.push_back(std::move(fn));
}
void insertStyledParagraph(HWPSDK::Document* doc, const QString& text, unsigned int heightHwpUnit, bool bold, unsigned int alignType) {
  auto* impl = doc ? dynamic_cast<openhwp::core::DocumentImpl*>(doc->GetImpl()) : nullptr;
  if (!impl) return;
  openhwp::core::ParagraphNode p;
  p.text_utf8 = text.toStdString(), p.char_shape.height = heightHwpUnit, p.char_shape.bold = bold ? 1U : 0U, p.para_shape.align_type = alignType;
  impl->Model().paragraphs.push_back(std::move(p));
}
void appendTableToModel(HWPSDK::Document* doc, const QVector<QStringList>& rows, uint32_t headerBgColor = 0xE3DACC) {
  auto* impl = (doc && !rows.isEmpty()) ? dynamic_cast<openhwp::core::DocumentImpl*>(doc->GetImpl()) : nullptr;
  if (!impl) return;
  const int rCount = rows.size(), cCount = rows.first().size();
  openhwp::core::TableNode tbl;
  tbl.InitializeGrid(rCount, cCount);
  tbl.treat_as_char = true, tbl.anchor_para = static_cast<int>(impl->Model().paragraphs.size()) - 1;
  for (int r = 0; r < rCount; ++r)
    for (int c = 0; c < cCount && c < rows[r].size(); ++c) {
      auto& cell = tbl.cells[static_cast<size_t>(r)][static_cast<size_t>(c)];
      cell.text_utf8 = rows[r][c].toStdString();
      if (r == 0) cell.bg_color = headerBgColor, cell.char_shape.bold = 1, cell.para_shape.align_type = 3;
    }
  impl->Model().tables.push_back(std::move(tbl));
}
}  // namespace
struct ReportDocumentSession::Impl {
  HWPSDK::Application app;
  HWPSDK::HwpObject* hwpObj = nullptr;
  HWPSDK::Document* doc = nullptr;
  Impl() { app.SetLicensePath(nullptr); app.InitInstance(); if ((hwpObj = app.GetHwpObject()) && (doc = hwpObj->CreateDocument())) doc->SetEditMode(1); }
  ~Impl() { if (hwpObj && doc) hwpObj->ReleaseDocument(doc); app.ExitInstance(); }
};
ReportDocumentSession::ReportDocumentSession() : m_impl(new Impl()) {}
ReportDocumentSession::~ReportDocumentSession() { delete m_impl; }

SurveySnapshot ReportDocumentSession::collectFromProject(QgsProject* project, bool enableRagSlot, const RagSlotCallback& ragSlot, double radiusMeters) {
  SurveySnapshot snap;
  snap.surveyTitle = QStringLiteral("경주 지역 유적 발굴(시굴)조사");
  snap.surveyCrs = QStringLiteral("EPSG:5187");
  snap.radiusMeters = radiusMeters > 0.0 ? radiusMeters : 500.0;
  if (project) {
    const QString fBase = QFileInfo(project->fileName()).completeBaseName();
    if (!project->title().trimmed().isEmpty()) snap.surveyTitle = project->title().trimmed();
    else if (!fBase.isEmpty() && !fBase.startsWith(QStringLiteral("ka-hgis"))) snap.surveyTitle = fBase;
    if (project->crs().isValid()) snap.surveyCrs = project->crs().authid();
    for (QgsVectorLayer* al : LayerOps::findAllByLayerKey(project, QStringLiteral("survey_area"))) {
      if (!al || !al->isValid()) continue;
      const QString gBase = QFileInfo(al->source().section(QLatin1Char('|'), 0, 0)).completeBaseName();
      if (!gBase.isEmpty() && !gBase.startsWith(QStringLiteral("ka-hgis")) && snap.surveyTitle.startsWith(QStringLiteral("경주 지역"))) snap.surveyTitle = gBase;
      QgsFeatureIterator it = al->getFeatures(); QgsFeature f;
      while (it.nextFeature(f)) if (f.hasGeometry()) snap.totalAreaSqm += std::abs(f.geometry().area()), ++snap.surveyPolygonCount;
    }
    for (const auto& def : QList<QPair<QString, QString>>{{QStringLiteral("survey_area"), QStringLiteral("조사구역")}, {QStringLiteral("trial_trench"), QStringLiteral("트렌치")},
                                                          {QStringLiteral("feature_poly"), QStringLiteral("유구(면)")}, {QStringLiteral("feature_line"), QStringLiteral("유구(선)")},
                                                          {QStringLiteral("artifact_point"), QStringLiteral("유물(점)")}, {QStringLiteral("section_line"), QStringLiteral("단면선")},
                                                          {QStringLiteral("control_points"), QStringLiteral("기준점")}}) {
      int cnt = 0;
      for (QgsVectorLayer* vl : LayerOps::findAllByLayerKey(project, def.first)) if (vl && vl->isValid()) cnt += static_cast<int>(LayerFeatures::count(vl));
      snap.domainCounts.push_back({def.first, def.second, cnt});
    }
    if (const auto nearby = NearbyHeritageExport::collect(project, nullptr, snap.radiusMeters); nearby.ok()) snap.nearbySites = nearby.items;
  }
  populateRagEvidence(&snap, enableRagSlot);
  if (enableRagSlot && ragSlot) snap.ragCitationSlotText = ragSlot(snap);
  else if (!enableRagSlot) snap.ragCitationSlotText = QStringLiteral("[RAG 연결 슬롯 대기 중 — 로컬 발굴조사보고서 인덱싱 완료 후 자동 인용 연결]");
  return snap;
}

bool ReportDocumentSession::buildStandardReport(const SurveySnapshot& snap, QString* errorOut, int maxStage) {
  if (!m_impl || !m_impl->doc) return errorOut ? (*errorOut = QStringLiteral("OpenHwpSDK 초기화 실패"), false) : false;
  SurveySnapshot s = snap;
  if (s.siteEntries.isEmpty() && !s.nearbySites.isEmpty()) populateRagEvidence(&s, false);
  HWPSDK::Document* doc = m_impl->doc;
  doc->Clear(), doc->SetEditMode(1);
  const QString areaStr = QLocale(QLocale::Korean).toString(std::round(s.totalAreaSqm)) + QStringLiteral(" ㎡");
  insertStyledParagraph(doc, QStringLiteral("%1 문화재 발굴(시굴)조사 보고서").arg(s.surveyTitle), 1800, true, 3);
  insertStyledParagraph(doc, QStringLiteral("〈일러두기〉 (동국문화재연구원 보고서 작성 기준)\n1. 도면 방위는 조사 당시 자북(磁北) 기준이며, 토층·단면 수치는 해발고도(m)를 나타낸다.\n2. 도면 축소비율은 유구배치도 1/200, 수혈·건물지 평단면도 1/20·1/40(구상유구 1/80), 유물 실측도 1/3을 기본으로 한다.\n3. 유물 일련번호는 유구별 토기류→기와류→기타 순으로 부여하고 토색·색조는 『新版 標準土色帖』(2007판)을 기준으로 한다."), 920, false, 0);
  insertStyledParagraph(doc, QStringLiteral("Ⅰ. 머리말 (조사경위·조사단 구성·발굴조사 요약)"), 1300, true, 1);
  insertStyledParagraph(doc, QStringLiteral("1. 조사명: %1\n2. 조사면적: %2 (%3개 구역)\n3. 작업좌표계: %4\n4. 지역 보고서 RAG: %5 %6권(본문 %7 · 스캔 %8)").arg(s.surveyTitle, areaStr).arg(s.surveyPolygonCount).arg(s.surveyCrs, s.regionName).arg(s.regionTotalReports).arg(s.regionTextReports).arg(s.regionScanReports), 1000, false, 0);
  bindField(doc, u"조사명", u"SURVEY_NAME", s.surveyTitle), bindField(doc, u"조사면적", u"SURVEY_AREA_M2", areaStr), bindField(doc, u"좌표계", u"SURVEY_CRS", s.surveyCrs);
  if (maxStage <= 1) return true;

  insertStyledParagraph(doc, QStringLiteral("Ⅱ. 유적의 위치와 환경 — 2. 역사·고고 환경 (<표 1>)"), 1300, true, 1);
  QVector<QStringList> siteRows = {{QStringLiteral("연번"), QStringLiteral("유적명"), QStringLiteral("최단거리·방위(G)"),
                                    QStringLiteral("GIS 보고서명(공식 서지)"), QStringLiteral("시대(R)"),
                                    QStringLiteral("유구·층위(R)"), QStringLiteral("원문 대조")}};
  for (int i = 0; i < s.siteEntries.size(); ++i) {
    const auto& e = s.siteEntries[i];
    const QString bib = Detail::bibliographyLine(e);
    siteRows.push_back({QString::number(i + 1), e.siteName, QStringLiteral("%1 %2m").arg(e.direction).arg(e.distanceMeters),
                        bib.isEmpty() ? QStringLiteral("서지 없음") : bib, e.eraSummary, e.featureSummary,
                        Detail::reportSourceLabel(e)});
  }
  if (siteRows.size() == 1)
    siteRows.push_back({QStringLiteral("-"), QStringLiteral("반경 내 주변유적 없음"), QStringLiteral("-"),
                        QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("-")});
  appendTableToModel(doc, siteRows);
  for (int i = 0; i < s.siteEntries.size() && i < 4; ++i) {
    const auto& e = s.siteEntries[i];
    const QString bib = Detail::bibliographyLine(e);
    const QString meta = e.institution.isEmpty() ? QString() : QStringLiteral(" / %1").arg(e.institution);
    QStringList facts;
    if (!e.heritageCode.isEmpty()) facts << QStringLiteral("유산코드 %1").arg(e.heritageCode);
    if (!e.eraField.isEmpty()) facts << QStringLiteral("시대 %1").arg(e.eraField);
    if (!e.heritagePresence.isEmpty()) facts << QStringLiteral("유적유무 %1").arg(e.heritagePresence);
    if (!e.permitNumber.isEmpty()) facts << QStringLiteral("허가번호 %1").arg(e.permitNumber);
    if (!e.siteAddress.isEmpty()) facts << QStringLiteral("소재지 %1").arg(e.siteAddress);
    insertStyledParagraph(doc, QStringLiteral("(%1) %2 [조사구역 경계 기준 %3 %4m]\n  • GIS 서지: %5%6\n  • GIS 원자료: %7\n  • 층위·유구: %8 / %9\n  • ⑤ 조사구역과의 연관성: %10")
                                   .arg(i + 1).arg(e.siteName, e.direction).arg(e.distanceMeters)
                                   .arg(bib.isEmpty() ? QStringLiteral("서지 없음") : bib, meta)
                                   .arg(facts.isEmpty() ? QStringLiteral("기재 없음") : facts.join(QStringLiteral(" · ")),
                                        e.strataSummary, e.featureSummary, e.relationToSurvey), 950, false, 0);
  }
  if (maxStage == 2) return true;

  insertStyledParagraph(doc, QStringLiteral("Ⅲ. 조사 범위와 방법 및 층위 / Ⅳ. 조사 내용 (<표 2>)"), 1300, true, 1);
  QVector<QStringList> domRows = {{QStringLiteral("레이어 구분"), QStringLiteral("식별 키"), QStringLiteral("도형 수(건)")}};
  for (const auto& d : s.domainCounts) domRows.push_back({d.label, d.layerKey, QString::number(d.featureCount)});
  appendTableToModel(doc, domRows);
  if (maxStage == 3) return true;

  insertStyledParagraph(doc, QStringLiteral("Ⅴ. 고찰 (1. 유구 · 2. 유물) 및 Ⅵ. 맺음말"), 1300, true, 1);
  insertStyledParagraph(doc, s.discussionDraft, 1000, false, 0);
  insertStyledParagraph(doc, s.ragCitationSlotText, 1000, false, 0);
  bindField(doc, u"고찰본문", u"DISCUSSION_BODY", s.discussionDraft);
  bindField(doc, u"RAG인용슬롯", u"RAG_CITATION_SLOT", s.ragCitationSlotText);
  return true;
}

bool ReportDocumentSession::openTemplateAndFill(const std::filesystem::path& templatePath, const SurveySnapshot& snap, QString* errorOut) {
  if (!m_impl || !m_impl->doc || !m_impl->doc->Open(openhwp::core::PathToUtf16(templatePath).c_str()))
    return errorOut ? (*errorOut = QStringLiteral("한글 양식 파일을 열지 못했습니다."), false) : false;
  const QString areaStr = QLocale(QLocale::Korean).toString(std::round(snap.totalAreaSqm)) + QStringLiteral(" ㎡");
  for (const auto& kv : QList<QPair<QString, QString>>{{QStringLiteral("SURVEY_NAME"), snap.surveyTitle}, {QStringLiteral("사업명"), snap.surveyTitle},
                                                       {QStringLiteral("SURVEY_AREA_M2"), areaStr}, {QStringLiteral("SURVEY_CRS"), snap.surveyCrs},
                                                       {QStringLiteral("DISCUSSION_BODY"), snap.discussionDraft}, {QStringLiteral("RAG_CITATION_SLOT"), snap.ragCitationSlotText}})
    setFieldValue(kv.first, kv.second);
  return true;
}
bool ReportDocumentSession::saveAs(const std::filesystem::path& outputPath, const QString& format, QString* errorOut) {
  const std::u16string fmtU16 = toU16(format.trimmed().isEmpty() ? QStringLiteral("HWPX") : format.toUpper());
  if (m_impl && m_impl->doc && m_impl->doc->SaveAs(openhwp::core::PathToUtf16(outputPath).c_str(), fmtU16.c_str())) return true;
  return errorOut ? (*errorOut = QStringLiteral("저장 실패"), false) : false;
}
QVector<FieldEntry> ReportDocumentSession::fields() const {
  QVector<FieldEntry> out;
  if (auto* impl = (m_impl && m_impl->doc) ? dynamic_cast<openhwp::core::DocumentImpl*>(m_impl->doc->GetImpl()) : nullptr)
    for (const auto& f : impl->Model().fields) out.push_back({QString::fromStdString(f.name_utf8), QString::fromStdString(f.text_utf8)});
  return out;
}
bool ReportDocumentSession::setFieldValue(const QString& fieldName, const QString& value) {
  const std::u16string nameU16 = toU16(fieldName);
  return m_impl && m_impl->doc && m_impl->doc->FieldExist(nameU16.c_str()) && (m_impl->doc->PutFieldText(nameU16.c_str(), toU16(value).c_str()), true);
}

bool ReportDocumentSession::applyUserRequest(const QString& userPrompt, const SurveySnapshot& snap, QString* replyOut) {
  const QString req = userPrompt.trimmed();
  auto* impl = (!req.isEmpty() && m_impl && m_impl->doc) ? dynamic_cast<openhwp::core::DocumentImpl*>(m_impl->doc->GetImpl()) : nullptr;
  if (!impl) return false;
  const QRegularExpression rx(QStringLiteral("['\"]([^'\"]+)['\"]\\s*(?:을|를)?\\s*['\"]([^'\"]+)['\"]\\s*(?:으로|로)?\\s*(?:바꿔|변경|치환)"));
  if (const QRegularExpressionMatch m = rx.match(req); m.hasMatch()) {
    const std::string fromStr = m.captured(1).toStdString(), toStr = m.captured(2).toStdString();
    auto replaceAll = [&](std::string& s) {
      for (std::size_t p = 0; (p = s.find(fromStr, p)) != std::string::npos; p += toStr.size()) s.replace(p, fromStr.size(), toStr);
    };
    for (auto& p : impl->Model().paragraphs) replaceAll(p.text_utf8);
    for (auto& f : impl->Model().fields) replaceAll(f.text_utf8);
    if (replyOut) *replyOut = QStringLiteral("'%1' → '%2' 치환 완료").arg(m.captured(1), m.captured(2));
    return true;
  }
  const std::string defReg = (snap.regionName.isEmpty() ? Detail::detectRegionName(snap) : snap.regionName).toStdString();
  const std::string regScope = strata::core::LocalReportRag::extractRegionScope(req.toStdString(), defReg);
  const auto hits = strata::core::LocalReportRag::search(req.toStdString(), regScope, 8, 2);
  QString gen = QStringLiteral("[다차원 고찰 보완 · 지역범위: %1] %2").arg(QString::fromStdString(regScope), req);
  QVector<QStringList> rows = {{QStringLiteral("기호"), QStringLiteral("권역·시군구"), QStringLiteral("보고서명(쪽)"), QStringLiteral("핵심 원문 발췌")}};
  for (std::size_t i = 0; i < hits.size(); ++i) {
    const auto& h = hits[i];
    const QString snip = QString::fromStdString(h.snippet.empty() ? h.content : h.snippet).simplified().left(110);
    gen += QStringLiteral("\n  • [R%1: %2 『%3』 p.%4] %5").arg(i + 1).arg(QString::fromStdString(h.sigungu), QString::fromStdString(h.report_name)).arg(h.page_no).arg(snip);
    rows.push_back({QStringLiteral("R%1").arg(i + 1), QString::fromStdString(h.macro_region + " · " + h.sigungu), QStringLiteral("%1 (p.%2)").arg(QString::fromStdString(h.report_name)).arg(h.page_no), snip});
  }
  insertStyledParagraph(m_impl->doc, gen, 1000, false, 0);
  if (rows.size() > 1) appendTableToModel(m_impl->doc, rows);
  setFieldValue(QStringLiteral("DISCUSSION_BODY"), snap.discussionDraft + QStringLiteral("\n") + gen);
  if (replyOut) *replyOut = QStringLiteral("지역(%1) 기준 RAG 원문 %2건을 분류·반영했습니다.").arg(QString::fromStdString(regScope)).arg(hits.size());
  return true;
}

QString ReportDocumentSession::renderPreviewHtml(const QString& liveStageLabel, int zoomPercent, bool showFieldMarks, const QString& extraVisualsHtml) const {
  auto* impl = (m_impl && m_impl->doc) ? dynamic_cast<openhwp::core::DocumentImpl*>(m_impl->doc->GetImpl()) : nullptr;
  if (!impl) return {};
  const auto& model = impl->Model();
  const int pageW = qBound(420, 680 * zoomPercent / 100, 1300);
  QString html = QStringLiteral(
      "<html><body style='background:#EAE6DF;margin:0;padding:18px;font-family:\"Noto Serif CJK KR\",\"NanumMyeongjo\",serif;'>"
      "<table align='center' width='%1' cellspacing='0' cellpadding='0' style='background:#FAF8F5;border:1px solid #C8C1B4;'>"
      "<tr><td style='padding:20px 38px 8px 38px;color:#6E6A63;font-size:8.5pt;border-bottom:1px solid #DDD6C9;'>"
      "<span>┏ 한글(HWPX/HWP) A4 조판 (210×297mm)</span><span style='float:right;'>┓</span></td></tr>"
      "<tr><td style='padding:20px 40px;color:#1F1E1B;line-height:1.72;'>").arg(pageW);
  if (!liveStageLabel.isEmpty())
    html += QStringLiteral("<div style='background:#F3ECE2;border:1px solid #B5573A;color:#1F1E1B;padding:6px 10px;margin-bottom:10px;font-weight:bold;font-size:9pt;'>[진행 단계] %1</div>").arg(liveStageLabel.toHtmlEscaped());
  auto renderTableHtml = [](const openhwp::core::TableNode& tbl) {
    QString t = QStringLiteral("<table border='1' cellspacing='0' cellpadding='5' style='border-collapse:collapse;width:100%;margin:10px 0;font-size:8.6pt;border-top:2px solid #1F1E1B;border-bottom:2px solid #1F1E1B;border-color:#A8A196;'>");
    for (int r = 0; r < tbl.rows; ++r) {
      t += QStringLiteral("<tr>");
      for (int c = 0; c < tbl.cols; ++c)
        t += QStringLiteral("<td align='%4' style='background:%1;font-weight:%2;'>%3</td>")
                 .arg(r == 0 ? QStringLiteral("#E3DACC") : QStringLiteral("#FAF8F5"), r == 0 ? QStringLiteral("bold") : QStringLiteral("normal"),
                      QString::fromStdString(tbl.cells[static_cast<size_t>(r)][static_cast<size_t>(c)].text_utf8).toHtmlEscaped(), r == 0 ? QStringLiteral("center") : QStringLiteral("left"));
      t += QStringLiteral("</tr>");
    }
    return t + QStringLiteral("</table>");
  };
  for (size_t i = 0; i < model.paragraphs.size(); ++i) {
    const auto& p = model.paragraphs[i];
    const QString txt = QString::fromStdString(p.text_utf8).trimmed();
    if (!txt.isEmpty())
      html += QStringLiteral("<p style='font-size:%1pt;font-weight:%2;text-align:%3;margin:7px 0;'>%4</p>")
                  .arg(qMax(10, static_cast<int>(p.char_shape.height / 100))).arg(p.char_shape.bold ? QStringLiteral("bold") : QStringLiteral("normal"),
                       p.para_shape.align_type == 3 ? QStringLiteral("center") : QStringLiteral("left"), txt.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>")));
    for (const auto& tbl : model.tables) if (tbl.anchor_para == static_cast<int>(i)) html += renderTableHtml(tbl);
  }
  for (const auto& tbl : model.tables) if (tbl.anchor_para < 0 || tbl.anchor_para >= static_cast<int>(model.paragraphs.size())) html += renderTableHtml(tbl);
  if (!extraVisualsHtml.isEmpty()) html += extraVisualsHtml;
  if (showFieldMarks && !model.fields.empty()) {
    html += QStringLiteral("<div style='margin-top:14px;padding:8px;background:#F3EFE8;border:1px dashed #B5573A;font-size:8.4pt;color:#4B4842;'><b style='color:#B5573A;'>⟪누름틀⟫</b> ");
    for (const auto& f : model.fields) html += QStringLiteral("<b>⟪%1⟫</b> %2 &nbsp; ").arg(QString::fromStdString(f.name_utf8).toHtmlEscaped(), QString::fromStdString(f.text_utf8).toHtmlEscaped());
    html += QStringLiteral("</div>");
  }
  return html + QStringLiteral("</td></tr><tr><td align='center' style='padding:10px 38px 18px 38px;color:#6E6A63;font-size:8.4pt;border-top:1px solid #DDD6C9;'>┗ - 1 - ┛</td></tr></table></body></html>");
}
}  // namespace HwpSurveyReportService
