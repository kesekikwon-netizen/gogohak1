"""기존 보고서 RAG(_rag_index.db)에서 경주 보고서 전문 청크를 경주 전용 RAG로 복사한다.

원본은 읽기 전용으로만 열고, 경주시(sigungu='경주시') 문서·청크만 가져온다.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import store  # noqa: E402

BATCH = 2000

# 지역 분류가 틀어져 경주시가 아닌 곳에 들어간 진짜 경주 보고서.
# (서울 성동구=경주 성동동 오분류, _미분류 왕경유적, 정선군=황오동 오분류)
# 대구 월성동·상주 성동동·진해 여좌동은 경주가 아니므로 제외한다.
RESCUE_SUBSTRINGS = [
    "월성해자 발굴조사 보고서_라구역",
    "인왕동 왕경유적2(",
    "경주 성동동 254-16번지",
    "경주 성동동 182-14",
    "경주 성동동 182-17번지",
    "황오동 341-12번지 본문-2",
]


def rescue_misclassified(source: Path, target: Path) -> int:
    """오분류된 경주 보고서를 추가로 가져온다. 가져온 종 수를 돌려준다."""
    conn = store.connect(target)
    src = sqlite3.connect(f"file:{source}?mode=ro", uri=True, timeout=60)
    src.execute("PRAGMA query_only=ON")
    have = {r[0] for r in conn.execute("SELECT DISTINCT rel_path FROM documents")}
    got_docs = got_chunks = 0
    for sub in RESCUE_SUBSTRINGS:
        for sig, rp, rn, tp, em in src.execute(
                "SELECT sigungu, rel_path, report_name, total_pages, extraction_mode "
                "FROM documents WHERE rel_path LIKE ?", ("%" + sub + "%",)):
            if rp in have:
                continue
            conn.execute(
                "INSERT OR REPLACE INTO documents(rel_path,sigungu,report_name,total_pages,extraction_mode) "
                "VALUES(?,?,?,?,?)", (rp, "경주시(복구)", rn, tp, em))
            rows = src.execute(
                "SELECT rel_path, report_name, page_no, content FROM chunks_fts WHERE rel_path=?",
                (rp,)).fetchall()
            conn.executemany(
                "INSERT INTO chunks(rel_path,report_name,page_no,content) VALUES(?,?,?,?)", rows)
            first_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0] - len(rows) + 1
            conn.executemany(
                "INSERT INTO chunks_fts(rowid,rel_path,report_name,page_no,content) VALUES(?,?,?,?,?)",
                [(first_id + i, r[0], r[1], r[2], r[3]) for i, r in enumerate(rows)],
            )
            have.add(rp)
            got_docs += 1
            got_chunks += len(rows)
    conn.commit()
    store.set_meta(conn, "rescue_done", "1")
    log(f"복구 {got_docs}종 {got_chunks}청크")
    return got_docs


def log(msg: str) -> None:
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def import_local(source: Path, target: Path, force: bool = False) -> dict:
    if not source.exists():
        raise SystemExit(f"원본 RAG DB가 없습니다: {source}")
    conn = store.connect(target)
    if not force and store.get_meta(conn, "local_import_done") == "1":
        log("이미 가져왔습니다. --force 로 다시 실행할 수 있습니다.")
        return store.stats(conn)

    src = sqlite3.connect(f"file:{source}?mode=ro", uri=True, timeout=60)
    src.execute("PRAGMA query_only=ON")
    docs = src.execute(
        "SELECT rel_path, sigungu, report_name, total_pages, extraction_mode "
        "FROM documents WHERE sigungu LIKE '%경주%'"
    ).fetchall()
    log(f"경주 문서 {len(docs)}건")
    conn.executemany(
        "INSERT OR REPLACE INTO documents(rel_path,sigungu,report_name,total_pages,extraction_mode) "
        "VALUES(?,?,?,?,?)", docs)
    conn.commit()

    if force:
        conn.execute("DELETE FROM chunks_fts")
        conn.execute("DELETE FROM chunks")
        conn.commit()

    cur = src.execute(
        "SELECT rel_path, report_name, page_no, content FROM chunks_fts WHERE sigungu='경주시'"
    )
    total = 0
    while True:
        rows = cur.fetchmany(BATCH)
        if not rows:
            break
        conn.executemany(
            "INSERT INTO chunks(rel_path,report_name,page_no,content) VALUES(?,?,?,?)", rows)
        first_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0] - len(rows) + 1
        conn.executemany(
            "INSERT INTO chunks_fts(rowid,rel_path,report_name,page_no,content) VALUES(?,?,?,?,?)",
            [(first_id + i, r[0], r[1], r[2], r[3]) for i, r in enumerate(rows)],
        )
        conn.commit()
        total += len(rows)
        if total % 20000 == 0:
            log(f"청크 {total:,}건 복사")
    src.close()

    store.set_meta(conn, "local_import_done", "1")
    store.set_meta(conn, "local_import_at", store.now())
    store.set_meta(conn, "local_source", str(source))
    conn.commit()
    st = store.stats(conn)
    log("완료: " + json.dumps(st, ensure_ascii=False))
    return st


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=str(store.LOCAL_RAG_DB))
    ap.add_argument("--db", default=str(store.DEFAULT_DB))
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--rescue-only", action="store_true")
    args = ap.parse_args()
    if args.rescue_only:
        n = rescue_misclassified(Path(args.source), Path(args.db))
        print(json.dumps({"rescued": n}, ensure_ascii=False))
        return 0
    print(json.dumps(import_local(Path(args.source), Path(args.db), args.force),
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
