#include "core/LocalReportText.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

namespace strata::core::LocalReportText {
namespace {
using U32 = std::u32string;

bool isDigit(char32_t c) {
  return c >= U'0' && c <= U'9';
}
bool isHangul(char32_t c) {
  return c >= 0xAC00 && c <= 0xD7A3;
}
bool isAsciiAlpha(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}
bool isEnding(char32_t c) {
  return c == 0xB3D9 || c == 0xB9AC || c == 0xAC00 || c == 0xBA74 || c == 0xC74D;
}

U32 utf8ToU32(std::string_view s) {
  U32 out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = 0;
    int n = 0;
    if (c < 0x80)
      cp = c, n = 1;
    else if ((c >> 5) == 0x6)
      cp = c & 0x1F, n = 2;
    else if ((c >> 4) == 0xE)
      cp = c & 0x0F, n = 3;
    else if ((c >> 3) == 0x1E)
      cp = c & 0x07, n = 4;
    else {
      ++i;
      continue;
    }
    if (i + static_cast<std::size_t>(n) > s.size()) break;
    bool ok = true;
    for (int k = 1; k < n; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
      if ((cc >> 6) != 0x2) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) {
      ++i;
      continue;
    }
    out.push_back(cp);
    i += static_cast<std::size_t>(n);
  }
  return out;
}

std::string u32ToUtf8(const U32& s) {
  std::string out;
  for (char32_t cp : s) {
    if (cp < 0x80)
      out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

U32 lowerAscii(U32 s) {
  for (char32_t& c : s)
    if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
  return s;
}

void eraseAll(U32& s, std::u32string_view needle) {
  if (needle.empty()) return;
  for (std::size_t p = 0; (p = s.find(needle, p)) != U32::npos;)
    s.erase(p, needle.size());
}

void replaceAll(U32& s, std::u32string_view from, std::u32string_view to) {
  if (from.empty()) return;
  for (std::size_t p = 0; (p = s.find(from, p)) != U32::npos; p += to.size())
    s.replace(p, from.size(), to);
}

bool dropCodepoint(char32_t c) {
  if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r') return true;
  static const U32 kDrop = U"()[]{}<>《》『』「」“”\"'·ㆍ,.;:-_/\\|`!?~+*=#&%$@^";
  return kDrop.find(c) != U32::npos;
}

void stripLeadingVolume(U32& s) {
  for (int round = 0; round < 3; ++round) {
    std::size_t i = 0;
    if (i < s.size() && s[i] == U'제') ++i;
    const std::size_t d = i;
    while (i < s.size() && isDigit(s[i]))
      ++i;
    if (i == d) break;
    if (i < s.size() && (s[i] == U'.' || s[i] == U')' || s[i] == U'책' || s[i] == U'권')) ++i;
    s.erase(0, i);
  }
}

std::string tokenWithNumber(const U32& s, std::size_t ending) {
  std::size_t start = ending;
  while (start > 0 && isHangul(s[start - 1]) && ending - start < 5)
    --start;
  U32 tok = s.substr(start, ending - start + 1);
  if (tok.size() > 3) tok = tok.substr(tok.size() - 3);
  U32 num;
  for (std::size_t k = ending + 1; k < s.size() && k <= ending + 14; ++k) {
    const char32_t c = s[k];
    if (isDigit(c)) {
      num.push_back(c);
      while (k + 1 < s.size() && (isDigit(s[k + 1]) || (s[k + 1] == U'-' && k + 2 < s.size() && isDigit(s[k + 2])))) {
        num.push_back(s[++k]);
      }
      break;
    }
    if (c == U'산' && k + 1 < s.size() && isDigit(s[k + 1])) {
      num.push_back(c);
      num.push_back(s[++k]);
      while (k + 1 < s.size() && (isDigit(s[k + 1]) || (s[k + 1] == U'-' && k + 2 < s.size() && isDigit(s[k + 2]))))
        num.push_back(s[++k]);
      break;
    }
    if (isHangul(c) && c != U'번' && c != U'지' && c != U'외' && c != U'필' && c != U'일' && c != U'원' && c != U'대')
      break;
  }
  return num.empty() ? std::string() : u32ToUtf8(tok + num);
}
}  // namespace

std::string normalize(std::string_view text_utf8) {
  U32 s = lowerAscii(utf8ToU32(text_utf8));
  for (const char32_t* p : {U"(재)", U"（재）", U"[재]"})
    eraseAll(s, p);
  for (const char32_t* p : {U"재단법인", U"사단법인", U"학교법인", U"주식회사", U"유한회사"})
    eraseAll(s, p);
  stripLeadingVolume(s);
  U32 out;
  for (char32_t c : s)
    if (!dropCodepoint(c)) out.push_back(c);
  stripLeadingVolume(out);
  return u32ToUtf8(out);
}

std::string titleKey(std::string_view text_utf8) {
  U32 s = utf8ToU32(normalize(text_utf8));
  static const char32_t* const kBoiler[] = {U"문화유적발굴조사보고서",
                                            U"매장문화재발굴조사보고서",
                                            U"문화재발굴조사보고서",
                                            U"매장유산발굴조사보고서",
                                            U"발굴조사보고서",
                                            U"시굴조사보고서",
                                            U"표본조사보고서",
                                            U"약식보고서",
                                            U"약보고서",
                                            U"결과보고서",
                                            U"조사보고서",
                                            U"보고서",
                                            U"발굴조사",
                                            U"시굴조사",
                                            U"발시굴조사",
                                            U"표본조사",
                                            U"문화재조사",
                                            U"문화유산조사",
                                            U"매장문화재",
                                            U"매장유산",
                                            U"문화재",
                                            U"웹용",
                                            U"최적화",
                                            U"부지내",
                                            U"일원",
                                            U"일대"};
  for (int round = 0; round < 4; ++round) {
    const U32 before = s;
    for (const char32_t* b : kBoiler)
      eraseAll(s, b);
    if (s == before) break;
  }
  return u32ToUtf8(s);
}

std::string addressKey(std::string_view text_utf8) {
  const U32 s = utf8ToU32(text_utf8);
  for (std::size_t i = 0; i < s.size(); ++i)
    if (isEnding(s[i])) {
      const std::string key = tokenWithNumber(s, i);
      if (!key.empty()) return key;
    }
  return {};
}

std::string locality(std::string_view text_utf8) {
  const U32 s = utf8ToU32(text_utf8);
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (!isEnding(s[i])) continue;
    std::size_t start = i;
    while (start > 0 && isHangul(s[start - 1]) && i - start < 5)
      --start;
    U32 tok = s.substr(start, i - start + 1);
    if (tok.size() > 3) tok = tok.substr(tok.size() - 3);
    return u32ToUtf8(tok);
  }
  return {};
}

bool hasAddressNumber(std::string_view text_utf8) {
  return !addressKey(text_utf8).empty();
}

int parseYear(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    if (!(text[i] >= '0' && text[i] <= '9')) {
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
      ++i;
    if (i - start != 4) continue;
    const int y = std::atoi(std::string(text.substr(start, 4)).c_str());
    if ((y >= 1900 && y <= 2099) && (start == 0 || !(text[start - 1] >= '0' && text[start - 1] <= '9')) &&
        (i >= text.size() || !(text[i] >= '0' && text[i] <= '9')))
      return y;
  }
  return 0;
}

int phase(std::string_view text_utf8) {
  const U32 s = utf8ToU32(text_utf8);
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (isDigit(s[i]) && i + 1 < s.size() && s[i + 1] == U'차') {
      const int n = static_cast<int>(s[i] - U'0');
      if (n >= 1 && n <= 9) return n;
    }
    if (s[i] >= 0x2160 && s[i] <= 0x2162) {
      if (i + 1 >= s.size() || s[i + 1] != U'권') return static_cast<int>(s[i] - 0x2160 + 1);
    }
  }
  return 0;
}

double dice(std::string_view a_utf8, std::string_view b_utf8) {
  const U32 a = utf8ToU32(a_utf8), b = utf8ToU32(b_utf8);
  if (a.size() < 2 || b.size() < 2) return a == b ? 1.0 : 0.0;
  std::unordered_set<std::uint64_t> ga, gb;
  for (std::size_t i = 0; i + 1 < a.size(); ++i)
    ga.insert((static_cast<std::uint64_t>(a[i]) << 32) | a[i + 1]);
  for (std::size_t i = 0; i + 1 < b.size(); ++i)
    gb.insert((static_cast<std::uint64_t>(b[i]) << 32) | b[i + 1]);
  std::size_t inter = 0;
  for (std::uint64_t k : ga)
    if (gb.count(k)) ++inter;
  return 2.0 * static_cast<double>(inter) / static_cast<double>(ga.size() + gb.size());
}

bool contains(std::string_view haystack_utf8, std::string_view needle_utf8) {
  return !needle_utf8.empty() && haystack_utf8.find(needle_utf8) != std::string_view::npos;
}

std::vector<std::string> tokens(std::string_view text_utf8, std::size_t min_chars) {
  const U32 s = utf8ToU32(text_utf8);
  std::vector<std::string> out;
  U32 cur;
  auto flush = [&] {
    if (cur.size() >= min_chars) out.push_back(u32ToUtf8(cur));
    cur.clear();
  };
  for (char32_t c : s) {
    if (isHangul(c) || isDigit(c) || isAsciiAlpha(c))
      cur.push_back(c);
    else
      flush();
  }
  flush();
  return out;
}

}  // namespace strata::core::LocalReportText
