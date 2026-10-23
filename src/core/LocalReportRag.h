#pragma once
// 로컬 발굴조사보고서 RAG 검색·인용 클라이언트 (순수 표준 C++20)
// - 표준 경로: /home/kwon/Disks/report1/report/_rag_index_and_search.py 및 report_text/_rag_index.db
// - 광역권역(전남·경북·전국 등) 및 복수 지역 병합·분할 검색 지원
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace strata::core {

struct LocalReportMatchResult;

struct LocalReportHit {
  std::string sigungu;
  std::string macro_region;
  std::string report_name;
  int page_no = 0;
  double score = 0.0;
  std::string snippet;
  std::string content;
  std::filesystem::path txt_path;
  std::filesystem::path pdf_path;
};

struct LocalReportStats {
  bool available = false;
  int report_count = 0;
  int region_count = 0;
  int chunk_count = 0;
  std::string summary_utf8;
};

struct RegionReportStats {
  std::string region_utf8;
  int total_reports = 0;
  int text_reports = 0;
  int scan_reports = 0;
  int chunk_count = 0;
};

struct SiteRagEvidence {
  std::string status = "○";        // "✓" (원문 확인), "◌" (스캔본 미추출), "○" (원문 없음)
  std::string era_status = "○";
  std::string feature_status = "○";
  std::string strata_status = "○";
  std::string report_name;         // 실제 로컬 PDF 파일명
  int page_no = 0;
  int strata_page_no = 0;
  int total_pages = 0;
  std::string era_summary;
  std::string feature_summary;
  std::string strata_summary;
  bool has_local_pdf = false;      // 로그에서 PDF를 찾았으나 아직 색인되지 않은 경우
  bool indexed = false;            // _rag_index.db documents 에 존재
  int report_year = 0;
  std::string institution;
  bool same_report = false;        // GIS 서지와 같은 보고서
  bool locality_reference = false; // 같은 동·리 참고(다른 보고서)
};

class LocalReportRag {
 public:
  static std::filesystem::path ragScriptPath();
  static std::filesystem::path pipelineScriptPath();
  static std::filesystem::path ragDbPath();

  // 경로의 도 폴더 우선, 없으면 시군구 이름으로 광역권역을 짐작한다. 못 정하면 「미분류」
  static std::string classifyMacroRegion(std::string_view sigungu_utf8);
  // 자연어 질의에서 지역 범위(전국·전남·경북·복수지역 등)를 추출한다 (없으면 default_region_utf8)
  static std::string extractRegionScope(std::string_view prompt_utf8,
                                        std::string_view default_region_utf8 = "경주시");
  // 광역권역("전남", "경북", "전국") 또는 복수 지역("경주·경산")을 SQLite sigungu 필터 목록으로 확장
  static std::vector<std::string> expandRegionFilters(std::string_view region_expr_utf8);

  static LocalReportStats queryStats();
  static RegionReportStats queryRegionStats(std::string_view region_utf8);

  // 키워드 + 지역(단일·광역·복수·전국) 기반 보고서 본문 검색 (권당 최대 max_per_report건 제한, 스캔본 제외)
  static std::vector<LocalReportHit> search(std::string_view query_utf8,
                                            std::string_view region_utf8 = {},
                                            int top_k = 10,
                                            int max_per_report = 2);

  static SiteRagEvidence inspectSiteEvidence(std::string_view site_name_utf8,
                                             std::string_view region_utf8 = {});
  // GIS 보고서명으로 찾은 특정 보고서(rel_path) 안에서만 시대·유구·층위 근거를 뽑는다.
  static SiteRagEvidence inspectMatchedReport(const LocalReportMatchResult& match,
                                              std::string_view hint_utf8,
                                              std::string_view region_utf8 = {});

  static std::vector<LocalReportHit> parseSearchJson(std::string_view json_utf8);
  static std::string formatPromptContext(std::span<const LocalReportHit> hits);
  static bool startPipelineDetached();
};

}  // namespace strata::core
