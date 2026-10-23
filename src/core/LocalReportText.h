#pragma once
// 로컬 발굴조사보고서 서지 정규화 (UTF-8, 순수 표준 C++20)
// 보고서명·기관·소재지의 표기 차이(띄어쓰기·괄호·권차·오탈자)를 흡수해
// GIS 서지와 _download_log.csv / _rag_index.db 를 같은 키로 비교한다.
#include <string>
#include <string_view>
#include <vector>

namespace strata::core::LocalReportText {

std::string normalize(std::string_view text_utf8);
std::string titleKey(std::string_view text_utf8);
std::string addressKey(std::string_view text_utf8);
std::string locality(std::string_view text_utf8);
bool hasAddressNumber(std::string_view text_utf8);
int parseYear(std::string_view text_utf8);
int phase(std::string_view text_utf8);
double dice(std::string_view a_utf8, std::string_view b_utf8);
bool contains(std::string_view haystack_utf8, std::string_view needle_utf8);
std::vector<std::string> tokens(std::string_view text_utf8, std::size_t min_chars = 2);

}  // namespace strata::core::LocalReportText
