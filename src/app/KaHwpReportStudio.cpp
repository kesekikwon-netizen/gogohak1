#include "KaHwpReportStudio.h"
#include "KaGeminiBridgeClient.h"
#include "KaHwpLivePaperWindow.h"
#include "KaIcons.h"
#include "KaTheme.h"
#include "core/GisFactHarness.h"
#include "core/LocalReportRag.h"
#include <QtWidgets>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <qgsproject.h>

namespace {
QToolButton* makeCheckBtn(QWidget* parent, const QString& text, bool checked = false) {
  auto* b = new QToolButton(parent);
  b->setText(text);
  b->setCheckable(true);
  b->setChecked(checked);
  return b;
}
// DBpia 로그인 창 전용 프로필(Gemini와 분리). 기본은 쿠키를 저장하지 않고, 「로그인 기억」을 켜면 저장한다.
QWebEngineProfile* dbpiaProfile() {
  static auto* prof = [] {
    auto* p = new QWebEngineProfile(QStringLiteral("ka-hgis-dbpia"), qApp);
    p->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    return p;
  }();
  return prof;
}
}  // namespace

KaHwpReportStudio::KaHwpReportStudio(QWidget* parent)
    : QDialog(parent),
      m_session(std::make_unique<HwpSurveyReportService::ReportDocumentSession>()),
      m_dbpiaBridge(new KaGeminiBridgeClient(this)) {
  setWindowTitle(QStringLiteral("Strata · 보고서"));
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 8);
  root->setSpacing(8);

  auto* split = new QSplitter(Qt::Horizontal, this);
  auto* leftPane = new QFrame(split);
  auto* leftL = new QVBoxLayout(leftPane);
  leftL->setContentsMargins(4, 4, 6, 4);
  leftL->setSpacing(6);

  auto* leftHead = new QHBoxLayout();
  leftHead->addWidget(new QLabel(QStringLiteral("근거 확인"), leftPane));
  leftHead->addStretch(1);
  m_btnRadius500 = makeCheckBtn(leftPane, QStringLiteral("500m"), true);
  m_btnRadius1000 = makeCheckBtn(leftPane, QStringLiteral("1000m"), false);
  m_ragSlotCheck = new QCheckBox(QStringLiteral("보고서 원문"), leftPane);
  m_ragSlotCheck->setChecked(true);
  leftHead->addWidget(m_btnRadius500);
  leftHead->addWidget(m_btnRadius1000);
  leftHead->addWidget(m_ragSlotCheck);
  leftL->addLayout(leftHead);

  m_summaryLabel = new QLabel(leftPane);
  m_summaryLabel->setWordWrap(true);
  leftL->addWidget(m_summaryLabel);

  m_evidenceTable = new QTableWidget(0, 8, leftPane);
  m_evidenceTable->setHorizontalHeaderLabels({QStringLiteral("연번"), QStringLiteral("유적명"), QStringLiteral("거리"),
                                              QStringLiteral("방위"), QStringLiteral("시대"), QStringLiteral("유구"),
                                              QStringLiteral("층위"), QStringLiteral("보고서")});
  m_evidenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_evidenceTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_evidenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_evidenceTable->verticalHeader()->setVisible(false);
  m_evidenceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  for (int c : {0, 2, 3, 4, 5, 6, 7}) m_evidenceTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  leftL->addWidget(m_evidenceTable, 3);

  leftL->addWidget(new QLabel(QStringLiteral("선택 유적 원문 및 ⑤ 조사구역과의 연관성"), leftPane));
  m_siteDetailView = new QTextBrowser(leftPane);
  m_siteDetailView->setMaximumHeight(135);
  leftL->addWidget(m_siteDetailView, 1);

  m_connStatusLabel = new QLabel(leftPane);
  m_connStatusLabel->setWordWrap(true);
  leftL->addWidget(m_connStatusLabel);

  auto* centerPane = new QFrame(split);
  auto* centerL = new QVBoxLayout(centerPane);
  centerL->setContentsMargins(6, 4, 6, 4);
  centerL->setSpacing(6);

  auto* centerBar = new QHBoxLayout();
  auto* btnDocReport = makeCheckBtn(centerPane, QStringLiteral("고찰"), true);
  auto* btnDocThesis = makeCheckBtn(centerPane, QStringLiteral("논문"), false);
  auto* docGrp = new QButtonGroup(this);
  docGrp->addButton(btnDocReport, 0);
  docGrp->addButton(btnDocThesis, 1);

  auto* btnViewPaper = makeCheckBtn(centerPane, QStringLiteral("용지"), true);
  auto* btnViewVerify = makeCheckBtn(centerPane, QStringLiteral("검수"), false);
  auto* btnViewCompare = makeCheckBtn(centerPane, QStringLiteral("비교"), false);
  auto* btnViewDbpia = makeCheckBtn(centerPane, QStringLiteral("DBpia 로그인"), false);
  auto* viewGrp = new QButtonGroup(this);
  viewGrp->addButton(btnViewPaper, 0);
  viewGrp->addButton(btnViewVerify, 1);
  viewGrp->addButton(btnViewCompare, 2);
  viewGrp->addButton(btnViewDbpia, 3);

  m_regionCombo = new QComboBox(centerPane);
  m_regionCombo->addItem(QStringLiteral("지역: 자동(조사지역)"), QString());
  for (const QString& d : {QStringLiteral("전국"), QStringLiteral("경상도"), QStringLiteral("경기도"),
                           QStringLiteral("충청도"), QStringLiteral("전라도"), QStringLiteral("강원도"), QStringLiteral("제주도")})
    m_regionCombo->addItem(QStringLiteral("6대 도: ") + d, d);

  m_zoomCombo = new QComboBox(centerPane);
  m_zoomCombo->addItems({QStringLiteral("90%"), QStringLiteral("100%"), QStringLiteral("120%")});
  m_zoomCombo->setCurrentIndex(1);

  auto* btnSaveMenu = new QToolButton(centerPane);
  btnSaveMenu->setText(QStringLiteral("저장 ▾"));
  btnSaveMenu->setPopupMode(QToolButton::InstantPopup);
  auto* saveMenu = new QMenu(btnSaveMenu);
  saveMenu->addAction(QStringLiteral("한글 표준(.hwpx) 저장…"), this, &KaHwpReportStudio::onSaveHwpxClicked);
  saveMenu->addAction(QStringLiteral("한글 5.0(.hwp) 저장…"), this, &KaHwpReportStudio::onSaveHwp5Clicked);
  saveMenu->addSeparator();
  saveMenu->addAction(QStringLiteral("기관 한글 양식(.hwpx/.hwp) 열기…"), this, &KaHwpReportStudio::onOpenTemplateClicked);
  saveMenu->addAction(QStringLiteral("별도 창으로 보기…"), this, &KaHwpReportStudio::openLivePaperWindow);
  btnSaveMenu->setMenu(saveMenu);

  for (QWidget* w : {static_cast<QWidget*>(btnDocReport), static_cast<QWidget*>(btnDocThesis),
                     static_cast<QWidget*>(btnViewPaper), static_cast<QWidget*>(btnViewVerify),
                     static_cast<QWidget*>(btnViewCompare), static_cast<QWidget*>(btnViewDbpia)})
    centerBar->addWidget(w);
  centerBar->addStretch(1);
  centerBar->addWidget(m_regionCombo);
  centerBar->addWidget(m_zoomCombo);
  centerBar->addWidget(btnSaveMenu);
  centerL->addLayout(centerBar);

  m_centerStack = new QStackedWidget(centerPane);
  m_previewView = new QTextBrowser(m_centerStack);
  m_verifyView = new QTextBrowser(m_centerStack);
  m_compareView = new QTextBrowser(m_centerStack);
  auto* dbpiaPane = new QWidget(m_centerStack);
  auto* dbpiaL = new QVBoxLayout(dbpiaPane);
  dbpiaL->setContentsMargins(0, 0, 0, 0);
  auto* dbpiaBar = new QHBoxLayout();
  auto* btnLogin = new QPushButton(QStringLiteral("개인 로그인"), dbpiaPane);
  auto* btnOrgLogin = new QPushButton(QStringLiteral("기관 로그인"), dbpiaPane);
  auto* btnDbpiaAi = new QPushButton(QStringLiteral("DBpia AI"), dbpiaPane);
  auto* btnHarvest = new QPushButton(QStringLiteral("자동 문헌수집 실행"), dbpiaPane);
  auto* chkRemember = new QCheckBox(QStringLiteral("로그인 기억"), dbpiaPane);
  chkRemember->setToolTip(QStringLiteral("켜면 이 PC에 DBpia 로그인을 저장합니다. 공용 PC에서는 켜지 마세요."));
  connect(chkRemember, &QCheckBox::toggled, this, [](bool on) {
    dbpiaProfile()->setPersistentCookiesPolicy(on ? QWebEngineProfile::ForcePersistentCookies : QWebEngineProfile::NoPersistentCookies);
  });
  for (QWidget* w : {static_cast<QWidget*>(chkRemember), static_cast<QWidget*>(btnLogin), static_cast<QWidget*>(btnOrgLogin), static_cast<QWidget*>(btnDbpiaAi), static_cast<QWidget*>(btnHarvest)})
    dbpiaBar->addWidget(w);
  dbpiaBar->addStretch(1);
  dbpiaL->addLayout(dbpiaBar);
  auto ensureDbpiaWeb = [dbpiaPane, dbpiaL](const QString& url) {
    auto* web = dbpiaPane->findChild<QWebEngineView*>();
    if (!web) {
      web = new QWebEngineView(dbpiaPane);
      web->setPage(new QWebEnginePage(dbpiaProfile(), web));
      dbpiaL->addWidget(web, 1);
    }
    if (!url.isEmpty()) web->setUrl(QUrl(url));
  };
  connect(btnLogin, &QPushButton::clicked, this, [ensureDbpiaWeb] { ensureDbpiaWeb(QStringLiteral("https://www.dbpia.co.kr/member/login")); });
  connect(btnOrgLogin, &QPushButton::clicked, this, [ensureDbpiaWeb] { ensureDbpiaWeb(QStringLiteral("https://www.dbpia.co.kr/member/b2bLogin")); });
  connect(btnDbpiaAi, &QPushButton::clicked, this, [ensureDbpiaWeb] { ensureDbpiaWeb(QStringLiteral("https://ai.dbpia.co.kr/ko/search")); });
  connect(btnHarvest, &QPushButton::clicked, this, [this] { startDbpiaResearch(m_promptEdit ? m_promptEdit->text() : QString()); });
  m_centerStack->addWidget(m_previewView);
  m_centerStack->addWidget(m_verifyView);
  m_centerStack->addWidget(m_compareView);
  m_centerStack->addWidget(dbpiaPane);
  centerL->addWidget(m_centerStack, 1);

  auto* rightPane = new QFrame(split);
  auto* rightL = new QVBoxLayout(rightPane);
  rightL->setContentsMargins(6, 4, 4, 4);
  rightL->setSpacing(6);
  rightL->addWidget(new QLabel(QStringLiteral("작업 순서"), rightPane));
  m_stepList = new QListWidget(rightPane);
  m_stepList->setMaximumHeight(150);
  rightL->addWidget(m_stepList);
  rightL->addWidget(new QLabel(QStringLiteral("진행 기록"), rightPane));
  m_chatView = new QTextBrowser(rightPane);
  rightL->addWidget(m_chatView, 2);
  rightL->addWidget(new QLabel(QStringLiteral("한글 양식 칸 편집"), rightPane));
  m_fieldTable = new QTableWidget(0, 2, rightPane);
  m_fieldTable->setHorizontalHeaderLabels({QStringLiteral("양식 칸"), QStringLiteral("값")});
  m_fieldTable->verticalHeader()->setVisible(false);
  m_fieldTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_fieldTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  rightL->addWidget(m_fieldTable, 1);

  split->addWidget(leftPane);
  split->addWidget(centerPane);
  split->addWidget(rightPane);
  split->setStretchFactor(0, 4);
  split->setStretchFactor(1, 6);
  split->setStretchFactor(2, 3);
  root->addWidget(split, 1);

  auto* bottomBar = new QHBoxLayout();
  m_promptEdit = new QLineEdit(this);
  m_promptEdit->setObjectName(QStringLiteral("hwpPromptInput"));
  m_promptEdit->setPlaceholderText(QStringLiteral("다차원 분석·집필 지시 (예: 전남 원형주거지와 방형주거지 현황 / 전국 후기구석기 분포 / 지역별 시대별 건물지 크기 변화)"));
  m_btnPrimary = new QPushButton(QStringLiteral("작성 시작"), this);
  m_btnPrimary->setObjectName(QStringLiteral("primary"));
  m_btnPrimary->setProperty("class", QStringLiteral("primary"));
  bottomBar->addWidget(m_promptEdit, 1);
  bottomBar->addWidget(m_btnPrimary);
  root->addLayout(bottomBar);

  m_stageTimer = new QTimer(this);
  m_stageTimer->setInterval(320);
  connect(m_stageTimer, &QTimer::timeout, this, [this] {
    ++m_currentStage;
    m_session->buildStandardReport(m_cachedSnap, nullptr, qMin(4, m_currentStage));
    updateStepRail(m_currentStage);
    const QStringList labels = {QString(), QStringLiteral("1/6단계: GIS 조사구역·최단거리 확정"),
                                QStringLiteral("2/6단계: 발굴조사보고서 원문·층위 대조"),
                                QStringLiteral("3/6단계: 학술문헌 서지 확인"),
                                QStringLiteral("4/6단계: 사실·인용 교차검증"),
                                QStringLiteral("5/6단계: Ⅱ장 유적별 현황·연관성 집필"),
                                QStringLiteral("6/6단계: 층위 대비도·유구 비교표 작성 완료")};
    if (m_currentStage >= 6) m_stageTimer->stop();
    syncUiFromSession(m_currentStage >= 6 ? QString() : labels.value(m_currentStage), qMin(100, m_currentStage * 17));
  });

  connect(m_btnRadius500, &QToolButton::clicked, this, [this] { m_btnRadius500->setChecked(true); m_btnRadius1000->setChecked(false); m_radiusMeters = 500.0; refreshFromActiveProject(true); });
  connect(m_btnRadius1000, &QToolButton::clicked, this, [this] { m_btnRadius1000->setChecked(true); m_btnRadius500->setChecked(false); m_radiusMeters = 1000.0; refreshFromActiveProject(true); });
  connect(m_ragSlotCheck, &QCheckBox::toggled, this, [this] { refreshFromActiveProject(true); });
  connect(m_evidenceTable, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) { onSiteRowSelected(r); });
  connect(viewGrp, &QButtonGroup::idClicked, this, [this, dbpiaPane, ensureDbpiaWeb](int id) {
    m_centerStack->setCurrentIndex(id);
    if (id == 3 && !dbpiaPane->findChild<QWebEngineView*>()) ensureDbpiaWeb(QStringLiteral("https://www.dbpia.co.kr/member/login"));
  });
  connect(docGrp, &QButtonGroup::idClicked, this, [this](int id) { m_isThesisMode = (id == 1); syncUiFromSession(); });
  connect(m_regionCombo, &QComboBox::currentIndexChanged, this, [this] { refreshFromActiveProject(true); });
  connect(m_zoomCombo, &QComboBox::currentIndexChanged, this, [this] { syncUiFromSession(); });
  connect(m_fieldTable, &QTableWidget::cellChanged, this, &KaHwpReportStudio::onFieldCellChanged);
  connect(m_btnPrimary, &QPushButton::clicked, this, &KaHwpReportStudio::onSendPromptClicked);
  connect(m_promptEdit, &QLineEdit::returnPressed, this, &KaHwpReportStudio::onSendPromptClicked);

  connect(m_dbpiaBridge, &KaGeminiBridgeClient::dbpiaPacketReceived, this, [this](const QString& q, int refCount, const QString& preview) {
    m_dbpiaSummary.lastQuery = q; m_dbpiaSummary.roundNumber = m_dbpiaBridge->currentRound();
    m_dbpiaSummary.collectedPapers = m_dbpiaBridge->lastCollectedPapers();
    updateStepRail(3);
    appendChatMessage(QStringLiteral("문헌 수집"), QStringLiteral("\"%1\" → 신규 %2편 (누적 %3편)\n%4").arg(q).arg(refCount).arg(m_dbpiaSummary.collectedPapers.size()).arg(preview));
    syncUiFromSession(QStringLiteral("학술문헌 수렴 중: \"%1\" (%2편)").arg(q).arg(m_dbpiaSummary.collectedPapers.size()), 60);
  });
  connect(m_dbpiaBridge, &KaGeminiBridgeClient::dbpiaHarvestCompleted, this, [this](const QJsonArray&, int totalUnique) {
    m_dbpiaSummary.collectedPapers = m_dbpiaBridge->lastCollectedPapers(); m_dbpiaSummary.converged = true;
    const QString q1 = m_dbpiaSummary.lastQuery.isEmpty() ? m_cachedSnap.regionName : m_dbpiaSummary.lastQuery;
    m_dbpiaSummary.dbpiaAiTurns.append(QJsonObject{{QStringLiteral("question"), q1}, {QStringLiteral("answer"), m_dbpiaBridge->buildPaperGroundedFallbackAnswer(q1, 1)}});
    m_dbpiaSummary.synthesizedDiscussion = m_dbpiaBridge->buildStructuredDataSynthesis(m_dbpiaSummary.dbpiaAiTurns);
    HwpAcademicVisuals::appendDbpiaAndVisualSummaryToDoc(m_cachedSnap, m_dbpiaSummary, m_session.get());
    updateStepRail(6);
    appendChatMessage(QStringLiteral("검증 완료"), totalUnique > 0
        ? QStringLiteral("학술문헌 %1편 및 층위 대비도·유구 비교표 작성 완료").arg(totalUnique)
        : QStringLiteral("DBpia 수집 문헌 없음(DBpia 응답 없음) — 문헌 표를 임의로 채우지 않았습니다."));
    syncUiFromSession(QString(), 100);
  });

  updateStepRail(6);
  refreshFromActiveProject(true);
}

KaHwpReportStudio::~KaHwpReportStudio() = default;
