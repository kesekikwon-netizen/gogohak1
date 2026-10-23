#pragma once

#include "core/HwpAcademicVisuals.h"
#include "core/HwpSurveyReportService.h"

#include <QDialog>
#include <memory>

class KaGeminiBridgeClient;
class KaHwpLivePaperWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMainWindow;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;
class QTimer;
class QToolButton;

class KaHwpReportStudio : public QDialog {
  Q_OBJECT
 public:
  explicit KaHwpReportStudio(QWidget* parent = nullptr);
  ~KaHwpReportStudio() override;

  static void attach(QMainWindow* mainWindow);

  void refreshFromActiveProject(bool forceReload = false);
  void startLiveOrganizeDemo();
  void startDbpiaResearch(const QString& customTopic = {});
  void openLivePaperWindow();
  void submitUserPrompt(const QString& promptText);

 public slots:
  void onOpenTemplateClicked();
  void onSaveHwpxClicked();
  void onSaveHwp5Clicked();
  void onSaveAssessmentClicked();
  void refreshAssessmentView();
  void onFieldCellChanged(int row, int column);
  void onSendPromptClicked();

 private:
  void syncUiFromSession(const QString& liveStageLabel = {}, int progressPercent = 100);
  void updateStepRail(int activeStep);
  void populateEvidenceTable();
  void onSiteRowSelected(int row);
  void appendChatMessage(const QString& role, const QString& text);

  std::unique_ptr<HwpSurveyReportService::ReportDocumentSession> m_session;
  HwpSurveyReportService::SurveySnapshot m_cachedSnap;
  bool m_hasCachedSnap = false;
  double m_radiusMeters = 500.0;
  bool m_isThesisMode = false;
  KaGeminiBridgeClient* m_dbpiaBridge = nullptr;
  HwpAcademicVisuals::DbpiaSummary m_dbpiaSummary;
  KaHwpLivePaperWindow* m_liveWindow = nullptr;
  QTimer* m_stageTimer = nullptr;
  int m_currentStage = 4;

  QLabel* m_summaryLabel = nullptr;
  QLabel* m_connStatusLabel = nullptr;
  QToolButton* m_btnRadius500 = nullptr;
  QToolButton* m_btnRadius1000 = nullptr;
  QCheckBox* m_ragSlotCheck = nullptr;
  QTableWidget* m_evidenceTable = nullptr;
  QTextBrowser* m_siteDetailView = nullptr;

  QStackedWidget* m_centerStack = nullptr;
  QTextBrowser* m_previewView = nullptr;
  QTextBrowser* m_verifyView = nullptr;
  QTextBrowser* m_compareView = nullptr;
  QTextBrowser* m_assessView = nullptr;
  QComboBox* m_regionCombo = nullptr;
  QComboBox* m_zoomCombo = nullptr;

  QListWidget* m_stepList = nullptr;
  QTextBrowser* m_chatView = nullptr;
  QTableWidget* m_fieldTable = nullptr;
  QLineEdit* m_promptEdit = nullptr;
  QPushButton* m_btnPrimary = nullptr;
  bool m_updatingTable = false;
};
