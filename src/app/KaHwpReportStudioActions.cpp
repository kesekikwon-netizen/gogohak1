// KaHwpReportStudio의 근거 표·원문 대조·DBpia·저장 동작.
// KaHwpLivePaperWindow.cpp에서 창 조판 코드와 분리했다.
#include "KaGeminiBridgeClient.h"
#include "KaHwpLivePaperWindow.h"
#include "KaHwpReportStudio.h"
#include "KaIcons.h"
#include "KaTheme.h"
#include "core/GisFactHarness.h"
#include "core/LocalReportRag.h"
#include <QtWidgets>
#include <qgsproject.h>

void KaHwpReportStudio::updateStepRail(int activeStep) {
  if (!m_stepList) return;
  m_stepList->clear();
  const QStringList steps = {QStringLiteral("1. 조사 개요 작성 (GIS 값)"), QStringLiteral("2. 주변유적 현황 작성"),
                             QStringLiteral("3. 조사 범위(도형 수) 작성"), QStringLiteral("4. 고찰 틀과 부록 작성")};
  for (int i = 0; i < steps.size(); ++i)
    m_stepList->addItem(((i + 1 < activeStep) ? QStringLiteral("✓ ")
                                              : ((i + 1 == activeStep) ? QStringLiteral("• ") : QStringLiteral("○ "))) +
                        steps[i]);
}

void KaHwpReportStudio::populateEvidenceTable() {
  if (!m_evidenceTable) return;
  m_evidenceTable->setRowCount(m_cachedSnap.siteEntries.size());
  for (int i = 0; i < m_cachedSnap.siteEntries.size(); ++i) {
    const auto& e = m_cachedSnap.siteEntries[i];
    const QString rptBadge = e.reportStatus == QStringLiteral("✓")
                                 ? QStringLiteral("✓ p.%1").arg(e.pageNo > 0 ? e.pageNo : 1)
                                 : (e.reportStatus == QStringLiteral("△")
                                        ? QStringLiteral("△ 참고")
                                        : (e.reportStatus == QStringLiteral("◌")
                                               ? QStringLiteral("◌ 스캔")
                                               : (e.hasLocalPdf ? QStringLiteral("○ 색인 전")
                                                                : (e.bibliography.trimmed().isEmpty()
                                                                       ? QStringLiteral("○ 서지 없음")
                                                                       : QStringLiteral("○ 보고서 미수록")))));
    const QStringList cols = {QString::number(i + 1),
                              e.siteName,
                              QStringLiteral("%1m").arg(e.distanceMeters),
                              e.direction,
                              e.eraStatus,
                              e.featureStatus,
                              e.strataStatus,
                              rptBadge};
    for (int c = 0; c < cols.size(); ++c) {
      auto* item = new QTableWidgetItem(cols[c]);
      if (c != 1) item->setTextAlignment(Qt::AlignCenter);
      m_evidenceTable->setItem(i, c, item);
    }
  }
  if (!m_cachedSnap.siteEntries.isEmpty())
    m_evidenceTable->selectRow(0);
  else
    onSiteRowSelected(-1);
}

void KaHwpReportStudio::onSiteRowSelected(int row) {
  if (!m_siteDetailView) return;
  if (row < 0 || row >= m_cachedSnap.siteEntries.size()) {
    m_siteDetailView->setHtml(
        QStringLiteral("<div style='color:#6E6A63;font-size:8.8pt;'>지도에 주변유적을 올리거나 행을 선택하면 원문 "
                       "발췌와 <b>⑤ 조사구역과의 연관성</b>이 표시됩니다.</div>"));
    return;
  }
  const auto& e = m_cachedSnap.siteEntries[row];
  const QString bib =
      e.bibliography.trimmed().isEmpty() ? QStringLiteral("서지 없음") : QStringLiteral("『%1』").arg(e.bibliography);
  QStringList bibMetaParts;
  if (!e.institution.isEmpty()) bibMetaParts << e.institution;
  if (e.reportYear > 0) bibMetaParts << QStringLiteral("%1년").arg(e.reportYear);
  const QString bibMeta =
      bibMetaParts.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(bibMetaParts.join(QStringLiteral(", ")));
  m_siteDetailView->setHtml(
      QStringLiteral("<div style='font-size:8.6pt;line-height:1.55;color:#1F1E1B;'>"
                     "<b>[%1]</b> 조사구역 경계 기준 <b>%2 %3m</b> · 원문: <b>%4</b><br/>"
                     "• <b>공식 서지:</b> %5%6<br/>"
                     "• <b>층위·토층(③):</b> %7<br/>• <b>유구·시대(④):</b> [%8] %9<br/>"
                     "• <b style='color:#B5573A;'>⑤ 조사구역과의 연관성:</b> %10</div>")
          .arg(e.siteName.toHtmlEscaped(), e.direction.toHtmlEscaped())
          .arg(e.distanceMeters)
          .arg(e.reportName.isEmpty() ? QStringLiteral("원문 미확보(○)")
                                      : QStringLiteral("%1 (%2)").arg(e.reportName.toHtmlEscaped(), e.reportStatus))
          .arg(bib.toHtmlEscaped(), bibMeta.toHtmlEscaped(), e.strataSummary.toHtmlEscaped(),
               e.eraSummary.toHtmlEscaped(), e.featureSummary.toHtmlEscaped(), e.relationToSurvey.toHtmlEscaped()));
}

void KaHwpReportStudio::refreshFromActiveProject(bool forceReload) {
  if (!m_hasCachedSnap || forceReload) {
    m_cachedSnap = HwpSurveyReportService::ReportDocumentSession::collectFromProject(
        QgsProject::instance(), m_ragSlotCheck && m_ragSlotCheck->isChecked(), {}, m_radiusMeters);
    if (m_regionCombo && !m_regionCombo->currentData().toString().isEmpty()) {
      m_cachedSnap.regionName = m_regionCombo->currentData().toString();
      const auto rs = strata::core::LocalReportRag::queryRegionStats(m_cachedSnap.regionName.toStdString());
      m_cachedSnap.regionTotalReports = rs.total_reports, m_cachedSnap.regionTextReports = rs.text_reports,
      m_cachedSnap.regionScanReports = rs.scan_reports;
    }
    m_hasCachedSnap = true;
  }
  if (m_compareView) m_compareView->clear();
  m_session->buildStandardReport(m_cachedSnap, nullptr, 4);
  m_summaryLabel->setText(QStringLiteral("조사명: %1 · 면적 %2㎡ (%3) · 반경 %4m 주변유적 %5개소")
                              .arg(m_cachedSnap.surveyTitle)
                              .arg(qRound(m_cachedSnap.totalAreaSqm))
                              .arg(m_cachedSnap.surveyCrs)
                              .arg(qRound(m_radiusMeters))
                              .arg(m_cachedSnap.siteEntries.size()));
  m_connStatusLabel->setText(
      QStringLiteral("연결 상태 — 발굴조사보고서(%1): 총 %2권 (본문 %3권 · 스캔 %4권) | DBpia 문헌: %5편")
          .arg(m_cachedSnap.regionName.isEmpty() ? QStringLiteral("경주시") : m_cachedSnap.regionName)
          .arg(m_cachedSnap.regionTotalReports)
          .arg(m_cachedSnap.regionTextReports)
          .arg(m_cachedSnap.regionScanReports)
          .arg(m_dbpiaSummary.collectedPapers.size()));
  populateEvidenceTable();
  syncUiFromSession();
}

void KaHwpReportStudio::startDbpiaResearch(const QString& customTopic) {
  GisFactHarness::Ledger ledger = GisFactHarness::buildLedger(QgsProject::instance());
  if (!customTopic.trimmed().isEmpty()) ledger.dbpiaQueries.prepend(customTopic.trimmed());
  if (ledger.dbpiaQueries.isEmpty())
    ledger.dbpiaQueries = {m_cachedSnap.regionName + QStringLiteral(" 발굴조사 유구 편년")};
  m_dbpiaBridge->startResearchAndEvaluate(customTopic, ledger, qRound(m_radiusMeters),
                                          m_isThesisMode ? QStringLiteral("thesis") : QStringLiteral("report"), false);
}

void KaHwpReportStudio::openLivePaperWindow() {
  if (!m_liveWindow) {
    m_liveWindow = new KaHwpLivePaperWindow(this);
    m_liveWindow->setActionCallbacks([this](const QString& txt) { submitUserPrompt(txt); },
                                     [this] { syncUiFromSession(); }, [this] { onSaveHwpxClicked(); },
                                     [this] { onSaveHwp5Clicked(); });
  }
  m_liveWindow->show();
  m_liveWindow->raise();
  m_liveWindow->activateWindow();
  syncUiFromSession();
}

void KaHwpReportStudio::startLiveOrganizeDemo() {
  refreshFromActiveProject(true);
  m_currentStage = 0;
  m_stageTimer->start();
}

void KaHwpReportStudio::appendChatMessage(const QString& role, const QString& text) {
  if (m_chatView)
    m_chatView->append(
        QStringLiteral("<div style='margin:3px 0;font-size:8.6pt;color:#1F1E1B;'><b>[%1]</b> %2</div>")
            .arg(role.toHtmlEscaped(), text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"))));
}

void KaHwpReportStudio::onSendPromptClicked() {
  const QString text = m_promptEdit ? m_promptEdit->text().trimmed() : QString();
  if (text.isEmpty()) {
    startLiveOrganizeDemo();
    return;
  }
  m_promptEdit->clear();
  submitUserPrompt(text);
}

void KaHwpReportStudio::submitUserPrompt(const QString& promptText) {
  const QString req = promptText.trimmed();
  if (req.isEmpty()) return;
  appendChatMessage(QStringLiteral("요청"), req);
  QString reply;
  if (m_session->applyUserRequest(req, m_cachedSnap, &reply)) appendChatMessage(QStringLiteral("반영"), reply);
  if (m_compareView) m_compareView->setHtml(HwpAcademicVisuals::renderFeatureComparisonHtml(m_cachedSnap, req));
  if (req.contains(QStringLiteral("비교")) || req.contains(QStringLiteral("현황")) ||
      req.contains(QStringLiteral("분포")) || req.contains(QStringLiteral("변화")))
    m_centerStack->setCurrentWidget(m_compareView);
  if (req.contains(QStringLiteral("논문")) || req.contains(QStringLiteral("DBpia"), Qt::CaseInsensitive))
    appendChatMessage(QStringLiteral("안내"), QStringLiteral("논문 수집은 아직 이 화면에서 하지 않습니다. 지금은 GIS 값과 보고서 근거로 작성합니다."));
  syncUiFromSession(QStringLiteral("요청 반영 완료: ") + req, 100);
}

void KaHwpReportStudio::onOpenTemplateClicked() {
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("한글 보고서 양식 열기"), QDir::homePath(),
                                                    QStringLiteral("한글 문서 (*.hwpx *.hwp)"));
  QString err;
  if (!path.isEmpty() && !m_session->openTemplateAndFill(std::filesystem::path(path.toStdString()), m_cachedSnap, &err))
    QMessageBox::warning(this, QStringLiteral("양식 열기 실패"), err);
  else if (!path.isEmpty())
    syncUiFromSession(QStringLiteral("기관 한글 양식 칸 자동 기입 완료"), 100);
}

void KaHwpReportStudio::onSaveHwpxClicked() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("한글 표준(.hwpx)으로 저장"),
                                                    QDir::home().filePath(QStringLiteral("발굴조사보고서.hwpx")),
                                                    QStringLiteral("한글 표준 문서 (*.hwpx)"));
  QString err;
  if (!path.isEmpty() && !m_session->saveAs(std::filesystem::path(path.toStdString()), QStringLiteral("HWPX"), &err))
    QMessageBox::warning(this, QStringLiteral("저장 실패"), err);
}

void KaHwpReportStudio::onSaveHwp5Clicked() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("한글 5.0(.hwp)으로 저장"),
                                                    QDir::home().filePath(QStringLiteral("발굴조사보고서.hwp")),
                                                    QStringLiteral("한글 5.0 문서 (*.hwp)"));
  QString err;
  if (!path.isEmpty() && !m_session->saveAs(std::filesystem::path(path.toStdString()), QStringLiteral("HWP"), &err))
    QMessageBox::warning(this, QStringLiteral("저장 실패"), err);
}

void KaHwpReportStudio::onFieldCellChanged(int row, int column) {
  if (m_updatingTable || column != 1) return;
  QTableWidgetItem *nameItem = m_fieldTable->item(row, 0), *valItem = m_fieldTable->item(row, 1);
  if (nameItem && valItem && m_session->setFieldValue(nameItem->text(), valItem->text()))
    syncUiFromSession(QStringLiteral("양식 칸 갱신: ") + nameItem->text(), 100);
}

void KaHwpReportStudio::syncUiFromSession(const QString& liveStageLabel, int progressPercent) {
  m_updatingTable = true;
  const auto list = m_session->fields();
  m_fieldTable->setRowCount(list.size());
  for (int i = 0; i < list.size(); ++i) {
    auto* kItem = new QTableWidgetItem(list[i].name);
    kItem->setFlags(kItem->flags() & ~Qt::ItemIsEditable);
    m_fieldTable->setItem(i, 0, kItem);
    m_fieldTable->setItem(i, 1, new QTableWidgetItem(list[i].value));
  }
  m_updatingTable = false;
  const int zoom = (m_zoomCombo && m_zoomCombo->currentIndex() == 0)
                       ? 90
                       : ((m_zoomCombo && m_zoomCombo->currentIndex() == 2) ? 120 : 100);
  const QString visualsHtml = HwpAcademicVisuals::renderVisualsAndDbpiaSectionHtml(m_cachedSnap, m_dbpiaSummary);
  const QString html = m_session->renderPreviewHtml(liveStageLabel, zoom, true, visualsHtml);
  m_previewView->setHtml(html);
  if (m_verifyView)
    m_verifyView->setHtml(HwpAcademicVisuals::renderVerificationReportHtml(m_cachedSnap, m_dbpiaSummary));
  if (m_compareView && m_compareView->toPlainText().isEmpty())
    m_compareView->setHtml(HwpAcademicVisuals::renderFeatureComparisonHtml(m_cachedSnap));
  if (m_liveWindow) m_liveWindow->updatePaperHtml(html, liveStageLabel, progressPercent);
}

void KaHwpReportStudio::attach(QMainWindow* mainWindow) {
  if (!mainWindow) return;
  auto* tabs = mainWindow->findChild<QTabWidget*>(QStringLiteral("viewTabs"));
  auto* studioPage = new KaHwpReportStudio(tabs ? static_cast<QWidget*>(tabs) : mainWindow);
  studioPage->setObjectName(QStringLiteral("kaHwpReportStudio"));
  studioPage->setWindowFlags(Qt::Widget);
  studioPage->hide();
  auto openStudio = [tabs, studioPage]() {
    if (!tabs) return;
    if (tabs->indexOf(studioPage) < 0)
      tabs->addTab(studioPage, KaIcons::icon(QStringLiteral("note"), KaTheme::tokens().inkMuted),
                   QStringLiteral("보고서"));
    studioPage->show();
    tabs->setCurrentWidget(studioPage);
    studioPage->refreshFromActiveProject(true);
  };
  if (tabs)
    QObject::connect(tabs, &QTabWidget::currentChanged, studioPage, [tabs, studioPage](int) {
      if (tabs->currentWidget() == studioPage) studioPage->refreshFromActiveProject(true);
    });
  auto* actReport =
      new QAction(KaIcons::icon(QStringLiteral("note")), QStringLiteral("보고서 작성 (Ctrl+G)"), mainWindow);
  actReport->setObjectName(QStringLiteral("actionReportStudio"));
  actReport->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
  actReport->setShortcutContext(Qt::WindowShortcut);
  QObject::connect(actReport, &QAction::triggered, mainWindow, openStudio);
  mainWindow->addAction(actReport);
  for (auto* btn : mainWindow->findChildren<QToolButton*>())
    if (btn->text() == QStringLiteral("더보기") && btn->menu()) btn->menu()->addAction(actReport);
}
