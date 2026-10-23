"""ask_ai.js가 저장한 ai_answers.jsonl을 경주 RAG DB로 가져온다."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import store  # noqa: E402

DEFAULT_IN = Path("/mnt/data-drawing/gyeongju_rag/ai_answers.jsonl")


def refs_from_links(links: list[dict]) -> list[dict]:
    out = []
    for l in links or []:
        href = str(l.get("href") or "")
        m = re.search(r"nodeId=([A-Za-z0-9]+)", href)
        if not m:
            continue
        out.append({"node_id": m.group(1), "title": str(l.get("title") or ""), "url": href})
    return out


def import_file(path: Path, db: Path) -> dict:
    conn = store.connect(db)
    mapping = {}
    map_path = path.parent / "gap_question_map.json"
    if map_path.exists():
        try:
            mapping = json.loads(map_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            mapping = {}
    n = 0
    if not path.exists():
        print(f"파일 없음: {path}")
        return store.stats(conn)
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        q = str(rec.get("question") or "").strip()
        if not q:
            continue
        turn_id = store.add_ai_turn(conn, q, str(rec.get("answer") or ""),
                                    refs_from_links(rec.get("links") or []),
                                    model="dbpia-ai-deep" if q.startswith("[학술 심층]") else "dbpia-ai",
                                    conversation_id=str(rec.get("b2cId") or ""))
        if turn_id:
            conn.execute("DELETE FROM ai_fts WHERE rowid=?", (turn_id,))
            conn.execute("INSERT INTO ai_fts(rowid,question,answer) VALUES(?,?,?)",
                         (turn_id, q, str(rec.get("answer") or "")))
            node_id = mapping.get(q, "")
            if node_id:
                conn.execute("UPDATE papers SET ai_summary=? WHERE node_id=?",
                             (str(rec.get("answer") or ""), node_id))
        n += 1
        if n % 20 == 0:
            conn.commit()
    conn.commit()
    print(f"{n}건 가져옴 -> {db}")
    return store.stats(conn)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", default=str(DEFAULT_IN))
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    args = ap.parse_args()
    print(json.dumps(import_file(Path(args.inp), Path(args.db)), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
