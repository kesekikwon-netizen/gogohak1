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

# 조사구역(성동동 150-3·172-9, 구 경주역사 일대) 인접 키워드.
# 초록 없는 논문 정렬에서 인접 관련을 앞으로 당긴다.
NEARBY_KEYS = [
    "성동동", "황오동", "동천동", "인왕동", "탑동", "구황동", "사정동",
    "노서동", "황남동", "노동동", "동부동", "서부동", "북부동", "성건동",
    "월성", "왕경", "경주역", "북천", "알천", "성동천", "방리", "황오리",
]

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


def build_nearby() -> list[str]:
    """조사구역 인접(반경 500m) 보고서·유적부터 학술 연계 질문을 만든다. 거리순."""
    from deep_questions import RULE as NEAR_RULE  # noqa: E402

    p = OUT_DIR / "site_profile.json"
    if not p.exists():
        return []
    prof = json.loads(p.read_text(encoding="utf-8"))
    rows = list(prof.get("조사구역내", [])) + list(prof.get("거리순30", []))
    seen_sites, out, seen = set(), [], set()

    def add(q: str) -> None:
        q = " ".join(q.split())
        if q in seen:
            return
        seen.add(q)
        out.append(q)

    def dist_key(x: dict) -> tuple:
        d = x.get("거리m")
        return (d is None, d if isinstance(d, (int, float)) else 0)

    for r in sorted(rows, key=dist_key):
        site = str(r.get("유적명") or "").strip()
        rep = str(r.get("보고서") or "").strip()
        org = str(r.get("조사기관") or "").strip()
        era = str(r.get("시대") or "").strip()
        dist = r.get("거리m")
        if site and site not in seen_sites:
            seen_sites.add(site)
            where = f"(조사구역에서 {dist}m)" if isinstance(dist, (int, float)) else ""
            add(f"경주 {site}{where}의 발굴조사 성과(조사기관·연도·유구·유물·편년)를 "
                f"학술논문 근거와 함께 정리하라. {NEAR_RULE}")
            add(f"경주 {site}와 주변(반경 500m) 유적의 층위·유구·유물 비교로 본 편년과 성격을 "
                f"발굴보고서·학술논문 근거로 정리하라. {NEAR_RULE}")
        if rep:
            add(f"「{rep}」({org})의 조사 성과(유구·유물·편년)와 관련 학술논문의 쟁점을 정리하라. "
                f"{NEAR_RULE} {era}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--aspects-out", default=str(OUT_DIR / "aspect_questions.jsonl"))
    ap.add_argument("--cited-out", default=str(OUT_DIR / "cited_questions.jsonl"))
    ap.add_argument("--nearby-out", default=str(OUT_DIR / "nearby_questions.jsonl"))
    args = ap.parse_args()
    aspects = build_aspects()
    Path(args.aspects_out).write_text("\n".join(aspects) + "\n", encoding="utf-8")
    cited = build_cited()
    Path(args.cited_out).write_text("\n".join(cited) + "\n", encoding="utf-8")
    nearby = build_nearby()
    Path(args.nearby_out).write_text("\n".join(nearby) + "\n", encoding="utf-8")
    print(f"aspects {len(aspects)} -> {args.aspects_out}")
    print(f"cited {len(cited)} -> {args.cited_out}")
    print(f"nearby {len(nearby)} -> {args.nearby_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
