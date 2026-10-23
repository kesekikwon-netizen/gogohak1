// KaHwpReportStudio의 시굴 전 평가 동작: 평가 보기와 평가 자료 저장.
// AI 없이 만든 뼈대(AssessmentSkeleton)를 쓴다. 지도·레이어를 바꾸지 않는다.
#include "KaHwpReportStudio.h"
#include "core/AssessmentSkeleton.h"
#include "core/GisFactHarness.h"
#include <QtWidgets>
#include <qgsproject.h>

namespace {

struct Built {
  GisFactHarness::Ledger ledger;
  AssessmentSkeleton::Result result;
  QStringList problems;
};

Built buildAssessment(const HwpSurveyReportService::SurveySnapshot& snapshot, double radiusMeters) {
  Built b;
  b.ledger = GisFactHarness::buildLedger(QgsProject::instance(), QString(), radiusMeters);
  if (!b.ledger.ok()) return b;
  b.result = AssessmentSkeleton::build(b.ledger, snapshot);
  b.problems = AssessmentContract::validateAssessment(b.result.assessment, b.result.evidence, b.ledger.uncollectedDomains,
                                                      AssessmentContract::gisIdsOf(b.result.facts));
  return b;
}

}  // namespace

// 「평가」 보기: 지금 조사구역과 반경으로 평가 뼈대를 읽기 쉽게 보여 준다.
void KaHwpReportStudio::refreshAssessmentView() {
  if (!m_assessView) return;
  const Built b = buildAssessment(m_cachedSnap, m_radiusMeters);
  if (!b.ledger.ok()) {
    m_assessView->setHtml(QStringLiteral("<p>%1</p>").arg(b.ledger.error.toHtmlEscaped()));
    return;
  }
  m_assessView->setHtml(AssessmentSkeleton::renderHtml(b.result, b.problems));
}

// 평가 뼈대와 그 바탕 자료(사실 묶음·근거)를 폴더에 저장한다. 나중에 SDK 가 같은 파일을 읽는다.
void KaHwpReportStudio::onSaveAssessmentClicked() {
  const Built b = buildAssessment(m_cachedSnap, m_radiusMeters);
  if (!b.ledger.ok()) {
    QMessageBox::warning(this, QStringLiteral("저장 실패"), b.ledger.error);
    return;
  }
  const QString base = QFileDialog::getExistingDirectory(this, QStringLiteral("평가 자료를 저장할 폴더"), QDir::homePath());
  if (base.isEmpty()) return;
  const QString folder = QDir(base).filePath(QStringLiteral("평가 자료"));
  QString err;
  if (!AssessmentSkeleton::writeBundle(folder, b.result, &err)) {
    QMessageBox::warning(this, QStringLiteral("저장 실패"), err);
    return;
  }
  appendChatMessage(QStringLiteral("저장"), QStringLiteral("평가 자료를 저장했습니다: %1").arg(QDir::toNativeSeparators(folder)) +
                                               (b.problems.isEmpty() ? QString() : QStringLiteral("\n확인할 점 %1건: %2").arg(b.problems.size()).arg(b.problems.first())));
}
