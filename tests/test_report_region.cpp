// 보고서 지역 분류(LocalReportRag::classifyMacroRegion). 사용자의 실제 보고서 DB는 건드리지 않는다.
#include "core/LocalReportRag.h"

#include <iostream>
#include <string>
#include <utility>

int main() {
  // 지역 분류: 경로의 도 폴더가 우선, 같은 이름의 다른 지역(고성·광주·영동)과 모르는 이름은 「미분류」
  {
    using strata::core::LocalReportRag;
    const std::pair<const char*, const char*> cases[] = {
        {"강원도/고성군/보고서.txt", "강원도"},
        {"경상도/고성군/보고서.txt", "경상도(경남·부울)"},
        {"경기도/광주시/보고서.txt", "경기도(수도권)"},
        {"전라도/광주광역시/보고서.txt", "전라도(전남·광주)"},
        {"충청도/영동군/보고서.txt", "충청도(충북)"},
        {"강원도/영동/보고서.txt", "강원도"},
        {"경주시", "경상도(경북·대구)"},
        {"수원시", "경기도(수도권)"},
        {"고성", "미분류"},
        {"광주", "미분류"},
        {"영동", "미분류"},
        {"알수없는곳", "미분류"},
    };
    for (const auto& [input, want] : cases) {
      const std::string got = LocalReportRag::classifyMacroRegion(input);
      if (got != want) {
        std::cerr << "[FAIL] classifyMacroRegion(" << input << ") = " << got << ", want " << want << "\n";
        return 1;
      }
    }
  }

  std::cout << "[PASS] classifyMacroRegion\n";
  return 0;
}
