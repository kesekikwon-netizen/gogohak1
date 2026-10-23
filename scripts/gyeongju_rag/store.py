"""경주 RAG 저장소.

한 파일(SQLite)에 DBpia 서지·초록, 로컬 보고서 전문 청크, AI 질의응답을 함께 담는다.
FTS5(unicode61)로 한국어 검색을 지원한다.
"""

from __future__ import annotations

import json
import os
import sqlite3
import time
from pathlib import Path

DEFAULT_DB = Path("/mnt/data-drawing/gyeongju_rag/gyeongju_rag.db")
LOCAL_RAG_DB = Path("/mnt/data-drawing/report_text/_rag_index.db")

SCHEMA = """
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS papers (
    node_id       TEXT PRIMARY KEY,
    node_type     TEXT,
    title         TEXT NOT NULL,
    title_clean   TEXT,
    authors       TEXT,
    journal       TEXT,
    publisher     TEXT,
    year          TEXT,
    abstract      TEXT,
    abstract_is_ai INTEGER DEFAULT 0,
    url           TEXT,
    subject_code  TEXT,
    subject_name  TEXT,
    start_page    INTEGER,
    end_page      INTEGER,
    total_pages   INTEGER,
    use_count     INTEGER,
    keywords      TEXT,
    relevance     INTEGER DEFAULT 1,
    relevance_note TEXT,
    ai_summary    TEXT,
    first_query   TEXT,
    first_seen    TEXT,
    last_seen     TEXT
);
CREATE INDEX IF NOT EXISTS idx_papers_year ON papers(year);
CREATE INDEX IF NOT EXISTS idx_papers_relevance ON papers(relevance);
CREATE INDEX IF NOT EXISTS idx_papers_journal ON papers(journal);

CREATE TABLE IF NOT EXISTS queries (
    q             TEXT NOT NULL,
    sort          TEXT NOT NULL DEFAULT 'RANK',
    status        TEXT NOT NULL DEFAULT 'pending',  -- pending|running|done|error|skip
    total_hits    INTEGER DEFAULT 0,
    pages_done    INTEGER DEFAULT 0,
    pages_total   INTEGER DEFAULT 0,
    added         INTEGER DEFAULT 0,
    priority      INTEGER DEFAULT 9,
    source        TEXT,
    discovered_from TEXT,
    error         TEXT,
    created_at    TEXT,
    updated_at    TEXT,
    PRIMARY KEY (q, sort)
);
CREATE INDEX IF NOT EXISTS idx_queries_status ON queries(status, priority);

CREATE TABLE IF NOT EXISTS paper_queries (
    node_id TEXT NOT NULL,
    q       TEXT NOT NULL,
    sort    TEXT NOT NULL DEFAULT 'RANK',
    PRIMARY KEY (node_id, q, sort)
);
CREATE INDEX IF NOT EXISTS idx_paper_queries_q ON paper_queries(q);

-- 경주와 무관하다고 판정된 검색 결과. 초록·전문은 저장하지 않고 감사용 서지만 남긴다.
CREATE TABLE IF NOT EXISTS papers_skipped (
    node_id TEXT PRIMARY KEY,
    title   TEXT,
    journal TEXT,
    reason  TEXT,
    q       TEXT,
    seen_at TEXT
);

CREATE VIRTUAL TABLE IF NOT EXISTS papers_fts USING fts5(
    node_id UNINDEXED,
    title,
    abstract,
    authors,
    journal,
    keywords,
    tokenize='unicode61'
);

CREATE TABLE IF NOT EXISTS documents (
    rel_path     TEXT PRIMARY KEY,
    sigungu      TEXT,
    report_name  TEXT,
    total_pages  INTEGER,
    extraction_mode TEXT
);

CREATE TABLE IF NOT EXISTS chunks (
    id          INTEGER PRIMARY KEY,
    rel_path    TEXT NOT NULL,
    report_name TEXT,
    page_no     INTEGER,
    content     TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_chunks_rel ON chunks(rel_path);

CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(
    rel_path UNINDEXED,
    report_name,
    page_no UNINDEXED,
    content,
    tokenize='unicode61'
);

CREATE TABLE IF NOT EXISTS ai_turns (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    question    TEXT NOT NULL,
    answer      TEXT,
    model       TEXT,
    conversation_id TEXT,
    refs_json   TEXT,
    created_at  TEXT,
    status      TEXT DEFAULT 'done'
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_ai_question ON ai_turns(question);

CREATE VIRTUAL TABLE IF NOT EXISTS ai_fts USING fts5(
    question,
    answer,
    tokenize='unicode61'
);

CREATE TABLE IF NOT EXISTS ai_refs (
    turn_id     INTEGER,
    node_id     TEXT,
    title       TEXT,
    authors     TEXT,
    journal     TEXT,
    year        TEXT,
    url         TEXT,
    PRIMARY KEY (turn_id, node_id)
);

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""


def now() -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S")


def connect(db_path: Path | str = DEFAULT_DB) -> sqlite3.Connection:
    p = Path(db_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(p), timeout=60)
    conn.execute("PRAGMA busy_timeout=60000")
    conn.executescript(SCHEMA)
    cols = [r[1] for r in conn.execute("PRAGMA table_info(papers)")]
    if "ai_summary" not in cols:
        conn.execute("ALTER TABLE papers ADD COLUMN ai_summary TEXT")
    conn.commit()
    return conn


def set_meta(conn, key: str, value) -> None:
    conn.execute(
        "INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        (key, str(value)),
    )


def get_meta(conn, key: str, default=None):
    row = conn.execute("SELECT value FROM meta WHERE key=?", (key,)).fetchone()
    return row[0] if row else default


def stats(conn) -> dict:
    def one(sql):
        return conn.execute(sql).fetchone()[0]

    return {
        "papers": one("SELECT COUNT(*) FROM papers"),
        "papers_gyeongju": one("SELECT COUNT(*) FROM papers WHERE relevance=1"),
        "papers_fts": one("SELECT COUNT(*) FROM papers_fts"),
        "queries_done": one("SELECT COUNT(*) FROM queries WHERE status='done'"),
        "queries_pending": one("SELECT COUNT(*) FROM queries WHERE status='pending'"),
        "chunks": one("SELECT COUNT(*) FROM chunks"),
        "ai_turns": one("SELECT COUNT(*) FROM ai_turns"),
    }


def add_query(conn, q: str, priority: int = 9, source: str = "", sort: str = "RANK",
              discovered_from: str = "") -> bool:
    q = " ".join(q.split())
    if len(q) < 2:
        return False
    cur = conn.execute(
        """INSERT OR IGNORE INTO queries(q, sort, priority, source, discovered_from, created_at, updated_at)
           VALUES(?,?,?,?,?,?,?)""",
        (q, sort, priority, source, discovered_from, now(), now()),
    )
    return cur.rowcount > 0


def add_skipped(conn, p: dict, query: str, reason: str) -> None:
    conn.execute(
        """INSERT OR IGNORE INTO papers_skipped(node_id,title,journal,reason,q,seen_at)
           VALUES(?,?,?,?,?,?)""",
        (p.get("node_id", ""), p.get("title", ""), p.get("journal", ""), reason, query, now()),
    )


def upsert_paper(conn, p: dict, query: str, sort: str) -> bool:
    """새 논문이면 True. 기존 논문은 last_seen만 갱신한다."""
    node_id = p.get("node_id") or ""
    title = (p.get("title") or "").strip()
    if not node_id or not title:
        return False
    ts = now()
    cur = conn.execute(
        """INSERT OR IGNORE INTO papers(
               node_id,node_type,title,title_clean,authors,journal,publisher,year,abstract,
               abstract_is_ai,url,subject_code,subject_name,start_page,end_page,total_pages,
               use_count,keywords,relevance,relevance_note,first_query,first_seen,last_seen)
           VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
        (
            node_id, p.get("node_type", ""), title, p.get("title_clean", title),
            p.get("authors", ""), p.get("journal", ""), p.get("publisher", ""),
            p.get("year", ""), p.get("abstract", ""), 1 if p.get("abstract_is_ai") else 0,
            p.get("url", ""), p.get("subject_code", ""), p.get("subject_name", ""),
            p.get("start_page"), p.get("end_page"), p.get("total_pages"),
            p.get("use_count"), json.dumps(p.get("keywords") or [], ensure_ascii=False),
            int(p.get("relevance", 1)), p.get("relevance_note", ""), query, ts, ts,
        ),
    )
    inserted = cur.rowcount > 0
    if inserted:
        conn.execute(
            """INSERT INTO papers_fts(node_id,title,abstract,authors,journal,keywords)
               VALUES(?,?,?,?,?,?)""",
            (node_id, title, p.get("abstract", ""), p.get("authors", ""),
             p.get("journal", ""), " ".join(p.get("keywords") or [])),
        )
    else:
        # 이미 있는 논문이라도 초록이 더 길게 오면 갱신한다(잘린 초록 복원용).
        row = conn.execute("SELECT LENGTH(abstract) FROM papers WHERE node_id=?",
                           (node_id,)).fetchone()
        if row and len(p.get("abstract") or "") > (row[0] or 0):
            conn.execute("UPDATE papers SET abstract=?, last_seen=? WHERE node_id=?",
                         (p.get("abstract", ""), ts, node_id))
            conn.execute("DELETE FROM papers_fts WHERE node_id=?", (node_id,))
            conn.execute(
                """INSERT INTO papers_fts(node_id,title,abstract,authors,journal,keywords)
                   VALUES(?,?,?,?,?,?)""",
                (node_id, title, p.get("abstract", ""), p.get("authors", ""),
                 p.get("journal", ""), " ".join(p.get("keywords") or [])),
            )
        else:
            conn.execute("UPDATE papers SET last_seen=? WHERE node_id=?", (ts, node_id))
    conn.execute(
        "INSERT OR IGNORE INTO paper_queries(node_id,q,sort) VALUES(?,?,?)",
        (node_id, query, sort),
    )
    return inserted


def add_ai_turn(conn, question: str, answer: str, refs: list[dict], model: str = "dbpia-ai",
                conversation_id: str = "", status: str = "done") -> int:
    cur = conn.execute(
        """INSERT INTO ai_turns(question,answer,model,conversation_id,refs_json,created_at,status)
           VALUES(?,?,?,?,?,?,?)
           ON CONFLICT(question) DO UPDATE SET answer=excluded.answer, refs_json=excluded.refs_json,
               status=excluded.status, created_at=excluded.created_at""",
        (question, answer, model, conversation_id,
         json.dumps(refs, ensure_ascii=False), now(), status),
    )
    turn_id = cur.lastrowid
    if not turn_id:
        row = conn.execute("SELECT id FROM ai_turns WHERE question=?", (question,)).fetchone()
        turn_id = row[0] if row else 0
    if turn_id:
        for r in refs:
            nid = str(r.get("node_id") or r.get("nodeId") or "")
            if not nid:
                continue
            conn.execute(
                """INSERT OR IGNORE INTO ai_refs(turn_id,node_id,title,authors,journal,year,url)
                   VALUES(?,?,?,?,?,?,?)""",
                (turn_id, nid, r.get("title", ""), r.get("authors", ""),
                 r.get("journal", ""), r.get("year", ""), r.get("url", "")),
            )
    return turn_id
