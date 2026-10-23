#include "core/LocalReportMatch.h"
#include "core/LocalReportRag.h"
#include "core/LocalReportText.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>

extern "C" {
struct sqlite3;
struct sqlite3_stmt;
int sqlite3_open_v2(const char*, sqlite3**, int, const char*);
int sqlite3_busy_timeout(sqlite3*, int);
int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, void (*)(void*));
int sqlite3_step(sqlite3_stmt*);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int);
int sqlite3_column_int(sqlite3_stmt*, int);
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_close(sqlite3*);
}

namespace strata::core {
namespace {
constexpr int kSqliteOk = 0, kSqliteRow = 100, kSqliteRo = 0x00008001;

sqlite3* openRagDb() {
  sqlite3* db = nullptr;
  const auto p = LocalReportRag::ragDbPath();
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || ::sqlite3_open_v2(p.string().c_str(), &db, kSqliteRo, nullptr) != kSqliteOk)
    return db ? (::sqlite3_close(db), nullptr) : nullptr;
  return ::sqlite3_busy_timeout(db, 2000), db;
}

std::string colStr(sqlite3_stmt* st, int col) {
  const unsigned char* p = ::sqlite3_column_text(st, col);
  return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}

std::string cleanOneLine(std::string_view s, std::size_t max_len = 120) {
  std::string out;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
    if (out.size() >= max_len) break;
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

std::string ftsToken(std::string_view t) {
  std::string out;
  for (char c : t) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u >= 0x80) out.push_back(c);
  }
  return out;
}

bool isGenericToken(const std::string& t) {
  static const std::unordered_set<std::string> kGeneric = {
      "경주",   "유적", "발굴", "조사", "보고서", "문화재", "문화유산", "부지", "일원", "내",
      "외",     "지구", "구역", "사업", "신축",   "증축",   "개축",     "공사", "설치", "정비",
      "현대화", "주변", "지역", "유물", "시대",   "토층",   "층위",     "유구", "일대", "번지"};
  return kGeneric.count(t) > 0;
}

std::string scopeToken(const LocalReportMatchResult& match, std::string_view hint) {
  std::vector<std::string> toks = LocalReportText::tokens(hint, 2);
  for (const auto& t : LocalReportText::tokens(match.report_name, 2))
    toks.push_back(t);
  for (const auto& t : LocalReportText::tokens(match.pdf_name, 2))
    toks.push_back(t);
  std::string bestKorean, bestAscii;
  for (const std::string& raw : toks) {
    const std::string t = ftsToken(raw);
    if (t.size() < 4 || isGenericToken(t)) continue;
    const bool korean = std::any_of(t.begin(), t.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
    if (korean && t.size() > bestKorean.size()) bestKorean = t;
    if (!korean && t.size() > bestAscii.size()) bestAscii = t;
  }
  return !bestKorean.empty() ? bestKorean : bestAscii;
}

struct Chunk {
  int page = 0;
  std::string content;
  std::string snippet;
};

std::vector<Chunk> runScopedQuery(sqlite3* db, const std::string& rel_path, const std::string& match_expr) {
  std::vector<Chunk> out;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT page_no,content,snippet(chunks_fts,4,'[[',']]',' ... ',48) FROM chunks_fts "
      "WHERE chunks_fts MATCH ? AND rel_path = ? ORDER BY bm25(chunks_fts) LIMIT 80";
  if (::sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == kSqliteOk) {
    ::sqlite3_bind_text(st, 1, match_expr.c_str(), static_cast<int>(match_expr.size()), nullptr);
    ::sqlite3_bind_text(st, 2, rel_path.c_str(), static_cast<int>(rel_path.size()), nullptr);
    while (::sqlite3_step(st) == kSqliteRow)
      out.push_back({::sqlite3_column_int(st, 0), colStr(st, 1), colStr(st, 2)});
    ::sqlite3_finalize(st);
  }
  return out;
}

void analyzeChunks(const std::vector<Chunk>& chunks, SiteRagEvidence* ev) {
  if (chunks.empty()) return;
  const char* kEras[] = {"후기구석기", "구석기", "신석기", "청동기", "원삼국", "통일신라",
                         "신라",       "삼국",   "백제",   "가야",   "고려",   "조선"};
  const char* kFeatures[] = {"주거지", "수혈", "건물지", "고분", "석곽묘", "석실묘", "토광묘",
                             "구덩이", "가마", "우물",   "수로", "배수로", "기와",   "토기"};
  const char* kStrata[] = {"층위", "토층", "문화층", "생토", "사질점토", "점토층", "모래층"};
  ev->page_no = chunks.front().page;
  ev->feature_summary = cleanOneLine(chunks.front().snippet.empty() ? chunks.front().content : chunks.front().snippet);
  ev->feature_status = "✓";
  for (const Chunk& c : chunks) {
    const std::string& text = c.content;
    for (const char* era : kEras) {
      if (!ev->era_summary.empty()) break;
      if (text.find(era) != std::string::npos) {
        ev->era_summary = cleanOneLine(c.snippet.empty() ? text : c.snippet);
        ev->era_status = "✓";
      }
    }
    for (const char* feat : kFeatures) {
      if (ev->feature_summary.empty() || text.find(feat) == std::string::npos) continue;
      if (ev->page_no == chunks.front().page) {
        ev->feature_summary = cleanOneLine(c.snippet.empty() ? text : c.snippet);
        ev->page_no = c.page;
      }
      break;
    }
    if (ev->strata_summary.empty()) {
      for (const char* s : kStrata) {
        if (text.find(s) == std::string::npos) continue;
        ev->strata_summary = cleanOneLine(c.snippet.empty() ? text : c.snippet);
        ev->strata_status = "✓";
        ev->strata_page_no = c.page;
        break;
      }
    }
  }
}
}  // namespace

SiteRagEvidence LocalReportRag::inspectMatchedReport(const LocalReportMatchResult& match, std::string_view hint,
                                                     std::string_view) {
  SiteRagEvidence ev;
  ev.has_local_pdf = match.matched || match.indexed;
  ev.indexed = match.indexed;
  ev.report_year = match.year > 0 ? match.year : LocalReportMatch::parseYear(match.report_name);
  ev.institution = match.institution;
  ev.total_pages = match.total_pages;
  ev.report_name = !match.pdf_name.empty() ? match.pdf_name : match.report_name;
  if (!match.indexed) return ev;  // 로컬 PDF는 찾았지만 색인 전: ○ (미색인)
  if (match.scan) {
    ev.status = ev.era_status = ev.feature_status = ev.strata_status = "◌";
    return ev;
  }
  sqlite3* db = openRagDb();
  if (!db) return ev;
  const std::string token = scopeToken(match, hint);
  std::vector<Chunk> chunks;
  if (!token.empty())
    chunks =
        runScopedQuery(db, match.rel_path,
                       "report_name : \"" + token +
                           "\" AND (\"토층\" OR \"층위\" OR \"문화층\" OR \"생토\" OR "
                           "\"유구\" OR \"유물\" OR \"시대\" OR \"주거지\" OR \"건물지\" OR \"고분\" OR \"수혈\")");
  if (chunks.empty() && !token.empty()) chunks = runScopedQuery(db, match.rel_path, "report_name : \"" + token + "\"");
  if (chunks.empty())
    chunks = runScopedQuery(db, match.rel_path, "\"토층\" OR \"층위\" OR \"유구\" OR \"시대\" OR \"유물\"");
  ::sqlite3_close(db);
  ev.status = "✓";
  analyzeChunks(chunks, &ev);
  if (ev.page_no <= 0) ev.page_no = 1;
  return ev;
}

SiteRagEvidence LocalReportRag::inspectSiteEvidence(std::string_view site_name_utf8, std::string_view region_utf8) {
  SiteRagEvidence ev;
  if (site_name_utf8.empty()) return ev;
  LocalReportMatchQuery q;
  q.report_name = std::string(site_name_utf8);
  q.site_name = std::string(site_name_utf8);
  q.address = std::string(site_name_utf8);
  q.region = std::string(region_utf8);
  LocalReportMatchResult match = LocalReportMatch::find(q);
  if (!match.indexed && !LocalReportMatch::hasAddressNumber(site_name_utf8))
    match = LocalReportMatch::localityDocument(site_name_utf8, region_utf8);
  if (!match.matched && !match.indexed) return ev;
  return inspectMatchedReport(match, site_name_utf8, region_utf8);
}
}  // namespace strata::core
