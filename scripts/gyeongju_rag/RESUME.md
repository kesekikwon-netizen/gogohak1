# 재부팅 후 재개 (gyeongju_rag 수집)

## 상태 (2026-10-23 16:06 KST 저장)
- 논문 10,380편 · 전문 100,673청크 · AI 답변 183건(DB) / 144건(jsonl)
- DB WAL 병합 완료, 단일 파일 567MB
- 로그인 쿠키: `chrome-dbpia/`에 복사됨 (DBpia PREMIUM, ~11/1 만료)
- 남은 작업: 시굴지 34문항 중 진행 중 → 이후 site_loop 완성 회전

## 재개 순서 (2026-10-23 16:55 KST 검증됨)

1. Xvfb 가상 화면 + headed Chrome (로그인 프로필 사용):
   ```
   Xvfb :99 -screen 0 1600x1000x24 &
   DISPLAY=:99 google-chrome --remote-debugging-port=9222 \
     --user-data-dir=/mnt/data-drawing/gyeongju_rag/chrome-dbpia \
     --no-first-run --no-default-browser-check \
     --disable-dev-shm-usage --disable-gpu --window-size=1600,1000 \
     https://ai.dbpia.co.kr/ko/search
   ```
   주의:
   - 실제 화면(:0)의 headed 크롬은 GPU 문제로 수십 초 만에 죽는다. 반드시 Xvfb(:99) 위에서 띄운다.
   - `--headless=new`는 살지만 DBpia가 세션을 로그인으로 인정하지 않는다(UA·힌트 위장해도 안 됨). headless로 수집하지 않는다.
   - `ask_ai.js --check/--questions/--login`은 붙은 브라우저를 닫지 않고 끊기만 한다(release). 구버전은 close()로 창을 꺼버려 다음 실행이 9222 연결 거부로 죽었다.
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
