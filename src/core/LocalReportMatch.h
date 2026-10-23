#pragma once
// GIS 발굴유적 서지 ↔ 로컬 발굴조사보고서(PDF/색인 DB) 1:1 매칭.
// 유적명만으로 묶지 않고 보고서명·조사기관·소재지·연도를 함께 본다.
#include <string>
#include <string_view>
#include <vector>

namespace strata::core {

struct LocalReportDocument {
  std::string rel_path;
  std::string sigungu;
  std::string report_name;
  std::string extraction_mode;
  int total_pages = 0;
  bool scan = false;
};

struct LocalReportMatchQuery {
  std::string report_name;   // GIS 보고서명 (기관, 연도, 제목)
  std::string institution;   // GIS 조사기관
  std::string address;       // GIS 소재지
  std::string site_name;     // GIS 유적명
  std::string project_name;  // GIS 사업명
  std::string region;        // 시군구(예: 경주시)
  int year = 0;              // 보고서명에서 뽑은 연도
};

struct LocalReportMatchResult {
  bool matched = false;     // _download_log.csv 에서 서지 후보를 찾음
  bool indexed = false;     // _rag_index.db documents 에서 원문/스캔을 찾음
  bool scan = false;        // 텍스트가 없는 스캔본
  std::string report_name;  // 로그 제목(서지)
  std::string pdf_name;     // 실제 PDF 파일명
  std::string rel_path;
  std::string sigungu;
  std::string extraction_mode;
  std::string institution;
  int year = 0;
  int total_pages = 0;
  double score = 0.0;
};

class LocalReportMatch {
 public:
  static LocalReportMatchResult find(const LocalReportMatchQuery& query);
  // 주소·번호가 없는 유적명(예: "경주 황남동 유적")만 같은 동·리의 보고서를 참고한다.
  static LocalReportMatchResult localityDocument(std::string_view site_name_utf8, std::string_view region_utf8);
  static std::vector<LocalReportDocument> documentsLike(std::string_view text_utf8, std::string_view region_utf8,
                                                        int limit = 8);
  static int parseYear(std::string_view text_utf8);
  static std::string normalize(std::string_view text_utf8);
  static std::string titleKey(std::string_view text_utf8);
  static std::string addressKey(std::string_view text_utf8);
  static bool hasAddressNumber(std::string_view text_utf8);
};

}  // namespace strata::core
