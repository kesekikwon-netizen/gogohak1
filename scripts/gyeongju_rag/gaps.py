"""경주 RAG의 빈 곳을 찾아 DBpia AI에 물을 질문을 만든다.

- DBpia 논문은 있는데 로컬 보고서 전문이 없는 유적
- 로컬 보고서는 있는데 DBpia 논문이 없는 유적
- 초록이 비어 있는 논문
- DBpia 검색 결과가 0건인 질의

결과: /mnt/data-drawing/gyeongju_rag/gap_report.json, gap_questions.jsonl
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402
import store  # noqa: E402
from search import match_expr  # noqa: E402

OUT_DIR = Path("/mnt/data-drawing/gyeongju_rag")

RULE = (
    "조건: 각 주장에 저자·연도·학술지/보고서명·권호·쪽수를 붙이고, "
    "확인된 사실과 추정을 구분하고, 수치는 원문 근거가 있을 때만 써라."
)


def fts_count(conn, table: str, text: str) -> int:
    try:
        return conn.execute(f"SELECT COUNT(*) FROM {table} WHERE {table} MATCH ?",
                            (match_expr(text),)).fetchone()[0]
    except sqlite3.OperationalError:
        return 0


def analyze(db: Path) -> dict:
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=30)
    papers = conn.execute("SELECT COUNT(*) FROM papers").fetchone()[0]
    no_abs = conn.execute("SELECT COUNT(*) FROM papers WHERE abstract=''").fetchone()[0]
    chunks = conn.execute("SELECT COUNT(*) FROM chunks").fetchone()[0]
    zero_q = conn.execute(
        "SELECT q FROM queries WHERE status='done' AND total_hits=0 ORDER BY priority LIMIT 200"
    ).fetchall()
    zero_q = [r[0] for r in zero_q]
    missing = conn.execute(
        """SELECT node_id,title,authors,journal,year FROM papers
           WHERE abstract='' AND (ai_summary IS NULL OR ai_summary='')
           ORDER BY CASE WHEN journal IN ('신라문화','신라사학보','신라문물연구','경주사학',
                                          '한국고고학보','야외고고학','고고학','보존과학회지','문화재')
                         THEN 0 ELSE 1 END,
                    CASE WHEN year='' THEN 1 ELSE 0 END, year DESC LIMIT 5000"""
    ).fetchall()

    site_rows = []
    for s in seeds.SITES:
        p = fts_count(conn, "papers_fts", s)
        c = fts_count(conn, "chunks_fts", s)
        if p == 0 or c == 0:
            site_rows.append({"site": s, "papers": p, "chunks": c})
    site_rows.sort(key=lambda r: (r["chunks"] > 0, r["papers"] > 0, r["papers"] + r["chunks"]))

    conn.close()
    return {
        "papers": papers,
        "papers_without_abstract": no_abs,
        "chunks": chunks,
        "zero_hit_queries": zero_q,
        "missing_abstracts": [
            {"node_id": r[0], "title": r[1], "authors": r[2], "journal": r[3], "year": r[4]}
            for r in missing
        ],
        "site_gaps": site_rows,
    }


def gap_questions(rep: dict, limit: int) -> tuple[list[str], dict[str, str]]:
    qs: list[str] = []
    seen = set()
    mapping: dict[str, str] = {}

    def add(q: str, node_id: str = "") -> None:
        q = " ".join(q.split()) + " " + RULE
        if q in seen:
            return
        seen.add(q)
        qs.append(q)
        if node_id:
            mapping[q] = node_id

    for g in rep["site_gaps"]:
        s = g["site"]
        if g["papers"] == 0 and g["chunks"] == 0:
            add(f"경주 {s}에 대해 알려진 발굴조사·연구 성과를 연도·유구·유물 중심으로 정리해줘. "
                f"로컬 보고서에 없는 내용을 우선해줘.")
        elif g["chunks"] == 0:
            add(f"경주 {s} 관련 논문·학술지의 핵심 주장과 쟁점을 정리해줘. "
                f"발굴조사 보고서가 없어 논문 근거가 필요한 상태야.")
        elif g["papers"] == 0:
            add(f"경주 {s}의 발굴조사 성과를 논문·학술지 관점에서 정리해줘. "
                f"보고서는 있지만 학술 논문 근거가 부족해.")
        if len(qs) >= limit:
            return qs, mapping

    for q in rep["zero_hit_queries"]:
        add(f"{q}에 관한 발굴조사·연구 성과를 알려줘. DBpia 검색에서 0건이었어.")
        if len(qs) >= limit:
            return qs, mapping

    for p in rep.get("missing_abstracts", []):
        title = p["title"]
        add(f"「{title}」({p['authors']}, {p['journal']}, {p['year']}) 논문의 핵심 내용"
            f"(연구 목적·방법·결론)과 경주 고고학적 의의를 5문장 이내로 요약해줘.", p["node_id"])
        if len(qs) >= limit:
            break
    return qs, mapping


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    ap.add_argument("--limit", type=int, default=200)
    args = ap.parse_args()
    rep = analyze(Path(args.db))
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "gap_report.json").write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
    qs, mapping = gap_questions(rep, args.limit)
    (OUT_DIR / "gap_questions.jsonl").write_text("\n".join(qs) + "\n", encoding="utf-8")
    (OUT_DIR / "gap_question_map.json").write_text(json.dumps(mapping, ensure_ascii=False, indent=2),
                                                  encoding="utf-8")
    print(json.dumps({
        "papers": rep["papers"],
        "papers_without_abstract": rep["papers_without_abstract"],
        "chunks": rep["chunks"],
        "site_gaps": len(rep["site_gaps"]),
        "zero_hit_queries": len(rep["zero_hit_queries"]),
        "questions": len(qs),
    }, ensure_ascii=False, indent=2))
    print("\n".join(qs[:10]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
