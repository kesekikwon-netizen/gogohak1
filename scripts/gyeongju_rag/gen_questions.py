"""DBpia AI에 물을 질문 목록을 만든다.

경주 유적·주제·행정구역을 조합해 우선순위가 높은 질문부터 JSONL로 저장한다.
답변은 ask_ai.js가 같은 폴더의 ai_answers.jsonl에 쌓는다.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402
import store  # noqa: E402

OUT = Path("/mnt/data-drawing/gyeongju_rag/questions.jsonl")


def build(limit: int = 400) -> list[str]:
    qs: list[str] = []
    seen = set()

    def add(q: str) -> None:
        q = " ".join(q.split())
        if q in seen or len(q) < 12:
            return
        seen.add(q)
        qs.append(q)

    for s in seeds.SITES:
        add(f"경주 {s}의 발굴조사 성과와 주요 유구·유물을 정리하고, 관련 논문·보고서의 쟁점을 비교해줘.")
    for u in seeds.admin_units() + seeds.LEGAL_DONG[:20]:
        add(f"경주 {u} 일원의 발굴조사 현황과 주요 유적·유구를 정리해줘.")
    for p in seeds.PERIODS:
        for t in ["주거", "고분", "사찰", "생업", "성곽"]:
            add(f"경주 지역 {p} {t} 유적의 양상과 편년, 학술적 쟁점을 정리해줘.")
    for t in seeds.THEMES:
        add(f"경주 지역 {t} 연구의 주요 성과와 쟁점을 논문 근거로 정리해줘.")
    return qs[:limit]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--limit", type=int, default=400)
    args = ap.parse_args()
    qs = build(args.limit)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(qs) + "\n", encoding="utf-8")
    print(f"{len(qs)}개 질문 -> {out}")
    conn = store.connect()
    store.set_meta(conn, "ai_questions", len(qs))
    conn.commit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
