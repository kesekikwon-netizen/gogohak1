"""고고학·역사 관련성 분류. '경주' 지명만으로는 relevant가 되지 않는다.

- 관련: 제목/초록/학술지에 고고학·역사 강한 신호, 핵심 저널, 유적명
- 무관: 스포츠 경주 오검색, 전력·토목·건축·의학·문학창작·시사 등
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402

# 문예·관광·공학 등 고고학과 무관한 학술지(제목에 경주가 있어도 제외)
NON_ARCH_JOURNAL = [
    "시조", "문학", "아동", "문예", "창작", "시학", "평론", "소설", "수필",
    "국어국문", "한글", "중어중문", "관광", "리조트", "체육", "경영", "법학",
    "행정", "교육", "의학", "신경", "비뇨", "건축", "토목", "공학", "전력",
    "지질학", "방사성", "자원환경", "지진공학", "수자원", "대기환경", "사회학",
    "과학과 기술", "치과", "샘터", "양계", "계간 시작", "창비", "콘크리트",
    "월간", "암석", "지구과학", "지리학", "마루", "국토", "새가정", "등급정보",
    "데코", "문화콘텐츠", "디자인", "국립공원", "인물과사상", "기독교", "보조사상",
    "더원미술", "동학연구", "중앙아시아", "국제언어", "비교민속", "하천과",
    "Electric", "화학", "Power",
]

STRONG = set(
    [t for t in seeds.ARCH_TERMS if len(t) >= 2 and t not in ("조사", "유적", "성", "궁", "문화재", "문화유산")] + [
        "민속", "미술", "서지", "금석", "고문서",
        "석탑", "탑지", "탑파", "불상", "사적", "왕릉",
        "석실", "목곽", "옹관", "분묘", "묘역",
        "고지도", "읍지", "지리지", "고지진", "고환경", "화분",
    ]
)

# 경주 범위: 지명·왕경·유적명 중 하나가 있어야 한다. 고고·역사 신호만으로는 부족하다.
# '계림'은 중국 지명·언어학 문헌과 겹치므로 단독으로 쓰지 않는다.
LOCALITY = [
    "경주", "慶州", "서라벌", "徐羅伐", "왕경", "王京", "신라", "新羅",
    "월성", "황룡사", "분황사", "사천왕사", "황남대총", "천마총", "대릉원",
    "동궁과 월지", "동궁", "월지", "안압지", "첨성대", "감은사", "불국사",
    "석굴암", "남산", "북천", "방리", "9주", "5소경", "소경",
]

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


def in_scope(title: str, abstract: str, journal: str) -> bool:
    """수집 대상 범위: 경주·신라 지명 신호 + 고고·역사 신호가 둘 다 있어야 한다."""
    t, a, j = title or "", abstract or "", journal or ""
    if any(x in j for x in NON_ARCH_JOURNAL):
        return False
    # '환경주사' 같은 낱말 속 '경주'는 지명이 아니다.
    blob = (t + " " + a + " " + j).replace("환경주", "")
    if not any(x in blob for x in LOCALITY):
        return False
    rel, _ = classify(title, abstract, journal)
    return bool(rel)


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
