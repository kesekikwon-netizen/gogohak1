#!/bin/bash
# 수집용 headed 크롬 (Xvfb :99) 시작·정지·상태·재로그인.
# pid 파일로만 멈춘다 (pkill 패턴 오폭 금지).
DIR=/mnt/data-drawing/gyeongju_rag
PIDF=$DIR/chrome-headed.pid
PROF=$DIR/chrome-dbpia

alive() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

case "$1" in
  start)
    if alive; then echo "이미 실행 중 (pid $(cat "$PIDF"))"; exit 0; fi
    if [ ! -e /tmp/.X11-unix/X99 ]; then Xvfb :99 -screen 0 1600x1000x24 & fi
    sleep 2
    DISPLAY=:99 google-chrome --remote-debugging-port=9222 \
      --user-data-dir="$PROF" --no-first-run --no-default-browser-check \
      --disable-dev-shm-usage --disable-gpu --window-size=1600,1000 \
      https://ai.dbpia.co.kr/ko/search >>"$DIR/chrome.log" 2>&1 &
    echo $! > "$PIDF"
    sleep 8
    if curl -s --max-time 5 http://127.0.0.1:9222/json/version >/dev/null; then
      echo "시작됨 (pid $(cat "$PIDF"), :9222)"
    else
      echo "시작 실패 — $DIR/chrome.log 확인"; exit 1
    fi
    ;;
  stop)
    if alive; then kill "$(cat "$PIDF")" && rm -f "$PIDF" && echo "정지됨"; else
      echo "실행 중 아님"; rm -f "$PIDF"
    fi
    ;;
  status)
    if alive && curl -s --max-time 5 http://127.0.0.1:9222/json/version >/dev/null; then
      echo "정상 (pid $(cat "$PIDF"), :9222 응답)"
    else
      echo "비정상"; exit 1
    fi
    ;;
  login)
    # 로그인 만료 시: 수집 크롬을 멈추고 실제 화면(:0)에 로그인 창을 띄운다.
    # 사용자가 직접 로그인하고 창을 닫으면 된다. 쿠키는 같은 프로필에 저장된다.
    "$0" stop
    DISPLAY=:0 google-chrome --user-data-dir="$PROF" \
      --no-first-run --no-default-browser-check \
      https://www.dbpia.co.kr/member/login
    echo "로그인 창을 닫았으면 '$0 start'로 수집 크롬을 다시 띄운다."
    ;;
  *)
    echo "사용: $0 {start|stop|status|login}"; exit 2
    ;;
esac
