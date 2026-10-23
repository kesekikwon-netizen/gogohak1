"""경주 RAG 검색 CLI.

DBpia 서지·초록(papers_fts)과 로컬 보고서 전문(chunks_fts)을 함께 검색한다.

예:
  python3 scripts/gyeongju_rag/search.py "황룡사지 창건"
  python3 scripts/gyeongju_rag/search.py "월성 해자" --papers 5 --chunks 5 --json
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import store  # noqa: E402

PARTICLES = ["으로부터", "에서부터", "에게서", "으로서", "으로써", "에서는", "에게는",
             "으로", "에서", "에게", "부터", "까지", "보다", "처럼", "조차", "마저",
             "의", "에", "를", "을", "은", "는", "이", "가", "와", "과", "로", "도", "만"]


def tokens(text: str) -> list[str]:
    out = []
    for raw in re.split(r"[^0-9A-Za-z가-힣]+", text):
        t = raw.strip()
        if len(t) < 2:
            continue
        for p in PARTICLES:
            if len(t) > len(p) + 1 and t.endswith(p):
                t = t[: -len(p)]
                break
        if len(t) >= 2 and t not in out:
            out.append(t)
    return out


def match_expr(text: str) -> str:
    toks = tokens(text)
    return " OR ".join(f'"{t}"*' for t in toks) if toks else '"경주"'


def search(db: Path, query: str, n_papers: int, n_chunks: int, only_gyeongju: bool) -> dict:
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=30)
    expr = match_expr(query)
    rel = " AND p.relevance=1" if only_gyeongju else ""
    papers = conn.execute(
        f"""SELECT p.node_id,p.title,p.authors,p.journal,p.year,p.abstract,p.url,
                   bm25(papers_fts) AS score
            FROM papers_fts f JOIN papers p ON p.node_id=f.node_id
            WHERE papers_fts MATCH ?{rel}
            ORDER BY score LIMIT ?""",
        (expr, n_papers),
    ).fetchall()
    chunks = conn.execute(
        """SELECT rel_path, report_name, page_no,
                  snippet(chunks_fts, 3, '…', '…', '…', 28) AS snip,
                  bm25(chunks_fts) AS score
           FROM chunks_fts WHERE chunks_fts MATCH ?
           ORDER BY score LIMIT ?""",
        (expr, n_chunks),
    ).fetchall()
    ai = conn.execute(
        """SELECT rowid, question, answer, bm25(ai_fts) AS score
           FROM ai_fts WHERE ai_fts MATCH ? ORDER BY score LIMIT 3""",
        (expr,),
    ).fetchall()
    conn.close()
    return {
        "query": query,
        "match": expr,
        "papers": [
            {"node_id": r[0], "title": r[1], "authors": r[2], "journal": r[3],
             "year": r[4], "abstract": r[5], "url": r[6], "score": r[7]}
            for r in papers
        ],
        "chunks": [
            {"rel_path": r[0], "report_name": r[1], "page_no": r[2],
             "snippet": r[3], "score": r[4]}
            for r in chunks
        ],
        "ai": [
            {"id": r[0], "question": r[1], "answer": r[2], "score": r[3]}
            for r in ai
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("query")
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    ap.add_argument("--papers", type=int, default=8)
    ap.add_argument("--chunks", type=int, default=8)
    ap.add_argument("--all-relevance", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    res = search(Path(args.db), args.query, args.papers, args.chunks, not args.all_relevance)
    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=2))
        return 0
    print(f"# 검색: {res['query']}  (FTS: {res['match']})")
    print("\n## 논문·서지")
    for i, p in enumerate(res["papers"], 1):
        print(f"{i}. {p['title']} ({p['year']})")
        print(f"   {p['authors']} · {p['journal']}")
        if p["abstract"]:
            print(f"   {p['abstract'][:200]}")
        print(f"   {p['url']}")
    print("\n## 보고서 전문")
    for i, c in enumerate(res["chunks"], 1):
        print(f"{i}. {c['report_name']} p.{c['page_no']}")
        print(f"   {c['snippet']}")
    if res.get("ai"):
        print("\n## DBpia AI 답변")
        for i, a in enumerate(res["ai"], 1):
            print(f"{i}. {a['question']}")
            print(f"   {a['answer'][:400]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
