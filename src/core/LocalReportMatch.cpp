#include "core/LocalReportMatch.h"
#include "core/LocalReportCatalog.h"
#include "core/LocalReportText.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace strata::core {
namespace {
using namespace LocalReportMatchDetail;

std::size_t pickDoc(const Catalog& c, const LogRow& best, const LocalReportMatchQuery& q) {
  std::vector<std::pair<double, std::size_t>> picks;
  std::unordered_set<std::size_t> seen;
  auto add = [&](const std::string& key) {
    const auto it = c.by_key.find(key);
    if (it == c.by_key.end()) return;
    for (std::size_t i : it->second)
      if (seen.insert(i).second) picks.emplace_back(0.0, i);
  };
  for (const std::string& k : {best.attach_key, best.title_key, best.base_key, best.site_key})
    add(k);
  for (auto& pick : picks) {
    const DocRow& d = c.docs[pick.second];
    pick.first = regionMatches(d, q.region) ? 4.0 : 0.0;
    if (!d.scan) pick.first += 2.0;
    if (d.title_key == best.title_key || d.title_key == best.attach_key)
      pick.first += 30.0;  // 서지 제목이 정확히 같은 원문을 먼저 쓴다.
    else if (!best.base_key.empty() && d.base_key == best.base_key)
      pick.first += 6.0;
    if (best.year > 0 && docYear(d) > 0) pick.first += std::max(0, 5 - std::abs(best.year - docYear(d)));
  }
  if (picks.empty()) {
    const std::string keys[] = {best.attach_key, best.title_key, best.site_key};
    for (std::size_t i = 0; i < c.docs.size(); ++i) {
      const DocRow& d = c.docs[i];
      if (!regionMatches(d, q.region)) continue;
      double sim = 0.0;
      bool contained = false;
      for (const std::string& k : keys) {
        if (k.empty() || d.title_key.empty()) continue;
        sim = std::max(sim, LocalReportText::dice(k, d.title_key));
        if (k.size() >= 8 && d.title_key.size() >= 8 &&
            (k.find(d.title_key) != std::string::npos || d.title_key.find(k) != std::string::npos))
          contained = true;
      }
      if (sim < 0.90 && !contained) continue;
      picks.emplace_back(sim * 10.0 + (d.scan ? 0.0 : 2.0) + 4.0, i);
    }
  }
  if (picks.empty()) return c.docs.size();
  std::sort(picks.begin(), picks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  return picks.front().second;
}

void fillDoc(const Catalog& c, std::size_t index, LocalReportMatchResult& out) {
  if (index >= c.docs.size()) return;
  const DocRow& d = c.docs[index];
  out.indexed = true, out.scan = d.scan;
  out.rel_path = d.rel_path, out.sigungu = d.sigungu, out.extraction_mode = d.mode, out.total_pages = d.total_pages;
}

// 유적명·보고서명·소재지의 번지를 모두 후보로 둔다.
// 372072/372-72, 299-1/199-1처럼 한쪽에 오기가 있어도 다른 쪽 번지로 대조할 수 있게 한다.
std::vector<std::string> addressKeys(const LocalReportMatchQuery& q) {
  std::vector<std::string> out;
  for (const std::string* s : {&q.site_name, &q.report_name, &q.address, &q.project_name}) {
    const std::string a = LocalReportText::addressKey(*s);
    if (!a.empty() && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
  }
  return out;
}

// GIS 서지와 실제 대조한 보고서 제목이 같은 문헌인지 판정한다.
// 유적명·사업명·보고서명을 모두 비교하고, 주소+연도도 보조 키로 쓴다.
// GIS 보고서명이 비어 있으면 '같은 보고서'라고 단정하지 않는다(✓ 금지).
bool sameReportText(const LocalReportMatchQuery& q, const std::string& raw_title, const std::string& raw_attach,
                    int known_year) {
  if (LocalReportText::titleKey(q.report_name).empty()) return false;
  const std::string doc_key =
      !raw_title.empty() ? LocalReportText::titleKey(raw_title) : LocalReportText::titleKey(raw_attach);
  if (doc_key.empty()) return false;
  const int qyear = q.year > 0 ? q.year : LocalReportText::parseYear(q.report_name);
  const int dyear = known_year > 0 ? known_year : LocalReportText::parseYear(raw_title + " " + raw_attach);
  const bool year_ok = qyear <= 0 || dyear <= 0 || std::abs(qyear - dyear) <= 2;

  const std::string keys[] = {LocalReportText::titleKey(q.site_name), LocalReportText::titleKey(q.project_name),
                              LocalReportText::titleKey(q.report_name)};
  for (const std::string& k : keys) {
    if (k.size() < 4) continue;
    if (k == doc_key) return year_ok;
    if (k.size() >= 6 && doc_key.size() >= 6 &&
        (k.find(doc_key) != std::string::npos || doc_key.find(k) != std::string::npos))
      return year_ok;
    if (LocalReportText::dice(k, doc_key) >= 0.78) return year_ok;
  }

  // 번지가 하나라도 일치하면 같은 유적으로 본다. 단, 같은 동·리 안에서만 인정해
  // 태종로/동성로처럼 같은 번지의 다른 사업을 오매칭하지 않는다.
  const std::string doc_addr = LocalReportText::addressKey(raw_title);
  if (doc_addr.empty()) return false;
  const std::string qloc = LocalReportText::locality(q.site_name + " " + q.report_name);
  const std::string dloc = LocalReportText::locality(raw_title);
  if (qloc.empty() || dloc.empty() || qloc != dloc) return false;
  for (const std::string& a : addressKeys(q))
    if (a == doc_addr) return year_ok;
  return false;
}
}  // namespace

int LocalReportMatch::parseYear(std::string_view text_utf8) {
  return LocalReportText::parseYear(text_utf8);
}
std::string LocalReportMatch::normalize(std::string_view text_utf8) {
  return LocalReportText::normalize(text_utf8);
}
std::string LocalReportMatch::titleKey(std::string_view text_utf8) {
  return LocalReportText::titleKey(text_utf8);
}
std::string LocalReportMatch::addressKey(std::string_view text_utf8) {
  return LocalReportText::addressKey(text_utf8);
}
bool LocalReportMatch::hasAddressNumber(std::string_view text_utf8) {
  return LocalReportText::hasAddressNumber(text_utf8);
}

LocalReportMatchResult LocalReportMatch::find(const LocalReportMatchQuery& q) {
  LocalReportMatchResult out;
  const Catalog& c = catalog();
  std::vector<std::string> keys;
  auto addKey = [&](const std::string& k) {
    if (k.size() >= 4 && std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
  };
  addKey(LocalReportText::titleKey(q.report_name));
  addKey(LocalReportText::titleKey(q.project_name));
  addKey(LocalReportText::titleKey(q.site_name));
  if (keys.empty()) return out;
  const int year = q.year > 0 ? q.year : LocalReportText::parseYear(q.report_name);
  const std::vector<std::string> addrs = addressKeys(q);
  const std::string inst = institutionKey(q.institution);
  int qPhase = LocalReportText::phase(q.project_name);
  if (qPhase == 0) qPhase = LocalReportText::phase(q.report_name);
  static const std::unordered_set<std::string> kGeneric = {"유적",     "문화유적",   "조사구역내유적", "발굴조사",
                                                           "시굴조사", "조사보고서", "발굴조사보고서"};
  const bool genericHit =
      std::any_of(keys.begin(), keys.end(), [&](const std::string& k) { return kGeneric.count(k) > 0; });
  const LogRow* best = nullptr;
  int bestYearDiff = 99;
  for (const LogRow& r : c.logs) {
    if (year > 0 && r.year > 0 && std::abs(year - r.year) > 5) continue;
    if (qPhase > 0 && r.phase != qPhase) continue;
    if (qPhase == 0 && r.phase > 0) continue;
    double sim = 0.0;
    bool exact = false;
    for (const std::string& k : keys) {
      for (const std::string& rk : {r.title_key, r.attach_key, r.site_key, r.base_key}) {
        if (rk.empty()) continue;
        exact = exact || k == rk;
        sim = std::max(sim, LocalReportText::dice(k, rk));
      }
    }
    int tscore = exact ? 100 : (sim >= 0.85 ? 100 : (sim >= 0.72 ? 70 : (sim >= 0.60 ? 40 : 0)));
    for (const std::string& k : keys)
      for (const std::string& rk : {r.title_key, r.attach_key, r.site_key})
        if (k.size() >= 8 && rk.size() >= 8 && (k.find(rk) != std::string::npos || rk.find(k) != std::string::npos))
          tscore = std::max(tscore, 70);
    const bool addrMatch =
        std::any_of(addrs.begin(), addrs.end(), [&](const std::string& a) { return a == r.addr_key; });
    const int ascore = addrMatch ? 40 : 0;
    const int diff = (year > 0 && r.year > 0) ? std::abs(year - r.year) : 99;
    const int yscore =
        diff == 0 ? 30
                  : (diff == 1 ? 25 : (diff == 2 ? 20 : (diff == 3 ? 15 : (diff == 4 ? 10 : (diff == 5 ? 5 : 0)))));
    const int iscore = (!inst.empty() && !r.org_key.empty() &&
                        (inst.find(r.org_key) != std::string::npos || r.org_key.find(inst) != std::string::npos))
                           ? 15
                           : 0;
    const int total = tscore + ascore + yscore + iscore;
    if (!ascore && tscore < 100 && !(tscore >= 70 && iscore > 0)) continue;
    if (genericHit && !ascore) continue;
    if (total < 80) continue;
    if (!best || total > static_cast<int>(out.score) || (total == static_cast<int>(out.score) && diff < bestYearDiff)) {
      best = &r, bestYearDiff = diff, out.score = total;
    }
  }
  if (!best) return out;
  out.matched = true;
  out.report_name = best->title;
  out.pdf_name = best->attach.empty() ? best->title : best->attach;
  out.institution = best->org;
  out.year = best->year;
  const std::size_t doc_index = pickDoc(c, *best, q);
  fillDoc(c, doc_index, out);
  // 실제로 대조할 PDF가 GIS 서지와 같은 보고서일 때만 same_report=true.
  if (doc_index < c.docs.size())
    out.same_report = sameReportText(q, c.docs[doc_index].report_name, {}, docYear(c.docs[doc_index]));
  else
    out.same_report = sameReportText(q, best->title, best->attach, best->year);
  return out;
}

std::vector<LocalReportDocument> LocalReportMatch::documentsLike(std::string_view text_utf8,
                                                                 std::string_view region_utf8, int limit) {
  return LocalReportMatchDetail::documentsLike(text_utf8, region_utf8, limit);
}

LocalReportMatchResult LocalReportMatch::localityDocument(std::string_view site_name_utf8,
                                                          std::string_view region_utf8) {
  LocalReportMatchResult out;
  if (hasAddressNumber(site_name_utf8)) return out;
  const std::string loc = LocalReportText::locality(site_name_utf8);
  if (loc.empty()) return out;
  const auto docs = documentsLike(loc, region_utf8, 8);
  if (docs.empty()) return out;
  const LocalReportDocument& d = docs.front();
  out.matched = out.indexed = true;
  out.scan = d.scan;
  out.report_name = out.pdf_name = d.report_name;
  out.rel_path = d.rel_path, out.sigungu = d.sigungu, out.extraction_mode = d.extraction_mode,
  out.total_pages = d.total_pages;
  out.same_report = false;
  out.locality_reference = true;
  return out;
}
}  // namespace strata::core
