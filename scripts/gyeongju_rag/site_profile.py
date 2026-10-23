"""시굴지(경주 성동동/황오동 구역사 일대) 프로파일을 엑셀에서 뽑아 JSON으로 저장한다.

포함: 중심지 정보, 반경 내 유적 목록, 145종 보고서 서지, 시대 분포, RAG 커버리지.
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import store  # noqa: E402

EXCEL = Path("/home/kwon/바탕화면/주변유적_현황_500m.xlsx")
OUT = Path("/mnt/data-drawing/gyeongju_rag/site_profile.json")


def load_rows():
    import openpyxl
    wb = openpyxl.load_workbook(EXCEL, data_only=True)
    ws = wb["주변유적_현황"]
    return list(ws.iter_rows(min_row=2, values_only=True))


def coverage(conn, keywords):
    expr = " OR ".join(f'"{k}"' for k in keywords)
    p = conn.execute("SELECT COUNT(*) FROM papers_fts WHERE papers_fts MATCH ?",
                     (expr,)).fetchone()[0]
    h = conn.execute("SELECT COUNT(*) FROM chunks_fts WHERE chunks_fts MATCH ?",
                     (expr,)).fetchone()[0]
    return p, h


def build() -> dict:
    rows = load_rows()
    center = [r for r in rows if r[12] == "조사구역 내"]
    nearby = sorted(
        [r for r in rows if r[12] != "조사구역 내" and isinstance(r[14], (int, float))],
        key=lambda r: r[14],
    )
    bibs = sorted({(r[5] or "").strip() for r in rows if (r[5] or "").strip()})
    era = Counter(str(r[9])[:30] for r in rows)

    conn = sqlite3.connect(f"file:{store.DEFAULT_DB}?mode=ro", uri=True, timeout=60)
    site_keys = [
        ("시굴지_150-3", ["성동동", "150-3"]),
        ("시굴지_172-9", ["성동동", "172-9"]),
        ("성동동사지", ["성동동", "사지"]),
        ("황오동삼층석탑", ["황오동", "삼층석탑"]),
        ("경주역부지", ["경주역"]),
        ("왕경유적", ["왕경유적"]),
        ("북천", ["북천", "알천"]),
        ("성동천", ["성동천"]),
        ("동천동", ["동천동"]),
        ("인왕동", ["인왕동"]),
        ("탑동", ["탑동"]),
        ("노서동", ["노서동"]),
        ("황남동", ["황남동"]),
        ("노동동", ["노동동"]),
        ("월성", ["월성"]),
        ("황룡사", ["황룡사"]),
        ("방리", ["방리"]),
        ("소경", ["소경", "5소경"]),
        ("9주", ["9주"]),
    ]
    cov = {k: coverage(conn, kw) for k, kw in site_keys}
    conn.close()

    def row(r):
        return {"유적명": r[3], "보고서": (r[5] or "")[:120], "사업명": (r[6] or "")[:80],
                "소재지": r[7], "조사기관": r[8], "시대": str(r[9])[:60],
                "유적유무": r[10], "거리m": r[14], "방향": r[13]}

    profile = {
        "중심지": "경주시 성동동 150-3번지·172-9번지 일원 (구 경주역사 일대, 왕경유적 내)",
        "조사구역내": [row(r) for r in center],
        "반경500m": len(nearby),
        "거리순30": [row(r) for r in nearby[:30]],
        "보고서수": len(bibs),
        "보고서": bibs,
        "시대분포": dict(era),
        "커버리지": {k: {"papers": v[0], "chunks": v[1]} for k, v in cov.items()},
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(profile, ensure_ascii=False, indent=1), encoding="utf-8")
    return profile


def main() -> int:
    p = build()
    print(json.dumps({"보고서": p["보고서수"], "반경500m": p["반경500m"],
                      "thin": {k: v for k, v in p["커버리지"].items()
                             if v["papers"] < 10 or v["chunks"] < 500}},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
