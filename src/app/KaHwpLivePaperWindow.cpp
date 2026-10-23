#include "KaHwpLivePaperWindow.h"

#include <QtWidgets>

KaHwpLivePaperWindow::KaHwpLivePaperWindow(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint) {
  setWindowTitle(QStringLiteral("Strata · 한글(HWPX / HWP) A4 조판 창"));
  resize(920, 960);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(8);

  auto* topBar = new QHBoxLayout();
  m_statusLabel = new QLabel(QStringLiteral("한글(HWPX/HWP) A4 조판 준비됨"), this);
  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(100);
  m_progressBar->setFixedWidth(130);

  auto* btnZoomOut = new QPushButton(QStringLiteral("축소 -"), this);
  m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
  auto* btnZoomIn = new QPushButton(QStringLiteral("확대 +"), this);
  m_fieldMarkCheck = new QCheckBox(QStringLiteral("누름틀 표시"), this);
  m_fieldMarkCheck->setChecked(true);
  auto* btnSaveHwpx = new QPushButton(QStringLiteral(".hwpx 저장"), this);
  auto* btnSaveHwp5 = new QPushButton(QStringLiteral(".hwp 저장"), this);

  topBar->addWidget(m_statusLabel, 1);
  topBar->addWidget(m_progressBar);
  topBar->addWidget(btnZoomOut);
  topBar->addWidget(m_zoomLabel);
  topBar->addWidget(btnZoomIn);
  topBar->addWidget(m_fieldMarkCheck);
  topBar->addWidget(btnSaveHwpx);
  topBar->addWidget(btnSaveHwp5);
  root->addLayout(topBar);

  m_paperBrowser = new QTextBrowser(this);
  m_paperBrowser->setOpenExternalLinks(false);
  root->addWidget(m_paperBrowser, 1);

  auto* bottomBar = new QHBoxLayout();
  m_livePromptEdit = new QLineEdit(this);
  m_livePromptEdit->setPlaceholderText(QStringLiteral("작성·비교·치환 지시를 입력하세요"));
  auto* btnSend = new QPushButton(QStringLiteral("문서 반영"), this);
  btnSend->setObjectName(QStringLiteral("primary"));
  btnSend->setProperty("class", QStringLiteral("primary"));
  bottomBar->addWidget(m_livePromptEdit, 1);
  bottomBar->addWidget(btnSend);
  root->addLayout(bottomBar);

  connect(btnZoomOut, &QPushButton::clicked, this, [this] {
    m_zoomPercent = qMax(70, m_zoomPercent - 15);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(m_zoomPercent));
    if (m_onRefresh) m_onRefresh();
  });
  connect(btnZoomIn, &QPushButton::clicked, this, [this] {
    m_zoomPercent = qMin(160, m_zoomPercent + 15);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(m_zoomPercent));
    if (m_onRefresh) m_onRefresh();
  });
  connect(m_fieldMarkCheck, &QCheckBox::toggled, this, [this](bool) {
    if (m_onRefresh) m_onRefresh();
  });
  auto sendFn = [this] {
    if (!m_livePromptEdit || m_livePromptEdit->text().trimmed().isEmpty()) return;
    const QString txt = m_livePromptEdit->text().trimmed();
    m_livePromptEdit->clear();
    if (m_onPrompt) m_onPrompt(txt);
  };
  connect(btnSend, &QPushButton::clicked, this, sendFn);
  connect(m_livePromptEdit, &QLineEdit::returnPressed, this, sendFn);
  connect(btnSaveHwpx, &QPushButton::clicked, this, [this] {
    if (auto* p = parentWidget()) QMetaObject::invokeMethod(p, "onSaveHwpxClicked");
  });
  connect(btnSaveHwp5, &QPushButton::clicked, this, [this] {
    if (auto* p = parentWidget()) QMetaObject::invokeMethod(p, "onSaveHwp5Clicked");
  });
}

KaHwpLivePaperWindow::~KaHwpLivePaperWindow() = default;
bool KaHwpLivePaperWindow::showFieldMarks() const {
  return !m_fieldMarkCheck || m_fieldMarkCheck->isChecked();
}

void KaHwpLivePaperWindow::updatePaperHtml(const QString& html, const QString& statusText, int progressPercent) {
  if (m_statusLabel && !statusText.isEmpty()) m_statusLabel->setText(statusText);
  if (m_progressBar) m_progressBar->setValue(qBound(0, progressPercent, 100));
  if (m_paperBrowser) {
    m_paperBrowser->setHtml(html);
    if (progressPercent < 100)
      if (QScrollBar* sb = m_paperBrowser->verticalScrollBar()) sb->setValue(sb->maximum());
  }
}

void KaHwpLivePaperWindow::setActionCallbacks(std::function<void(const QString&)> onPromptSubmitted,
                                              std::function<void()> onRefreshClicked,
                                              std::function<void()> onSaveHwpxClicked,
                                              std::function<void()> onSaveHwp5Clicked) {
  Q_UNUSED(onSaveHwpxClicked);
  Q_UNUSED(onSaveHwp5Clicked);
  m_onPrompt = std::move(onPromptSubmitted);
  m_onRefresh = std::move(onRefreshClicked);
}
