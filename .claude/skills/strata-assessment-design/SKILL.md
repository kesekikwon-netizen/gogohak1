---
name: strata-assessment-design
description: The agreed design for Strata's pre-survey assessment (시굴 전 종합 평가) — GIS facts, report/paper evidence and the Antigravity SDK. Use when touching report, RAG, DBpia, HWPX, Ledger or evidence code, planning the next milestone (M0-M7), or updating the design page. States what can be built now and what is blocked.
---

# 시굴 전 종합 평가 설계 (진행용)

정본: `docs/superpowers/specs/2026-10-23-시굴전-종합평가-설계.md`. 그림: `docs/design/2026-10-23-survey-assessment/index.html` (아티팩트 https://claude.ai/artifact/P5Am8Eo8dBq1HTqEHqwNXG). 요청 기록: `docs/intent/2026-10-23-survey-prior-assessment.md`. 먼저 정본을 읽는다. 여기에는 지켜야 할 것만 적는다.

## 불변 원칙
1. 유적과 보고서를 잇는 일(유산코드, 허가번호, 유적명, 소재지)은 **코드가 확정적으로** 한다. AI가 잇지 않는다.
2. 유적마다 근거 꾸러미 하나. AI는 꾸러미만 읽고 쓴다.
3. AI·인터넷·DBpia 로그인이 없어도 GIS 표와 보고서 근거표는 나온다.
4. 모든 사실 문장에 근거 번호(`E12`). 없으면 평가서에 못 들어간다. 문장 종류는 사실 / 추정 / 자료 없음.
5. `GisFactHarness::Ledger`의 `uncollectedDomains`(`soil`, `paleo_landform`, `dem`, `geology`)에 있는 도메인은 수치를 쓰지 않는다.
6. 위치·거리·축척이 있는 그림은 GIS가 그린다. 나노바나나 프로 그림은 설명용 모식도만, 「모식도(실측 아님)」 표시, 사람이 채택한 것만.
7. 앱 안 DBpia 직접 호출(`KaGeminiBridgeClient`)은 화면에서 숨기고 코드는 둔다. 수집은 DBpia AI를 통해서만, 현장에서는 저장소만 읽는다.

## 계약 세 가지 (JSON)
사실 묶음(앱 → SDK, Ledger), 근거 항목(저장소 → SDK: `id`, `siteRef`, `kind`, `source`, `page`, `text`, `status`), 평가 결과(SDK → 앱: `sections[].claims[]{text, evidenceIds, type}`, `figures[]`, `gaps[]`). 자세한 모양은 정본 6절. 문장이 인용하는 번호는 `E`(보고서·논문 근거), `G`(GIS 카드), `S`(GIS 유적 기록). 상태 네 가지: 동일 보고서 확인 / 참고 / 스캔본 / 미수록.

## 지금 할 수 있는 것, 막힌 것 (2026-10-23)
- **막힘**: Antigravity SDK 구현(토큰이 다 되어 며칠), RAG 수집(다른 터미널이 진행 중이라 건드리지 않음). 경주 독립 DB는 앱이 아직 읽지 않는다.
- **끝남**: M0 글자 정리(개발 용어 37곳, 자리표시자, 장식 문자)와 `ui_terms` 금지어 시험, M1 계약 코드 `src/core/AssessmentContract.{h,cpp}`(사실 묶음 `factsBundle`, 근거 항목 `validateEvidenceItem`, 평가 결과 `validateAssessment`, `gisIdsOf`) + 뼈대 `src/core/AssessmentSkeleton.{h,cpp}`(AI 없이 요약·조사구역 개요·주변유적·유적별 근거·부족한 자료·참고문헌을 쓴다, 시험 `assessment_skeleton`, 파일 내보내기 `writeBundle` + 보고서 화면 저장 메뉴 「평가 자료 저장…」) + 시험 `assessment_contract` + 샘플 `docs/design/2026-10-23-survey-assessment/samples/`. SDK 결과는 반드시 `validateAssessment`를 통과해야 앱에 쓴다.
- **할 수 있음 (SDK·수집 없이)**: M0 남은 것(기관 이름 설정화), M3의 평가서 화면 표시, 출고 검사 연결, 앱 안 DBpia 단추 숨김(M2 일부).
- 할 일을 고를 때는 정본 11절의 단계 순서(M0 → M1 → …)를 따르고, 막힌 단계는 건너뛴다.

## 진행하면서
- 단계 하나가 끝나면 정본과 HTML의 현황 표지(미착수 → 진행 → 완료)를 고치고 **같은 파일 경로**로 아티팩트를 다시 올린다(주소 유지).
- 새 사실을 확인하면 정본 2절 표에 근거와 함께 한 줄 더한다.
- 코드는 `strata-linux-commit` 스킬대로 검증하고 올리고, 보이는 글자는 `strata-ui-wording`을 따른다.
- 정본과 다르게 하고 싶으면 먼저 정본을 고치고 이유를 커밋 메시지에 쓴다.
