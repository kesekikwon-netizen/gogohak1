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

# gaps 수렴 뒤에 소진할 정적 풀(가치 순). done 집합으로 건너뛰므로 중복 질문 없음.
POOLS = [
    "nearby_questions.jsonl",
    "deep_questions.jsonl",
    "aspect_questions.jsonl",
    "cited_questions.jsonl",
    "questions.jsonl",
    "gap_questions_all.jsonl",
    "gap_questions.jsonl",
]


def log(msg: str) -> None:
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def snap(conn) -> dict:
    s = store.stats(conn)
    return {"papers": s["papers"], "chunks": s["chunks"], "ai_turns": s["ai_turns"],
            "with_summary": conn.execute(
                "SELECT COUNT(*) FROM papers WHERE ai_summary<>'' AND ai_summary IS NOT NULL"
            ).fetchone()[0]}


def run_asker(qlist: Path) -> int | None:
    """ask_ai.js 실행. 반환: 0 성공, 2 로그인 만료, 1 기타 실패, None 실행 오류."""
    env = dict(os.environ, GYEONGJU_ATTACH="http://127.0.0.1:9222",
               NODE_PATH="/tmp/opencode/dbpia/node_modules")
    try:
        r = subprocess.run(["node", str(ASK), "--questions", str(qlist)],
                           env=env, timeout=12 * 3600)
        return r.returncode
    except Exception as e:
        log(f"asker 오류: {e}")
        return None


def login_ok() -> bool:
    """수집 전 로그인 확인. 만료면 라운드를 돌리지 않고 바로 알린다."""
    env = dict(os.environ, GYEONGJU_ATTACH="http://127.0.0.1:9222",
               NODE_PATH="/tmp/opencode/dbpia/node_modules")
    try:
        r = subprocess.run(["node", str(ASK), "--check"], env=env,
                           timeout=120, capture_output=True, text=True)
        last = (r.stdout or "").strip().splitlines()
        log("로그인 확인: " + (last[-1] if last else f"exit={r.returncode}"))
        return r.returncode == 0
    except Exception as e:
        log(f"로그인 확인 실패: {e}")
        return False


def drain_pools(db: str, per_round: int) -> int:
    """정적 풀을 순서대로 소진한다. 배치마다 DB 반영 후 증가 없으면 중단."""
    total = 0
    conn = store.connect(db)
    prev = snap(conn)
    for name in POOLS:
        p = OUT_DIR / name
        if not p.exists():
            continue
        done = {r[0] for r in conn.execute("SELECT question FROM ai_turns")}
        qs = [l.strip() for l in p.read_text(encoding="utf-8").splitlines()
              if l.strip() and l.strip() not in done]
        log(f"[pool:{name}] 남은 {len(qs)}건")
        for i in range(0, len(qs), per_round):
            batch = qs[i:i + per_round]
            qf = OUT_DIR / "pool_batch.jsonl"
            qf.write_text("\n".join(batch) + "\n", encoding="utf-8")
            log(f"[pool:{name}] {i + len(batch)}/{len(qs)} 질문")
            code = run_asker(qf)
            if code == 2:
                log("DBpia 로그인 만료 — RESUME.md 재로그인 절차로 로그인 후 --pools-only로 재개")
                return total
            if code != 0:
                log("asker 실패 — 풀 소진 중단 (브라우저를 살린 뒤 --pools-only로 재개)")
                return total
            import_ai.import_file(OUT_DIR / "ai_answers.jsonl", Path(db))
            conn = store.connect(db)
            cur = snap(conn)
            growth = {k: cur[k] - prev[k] for k in prev}
            log(f"[pool:{name}] 증가: {growth}")
            prev = cur
            total += len(batch)
            if all(v <= 0 for v in growth.values()):
                log("증가 없음 — 풀 소진 중단")
                return total
    return total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--per-round", type=int, default=60)
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    ap.add_argument("--pools-only", action="store_true",
                    help="gaps 루프를 건너뛰고 정적 풀만 소진한다")
    args = ap.parse_args()

    if args.pools_only:
        if not login_ok():
            print(json.dumps({"error": "login_required",
                              "hint": "RESUME.md 재로그인 절차로 로그인 후 재개"},
                             ensure_ascii=False))
            return 2
        n = drain_pools(args.db, args.per_round)
        print(json.dumps({"pooled": n, **snap(store.connect(args.db))},
                         ensure_ascii=False, indent=2))
        return 0

    if not login_ok():
        print(json.dumps({"error": "login_required",
                          "hint": "RESUME.md 재로그인 절차로 로그인 후 재개"},
                         ensure_ascii=False))
        return 2

    conn = store.connect(args.db)
    prev = snap(conn)
    still = 0
    fail_streak = 0
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
        ok = run_asker(qf)
        if ok == 2:
            log(f"[R{rnd}] DBpia 로그인 만료 — RESUME.md 재로그인 절차로 로그인 후 재실행")
            break
        if ok != 0:
            fail_streak += 1
            log(f"[R{rnd}] asker 실패 {fail_streak}회 연속 — 브라우저(:9222) 확인 필요")
            if fail_streak >= 3:
                log("asker 3회 연속 실패 — 수집 중단 (브라우저를 살린 뒤 재실행)")
                break
            continue
        fail_streak = 0
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
    pooled = drain_pools(args.db, args.per_round)
    print(json.dumps({"pooled": pooled, **snap(store.connect(args.db))},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
