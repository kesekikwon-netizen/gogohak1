#include "HwpSurveyEvidence.h"

#include <QStringList>
#include <QtMath>

namespace HwpSurveyReportService::Detail {

QVector<QStringList> buildSiteTableRows(const SurveySnapshot& snapshot, int maxRows) {
  QVector<QStringList> rows;
  rows.push_back({QStringLiteral("연번"), QStringLiteral("유적명"), QStringLiteral("유산코드"),
                  QStringLiteral("허가번호"), QStringLiteral("최단거리·방위"),
                  QStringLiteral("GIS 보고서명(공식 서지)"), QStringLiteral("사업명"),
                  QStringLiteral("소재지·조사기관"), QStringLiteral("시대·유적유무"), QStringLiteral("원문 대조")});
  int count = 0;
  for (const auto& e : snapshot.siteEntries) {
    if (maxRows > 0 && count >= maxRows) break;
    ++count;
    const QString code =
        !e.heritageCode.trimmed().isEmpty()
            ? e.heritageCode.trimmed()
            : (!e.featureKey.trimmed().isEmpty() ? QStringLiteral("도형 %1").arg(e.featureKey.trimmed())
                                                 : QStringLiteral("-"));
    const QString bib = bibliographyLine(e);
    const QString address = QStringLiteral("소재지: %1 / 조사기관: %2")
                                .arg(e.siteAddress.trimmed().isEmpty() ? QStringLiteral("-") : e.siteAddress.trimmed(),
                                     e.institution.trimmed().isEmpty() ? QStringLiteral("-") : e.institution.trimmed());
    const QString era =
        QStringLiteral("시대: %1 / 유적유무: %2")
            .arg(e.eraField.trimmed().isEmpty() ? QStringLiteral("-") : e.eraField.trimmed(),
                 e.heritagePresence.trimmed().isEmpty() ? QStringLiteral("-") : e.heritagePresence.trimmed());
    rows.push_back({QString::number(count), e.siteName, code,
                    e.permitNumber.trimmed().isEmpty() ? QStringLiteral("-") : e.permitNumber.trimmed(),
                    QStringLiteral("%1 %2m")
                        .arg(e.direction.isEmpty() ? QStringLiteral("인접") : e.direction)
                        .arg(e.distanceMeters),
                    bib.isEmpty() ? QStringLiteral("서지 없음") : bib,
                    e.projectName.trimmed().isEmpty() ? QStringLiteral("-") : e.projectName.trimmed(), address, era,
                    reportSourceLabel(e)});
  }
  if (rows.size() == 1)
    rows.push_back({QStringLiteral("-"), QStringLiteral("반경 내 주변유적 없음"), QStringLiteral("-"),
                    QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("-"),
                    QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("-")});
  return rows;
}

QString siteStatusSummary(const SurveySnapshot& snapshot) {
  int verified = 0, locality = 0, otherReport = 0, scanSame = 0, scanOther = 0;
  int ragMissing = 0, gisMissing = 0, pdfPending = 0;
  for (const auto& e : snapshot.siteEntries) {
    verified += e.reportStatus == QStringLiteral("✓") ? 1 : 0;
    locality += e.reportStatus == QStringLiteral("△") && e.localityReference ? 1 : 0;
    otherReport += e.reportStatus == QStringLiteral("△") && !e.localityReference ? 1 : 0;
    scanSame += e.reportStatus == QStringLiteral("◌") && e.sameReport ? 1 : 0;
    scanOther += e.reportStatus == QStringLiteral("◌") && !e.sameReport ? 1 : 0;
    pdfPending += e.reportStatus == QStringLiteral("○") && e.hasLocalPdf ? 1 : 0;
    const bool noPdf = e.reportStatus == QStringLiteral("○") && !e.hasLocalPdf;
    ragMissing += noPdf && !e.bibliography.trimmed().isEmpty() ? 1 : 0;
    gisMissing += noPdf && e.bibliography.trimmed().isEmpty() ? 1 : 0;
  }
  QStringList lines;
  const QString tail = pdfPending > 0 ? QStringLiteral(" · ○ 로컬 PDF 색인 전 %1").arg(pdfPending) : QString();
  lines << QStringLiteral(
               "반경 %1m 내 주변유적 %2건: ✓ 동일 보고서 %3 · △ 참고 %4(같은 동·리 %5 / 다른 보고서 %6) · ◌ 스캔본 "
               "%7(수동 대조 %8 / 다른 보고서 %9) · ○ 보고서 미수록 %10 · ○ 공식 서지 없음 %11%12")
               .arg(QString::number(qRound(snapshot.radiusMeters)), QString::number(snapshot.siteEntries.size()),
                    QString::number(verified), QString::number(locality + otherReport), QString::number(locality),
                    QString::number(otherReport), QString::number(scanSame + scanOther), QString::number(scanSame),
                    QString::number(scanOther))
               .arg(QString::number(ragMissing), QString::number(gisMissing), tail);
  lines << QStringLiteral(
      "범례: ✓ 공식 서지와 같은 보고서 확인 · △ 다른 보고서/같은 동·리 참고(유구 연장 단정 유보) · ◌ 스캔본(수동 대조) "
      "· ○ 보고서 미수록(서지는 있음) / 공식 서지 없음.");
  lines << QStringLiteral(
      "※ ○는 보고서가 세상에 없다는 뜻이 아니라, 이 PC의 보고서 파일에 원문이 없다는 뜻입니다. 공식 서지가 있으면 공식 "
      "검색 경로(e-minwon·문화재청·NRICH)에서 원문을 확인하십시오.");
  return lines.join(QLatin1Char('\n'));
}

}  // namespace HwpSurveyReportService::Detail
