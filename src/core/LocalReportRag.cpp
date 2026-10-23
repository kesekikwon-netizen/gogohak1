#include "core/LocalReportRag.h"
#include "platform/IPlatformService.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <system_error>
#include <unordered_map>

extern "C" {
struct sqlite3;
struct sqlite3_stmt;
int sqlite3_open_v2(const char*, sqlite3**, int, const char*);
int sqlite3_busy_timeout(sqlite3*, int);
int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, void (*)(void*));
int sqlite3_bind_int(sqlite3_stmt*, int, int);
int sqlite3_step(sqlite3_stmt*);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int);
int sqlite3_column_int(sqlite3_stmt*, int);
double sqlite3_column_double(sqlite3_stmt*, int);
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_close(sqlite3*);
}

namespace strata::core {
namespace {
constexpr int kSqliteOk = 0, kSqliteRow = 100, kSqliteRo = 0x00008001;
std::string colStr(sqlite3_stmt* st, int col) {
  const unsigned char* p = ::sqlite3_column_text(st, col);
  return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}
sqlite3* openRagDb() {
  std::error_code ec;
  sqlite3* db = nullptr;
  const auto p = LocalReportRag::ragDbPath();
  if (!std::filesystem::exists(p, ec) || ::sqlite3_open_v2(p.string().c_str(), &db, kSqliteRo, nullptr) != kSqliteOk)
    return db ? (::sqlite3_close(db), nullptr) : nullptr;
  return ::sqlite3_busy_timeout(db, 2000), db;
}
bool hasAny(std::string_view s, std::initializer_list<const char*> keys) {
  for (const char* k : keys) if (s.find(k) != std::string_view::npos) return true;
  return false;
}
std::string stripParticles(std::string tok) {
  for (const char* suf : {"지역의", "지역별", "지역", "시대의", "시대별", "유구별", "권역별", "에서", "으로", "와의",
                          "과의", "들과", "들의", "와", "과", "의", "을", "를", "은", "는", "에", "도"}) {
    const std::string_view sv(suf);
    if (tok.size() > sv.size() + 3 && std::string_view(tok).substr(tok.size() - sv.size()) == sv)
      return tok.substr(0, tok.size() - sv.size());
  }
  return tok;
}
std::string expandArchToken(const std::string& t) {
  if (t == "원형주거지") return "(\"원형주거지\"* OR (\"원형\"* AND \"주거지\"*))";
  if (t == "방형주거지") return "(\"방형주거지\"* OR (\"방형\"* AND \"주거지\"*))";
  if (t == "장방형주거지") return "(\"장방형주거지\"* OR (\"장방형\"* AND \"주거지\"*))";
  if (t == "후기구석기") return "(\"후기구석기\"* OR (\"후기\"* AND \"구석기\"*))";
  return "\"" + t + "\"*";
}
std::string buildFtsQuery(std::string_view raw, bool strictAnd) {
  std::vector<std::string> rawToks, archToks;
  std::string cur, out;
  for (char c : raw)
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '/' || c == '(' || c == ')') {
      if (!cur.empty()) rawToks.push_back(std::exchange(cur, {}));
    } else if (c != '"' && c != '\'' && c != '*' && c != '^' && c != ':') cur.push_back(c);
  if (!cur.empty()) rawToks.push_back(cur);
  if (strictAnd) {
    for (const auto& t : rawToks) out += (out.empty() ? "\"" : " AND \"") + t + "\"";
    return out.empty() ? "\"\"" : out;
  }
  for (const auto& rt : rawToks) {
    const std::string t = stripParticles(rt);
    if (!t.empty() && !hasAny(t, {"전국", "전체", "전남", "전북", "호남", "경북", "경남", "영남", "충남", "충북", "강원", "경기", "경상북도", "경상남도", "전라남도", "전라북도", "충청남도", "충청북도", "단독주택", "신축부지", "개설구간", "공사구간", "국비", "일원", "현황", "분포", "변화", "크기", "규모", "비교", "양상", "구조", "특징", "검토", "분석", "정리", "고찰", "추가해줘", "알려줘", "바꿔줘", "지역별", "시대별"}) && t != "유적" && t != "내" && t != "외")
      archToks.push_back(t);
  }
  if (archToks.empty()) archToks = rawToks;
  for (const auto& t : archToks) out += (out.empty() ? "" : " OR ") + expandArchToken(t);
  return out.empty() ? "\"\"" : out;
}
std::string extractJsonStr(std::string_view obj, std::string_view key) {
  auto pos = obj.find("\"" + std::string(key) + "\"");
  if (pos == std::string_view::npos || (pos = obj.find(':', pos)) == std::string_view::npos || (pos = obj.find('"', pos + 1)) == std::string_view::npos) return {};
  std::string out;
  for (std::size_t i = pos + 1; i < obj.size() && obj[i] != '"'; ++i)
    out.push_back((obj[i] == '\\' && i + 1 < obj.size()) ? (obj[++i] == 'n' ? '\n' : obj[i]) : obj[i]);
  return out;
}
}  // namespace

std::filesystem::path LocalReportRag::ragScriptPath() { return strata::platform::platformService().reportPdfRootDir() / "_rag_index_and_search.py"; }
std::filesystem::path LocalReportRag::pipelineScriptPath() { return strata::platform::platformService().reportPdfRootDir() / "run_pipeline.sh"; }
std::filesystem::path LocalReportRag::ragDbPath() { return strata::platform::platformService().reportRagDbPath(); }

std::string LocalReportRag::classifyMacroRegion(std::string_view sig) {
  // 경로의 첫 폴더(도 이름)가 가장 믿을 만하다. 고성·광주·영동처럼 도마다 있는 이름은 폴더로만 가른다.
  const std::string_view folder = sig.substr(0, sig.find('/'));
  if (folder == "강원도") return "강원도";
  if (folder == "경기도") return "경기도(수도권)";
  if (folder == "제주도") return "제주도";
  if (folder == "경상도")
    return hasAny(sig, {"경북", "경주", "포항", "경산", "구미", "안동", "김천", "영주", "영천", "상주", "문경", "칠곡", "의성", "성주", "고령", "청도군", "영덕", "울진", "봉화", "예천", "군위", "대구", "달성"}) ? "경상도(경북·대구)" : "경상도(경남·부울)";
  if (folder == "전라도")
    return hasAny(sig, {"전남", "광주", "나주", "순천", "여수", "광양", "목포", "무안", "해남", "영암", "화순", "담양", "보성", "고흥", "장흥", "강진", "완도", "진도", "신안", "함평", "영광", "장성", "곡성", "구례"}) ? "전라도(전남·광주)" : "전라도(전북)";
  if (folder == "충청도")
    return hasAny(sig, {"충남", "대전", "세종", "부여", "공주", "천안", "아산", "서산", "논산", "당진", "보령", "홍성", "예산", "서천", "청양", "태안", "금산", "계룡"}) ? "충청도(충남·대전)" : "충청도(충북)";
  // 폴더가 없으면 이름으로 짐작하되, 도마다 있는 이름(고성·광주·영동)과 모르는 이름은 「미분류」로 둔다.
  if (hasAny(sig, {"경북", "경상북도", "경주", "포항", "경산", "구미", "안동", "김천", "영주", "영천", "상주", "문경", "칠곡", "의성", "성주", "고령", "청도", "영덕", "울진", "봉화", "예천", "군위", "대구", "달성"})) return "경상도(경북·대구)";
  if (hasAny(sig, {"경남", "경상남도", "부산", "울산", "김해", "창원", "진주", "양산", "사천", "밀양", "거제", "통영", "함안", "창녕", "합천", "거창", "함양", "산청", "하동", "남해", "의령", "울주"})) return "경상도(경남·부울)";
  if (hasAny(sig, {"전남", "전라남도", "광주광역시", "나주", "순천", "여수", "광양", "목포", "무안", "해남", "영암", "화순", "담양", "보성", "고흥", "장흥", "강진", "완도", "진도", "신안", "함평", "영광", "장성", "곡성", "구례"})) return "전라도(전남·광주)";
  if (hasAny(sig, {"전북", "전라북도", "전주", "익산", "군산", "정읍", "남원", "김제", "완주", "고창", "부안", "임실", "순창", "진안", "장수", "무주"})) return "전라도(전북)";
  if (hasAny(sig, {"충남", "충청남도", "대전", "세종", "부여", "공주", "천안", "아산", "서산", "논산", "당진", "보령", "홍성", "예산", "서천", "청양", "태안", "금산", "계룡"})) return "충청도(충남·대전)";
  if (hasAny(sig, {"충북", "충청북도", "청주", "충주", "제천", "음성", "진천", "옥천", "괴산", "증평", "보은", "단양"})) return "충청도(충북)";
  if (hasAny(sig, {"강원", "춘천", "원주", "강릉", "동해", "속초", "삼척", "홍천", "철원", "양양", "정선", "영월", "평창", "양구", "인제", "화천", "횡성"})) return "강원도";
  if (hasAny(sig, {"제주", "서귀포"})) return "제주도";
  if (hasAny(sig, {"경기", "서울", "인천", "수원", "성남", "용인", "화성", "평택", "파주", "연천", "포천", "양주", "하남", "이천", "여주", "안성", "가평", "김포", "강화", "고양", "부천", "안산", "안양", "의정부", "시흥", "광명", "군포", "오산", "의왕", "과천", "구리", "남양주", "동두천", "양평", "광주시"})) return "경기도(수도권)";
  return "미분류";
}

std::string LocalReportRag::extractRegionScope(std::string_view prompt_utf8, std::string_view default_region_utf8) {
  if (hasAny(prompt_utf8, {"전국", "전체"})) return "전국";
  if (prompt_utf8.find("강원") != std::string_view::npos && prompt_utf8.find("고성") != std::string_view::npos) return "강원도/고성군";
  if (hasAny(prompt_utf8, {"경남", "경상"}) && prompt_utf8.find("고성") != std::string_view::npos) return "경상도/고성군";
  std::string joined;
  for (const char* r : {"서울", "인천", "대전", "세종", "대구", "부산", "울산", "광주", "경주", "포항", "경산", "구미", "안동", "김천", "영주", "영천", "상주", "문경", "의성", "고령", "성주", "칠곡", "청도", "김해", "창원", "진주", "양산", "사천", "밀양", "창녕", "함안", "합천", "고성", "거창", "함양", "하동", "부여", "공주", "천안", "아산", "서산", "논산", "당진", "보령", "홍성", "예산", "서천", "청주", "충주", "제천", "진천", "음성", "보은", "옥천", "익산", "전주", "군산", "정읍", "남원", "김제", "완주", "고창", "부안", "장수", "나주", "순천", "여수", "광양", "목포", "해남", "영암", "무안", "화순", "담양", "장성", "함평", "영광", "강진", "장흥", "보성", "고흥", "곡성", "완도", "진도", "신안", "춘천", "원주", "강릉", "동해", "속초", "삼척", "홍천", "철원", "양양", "정선", "영월", "평창", "수원", "성남", "용인", "화성", "평택", "파주", "연천", "포천", "양주", "하남", "이천", "여주", "안성", "가평", "김포", "강화", "서귀포", "경기도", "강원도", "충청도", "경상도", "전라도", "제주도", "전남", "전북", "호남", "경북", "경남", "영남", "충남", "충북", "강원", "경기", "제주"})
    if (prompt_utf8.find(r) != std::string_view::npos) joined += (joined.empty() ? "" : ",") + std::string(r);
  if (!joined.empty()) return joined;
  return hasAny(prompt_utf8, {"지역별", "권역별", "시군구별", "도별"}) ? "전국" : (default_region_utf8.empty() ? "경주시" : std::string(default_region_utf8));
}
std::vector<std::string> LocalReportRag::expandRegionFilters(std::string_view expr) {
  if (expr.empty() || expr == "전국" || expr == "전체" || expr == "지역별" || expr == "도별") return {};
  std::vector<std::string> toks, out;
  std::string cur;
  for (char c : expr) if (c == ',' || c == '+') { if (!cur.empty()) toks.push_back(std::exchange(cur, {})); } else cur.push_back(c);
  if (!cur.empty()) toks.push_back(cur);
  auto addAll = [&](std::initializer_list<const char*> list) { for (const char* s : list) out.emplace_back(s); };
  for (const auto& tk : toks) {
    if (tk == "경기도" || tk == "경기" || tk == "수도권") addAll({"경기도/"});
    else if (tk == "강원도" || tk == "강원") addAll({"강원도/"});
    else if (tk == "충청도" || tk == "충청") addAll({"충청도/"});
    else if (tk == "경상도" || tk == "경상" || tk == "영남") addAll({"경상도/"});
    else if (tk == "전라도" || tk == "전라" || tk == "호남") addAll({"전라도/"});
    else if (tk == "제주도" || tk == "제주") addAll({"제주도/"});
    else if (hasAny(tk, {"전남", "전라남도"})) addAll({"나주", "순천", "여수", "광양", "목포", "무안", "해남", "영암", "화순", "담양", "보성", "고흥", "장흥", "강진", "완도", "함평", "영광", "장성", "곡성", "구례", "광주광역시"});
    else if (hasAny(tk, {"전북", "전라북도"})) addAll({"전주", "익산", "군산", "정읍", "남원", "김제", "완주", "고창", "부안", "임실", "순창", "진안", "장수", "무주"});
    else if (hasAny(tk, {"경북", "경상북도"})) addAll({"경주", "포항", "경산", "구미", "안동", "김천", "영주", "영천", "상주", "문경", "칠곡", "의성", "성주", "고령", "청도", "영덕", "울진", "봉화", "예천", "군위", "대구"});
    else if (hasAny(tk, {"경남", "경상남도"})) addAll({"김해", "창원", "진주", "양산", "사천", "밀양", "함안", "창녕", "합천", "거창", "함양", "산청", "하동", "경상도/고성", "부산", "울산"});
    else if (hasAny(tk, {"충남", "충청남도"})) addAll({"부여", "공주", "천안", "아산", "서산", "논산", "당진", "보령", "홍성", "예산", "서천", "청양", "태안", "금산", "대전", "세종"});
    else if (hasAny(tk, {"충북", "충청북도"})) addAll({"청주", "충주", "제천", "음성", "진천", "옥천", "영동", "괴산", "증평", "보은", "단양"});
    else out.push_back(tk);
  }
  return out;
}
LocalReportStats LocalReportRag::queryStats() {
  LocalReportStats st;
  sqlite3* db = openRagDb();
  if (!db) return st;
  auto qInt = [&](const char* sql) {
    sqlite3_stmt* s = nullptr;
    int v = (::sqlite3_prepare_v2(db, sql, -1, &s, nullptr) == kSqliteOk && ::sqlite3_step(s) == kSqliteRow) ? ::sqlite3_column_int(s, 0) : 0;
    return s ? (::sqlite3_finalize(s), v) : v;
  };
  st.report_count = qInt("SELECT COUNT(*) FROM documents"), st.region_count = qInt("SELECT COUNT(DISTINCT sigungu) FROM documents"), st.chunk_count = qInt("SELECT COUNT(*) FROM chunks_fts");
  ::sqlite3_close(db);
  st.available = (st.report_count > 0 || st.chunk_count > 0);
  st.summary_utf8 = "인덱싱된 보고서 수: " + std::to_string(st.report_count) + "권 | 지역 수: " + std::to_string(st.region_count) + "개";
  return st;
}
RegionReportStats LocalReportRag::queryRegionStats(std::string_view region_utf8) {
  RegionReportStats rs{std::string(region_utf8), 0, 0, 0, 0};
  sqlite3* db = region_utf8.empty() ? nullptr : openRagDb();
  if (!db) return rs;
  const auto regFilters = expandRegionFilters(region_utf8);
  std::string sql = "SELECT extraction_mode, COUNT(*) FROM documents";
  if (!regFilters.empty()) {
    sql += " WHERE ";
    for (std::size_t i = 0; i < regFilters.size(); ++i) sql += (i ? " OR " : "") + std::string("(sigungu LIKE ? OR rel_path LIKE ?)");
  }
  sql += " GROUP BY extraction_mode";
  sqlite3_stmt* st = nullptr;
  if (::sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == kSqliteOk) {
    std::vector<std::string> likes;
    likes.reserve(regFilters.size());
    for (std::size_t i = 0; i < regFilters.size(); ++i) {
      likes.push_back("%" + regFilters[i] + "%");
      ::sqlite3_bind_text(st, static_cast<int>(i * 2 + 1), likes.back().c_str(), static_cast<int>(likes.back().size()), nullptr);
      ::sqlite3_bind_text(st, static_cast<int>(i * 2 + 2), likes.back().c_str(), static_cast<int>(likes.back().size()), nullptr);
    }
    while (::sqlite3_step(st) == kSqliteRow) {
      const int cnt = ::sqlite3_column_int(st, 1);
      rs.total_reports += cnt, (colStr(st, 0).find("scan_no_text") != std::string::npos ? rs.scan_reports : rs.text_reports) += cnt;
    }
    ::sqlite3_finalize(st);
  }
  return ::sqlite3_close(db), rs;
}
std::vector<LocalReportHit> LocalReportRag::parseSearchJson(std::string_view json_utf8) {
  std::vector<LocalReportHit> hits;
  for (std::size_t pos = 0; (pos = json_utf8.find('{', pos)) != std::string_view::npos;) {
    auto end = json_utf8.find('}', pos);
    if (end == std::string_view::npos) break;
    const auto obj = json_utf8.substr(pos, end - pos + 1);
    const std::string sig = extractJsonStr(obj, "sigungu");
    LocalReportHit h{sig, classifyMacroRegion(sig), extractJsonStr(obj, "report_name"), 0, 0.0, extractJsonStr(obj, "snippet"), extractJsonStr(obj, "content"), extractJsonStr(obj, "txt_path"), extractJsonStr(obj, "pdf_path")};
    if (auto p = obj.find("\"page_no\""); p != std::string_view::npos && (p = obj.find(':', p)) != std::string_view::npos)
      h.page_no = std::atoi(std::string(obj.substr(p + 1, 16)).c_str());
    if (!h.report_name.empty()) hits.push_back(std::move(h));
    pos = end + 1;
  }
  return hits;
}
std::vector<LocalReportHit> LocalReportRag::search(std::string_view query_utf8, std::string_view region_utf8, int top_k, int max_per_report) {
  std::vector<LocalReportHit> hits;
  sqlite3* db = query_utf8.empty() ? nullptr : openRagDb();
  if (!db) return hits;
  const std::string fts_q = buildFtsQuery(query_utf8, max_per_report < 0);
  const auto regFilters = expandRegionFilters(region_utf8);
  std::string sql = "SELECT rel_path, sigungu, report_name, page_no, snippet(chunks_fts, 4, '[[', ']]', ' ... ', 64), content, bm25(chunks_fts) AS score FROM chunks_fts WHERE chunks_fts MATCH ?";
  if (!regFilters.empty()) {
    sql += " AND (";
    for (std::size_t i = 0; i < regFilters.size(); ++i) sql += (i ? " OR " : "") + std::string("(sigungu LIKE ? OR rel_path LIKE ?)");
    sql += ")";
  }
  sql += " ORDER BY score LIMIT ?";
  sqlite3_stmt* stmt = nullptr;
  if (::sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == kSqliteOk) {
    int idx = 1;
    ::sqlite3_bind_text(stmt, idx++, fts_q.c_str(), static_cast<int>(fts_q.size()), nullptr);
    std::vector<std::string> likes;
    likes.reserve(regFilters.size());
    for (const auto& rf : regFilters) {
      likes.push_back("%" + rf + "%");
      ::sqlite3_bind_text(stmt, idx++, likes.back().c_str(), static_cast<int>(likes.back().size()), nullptr);
      ::sqlite3_bind_text(stmt, idx++, likes.back().c_str(), static_cast<int>(likes.back().size()), nullptr);
    }
    const int want = top_k > 0 ? top_k : 10, cap = std::abs(max_per_report) > 0 ? std::abs(max_per_report) : 2, sigCap = (regFilters.empty() || regFilters.size() > 1) ? std::max(3, want / 3) : want;
    ::sqlite3_bind_int(stmt, idx, want * 12);
    const auto& pal = strata::platform::platformService();
    std::unordered_map<std::string, int> rptCnt, sigCnt;
    while (::sqlite3_step(stmt) == kSqliteRow && static_cast<int>(hits.size()) < want) {
      std::string content = colStr(stmt, 5);
      if (content.size() < 70 || hasAny(content, {"스캔본/텍스트 미추출", ".....", "·····"})) continue;
      const std::filesystem::path rel(colStr(stmt, 0));
      const std::string leafSig = rel.parent_path().filename().string();
      const std::string sig = (!leafSig.empty() && leafSig != ".") ? leafSig : colStr(stmt, 1), rpt = colStr(stmt, 2);
      if (++rptCnt[rpt] > cap || ++sigCnt[sig] > sigCap) continue;
      auto pdf_rel = pal.reportPdfRootDir() / rel;
      pdf_rel.replace_extension(".pdf");
      if (content.size() > 800) content.resize(800);
      hits.push_back({sig, classifyMacroRegion(rel.string()), rpt, ::sqlite3_column_int(stmt, 3), ::sqlite3_column_double(stmt, 6), colStr(stmt, 4), std::move(content), pal.reportTextRootDir() / rel, std::move(pdf_rel)});
    }
    ::sqlite3_finalize(stmt);
  }
  return ::sqlite3_close(db), hits;
}
std::string LocalReportRag::formatPromptContext(std::span<const LocalReportHit> hits) {
  std::ostringstream oss;
  oss << "## [로컬 발굴조사보고서 RAG 원문 발췌 (" << hits.size() << "건)]\n";
  for (std::size_t i = 0; i < hits.size(); ++i)
    oss << "- **[R" << (i + 1) << "] [" << hits[i].sigungu << "] 『" << hits[i].report_name << "』 (p." << hits[i].page_no << ")**: " << hits[i].content << "\n";
  return oss.str();
}
bool LocalReportRag::startPipelineDetached() {
  std::error_code ec;
  const auto& pal = strata::platform::platformService();
  return std::filesystem::exists(pipelineScriptPath(), ec) &&
         pal.spawnDetached(pal.resolveExecutable("bash"), std::array<std::string, 1>{pipelineScriptPath().string()}, pipelineScriptPath().parent_path());
}
}  // namespace strata::core
