#include "core/LocalReportCatalog.h"
#include "core/LocalReportText.h"
#include "platform/IPlatformService.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

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
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_close(sqlite3*);
}

namespace strata::core::LocalReportMatchDetail {
namespace {
constexpr int kSqliteOk = 0, kSqliteRow = 100, kSqliteRo = 0x00008001;

std::vector<std::string> csvFields(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur.push_back('"');
          ++i;
        } else
          quoted = false;
      } else
        cur.push_back(c);
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      out.push_back(cur);
      cur.clear();
    } else if (c != '\r')
      cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

sqlite3* openRagDb() {
  sqlite3* db = nullptr;
  const auto p = strata::platform::platformService().reportRagDbPath();
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || ::sqlite3_open_v2(p.string().c_str(), &db, kSqliteRo, nullptr) != kSqliteOk)
    return db ? (::sqlite3_close(db), nullptr) : nullptr;
  return ::sqlite3_busy_timeout(db, 2000), db;
}

std::string colStr(sqlite3_stmt* st, int col) {
  const unsigned char* p = ::sqlite3_column_text(st, col);
  return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}

void loadLogs(Catalog& cat) {
  const auto path = strata::platform::platformService().reportPdfRootDir() / "_download_log.csv";
  std::ifstream in(path, std::ios::binary);
  if (!in) return;
  std::string line;
  if (!std::getline(in, line)) return;
  if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
  const auto head = csvFields(line);
  auto indexOf = [&](const char* name) {
    for (std::size_t i = 0; i < head.size(); ++i)
      if (head[i] == name) return static_cast<int>(i);
    return -1;
  };
  const int iy = indexOf("year"), it = indexOf("title"), io = indexOf("org_name"), is = indexOf("site_name");
  const int ia = indexOf("attachment_name"), ist = indexOf("status");
  if (it < 0 || ist < 0) return;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const auto f = csvFields(line);
    auto field = [&](int i) {
      return (i >= 0 && i < static_cast<int>(f.size())) ? f[static_cast<std::size_t>(i)] : std::string();
    };
    LogRow r;
    r.year = iy >= 0 ? std::atoi(field(iy).c_str()) : 0;
    r.title = field(it), r.org = field(io), r.site = field(is), r.attach = field(ia), r.status = field(ist);
    if (r.status != "downloaded" && r.status != "skipped_existing") continue;
    std::string attach_stem = r.attach;
    if (attach_stem.size() > 4 && attach_stem.compare(attach_stem.size() - 4, 4, ".pdf") == 0)
      attach_stem.erase(attach_stem.size() - 4);
    r.title_key = LocalReportText::titleKey(r.title);
    r.attach_key = LocalReportText::titleKey(attach_stem);
    r.site_key = LocalReportText::titleKey(r.site);
    r.base_key = stripYears(r.title_key);
    r.org_key = institutionKey(r.org);
    r.addr_key = LocalReportText::addressKey(r.site);
    if (r.addr_key.empty()) r.addr_key = LocalReportText::addressKey(r.title);
    r.phase = LocalReportText::phase(r.title);
    if (r.phase == 0) r.phase = LocalReportText::phase(r.attach);
    cat.logs.push_back(std::move(r));
  }
}

void loadDocs(Catalog& cat) {
  sqlite3* db = openRagDb();
  if (!db) return;
  sqlite3_stmt* st = nullptr;
  if (::sqlite3_prepare_v2(db, "SELECT rel_path,sigungu,report_name,total_pages,extraction_mode FROM documents", -1,
                           &st, nullptr) == kSqliteOk) {
    while (::sqlite3_step(st) == kSqliteRow) {
      DocRow d;
      d.rel_path = colStr(st, 0), d.sigungu = colStr(st, 1), d.report_name = colStr(st, 2);
      d.total_pages = ::sqlite3_column_int(st, 3), d.mode = colStr(st, 4);
      d.scan = d.mode.find("scan_no_text") != std::string::npos;
      std::string stem = d.report_name;
      if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".pdf") == 0) stem.erase(stem.size() - 4);
      d.title_key = LocalReportText::titleKey(stem);
      d.base_key = stripYears(d.title_key);
      cat.docs.push_back(std::move(d));
    }
    ::sqlite3_finalize(st);
  }
  ::sqlite3_close(db);
}

void buildIndex(Catalog& cat) {
  for (std::size_t i = 0; i < cat.docs.size(); ++i) {
    const DocRow& d = cat.docs[i];
    for (const std::string& k : {d.title_key, d.base_key})
      if (!k.empty()) cat.by_key[k].push_back(i);
    const std::string leaf = std::filesystem::path(d.rel_path).stem().string();
    for (const std::string& k : {LocalReportText::titleKey(leaf), stripYears(LocalReportText::titleKey(leaf))})
      if (!k.empty()) cat.by_key[k].push_back(i);
  }
}
}  // namespace

std::string stripYears(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] >= '0' && s[i] <= '9') {
      std::size_t j = i;
      while (j < s.size() && s[j] >= '0' && s[j] <= '9')
        ++j;
      // 주소 번지(150-3 등)를 연도로 오인해 지우지 않는다. 1900~2099만 연도로 본다.
      const int value = (j - i == 4) ? std::atoi(std::string(s.substr(i, 4)).c_str()) : 0;
      if (value < 1900 || value > 2099) out.append(s.substr(i, j - i));
      i = j;
    } else {
      out.push_back(s[i++]);
    }
  }
  return out;
}

std::string institutionKey(std::string_view text_utf8) {
  std::string out = LocalReportText::normalize(text_utf8);
  for (std::size_t p = 0; (p = out.find("문화재", p)) != std::string::npos;)
    out.replace(p, 9, "문화유산");
  return out;
}

bool regionMatches(const DocRow& doc, const std::string& region) {
  return region.empty() || doc.sigungu.find(region) != std::string::npos ||
         doc.rel_path.find(region) != std::string::npos;
}

int docYear(const DocRow& doc) {
  return LocalReportText::parseYear(doc.report_name);
}

const Catalog& catalog() {
  static const Catalog value = [] {
    Catalog c;
    loadLogs(c);
    loadDocs(c);
    buildIndex(c);
    return c;
  }();
  return value;
}

std::vector<LocalReportDocument> documentsLike(std::string_view text_utf8, std::string_view region_utf8, int limit) {
  std::vector<LocalReportDocument> out;
  sqlite3* db = text_utf8.empty() ? nullptr : openRagDb();
  if (!db) return out;
  const std::string like = "%" + std::string(text_utf8) + "%", reg = "%" + std::string(region_utf8) + "%";
  std::string sql =
      "SELECT rel_path,sigungu,report_name,total_pages,extraction_mode FROM documents WHERE report_name LIKE ?";
  if (!region_utf8.empty()) sql += " AND (sigungu LIKE ? OR rel_path LIKE ?)";
  sql += " ORDER BY CASE WHEN extraction_mode LIKE '%scan_no_text%' THEN 1 ELSE 0 END, total_pages DESC LIMIT ?";
  sqlite3_stmt* st = nullptr;
  if (::sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == kSqliteOk) {
    int idx = 1;
    ::sqlite3_bind_text(st, idx++, like.c_str(), static_cast<int>(like.size()), nullptr);
    if (!region_utf8.empty()) {
      ::sqlite3_bind_text(st, idx++, reg.c_str(), static_cast<int>(reg.size()), nullptr);
      ::sqlite3_bind_text(st, idx++, reg.c_str(), static_cast<int>(reg.size()), nullptr);
    }
    ::sqlite3_bind_int(st, idx, limit > 0 ? limit : 8);
    while (::sqlite3_step(st) == kSqliteRow) {
      LocalReportDocument d;
      d.rel_path = colStr(st, 0), d.sigungu = colStr(st, 1), d.report_name = colStr(st, 2);
      d.total_pages = ::sqlite3_column_int(st, 3), d.extraction_mode = colStr(st, 4);
      d.scan = d.extraction_mode.find("scan_no_text") != std::string::npos;
      out.push_back(std::move(d));
    }
    ::sqlite3_finalize(st);
  }
  ::sqlite3_close(db);
  return out;
}
}  // namespace strata::core::LocalReportMatchDetail
