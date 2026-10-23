---
name: strata-ui-wording
description: Rules for any text a Strata (ka-hgis) user can see on screen or in an exported document. Use when adding or changing a label, message, tooltip, table header, report sentence or placeholder, or when ui_terms fails. Has the glossary (internal term to plain Korean) and the forbidden-word list.
---

# 화면과 문서에 보이는 글자 규칙

사용자는 고고학 현장 전문가다. 개발 용어는 화면에도 제출 문서에도 나오지 않는다.

## 쓰기 전에
1. 사용자가 보는 글자(화면, 메시지, 툴팁, 표 머리, 내보내는 문서)에만 이 규칙이 적용된다. 객체 이름, 설정 키, 필드 식별자(`SURVEY_NAME` 등), 로그는 개발용이라 영어여도 된다.
2. 아래 표에 있는 내부 용어는 오른쪽 말로 쓴다. 표에 없는 새 내부 용어가 필요하면 표에 먼저 한 줄 더하고 쓴다.
3. 문서에 **자리표시자를 넣지 않는다.** 근거가 없으면 「자료 없음」과 이유를 쓴다. 일을 기다리는 중이라는 문구도 문서에 넣지 않는다.
4. ✓ △ ◌ ○ 같은 기호는 **화면에서만** 쓰고 범례를 같이 둔다. 내보내는 문서에는 말로 쓴다. ┏ ┓ ※ 같은 장식 문자는 쓰지 않는다.
5. 「조판」은 쓰지 않는다 → 「도면」(보고서 화면은 「작성」「미리보기」). 이미 `ui_terms` 시험이 지킨다.

## 글자 사전
| 내부 용어 | 사용자에게 보이는 말 |
|---|---|
| RAG, 로컬 RAG | 보고서 근거 |
| 로컬 RAG 미수록 | 보고서 미수록 |
| GIS 서지 | 공식 서지 |
| 피처ID | 도형 번호 |
| 누름틀, 필드 | 양식 칸 |
| 슬롯, `*_SLOT` | (문서에 넣지 않는다) |
| OpenHwpSDK 초기화 실패 | 한글 문서 엔진을 시작하지 못했습니다 |
| Antigravity SDK / DBpia 응답 없음 | 수집된 논문이 없습니다 |
| 하네스, SDK, MCP | (쓰지 않는다) |

써도 되는 이름: HWPX, HWP(파일 형식), DBpia, VWorld, GIS, GPKG, SHP, PDF.

## 걸러내는 곳
- `tests/test_ui_terms.cpp`: 소스의 문자열에 금지어가 들어오면 실패한다. 금지어를 넓힐 때는 이 시험에 함수를 더한다(원래 검사문은 건드리지 않는다).
- 원래 있던 시험이 옛 글자를 기대해서 바꿔야 하면 커밋 메시지에 `시험 변경: <이유>`를 쓴다.
- 설계 정본: `docs/superpowers/specs/2026-10-23-시굴전-종합평가-설계.md` 10절.
