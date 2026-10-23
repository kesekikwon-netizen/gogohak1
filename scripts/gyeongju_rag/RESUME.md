# 재부팅 후 재개 (gyeongju_rag 수집)

## 상태 (2026-10-23 16:06 KST 저장)
- 논문 10,380편 · 전문 100,673청크 · AI 답변 183건(DB) / 144건(jsonl)
- DB WAL 병합 완료, 단일 파일 567MB
- 로그인 쿠키: `chrome-dbpia/`에 복사됨 (DBpia PREMIUM, ~11/1 만료)
- 남은 작업: 시굴지 34문항 중 진행 중 → 이후 site_loop 완성 회전

## 재개 순서 (2026-10-23 17:30 KST 검증됨)

1. 수집 크롬 시작·확인 (스크립트 사용):
   ```
   bash /home/kwon/hgis/scripts/gyeongju_rag/chrome_headed.sh start
   bash /home/kwon/hgis/scripts/gyeongju_rag/chrome_headed.sh status
   ```
   주의:
   - 실제 화면(:0)의 headed 크롬은 GPU 문제로 수십 초 만에 죽는다. 반드시 Xvfb(:99) 위에서 띄운다.
   - `--headless=new`는 살지만 DBpia가 세션을 로그인으로 인정하지 않는다(UA·힌트 위장해도 안 됨). headless로 수집하지 않는다.
   - pid 파일로만 멈춘다. `pkill -f user-data-dir` 같은 패턴은 자기 셸을 죽인다(실사고 있음).
2. 수집 재개 (단일 창):
   ```
   cd /tmp/opencode/dbpia 2>/dev/null || mkdir -p /tmp/opencode/dbpia && cd /tmp/opencode/dbpia
   [ -d node_modules ] || npm i puppeteer-core@24
   export NODE_PATH=$PWD/node_modules GYEONGJU_ATTACH=http://127.0.0.1:9222
   node /home/kwon/hgis/scripts/gyeongju_rag/ask_ai.js --questions /mnt/data-drawing/gyeongju_rag/site_questions.jsonl
   ```
   (이미 답한 질문은 자동 건너뜀. 끝나면 site_loop가 이어받는다.)
3. 완성 루프:
   ```
   python3 /home/kwon/hgis/scripts/gyeongju_rag/site_loop.py --rounds 1000000 --per-round 60
   ```
4. DB 반영(5분마다 또는 수동):
   ```
   python3 /home/kwon/hgis/scripts/gyeongju_rag/import_ai.py
   python3 /home/kwon/hgis/scripts/gyeongju_rag/harvest.py --stats
   ```

## 원칙
- 수집은 DBpia AI 통해서만. 공개 검색 API(harvest.py)는 돌리지 않는다.
- 결제 행위 금지. 유료 원문 PDF는 내려받지 않는다.

## 로그인 만료 대비

- 감지: `site_loop.py` 시작 시 `--check`로 로그인부터 확인하고, 만료면 라운드를 돌리지 않고 즉시 멈춘다.
  수집 중 만료면 ask_ai가 `LOGIN_LOST`로 중단(exit 2)하고, site_loop도 "재로그인 후 재실행" 메시지와 함께 멈춘다.
  (월 한도 QUOTA와 구분된다. 창-가득 WINDOW_FULL은 자동 교체라 멈추지 않는다.)
- 재로그인 (네가 직접 1회):
  ```
  bash /home/kwon/hgis/scripts/gyeongju_rag/chrome_headed.sh login
  ```
  실제 화면(:0)에 DBpia 로그인 창이 뜬다. 로그인하고 창을 닫으면 된다.
  쿠키는 같은 `chrome-dbpia` 프로필에 저장된다. 비밀번호를 파일·스크립트에 남기지 않는다.
- 재개:
  ```
  bash /home/kwon/hgis/scripts/gyeongju_rag/chrome_headed.sh start
  python3 /home/kwon/hgis/scripts/gyeongju_rag/site_loop.py --rounds 1000000 --per-round 60
  ```
  이미 답한 질문은 자동 건너뛰므로 처음부터 다시 돌려도 된다.
