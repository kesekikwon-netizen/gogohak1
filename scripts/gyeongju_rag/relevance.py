"""고고학·역사 관련성 분류. '경주' 지명만으로는 relevant가 되지 않는다.

- 관련: 제목/초록/학술지에 고고학·역사 강한 신호, 핵심 저널, 유적명
- 무관: 스포츠 경주 오검색, 전력·토목·건축·의학·문학창작·시사 등
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402

STRONG = set(
    [t for t in seeds.ARCH_TERMS if len(t) >= 2] + [
        "역사", "민속", "미술", "서지", "금석", "고문서",
        "탑", "석탑", "불상", "사적", "지리", "왕릉",
        "석실", "목곽", "옹관", "분묘", "묘역",
        "고지도", "읍지", "지리지", "고지진", "고환경", "화분",
    ]
)

# 한자 표기(논문 제목이 한자 혼용인 경우가 많다)
HANJA = [
    "新羅", "慶州", "王京", "加耶", "百濟", "高句麗", "古墳",
    "寺址", "石塔", "土器", "瓦", "陵", "墓", "塔", "寺",
    "斯盧", "鷄林", "徐羅伐", "金石", "銘文", "木簡",
]

CORE_JOURNALS = set(seeds.JOURNALS) | {
    "한국고대사연구", "미술사학연구", "고고광장", "선사와고대",
    "영남고고학", "백산학보", "호서고고학", "충청고고학",
    "호남고고학회", "진단학보", "역사학보", "한국사연구", "국사관논총",
}


def classify(title: str, abstract: str, journal: str) -> tuple[int, str]:
    t, a, j = title or "", abstract or "", journal or ""
    blob = t + " " + a + " " + j
    if any(x in blob for x in seeds.RACE_NEGATIVE) and not any(x in blob for x in STRONG):
        return 0, "스포츠·자동차 경주 오검색"
    if any(x in t for x in STRONG) or any(x in t for x in HANJA):
        return 1, ""
    if any(x in a for x in STRONG) or any(x in a for x in HANJA):
        return 1, ""
    if any(x in j for x in STRONG) or j in CORE_JOURNALS:
        return 1, ""
    if any(s in t for s in seeds.SITES if len(s) >= 3):
        return 1, ""
    return 0, "고고학·역사 신호 없음"


def apply(db_path: str) -> dict:
    """DB 전체에 relevance를 다시 매긴다. backup은 호출자가 한다."""
    import sqlite3

    conn = sqlite3.connect(db_path, timeout=120)
    keep = drop = 0
    for nid, t, a, j in conn.execute("SELECT node_id,title,abstract,journal FROM papers"):
        rel, note = classify(t, a, j)
        conn.execute("UPDATE papers SET relevance=?, relevance_note=? WHERE node_id=?",
                     (rel, note, nid))
        if rel:
            keep += 1
        else:
            drop += 1
    conn.commit()
    conn.close()
    return {"keep": keep, "drop": drop}
