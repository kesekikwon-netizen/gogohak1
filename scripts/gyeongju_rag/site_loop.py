"""시굴지 RAG 완성 루프: 빈 곳이 없을 때까지 수집한다.

1회전 = 결측 진단(gaps) -> 질문 생성 -> DBpia AI(단일 창) -> RAG 반영 -> 재진단.
2회전 연속 증가가 없으면 멈춘다. 최대 6회전.

사용: python3 scripts/gyeongju_rag/site_loop.py            # 백그라운드 권장
      python3 scripts/gyeongju_rag/site_loop.py --rounds 3 --per-round 40
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gaps  # noqa: E402
import import_ai  # noqa: E402
import store  # noqa: E402

OUT_DIR = Path("/mnt/data-drawing/gyeongju_rag")
ASK = Path(__file__).resolve().parent / "ask_ai.js"


def log(msg: str) -> None:
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def snap(conn) -> dict:
    s = store.stats(conn)
    return {"papers": s["papers"], "chunks": s["chunks"], "ai_turns": s["ai_turns"],
            "with_summary": conn.execute(
                "SELECT COUNT(*) FROM papers WHERE ai_summary<>'' AND ai_summary IS NOT NULL"
            ).fetchone()[0]}


def run_asker(qlist: Path) -> bool:
    env = dict(os.environ, GYEONGJU_ATTACH="http://127.0.0.1:9222",
               NODE_PATH="/tmp/opencode/dbpia/node_modules")
    try:
        r = subprocess.run(["node", str(ASK), "--questions", str(qlist)],
                           env=env, timeout=12 * 3600)
        return r.returncode == 0
    except Exception as e:
        log(f"asker 오류: {e}")
        return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--per-round", type=int, default=60)
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    args = ap.parse_args()

    conn = store.connect(args.db)
    prev = snap(conn)
    still = 0
    for rnd in range(1, args.rounds + 1):
        rep = gaps.analyze(Path(args.db))
        qs, mapping = gaps.gap_questions(rep, args.per_round)
        # 이미 답한 질문 제외
        done = {r[0] for r in conn.execute("SELECT question FROM ai_turns")}
        qs = [q for q in qs if q not in done]
        log(f"[R{rnd}] 결측: 초록없음 {rep['papers_without_abstract']} · "
            f"0건질의 {len(rep['zero_hit_queries'])} · 유적공백 {len(rep['site_gaps'])} · "
            f"새질문 {len(qs)}")
        if not qs:
            log("물을 질문이 없다 — 수집 완료")
            break
        qf = OUT_DIR / f"loop_r{rnd}.jsonl"
        qf.write_text("\n".join(qs) + "\n", encoding="utf-8")
        (OUT_DIR / "gap_question_map.json").write_text(
            json.dumps(mapping, ensure_ascii=False, indent=1), encoding="utf-8")
        run_asker(qf)
        import_ai.import_file(OUT_DIR / "ai_answers.jsonl", Path(args.db))
        conn = store.connect(args.db)
        cur = snap(conn)
        growth = {k: cur[k] - prev[k] for k in prev}
        log(f"[R{rnd}] 증가: {growth}")
        store.set_meta(conn, "site_loop_round", str(rnd))
        conn.commit()
        if all(v <= 0 for v in growth.values()):
            still += 1
            if still >= 2:
                log("2회전 연속 증가 없음 — 수집 종료")
                break
        else:
            still = 0
        prev = cur
    print(json.dumps(snap(store.connect(args.db)), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
