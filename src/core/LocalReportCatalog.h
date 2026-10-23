#pragma once
// LocalReportMatch 내부용 서지 카탈로그(_download_log.csv + _rag_index.db documents).
#include "core/LocalReportMatch.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace strata::core::LocalReportMatchDetail {

struct LogRow {
  int year = 0, phase = 0;
  std::string title, org, site, attach, status;
  std::string title_key, attach_key, site_key, base_key, org_key, addr_key;
};

struct DocRow {
  std::string rel_path, sigungu, report_name, mode, title_key, base_key;
  int total_pages = 0;
  bool scan = false;
};

struct Catalog {
  std::vector<LogRow> logs;
  std::vector<DocRow> docs;
  std::unordered_map<std::string, std::vector<std::size_t>> by_key;
};

const Catalog& catalog();
std::vector<LocalReportDocument> documentsLike(std::string_view text_utf8, std::string_view region_utf8, int limit);
std::string institutionKey(std::string_view text_utf8);
std::string stripYears(std::string_view text_utf8);
bool regionMatches(const DocRow& doc, const std::string& region);
int docYear(const DocRow& doc);

}  // namespace strata::core::LocalReportMatchDetail
