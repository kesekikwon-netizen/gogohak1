// DBpia 로그인을 기다렸다가, 로그인되면 종료한다(0). 셸에서 이어서 ask_ai.js를 실행한다.
const puppeteer = require('puppeteer-core');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await puppeteer.connect({ browserURL: 'http://127.0.0.1:9222', defaultViewport: null });
  let page = (await browser.pages()).find((p) => p.url().includes('member/login'));
  if (!page) {
    page = await browser.newPage();
    await page.goto('https://www.dbpia.co.kr/member/login', { waitUntil: 'domcontentloaded', timeout: 60000 });
  }
  console.log(new Date().toISOString().slice(11, 19), '로그인 창에서 로그인해 주세요. 최대 30분 기다립니다.');
  for (let i = 0; i < 360; i++) {
    await sleep(5000);
    let info = {};
    try {
      info = JSON.parse(await page.evaluate(async () => {
        const r = await fetch('https://www.dbpia.co.kr/member/me', { credentials: 'include' });
        return await r.text();
      }));
    } catch (e) {}
    if (info && info.b2cId) {
      console.log(new Date().toISOString().slice(11, 19), '로그인 확인:', info.b2cName || info.b2cId);
      try { await page.close(); } catch (e) {}
      await browser.disconnect();
      process.exit(0);
    }
    if (i % 12 === 11) console.log(new Date().toISOString().slice(11, 19), '아직 로그인 전...');
  }
  console.log('시간 초과');
  await browser.disconnect();
  process.exit(2);
})().catch((e) => { console.error(e); process.exit(1); });
