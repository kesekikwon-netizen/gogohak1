# 경주 독립 RAG (gyeongju_rag)

경주에 관한 **발굴조사 보고서 전문 + DBpia 논문·학술지 서지/초록 + DBpia AI 답변**을 한 SQLite 파일에 모은다.
Strata 앱과 분리된 독립 자료이며, `/mnt/data-drawing/gyeongju_rag/`에 저장한다.

## 현재 자료

| 구분 | 내용 |
|---|---|
| 로컬 보고서 전문 | 경주시 보고서 832종, **100,407청크(약 81MB)** |
| DBpia 서지·초록 | 공개 검색 API로 수집 중 (경주 검색 12,433건 중 10,000건 + 질의 확장) |
| DBpia AI 답변 | 로그인 후 `ask_ai.js`로 수집 (현재 미로그인) |

DB: `/mnt/data-drawing/gyeongju_rag/gyeongju_rag.db` (WAL)
로그: `harvest.log`, `build_local.log`

## 빠른 사용

```bash
# 진행 상황
python3 scripts/gyeongju_rag/harvest.py --stats

# 검색 (논문 + 보고서 전문 + AI 답변)
python3 scripts/gyeongju_rag/search.py "황룡사지 창건"
python3 scripts/gyeongju_rag/search.py "월성 해자" --json

# 수집 재개/계속 (중단해도 pending부터 이어짐)
python3 -u scripts/gyeongju_rag/harvest.py

# 로컬 보고서 전문 다시 가져오기
python3 scripts/gyeongju_rag/build_local.py --force
```

## 수집 원리(정책: DBpia AI 통해서만 수집)

- 정보 수집은 **DBpia AI 통해서만** 한다(무제한 요금제). 공개 검색 API 수집은 중단했다.
- `harvest.py`는 과거 수집용으로만 보관한다. 새로 돌리지 않는다.

1. 601개 씨앗 질의: 경주 전체, 4읍 8면 10행정동, 44법정동, 142법정리, 핵심 유적 180곳,
   주제 150개, 시대·기관·학술지.
2. 결과의 `searchKeywords`, 유적명, 저자(4편 이상)로 질의를 확장한다.
3. 한 질의가 3,000건을 넘으면 `DATE` 정렬로 한 번 더 훑어 10,000건 상한을 우회한다.
4. 새 질의와 새 논문이 모두 0이 되면 수렴 종료. 중단 후 재실행하면 이어진다.
5. `relevance` 열로 경주 관련 여부를 표시한다(스포츠·자동차의 '경주' 오검색 등은 0).

## DBpia AI

DBpia AI는 **로그인 없이는 질의할 수 없다**(게스트 5회도 소진됨). 무료 계정은 월 20회,
기관 인증은 무제한이다. 다음 순서로 쓴다.

```bash
# 1) 화면이 있는 브라우저에서 한 번만 로그인 (전용 프로필)
node scripts/gyeongju_rag/ask_ai.js --login

# 2) 로그인 확인
node scripts/gyeongju_rag/ask_ai.js --check

# 3) 질문 생성 후 루프 실행 (한도에 닿으면 스스로 멈춤)
python3 scripts/gyeongju_rag/gen_questions.py
node scripts/gyeongju_rag/ask_ai.js --questions /mnt/data-drawing/gyeongju_rag/questions.jsonl

# 4) 답변을 RAG DB로 가져오기
python3 scripts/gyeongju_rag/import_ai.py
```

필요 패키지: `npm i puppeteer-core` (Chrome은 `/usr/bin/google-chrome` 사용).

## 범위와 한계

- DBpia에서 가져오는 것은 **서지·초록·인용 링크**다. 유료 원문 PDF 전문은 내려받지 않는다.
- 보고서 전문은 사용자가 이미 보유한 로컬 `_rag_index.db`의 경주시 문서만 복사한다(읽기 전용).
- `○`/`relevance=0`은 "세상에 자료가 없다"가 아니라 "이 수집 경로에서 경주 관련으로 확인되지 않았다"는 뜻이다.

## 파일

| 파일 | 역할 |
|---|---|
| `seeds.py` | 경주 행정구역·유적·주제·기관 어휘 |
| `store.py` | SQLite 스키마(FTS5)·저장 함수 |
| `harvest.py` | DBpia 공개 검색 수집 루프 |
| `build_local.py` | 기존 보고서 RAG에서 경주 전문 복사 |
| `search.py` | 통합 검색 CLI |
| `gen_questions.py` | DBpia AI 질문 목록 생성 |
| `ask_ai.js` | 로그인·AI 질의 루프(브라우저) |
| `import_ai.py` | AI 답변을 DB로 가져오기 |
