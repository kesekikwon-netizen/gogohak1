"""DBpia AI 대화 기록(ai_history/*.json)을 경주 RAG로 가져온다.

- 질문/답변 쌍을 ai_turns에 넣고, 답변의 <CIT NODE..._n> 표시에서 인용 논문 ID를 뽑는다.
- 경주·신라 왕경과 무관한 대화(제주·강릉·영천 등)는 건너뛴다.
"""

from __future__ import annotations

import argparse
import glob
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import store  # noqa: E402

CIT = re.compile(r"<CIT\s+([A-Za-z0-9]+)_\d+>")
GY = ["경주", "왕경", "서라벌", "방리", "5소경", "신라", "황룡사", "월성", "동천동", "인왕동"]
OTHER = ["제주", "강릉", "영천", "김해", "부여", "공주"]


def is_gyeongju(q: str, a: str) -> bool:
    text = q + " " + a
    if any(k in q for k in GY):
        return True
    if any(k in q for k in OTHER) and not any(k in text for k in ["경주", "왕경", "서라벌"]):
        return False
    return any(k in text for k in ["경주", "왕경", "서라벌", "황룡사", "월성"])


def refs_from_answer(answer: str) -> list[dict]:
    out, seen = [], set()
    for m in CIT.finditer(answer):
        nid = m.group(1)
        if nid in seen:
            continue
        seen.add(nid)
        out.append({
            "node_id": nid,
            "url": f"https://www.dbpia.co.kr/journal/articleDetail?nodeId={nid}",
        })
    return out


def import_dir(folder: Path, db: Path) -> dict:
    conn = store.connect(db)
    turns = skipped = 0
    for f in sorted(glob.glob(str(folder / "*.json"))):
        try:
            d = json.loads(Path(f).read_text(encoding="utf-8"))
        except Exception:
            continue
        msgs = d.get("messages") or []
        for i in range(len(msgs) - 1):
            u, a = msgs[i], msgs[i + 1]
            if u.get("role") != "user" or a.get("role") != "assistant":
                continue
            q = str(u.get("content") or "").strip()
            ans = str(a.get("content") or "").strip()
            if not q or not ans:
                continue
            if not is_gyeongju(q, ans):
                skipped += 1
                continue
            refs = refs_from_answer(ans)
            tid = store.add_ai_turn(conn, q, ans, refs, model="dbpia-ai-history",
                                    conversation_id=Path(f).stem)
            if tid:
                conn.execute("DELETE FROM ai_fts WHERE rowid=?", (tid,))
                conn.execute("INSERT INTO ai_fts(rowid,question,answer) VALUES(?,?,?)", (tid, q, ans))
                turns += 1
            if turns % 10 == 0:
                conn.commit()
    conn.commit()
    print(f"가져온 대화 {turns}건, 건너뜀 {skipped}건")
    return store.stats(conn)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="/mnt/data-drawing/gyeongju_rag/ai_history")
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    args = ap.parse_args()
    print(json.dumps(import_dir(Path(args.dir), Path(args.db)), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
