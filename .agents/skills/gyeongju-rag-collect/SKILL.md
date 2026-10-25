---
name: gyeongju-rag-collect
description: Collect Gyeongju (경주) archaeology RAG answers from DBpia AI through the logged-in built-in browser, three questions at a time, then merge and import them into the gyeongju_rag SQLite DB. Use for 경주 RAG 보완 수집, DBpia AI 질문 수집, 논문 수집 진행률, 답변 파일 반입, or resuming an interrupted DBpia collection run.
---

# 경주 RAG 보완 수집 (DBpia AI, 브라우저 방식)

이 스킬은 DBpia AI 답변을 **내장 브라우저 탭 안에서** 3개씩 병렬로 받아, 서버의 경주 RAG DB에 넣는 절차다.
어떤 AI도 이 절차를 그대로 따를 수 있도록 단계와 함정을 적었다.

## 0. 원칙 (어기면 안 됨)

- 수집은 **DBpia AI를 통해서만**. 공개 검색 API(`harvest.py`)는 돌리지 않는다.
- 결제·구독 행위 금지. 유료 원문 PDF를 내려받지 않는다.
- **계정 정보(아이디·비밀번호)를 저장소나 로그에 남기지 않는다.** 로그인은 사용자가 내장 브라우저에서 직접 한다.
- 동시 질문은 **3개까지**. 답변이 끝나야 다음 3개를 보낸다.
- 무관 논문(스포츠·공학·문학 등)은 수집하지 않는다. 관련성 기준은 `scripts/gyeongju_rag/relevance.py`.
- 사용자 실제 조사 파일(`*.gpkg`, `*.qgz`)은 건드리지 않는다. 이 스킬은 RAG DB와 답변 파일만 다룬다.

## 1. 경로

| 용도 | 위치 |
|---|---|
| RAG DB | `/mnt/data-drawing/gyeongju_rag/gyeongju_rag.db` |
| 답변 누적 파일 | `/mnt/data-drawing/gyeongju_rag/ai_answers.jsonl` |
| 답변 조각 보관 | `/mnt/data-drawing/gyeongju_rag/parts_run1/` |
| 무관 논문 제목 목록 | `/mnt/data-drawing/gyeongju_rag/irrelevant_titles.json` |
| 상태 파일 | `/mnt/data-drawing/gyeongju_rag/STATUS.md` (매번 갱신) |
| 대기 질문 목록 | `/tmp/opencode/dbpia/queue.json` (JSON 배열, 질문 문자열) |
| 병합 스크립트 | `scripts/gyeongju_rag/merge_parts.py` |
| DB 반영 스크립트 | `scripts/gyeongju_rag/import_ai.py` |
| 브라우저 작업 스크립트 | `.agents/skills/gyeongju-rag-collect/reference/browser_batch.js` |

## 2. 사전 확인

1. 내장 브라우저에서 DBpia AI 탭을 연다 (`tools.browser.tabs.open`, 주소 `https://ai.dbpia.co.kr/ko/search`).
2. 로그인 상태 확인: 페이지에서 `https://www.dbpia.co.kr/member/me`를 `credentials: 'include'`로 fetch 한다.
   - 응답의 `b2cId`가 있으면 로그인됨. `b2cName`, `b2cAiTier`(예: PREMIUM)를 상태에 적는다.
   - 로그인 안 됨이면 **사용자에게 로그인을 요청**한다. 자동 로그인 파일은 없다.
3. 대기 질문 목록을 준비한다. 범위 밖 논문 질문은 제외되어 있어야 한다(`irrelevant_titles.json` 기준).

## 3. 질문 목록을 페이지에 올리기

긴 질문 목록을 채팅에 붙여 넣지 않는다. 파일로 올린다.

1. 페이지에 파일 입력칸을 만든다:
   ```js
   const i = document.createElement('input'); i.type = 'file'; i.id = 'dbq-input';
   i.setAttribute('aria-label', 'dbq-queue-upload');
   i.style.cssText = 'position:fixed;top:0;left:0;width:20px;height:20px;opacity:1;z-index:99999';
   document.body.appendChild(i);
   ```
2. `tools.browser.find({ text: "dbq-queue-upload" })`로 참조 번호(예: `@e9`)를 찾는다.
3. `tools.browser.files.upload({ ref, paths: ["/tmp/opencode/dbpia/queue.json"] })`로 올린다.
4. 페이지에서 `window.__Q = JSON.parse(await i.files[0].text())`로 읽는다.

## 4. 작업 실행 (백그라운드, 3개 병렬)

`reference/browser_batch.js`를 `tools.browser.evaluate`의 `script`로 넣는다.

- **중요:** `evaluate`는 60초 안에 끝나지 않으면 `browser.timeout`이 난다. 그래도 페이지 안의 작업은 계속 돈다.
  그래서 스크립트는 **시작만 하고 바로 반환**하고, 진행은 `window.__job2`를 읽어서 확인한다.
- 작업 흐름: 대화 생성 → 질문 전송 → 답변 완료까지 폴링 → 20건마다 `ai_r2_NNN.jsonl`로 내려받기.
- 시작 번호(`START`)는 직전 실행의 `next` 값으로 정한다. 같은 질문을 두 번 보내지 않는다.

진행 확인:
```js
(() => { const j = window.__job2; return {state:j.state, next:j.next, sent:j.sent, done:j.done, failed:j.failed, part:j.part, halted:j.halted, errors:j.errors.length}; })()
```

## 5. 답변 파일 가져오기와 DB 반영

1. `tools.browser.files.list({ tabID })`에서 `ai_r2_*.jsonl` 항목을 찾는다.
2. 각 항목을 `tools.browser.files.get({ tabID, fileID })`로 받으면 서버 경로(`path`)가 나온다.
3. 그 파일을 `parts_run1/`로 복사하고:
   ```
   python3 scripts/gyeongju_rag/merge_parts.py parts_run1/ai_r2_*.jsonl
   python3 scripts/gyeongju_rag/import_ai.py
   ```
4. `merge_parts.py`는 같은 질문을 중복으로 넣지 않고, 무관 논문 질문을 버린다.
5. `STATUS.md`의 전송·완료·진행 중·실패·다음 질문 번호를 갱신한다.

## 6. 상태 보고 형식

사용자가 "진행률"이라고 물으면 아래 항목을 표로 답한다.
- 대상(예: 548건 중 137번부터 411건), 전송, 완료(%), 진행 중, 실패·에러, 다음 질문 번호, DB 누적 답변 수.
- 완료 수만 보고 끝내지 말고, 새로 생긴 답변 파일은 가져와서 DB에 넣은 뒤 숫자를 말한다.

## 7. 함정 (실제로 겪은 것)

- **마지막 탭을 닫으면 크롬이 꺼진다.** 새 탭을 먼저 열고 이전 탭을 닫는다.
- **로그인 쿠키 종류:** DBpia `SESSION` 쿠키는 세션 쿠키라 브라우저가 꺼지면 사라진다. 「자동 로그인」을 체크한 로그인만 유지된다. 로그아웃이 보이면 사용자에게 다시 로그인을 요청하고, 그 전에 브라우저를 껐다 켜지 않는다.
- **병렬 과다 전송:** 전송 루프에서 답변 완료를 기다리지 않으면 수십 건이 한꺼번에 생성된다. `MAX_IN_FLIGHT = 3`을 지킨다.
- **입력창:** 타이핑으로 입력하면 글자가 빠진다. 네이티브 `value` setter + `input` 이벤트로 넣고, Enter는 `keydown` 이벤트로 보낸다.
- **질문 텍스트가 곧 중복 키다.** `ai_turns.question`이 유일 키라 질문 문자열을 바꾸면 중복 답변이 생긴다.
- **서버 쪽 옛 수집기(`site_loop.py`, 9222 포트 크롬)는 쓰지 않는다.** 로그인 쿠키 문제로 멈춰 있다.
- **REA MCP는 이 수집에 필요 없다.** 엔드포인트는 이미 확인됐다(아래 8절).

## 8. DBpia AI API (확인됨)

모든 호출은 `credentials: 'include'`.

| 단계 | 메서드·경로 | 본문 | 결과 |
|---|---|---|---|
| 대화 생성 | `POST https://agent.dbpia.co.kr/vetter_conversations/` | `{"title": 질문}` | `{id, ...}` |
| 질문 전송 | `POST .../vetter_conversations/chat_message` | `{message, plct_types:["025001","025002","025015","025003","025005","025004","025000"], is_criticized:false, client_type:"web", conversation_id:id, referer:""}` | SSE. 완료 후 결과 조회 |
| 결과 조회 | `GET .../vetter_conversations/{id}` | - | `messages[]`에서 `role=assistant`의 마지막 `content` |
| 최근 대화 | `GET .../vetter_conversations/recent-conversations` | - | `data.chatHistoryList[].historyList[]` (`groupId`=대화 ID, `title`=질문) |

- 답변이 완성됐다고 보는 기준: 본문 길이 200자 초과이고, 15초 간격으로 두 번 연속 같은 내용.
- 인용 표식 `<CIT NODEID_0>`에서 `NODEID`를 뽑아 `https://www.dbpia.co.kr/journal/articleDetail?nodeId=NODEID` 링크로 만든다.
- 응답 본문이 `text/event-stream`이어도 `chat_message`의 `response.text()`가 끝나는 것을 기다리면 된다. 답변 내용은 결과 조회로 읽는다.

## 9. 중단 후 재개

1. `STATUS.md`와 `window.__job2` 상태를 읽는다. `state`가 `running`이면 그대로 두고, `halted`면 원인을 확인한다.
2. 이미 보낸 질문은 `recent-conversations`의 `title`로 확인할 수 있다. 여기 있는 질문은 다시 보내지 않는다.
3. 답변이 비어 있는 대화(`messages`에 `user`만 있음)는 다시 전송할지 사용자에게 묻는다.
