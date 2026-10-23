"""최대 추출용 질문 풀을 만든다.

- aspect_questions.jsonl: 유적×관점(조사연표/유물편년), 문화유산×관점(지정/발굴/유물·보존)
- cited_questions.jsonl: AI 답변에서 인용됐으나 DB에 없는 논문 nodeId의 서지 확인
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402
from deep_questions import RULE as DEEP_RULE  # noqa: E402

OUT_DIR = Path("/mnt/data-drawing/gyeongju_rag")

SITE_ASPECTS = [
    "지금까지 이뤄진 발굴조사(지표·시굴·정밀·구제)의 연도별 경과와 조사기관·보고서명, 확인된 주요 유구를 정리하라.",
    "출토 주요 유물(토기·와전·금속·목간 등)의 형식·편년과 학술적 쟁점을 정리하라.",
]

HERITAGE_ASPECTS = [
    "문화유산 지정 경위(연도·사유·지정가치)와 지정구역·보호관리 현황을 정리하라.",
    "발굴조사 연표(연도·조사기관·보고서명)와 확인된 주요 유구를 정리하라.",
    "출토 유물의 형식·편년, 보존처리·복원·정비 사례와 남은 쟁점을 정리하라.",
]


def build_aspects() -> list[str]:
    out, seen = [], set()

    def add(q: str) -> None:
        q = " ".join(q.split())
        if q in seen:
            return
        seen.add(q)
        out.append(q)

    for s in seeds.SITES:
        for a in SITE_ASPECTS:
            add(f"경주 {s}의 {a} {DEEP_RULE}")
    for h in seeds.HERITAGE:
        for a in HERITAGE_ASPECTS:
            add(f"경주 {h}의 {a} {DEEP_RULE}")
    return out


def build_cited(batch: int = 20) -> list[str]:
    p = OUT_DIR / "cited_missing.json"
    if not p.exists():
        return []
    try:
        ids = json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return []
    out = []
    for i in range(0, len(ids), batch):
        chunk = ", ".join(ids[i:i + batch])
        out.append(
            "다음 DBpia 논문 ID에 해당하는 논문의 서지사항"
            "(논문제목·저자·학술지명·권호·발행연도)를 표로 정리해줘. "
            f"모르는 ID는 '미확인'이라 하라: {chunk}"
        )
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--aspects-out", default=str(OUT_DIR / "aspect_questions.jsonl"))
    ap.add_argument("--cited-out", default=str(OUT_DIR / "cited_questions.jsonl"))
    args = ap.parse_args()
    aspects = build_aspects()
    Path(args.aspects_out).write_text("\n".join(aspects) + "\n", encoding="utf-8")
    cited = build_cited()
    Path(args.cited_out).write_text("\n".join(cited) + "\n", encoding="utf-8")
    print(f"aspects {len(aspects)} -> {args.aspects_out}")
    print(f"cited {len(cited)} -> {args.cited_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
