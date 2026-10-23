// DBpia AI 로그인·질의 도구.
//
// 로그인(처음 한 번, 화면 있음):
//   node scripts/gyeongju_rag/ask_ai.js --login
// 상태 확인:
//   node scripts/gyeongju_rag/ask_ai.js --check
// 질문 루프(로그인 후, 화면 없음):
//   node scripts/gyeongju_rag/ask_ai.js --questions /mnt/data-drawing/gyeongju_rag/questions.jsonl
//
// 로그인 세션은 전용 Chrome 프로필에 보관된다. DBpia AI의 무료/월간 한도를 넘으면
// 스스로 멈추고 ai_answers.jsonl에 그때까지의 답변을 남긴다.

const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer-core');

const OUT_DIR = process.env.GY_RAG_DIR || '/mnt/data-drawing/gyeongju_rag';
const PROFILE = path.join(OUT_DIR, 'chrome-profile');
const ANSWERS = path.join(OUT_DIR, 'ai_answers.jsonl');
const STATE = path.join(OUT_DIR, 'ai_state.json');
const AI_URL = 'https://ai.dbpia.co.kr/ko/search';
const LOGIN_URL = 'https://www.dbpia.co.kr/member/login';

const log = (...a) => {
  const kst = new Date(Date.now() + 9 * 3600 * 1000).toISOString().slice(11, 19);
  console.log(kst, ...a);
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function state(extra = {}) {
  let cur = {};
  try { cur = JSON.parse(fs.readFileSync(STATE, 'utf8')); } catch (e) {}
  const next = { ...cur, ...extra, at: new Date().toISOString() };
  fs.mkdirSync(OUT_DIR, { recursive: true });
  fs.writeFileSync(STATE, JSON.stringify(next, null, 2));
  return next;
}

async function launch(headless) {
  if (process.env.GYEONGJU_ATTACH) {
    return puppeteer.connect({ browserURL: process.env.GYEONGJU_ATTACH, defaultViewport: null });
  }
  return puppeteer.launch({
    executablePath: '/usr/bin/google-chrome',
    headless,
    userDataDir: PROFILE,
    args: ['--no-sandbox', '--disable-gpu', '--lang=ko-KR', '--window-size=1600,1000'],
    defaultViewport: { width: 1600, height: 1000 },
  });
}

async function memberInfo(page) {
  const res = await page.evaluate(async () => {
    try {
      const r = await fetch('https://www.dbpia.co.kr/member/me', { credentials: 'include' });
      return await r.text();
    } catch (e) { return '{}'; }
  });
  try {
    const j = JSON.parse(res);
    return j;
  } catch (e) {
    return {};
  }
}

async function dismissIntro(page) {
  for (const t of ['건너뛰기', '시작하기']) {
    try {
      const hit = await page.evaluate((txt) => {
        const b = Array.from(document.querySelectorAll('button')).find((x) => x.innerText.trim() === txt);
        if (b) { b.click(); return true; }
        return false;
      }, t);
      if (hit) await sleep(700);
    } catch (e) {}
  }
}

async function login() {
  const browser = await launch(false);
  const page = await browser.newPage();
  await page.goto(LOGIN_URL, { waitUntil: 'domcontentloaded', timeout: 60000 });
  log('브라우저에서 DBpia 로그인을 완료해 주세요. 최대 30분 기다립니다.');
  let info = {};
  for (let i = 0; i < 360; i++) {
    await sleep(5000);
    try {
      info = await memberInfo(page);
      if (info && info.b2cId) {
        log('로그인 확인:', info.b2cName || info.b2cId, 'AI등급:', info.b2cAiTier || '-');
        state({ loggedIn: true, b2cId: info.b2cId, b2cName: info.b2cName || '' });
        await browser.close();
        return 0;
      }
    } catch (e) {}
    if (i % 12 === 11) log('아직 로그인 전입니다... (로그인 창을 닫지 마세요)');
  }
  log('시간 초과. 로그인하지 못했습니다.');
  await browser.close();
  return 2;
}

async function check() {
  const browser = await launch('new');
  const page = await browser.newPage();
  await page.goto(AI_URL, { waitUntil: 'networkidle2', timeout: 60000 });
  await sleep(2500);
  const info = await memberInfo(page);
  const ok = !!(info && info.b2cId);
  log(ok ? `로그인됨: ${info.b2cName || info.b2cId}` : '로그인 안 됨');
  state({ loggedIn: ok, b2cId: info.b2cId || '', b2cName: info.b2cName || '' });
  await browser.close();
  return ok ? 0 : 2;
}

function currentConvId(page) {
  const m = page.url().match(/\/search\/(\d+)/);
  return m ? m[1] : '';
}

async function askOne(page, question) {
  const convId = currentConvId(page);
  async function snapshot() {
    try {
      return await page.evaluate(async (id) => {
        if (id) {
          const r = await fetch(`https://agent.dbpia.co.kr/vetter_conversations/${id}`, { credentials: 'include' });
          const j = JSON.parse(await r.text());
          const msgs = j.messages || [];
          const asst = msgs.filter((m) => m.role === 'assistant');
          const last = asst.length ? String(asst[asst.length - 1].content || '') : '';
          return { n: asst.length, last };
        }
        const ta = document.querySelector('textarea');
        return { n: -1, last: document.body.innerText, ta: ta ? ta.value : '' };
      }, convId);
    } catch (e) { return { n: -2, last: '' }; }
  }

  const before = await snapshot();
  const ta = await page.$('textarea');
  if (!ta) throw new Error('질문 입력창을 찾지 못했습니다.');
  await ta.click();
  await page.evaluate(() => { const t = document.querySelector('textarea'); t.value = ''; });
  // 타이핑이 잘렸는지 확인하고, 잘렸으면 다시 입력한다.
  for (let attempt = 0; attempt < 3; attempt++) {
    await page.evaluate(() => { const t = document.querySelector('textarea'); t.value = ''; });
    await ta.type(question, { delay: 8 });
    await sleep(400);
    const echo = await page.evaluate(() => document.querySelector('textarea').value || '');
    if (echo.replace(/\s+/g, ' ').trim().length >= question.replace(/\s+/g, ' ').trim().length * 0.95) break;
    if (attempt === 2) throw new Error('INPUT_TRUNCATED');
    await sleep(1000);
  }
  await page.keyboard.press('Enter');

  // 새 대화가 열리기를 기다린다.
  let id = convId;
  if (!id) {
    for (let i = 0; i < 30; i++) {
      await sleep(2000);
      id = currentConvId(page);
      if (id) break;
    }
  }
  if (id) {
    // 같은 대화를 이어쓸 때: 어시스턴트 메시지 수가 늘고,
    // 새 답변이 이전 답변과 다를 때만 완성으로 인정한다.
    const beforeN = id === convId ? before.n : 0;
    const beforeLast = id === convId ? before.last : '';
    let last = '', stable = 0;
    for (let i = 0; i < 120; i++) {
      await sleep(3000);
      let data = null;
      try {
        data = await page.evaluate(async (cid) => {
          const r = await fetch(`https://agent.dbpia.co.kr/vetter_conversations/${cid}`, { credentials: 'include' });
          return JSON.parse(await r.text());
        }, id);
      } catch (e) {}
      const msgs = (data && data.messages) || [];
      const asst = msgs.filter((m) => m.role === 'assistant');
      const content = asst.length ? String(asst[asst.length - 1].content || '') : '';
      const isNew = asst.length > beforeN && content !== beforeLast;
      if (isNew && content.length > 200 && content === last) stable++; else stable = 0;
      last = content;
      if (stable >= 2) {
        const links = [];
        const seen = new Set();
        for (const m of asst) {
          for (const c of String(m.content || '').matchAll(/<CIT\s+([A-Za-z0-9]+)_\d+>/g)) {
            if (seen.has(c[1])) continue;
            seen.add(c[1]);
            links.push({ href: `https://www.dbpia.co.kr/journal/articleDetail?nodeId=${c[1]}`, title: '' });
          }
        }
        // 인용은 새 답변에서만 집계한다.
        const fresh = [];
        {
          const seenF = new Set();
          for (const c of String(content).matchAll(/<CIT\s+([A-Za-z0-9]+)_\d+>/g)) {
            if (seenF.has(c[1])) continue;
            seenF.add(c[1]);
            fresh.push({ href: `https://www.dbpia.co.kr/journal/articleDetail?nodeId=${c[1]}`, title: '' });
          }
        }
        return { answer: content, links: fresh.length ? fresh : links, conversationId: id };
      }
      const body = await page.evaluate(() => document.body.innerText);
      if (/이용\s*횟수|무료\s*검색.*(끝|소진)|한도에 도달|로그인하세요/.test(body) && !isNew)
        throw new Error('QUOTA');
    }
    throw new Error('TIMEOUT');
  }

  // URL이 바뀌지 않는 경우(짧은 답변 등)는 화면 텍스트로 대신한다.
  // 이전 화면보다 길어진 경우만 인정해 낡은 답변 저장을 막는다.
  const baseLen = (before.last || '').length;
  let last = -1, stable = 0;
  for (let i = 0; i < 60; i++) {
    await sleep(3000);
    const now = await page.evaluate(() => ({
      text: document.body.innerText,
      links: Array.from(document.querySelectorAll('a[href*="articleDetail"]')).map((a) => ({
        href: a.href, title: (a.innerText || '').trim().slice(0, 160),
      })),
    }));
    if (now.text.length === last) stable++; else { stable = 0; last = now.text.length; }
    const delta = now.text.slice(-4000);
    const loading = /답변을 생성하고 있습니다|파일을 불러오고 있습니다|잠시만 기다려주세요|논문을 확인하고 있습니다|검색중/.test(delta);
    if (stable >= 3 && !loading && delta.length > 200 && now.text.length > baseLen + 200)
      return { answer: delta, links: now.links, conversationId: '' };
    if (/이용\s*횟수|무료\s*검색.*(끝|소진)|한도에 도달|로그인하세요/.test(delta) && stable >= 2)
      throw new Error('QUOTA');
  }
  throw new Error('TIMEOUT');
}

async function runQuestions(file) {
  if (!fs.existsSync(file)) throw new Error('질문 파일이 없습니다: ' + file);
  const questions = fs.readFileSync(file, 'utf8').split('\n').map((s) => s.trim()).filter(Boolean);
  const done = new Set();
  if (fs.existsSync(ANSWERS)) {
    for (const line of fs.readFileSync(ANSWERS, 'utf8').split('\n')) {
      try { const j = JSON.parse(line); if (j.question) done.add(j.question); } catch (e) {}
    }
  }
  const todo = questions.filter((q) => !done.has(q));
  log(`질문 ${questions.length}건 중 남은 ${todo.length}건`);

  const attached = !!process.env.GYEONGJU_ATTACH;
  // 기본은 하나의 창(하나의 대화)에서 끝까지. 창을 나누고 싶을 때만 GYEONGJU_BATCH=10 처럼 지정.
  const batchSize = parseInt(process.env.GYEONGJU_BATCH || '1000000', 10) || 1000000;
  const browser = await launch('new');

  async function newAskPage() {
    const p = await browser.newPage();
    await p.goto(AI_URL, { waitUntil: 'networkidle2', timeout: 60000 });
    await sleep(2500);
    await dismissIntro(p);
    return p;
  }

  let page = await newAskPage();
  const info = await memberInfo(page);
  if (!info || !info.b2cId) {
    log('로그인이 필요합니다: node scripts/gyeongju_rag/ask_ai.js --login');
    if (attached) await browser.disconnect(); else await browser.close();
    return 2;
  }
  log('로그인:', info.b2cName || info.b2cId, `· 창당 ${batchSize}문항`);

  let n = 0, inBatch = 0;
  for (const q of todo) {
    if (inBatch >= batchSize) {
      try { await page.close(); } catch (e) {}
      page = await newAskPage();
      inBatch = 0;
      log(`  ── 새 창에서 계속 (${n}건 완료) ──`);
    }
    try {
      log(`질문: ${q}`);
      const r = await askOne(page, q);
      const rec = {
        question: q, answer: r.answer, links: r.links,
        conversationId: r.conversationId || '',
        b2cId: info.b2cId, askedAt: new Date().toISOString(),
      };
      fs.appendFileSync(ANSWERS, JSON.stringify(rec) + '\n');
      n++;
      inBatch++;
      log(`  답변 ${r.answer.length}자, 인용링크 ${r.links.length}건`);
      await sleep(2500);
    } catch (e) {
      if (String(e.message) === 'QUOTA') {
        log('DBpia AI 이용 한도에 도달했습니다. 다음 달/기관인증 후 이어서 실행하십시오.');
        break;
      }
      // 같은 창을 유지한다. 탭이 죽은 경우에만 같은 창 안에 새 탭을 연다.
      log('  실패:', e.message, '(같은 창에서 계속)');
      try {
        await page.goto(AI_URL, { waitUntil: 'networkidle2', timeout: 60000 });
        await sleep(3000);
        await dismissIntro(page);
      } catch (e2) {}
      inBatch = 0;
      await sleep(3000);
    }
  }
  try { await page.close(); } catch (e) {}
  state({ answered: n, lastRunQuestions: todo.length });
  if (attached) await browser.disconnect(); else await browser.close();
  log(`저장: ${ANSWERS} (${n}건 추가)`);
  return 0;
}

(async () => {
  const arg = process.argv.slice(2);
  let code = 1;
  if (arg.includes('--login')) code = await login();
  else if (arg.includes('--check')) code = await check();
  else {
    const i = arg.indexOf('--questions');
    const file = i >= 0 ? arg[i + 1] : path.join(OUT_DIR, 'questions.jsonl');
    code = await runQuestions(file);
  }
  process.exit(code);
})().catch((e) => { console.error('FATAL', e); process.exit(1); });
