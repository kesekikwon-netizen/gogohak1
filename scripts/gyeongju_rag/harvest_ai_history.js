// 이미 로그인된 DBpia AI 브라우저(127.0.0.1:9222)에 붙어서
// 대화 기록 전체를 읽어 파일로 저장한다. 새 질문을 보내지 않으므로 이용 횟수를 쓰지 않는다.
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer-core');

const OUT = '/mnt/data-drawing/gyeongju_rag/ai_history';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await puppeteer.connect({ browserURL: 'http://127.0.0.1:9222', defaultViewport: null });
  const page = (await browser.pages()).find((p) => p.url().includes('ai.dbpia.co.kr'));
  if (!page) throw new Error('DBpia AI 탭을 찾지 못했습니다.');

  const recent = await page.evaluate(async () => {
    const r = await fetch('https://agent.dbpia.co.kr/vetter_conversations/recent-conversations', { credentials: 'include' });
    return JSON.parse(await r.text());
  });
  const ids = [];
  for (const day of recent.data?.chatHistoryList || []) {
    for (const h of day.historyList || []) {
      if (h.groupId && !ids.includes(h.groupId)) ids.push(h.groupId);
    }
  }
  console.log('대화', ids.length, '건');

  let ok = 0, msgs = 0, refs = 0;
  for (const id of ids) {
    const file = path.join(OUT, `${id}.json`);
    if (fs.existsSync(file)) { ok++; continue; }
    try {
      const detail = await page.evaluate(async (gid) => {
        const r = await fetch(`https://agent.dbpia.co.kr/vetter_conversations/${gid}`, { credentials: 'include' });
        return { status: r.status, text: await r.text() };
      }, id);
      if (detail.status !== 200) { console.log('  실패', id, detail.status); continue; }
      const data = JSON.parse(detail.text);
      fs.writeFileSync(file, JSON.stringify(data, null, 1));
      const m = (data.messages || []).length;
      const r = Object.keys(data.hovers || {}).length;
      msgs += m; refs += r; ok++;
      console.log(`  ${id}: 메시지 ${m}, 인용 ${r}`);
      await sleep(250);
    } catch (e) {
      console.log('  오류', id, e.message);
    }
  }
  console.log(JSON.stringify({ conversations: ok, messages: msgs, citations: refs }));
  await browser.disconnect();
})().catch((e) => { console.error('FATAL', e); process.exit(1); });
