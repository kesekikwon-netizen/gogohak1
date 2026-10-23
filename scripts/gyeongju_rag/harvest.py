"""DBpia 공개 검색 API로 경주 관련 서지·초록을 수렴할 때까지 수집한다.

- 로그인 없이 동작하는 www.dbpia.co.kr 검색 API만 사용한다(초록·서지 메타데이터).
- 질의를 넓게 시작해 결과의 키워드·유적명·저자로 질의를 계속 확장하고,
  새 질의와 새 논문이 모두 0이 되면 멈춘다.
- 중단 후 다시 실행하면 queries 테이블의 pending부터 이어서 수집한다.

사용:
  python3 scripts/gyeongju_rag/harvest.py --stats
  python3 scripts/gyeongju_rag/harvest.py --max-pages 3      # 시험
  python3 scripts/gyeongju_rag/harvest.py                    # 수렴까지
"""

from __future__ import annotations

import argparse
import json
import random
import re
import signal
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seeds  # noqa: E402
import store  # noqa: E402

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:135.0) Gecko/20100101 Firefox/135.0"
BASE = "https://www.dbpia.co.kr"
PAGE_SIZE = 100
MAX_PAGES = 100  # API가 page*pageSize 10,000건까지만 제공
HL = re.compile(r"<!HS>|<!HE>")
SITE_RE = re.compile(
    r"([가-힣]{2,10}(?:유적|유물산포지|고분군|사지|산성|읍성|토성|왕릉|릉|총|묘|가마|요지|"
    r"건물지|주거지|서원|사찰|불상|석탑|탑|비|궁|해자|성))"
)
GENERIC = {"유적", "유물", "고분", "고분군", "사지", "산성", "읍성", "토성", "건물지", "주거지"}

# 경주 밖의 같은 지명(서울 남산, 월성원자력, 전주 남고루 등)과 섞이기 쉬운 이름.
# 이 이름들은 반드시 '경주 '를 앞에 붙여 검색한다.
AMBIGUOUS_SITES = {
    "월성", "남산", "계림", "서악", "감포", "동궁", "월지", "대왕암", "부산성", "무산성",
    "북문", "옥산서원", "서악서원", "독락당", "오릉", "삼릉", "남고루", "대릉원",
}


def log(msg: str) -> None:
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


class DbpiaClient:
    def __init__(self, delay: float = 0.45):
        self.delay = delay
        self.key = ""
        self.key_at = 0.0
        self.opener = urllib.request.build_opener()

    def _open(self, req: urllib.request.Request, timeout: int = 30) -> bytes:
        with self.opener.open(req, timeout=timeout) as r:
            return r.read()

    def refresh_key(self, query: str = "경주") -> bool:
        url = f"{BASE}/search/topSearch?searchOption=all&query={urllib.parse.quote(query)}"
        req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Language": "ko-KR,ko;q=0.9"})
        try:
            html = self._open(req).decode("utf-8", "replace")
        except Exception as e:
            log(f"세션 키 요청 실패: {e}")
            return False
        m = re.search(r"searchResultKey\s*[:=]\s*['\"]([^'\"]+)['\"]", html)
        if not m:
            log("세션 키를 찾지 못했습니다.")
            return False
        self.key, self.key_at = m.group(1), time.time()
        return True

    def search(self, query: str, page: int, sort: str = "RANK") -> dict:
        if not self.key or time.time() - self.key_at > 1800:
            self.refresh_key(query)
        body = {
            "query": query, "page": page, "pageSize": PAGE_SIZE, "sort": sort,
            "collection": "ALL", "includeAr": False, "startDate": "1970.01.01",
        }
        for attempt in range(4):
            req = urllib.request.Request(
                f"{BASE}/api/search/list",
                data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
                headers={
                    "Content-Type": "application/json", "User-Agent": UA,
                    "X-JSP-KEY": self.key, "Referer": f"{BASE}/search/topSearch",
                    "Accept": "application/json",
                },
            )
            try:
                raw = self._open(req).decode("utf-8", "replace")
                data = json.loads(raw)
                if isinstance(data, dict) and "list" in data:
                    return data
                raise RuntimeError(f"예상 밖 응답: {raw[:160]}")
            except urllib.error.HTTPError as e:
                if e.code in (401, 403, 419, 429, 500, 502, 503):
                    self.refresh_key(query)
                    time.sleep(min(8, 1.2 * (attempt + 1)) + random.random())
                    continue
                raise
            except Exception as e:
                if attempt >= 3:
                    raise
                log(f"검색 재시도 {attempt + 1}/4: {e}")
                self.refresh_key(query)
                time.sleep(1.5 * (attempt + 1))
        raise RuntimeError("검색 실패")

    def pause(self) -> None:
        time.sleep(self.delay + random.random() * 0.25)


def clean(s) -> str:
    return HL.sub("", str(s or "")).strip()


def paper_from_item(it: dict) -> dict:
    node_id = str(it.get("nodeId") or "").strip()
    title = clean(it.get("nodeTitle"))
    abstract = clean(it.get("abstractText"))
    authors = []
    for a in it.get("authors") or []:
        nm = str((a or {}).get("autrNm") or "").strip()
        if nm:
            authors.append(nm)
    labels = it.get("labels") or {}
    keywords = [clean(k) for k in (it.get("searchKeywords") or []) if clean(k)]
    journal = str(it.get("plctNm") or "").strip()
    publisher = str(it.get("iprdNm") or "").strip()
    subject = it.get("subjectCodeName") or ""
    return {
        "node_id": node_id,
        "node_type": labels.get("nodeType", ""),
        "title": title,
        "title_clean": title,
        "authors": "·".join(authors),
        "journal": journal if journal else publisher,
        "publisher": publisher,
        "year": str(it.get("publishYymm") or "").strip(),
        "abstract": abstract,
        "abstract_is_ai": str(it.get("abstEstnYn") or "").upper() == "Y",
        "url": f"{BASE}/journal/articleDetail?nodeId={node_id}",
        "subject_code": str(it.get("subjectCode") or ""),
        "subject_name": str(subject),
        "start_page": it.get("startPage"),
        "end_page": it.get("endPage"),
        "total_pages": it.get("totalPageCnt"),
        "use_count": it.get("useCnt"),
        "keywords": keywords,
    }


def is_relevant(p: dict, query: str) -> tuple[int, str]:
    text = " ".join([p.get("title", ""), p.get("abstract", ""), p.get("journal", ""),
                     " ".join(p.get("keywords") or [])])
    has_arch = any(t in text for t in seeds.ARCH_TERMS)
    if any(t in text for t in seeds.RACE_NEGATIVE) and not has_arch:
        return 0, "스포츠·자동차 경주 오검색"
    if any(t in text for t in ["경주", "서라벌", "왕경", "계림", "동경", "慶州", "新羅", "鷄林", "徐羅伐"]):
        return 1, ""
    if any(s in text for s in seeds.SITES if len(s) >= 3):
        return 1, ""
    if any(j in text for j in seeds.JOURNALS):
        return 1, ""
    if "경주" in query and any(u in text for u in seeds.LEGAL_DONG + seeds.LEGAL_RI):
        return 1, ""
    return 0, "경주 관련 표지 없음"


def expand_from_papers(conn, papers: list[dict], max_add: int = 10) -> int:
    """결과에서 새 질의를 뽑는다. 무한 팽창을 막기 위해 어휘·지명 조건을 통과한 것만 넣는다."""
    locality = set(seeds.locality_tokens())
    vocab = set(seeds.SITES + seeds.THEMES + seeds.PERIODS + seeds.JOURNALS)
    known = {r[0].replace(" ", "").lower() for r in conn.execute("SELECT q FROM queries")}
    added = 0

    def want(q: str) -> bool:
        nonlocal added
        key = q.replace(" ", "").lower()
        if key in known or added >= max_add:
            return False
        known.add(key)
        if store.add_query(conn, q, priority=6, source="결과확장"):
            added += 1
            return True
        return False

    for p in papers:
        if not p.get("relevance"):
            continue
        for kw in p.get("keywords") or []:
            kw = " ".join(kw.split())
            if not (2 <= len(kw) <= 30):
                continue
            if kw in vocab or "경주" in kw:
                if not kw.startswith("경주 "):
                    want("경주 " + kw)
            elif any(loc in kw for loc in locality) and any(a in kw for a in seeds.ARCH_TERMS):
                want(kw)
        for m in SITE_RE.finditer(p["title"]):
            cand = m.group(1)
            if cand in GENERIC or len(cand) < 4 or cand in seeds.SITES:
                continue
            if any(loc in cand for loc in locality):
                want(cand)
        if added >= max_add:
            break
    return added


def expand_authors(conn) -> int:
    counter: Counter = Counter()
    for (authors,) in conn.execute("SELECT authors FROM papers WHERE relevance=1 AND authors<>''"):
        for name in re.split(r"[·,;/]", authors or ""):
            name = name.strip()
            if 2 <= len(name) <= 5:
                counter[name] += 1
    added_n = 0
    for author, n in counter.most_common(150):
        if n < 4:
            continue
        if store.add_query(conn, f"경주 {author}", priority=8, source="저자확장",
                           discovered_from="author-count"):
            added_n += 1
    return added_n


def harvest(args) -> int:
    conn = store.connect(args.db)
    if args.stats:
        print(json.dumps(store.stats(conn), ensure_ascii=False, indent=2))
        return 0

    if args.add_seeds or conn.execute("SELECT COUNT(*) FROM queries").fetchone()[0] == 0:
        n = 0
        for q, prio, src in seeds.seed_queries():
            n += store.add_query(conn, q, priority=prio, source=src)
        conn.commit()
        log(f"초기 질의 {n}건 등록")

    client = DbpiaClient(delay=args.delay)
    started = time.time()
    stop = {"flag": False}
    signal.signal(signal.SIGINT, lambda *_: stop.__setitem__("flag", True))

    # 지난 실행이 중간에 끊긴 질의는 다시 대기로 돌린다.
    conn.execute("UPDATE queries SET status='pending' WHERE status='running'")
    conn.commit()

    processed = 0
    while not stop["flag"]:
        if args.max_queries and processed >= args.max_queries:
            log(f"--max-queries {args.max_queries} 도달")
            break
        if args.max_hours and (time.time() - started) / 3600 >= args.max_hours:
            log(f"--max-hours {args.max_hours} 도달")
            break

        row = conn.execute(
            """SELECT q, sort FROM queries WHERE status='pending'
               ORDER BY priority ASC, created_at ASC LIMIT 1"""
        ).fetchone()
        if not row:
            # 대기 질의가 없으면 저자 확장 패스를 한 번 돌린다.
            added = expand_authors(conn)
            conn.commit()
            if added == 0:
                log("대기 질의·신규 확장 모두 0건 — 수렴 완료")
                break
            continue

        q, sort = row
        conn.execute("UPDATE queries SET status='running', updated_at=? WHERE q=? AND sort=?",
                     (store.now(), q, sort))
        conn.commit()
        processed += 1
        log(f"[{processed}] {sort:4s} \"{q}\" 수집 시작")

        total_hits = pages_total = pages_done = added_papers = 0
        all_papers = []
        try:
            for page in range(1, MAX_PAGES + 1):
                if args.max_pages and page > args.max_pages:
                    break
                if stop["flag"]:
                    break
                data = client.search(q, page, sort)
                info = data.get("pageInfo") or {}
                total_hits = int(info.get("totalCount") or 0)
                pages_total = min(int(info.get("totalPages") or 0), MAX_PAGES)
                items = data.get("list") or []
                if page == 1:
                    log(f"       총 {total_hits:,}건 / {pages_total}쪽")
                if not items:
                    break
                rows = []
                skipped = 0
                for it in items:
                    p = paper_from_item(it)
                    if not p["node_id"] or not p["title"]:
                        continue
                    rel, note = is_relevant(p, q)
                    p["relevance"], p["relevance_note"] = rel, note
                    if rel == 0:
                        store.add_skipped(conn, p, q, note)
                        skipped += 1
                        continue
                    rows.append(p)
                for p in rows:
                    if store.upsert_paper(conn, p, q, sort):
                        added_papers += 1
                all_papers.extend(rows)
                pages_done = page
                conn.execute(
                    """UPDATE queries SET total_hits=?, pages_total=?, pages_done=?, added=?,
                           updated_at=? WHERE q=? AND sort=?""",
                    (total_hits, pages_total, page, added_papers, store.now(), q, sort),
                )
                conn.commit()
                if args.verbose:
                    log(f"       p.{page}/{pages_total} +{len(rows)} (신규 누적 {added_papers})")
                if page >= pages_total:
                    break
                client.pause()

            new_q = expand_from_papers(conn, all_papers)
            if sort == "RANK" and total_hits > 3000:
                if store.add_query(conn, q, priority=4, source="심층정렬", sort="DATE"):
                    new_q += 1
            conn.execute(
                """UPDATE queries SET status='done', pages_done=?, added=?, updated_at=?
                   WHERE q=? AND sort=?""",
                (pages_done, added_papers, store.now(), q, sort),
            )
            conn.commit()
            log(f"       완료: {pages_done}쪽, 신규 {added_papers}편, 신규질의 +{new_q}")
        except Exception as e:
            conn.execute(
                "UPDATE queries SET status='error', error=?, updated_at=? WHERE q=? AND sort=?",
                (str(e)[:500], store.now(), q, sort),
            )
            conn.commit()
            log(f"       오류: {e}")
            time.sleep(2.0)

        if processed % 25 == 0:
            st = store.stats(conn)
            log(f"중간 집계: {json.dumps(st, ensure_ascii=False)}")

    st = store.stats(conn)
    store.set_meta(conn, "last_harvest_at", store.now())
    store.set_meta(conn, "last_harvest_stats", json.dumps(st, ensure_ascii=False))
    conn.commit()
    print(json.dumps(st, ensure_ascii=False, indent=2))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--add-seeds", action="store_true")
    ap.add_argument("--max-pages", type=int, default=0)
    ap.add_argument("--max-queries", type=int, default=0)
    ap.add_argument("--max-hours", type=float, default=0)
    ap.add_argument("--delay", type=float, default=0.45)
    ap.add_argument("--verbose", action="store_true")
    return harvest(ap.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
