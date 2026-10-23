#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <filesystem>
#include <functional>

#include "NearbyHeritageExport.h"

class QgsProject;

namespace HwpSurveyReportService {

struct DomainLayerCount {
  QString layerKey;
  QString label;
  int featureCount = 0;
};

struct SiteEvidenceRow {
  QString datasetName;
  QString siteName;
  int distanceMeters = 0;
  QString direction;
  QString eraStatus = QStringLiteral("○");
  QString featureStatus = QStringLiteral("○");
  QString strataStatus = QStringLiteral("○");
  QString reportStatus = QStringLiteral("○");  // "✓" | "△" | "◌" | "○"
  bool sameReport = false;   // GIS 서지와 실제 대조한 PDF가 같은 보고서
  bool localityReference = false;  // 같은 동·리 참고(다른 보고서)
  QString reportName;        // 실제 대조한 로컬 PDF 파일명
  int pageNo = 0;
  int strataPageNo = 0;
  QString eraSummary;
  QString featureSummary;
  QString strataSummary;
  QString relationToSurvey;  // ⑤ 조사구역과의 연관성
  // GIS 원자료의 공식 서지. 로컬 원문을 못 찾아도 이 값은 표에 남긴다.
  QString heritageCode;
  QString featureKey;        // 유산코드가 없을 때 도형을 구분하는 피처ID 키
  QString bibliography;      // GIS 보고서명(기관, 연도, 제목)
  QString projectName;       // GIS 사업명
  QString siteAddress;       // GIS 소재지
  QString eraField;          // GIS 시대
  QString heritagePresence;  // GIS 유적유무
  QString permitNumber;      // GIS 허가번호
  QString institution;       // GIS 조사기관
  int reportYear = 0;        // GIS 서지의 연도
  bool hasLocalPdf = false;  // 로컬 PDF 파일이 있음(색인 전 포함)
  bool indexed = false;      // _rag_index.db 에 색인됨
};

struct SurveySnapshot {
  QString surveyTitle;
  QString surveyCrs;
  QString regionName;
  double radiusMeters = 500.0;
  double totalAreaSqm = 0.0;
  int surveyPolygonCount = 0;
  int regionTotalReports = 0;
  int regionTextReports = 0;
  int regionScanReports = 0;
  QVector<DomainLayerCount> domainCounts;
  QVector<NearbyHeritageExport::Item> nearbySites;
  QVector<SiteEvidenceRow> siteEntries;
  QString discussionDraft;
  QString ragCitationSlotText;
};

struct FieldEntry {
  QString name;   // 내부 이름(양식 호환). 화면에는 보이지 않는다.
  QString value;
  QString label;  // 화면에 보이는 한글 이름표
};

using RagSlotCallback = std::function<QString(const SurveySnapshot& snapshot)>;

class ReportDocumentSession {
 public:
  ReportDocumentSession();
  ~ReportDocumentSession();

  ReportDocumentSession(const ReportDocumentSession&) = delete;
  ReportDocumentSession& operator=(const ReportDocumentSession&) = delete;

  static SurveySnapshot collectFromProject(QgsProject* project,
                                           bool enableRagSlot = false,
                                           const RagSlotCallback& ragSlot = {},
                                           double radiusMeters = 500.0);

  // 주변유적 목록(nearbySites)과 조사지역명(regionName)에 대해 로컬 발굴조사보고서 RAG 증거(✓/◌/○)와
  // ⑤ 조사구역과의 연관성을 채운다.
  static void populateRagEvidence(SurveySnapshot* snapshot, bool queryRagDb = true);

  bool buildStandardReport(const SurveySnapshot& snapshot,
                           QString* errorOut = nullptr,
                           int maxStage = 4);

  bool openTemplateAndFill(const std::filesystem::path& templatePath,
                           const SurveySnapshot& snapshot,
                           QString* errorOut = nullptr);

  bool saveAs(const std::filesystem::path& outputPath,
              const QString& format,
              QString* errorOut = nullptr);

  QVector<FieldEntry> fields() const;
  bool setFieldValue(const QString& fieldName, const QString& value);

  bool applyUserRequest(const QString& userPrompt,
                        const SurveySnapshot& snapshot,
                        QString* assistantReplyOut = nullptr);

  QString renderPreviewHtml(const QString& liveStageLabel = {},
                            int zoomPercent = 100,
                            bool showFieldMarks = true,
                            const QString& extraVisualsHtml = {}) const;

 private:
  struct Impl;
  Impl* m_impl = nullptr;
};

}  // namespace HwpSurveyReportService
