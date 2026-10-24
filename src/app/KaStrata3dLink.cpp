#include "KaStrata3dLink.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

#include <qgsproject.h>

KaStrata3dLink::KaStrata3dLink(QVBoxLayout* panelLayout, QLabel* status, QgsProject* project, QWidget* owner)
    : QObject(owner), m_project(project), m_owner(owner), m_status(status) {
  m_btnOpen = new QPushButton(QStringLiteral("strata-3d로 열기"), owner);
  m_btnOpen->setObjectName(QStringLiteral("strata3dOpenBtn"));
  m_btnOpen->setToolTip(QStringLiteral("열려 있는 조사를 정밀 3D 앱(strata-3d)으로 넘겨 실측 지형·건물·유적 위에서 500 m·1000 m 평가를 받습니다."));
  m_btnSheet = new QPushButton(QStringLiteral("strata-3d 도면 받기"), owner);
  m_btnSheet->setObjectName(QStringLiteral("strata3dSheetBtn"));
  m_btnSheet->setToolTip(QStringLiteral("strata-3d 에 떠 있는 장면의 입체지형 도면(PNG·PDF)을 받아 저장합니다."));
  // 맨 끝의 늘림칸(stretch) 앞에 넣는다.
  const int at = std::max(0, panelLayout->count() - 1);
  panelLayout->insertWidget(at, m_btnOpen);
  panelLayout->insertWidget(at + 1, m_btnSheet);
  connect(m_btnOpen, &QPushButton::clicked, this, &KaStrata3dLink::openSurvey);
  connect(m_btnSheet, &QPushButton::clicked, this, &KaStrata3dLink::receiveSheet);
}

void KaStrata3dLink::runAsync(const QString& what, std::function<Strata3dLink::Outcome()> job) {
  setBusy(true);
  QPointer<KaStrata3dLink> self(this);  // 작업 중에 탭이 닫혀도 결과를 버릴 뿐 죽은 객체를 부르지 않는다
  QThread* th = QThread::create([self, what, job = std::move(job)] {
    const Strata3dLink::Outcome r = job();
    if (self) QMetaObject::invokeMethod(self.data(), [self, what, r] { if (self) self->finished(what, r); }, Qt::QueuedConnection);
  });
  connect(th, &QThread::finished, th, &QObject::deleteLater);
  th->start();
}

void KaStrata3dLink::setBusy(bool busy) {
  m_btnOpen->setEnabled(!busy);
  m_btnSheet->setEnabled(!busy);
}

void KaStrata3dLink::openSurvey() {
  const QString gpkg = m_project ? Strata3dLink::surveyGpkgFromProjectFile(m_project->fileName()) : QString();
  if (gpkg.isEmpty()) {
    m_status->setText(QStringLiteral("열려 있는 조사 파일(.gpkg)이 없습니다. 조사를 저장하거나 연 뒤에 눌러 주세요."));
    return;
  }
  m_status->setText(QStringLiteral("strata-3d로 조사를 넘기는 중… 실측 지형·위성영상·건물·유적을 받아 올리므로 1~2분 걸릴 수 있습니다."));
  const QString pipe = Strata3dLink::pipeName(), exe = Strata3dLink::findExecutable();
  runAsync(QStringLiteral("open"), [=] { return Strata3dLink::openAndEvaluate(pipe, exe, gpkg); });
}

void KaStrata3dLink::receiveSheet() {
  const QString gpkg = m_project ? Strata3dLink::surveyGpkgFromProjectFile(m_project->fileName()) : QString();
  const QString dir = gpkg.isEmpty() ? QDir::homePath() : QFileInfo(gpkg).absolutePath();
  const QString png = QFileDialog::getSaveFileName(m_owner, QStringLiteral("strata-3d 입체지형 도면 저장"),
                                                   dir + QStringLiteral("/strata3d-도면.png"), QStringLiteral("PNG (*.png)"));
  if (png.isEmpty()) return;
  const QString pdf = QString(png).replace(QStringLiteral(".png"), QStringLiteral(".pdf"), Qt::CaseInsensitive);
  m_status->setText(QStringLiteral("strata-3d 에서 도면을 받는 중…"));
  const QString pipe = Strata3dLink::pipeName();
  runAsync(QStringLiteral("sheet"), [=] { return Strata3dLink::fetchSheet(pipe, png, pdf); });
}

void KaStrata3dLink::finished(const QString& what, const Strata3dLink::Outcome& r) {
  setBusy(false);
  if (!r.ok) {
    m_status->setText(QStringLiteral("strata-3d 연동 실패: ") + r.error);
    return;
  }
  if (what == QLatin1String("open")) {
    m_status->setText(r.surveyMessage + QStringLiteral(" · 좌표계 ") + r.crs + QStringLiteral("\n") + r.evaluationSummary);
  } else {
    m_status->setText(QStringLiteral("strata-3d 도면 저장: ") + QDir::toNativeSeparators(r.png) +
                      (r.pdf.isEmpty() ? QString() : QStringLiteral("\n") + QDir::toNativeSeparators(r.pdf)));
  }
}
