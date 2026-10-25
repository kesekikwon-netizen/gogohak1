// 경주 RAG 보완 수집: DBpia AI 답변을 3개씩 병렬로 받아 ai_r2_NNN.jsonl로 내려받는다.
// 사용: tools.browser.evaluate({ tabID, script: <이 파일 내용> }) 로 한 번만 넣는다.
// 전제: 페이지에 window.__Q(질문 배열)가 있고, 로그인되어 있어야 한다. SKILL.md 3절 참고.
// 주의: 이 스크립트는 시작만 하고 바로 반환한다. 진행은 window.__job2로 확인한다.
// START를 직전 실행의 next 값으로 바꾼다. 이미 보낸 질문을 다시 보내지 않기 위해서다.
(() => {
  if (window.__job2 && window.__job2.state === 'running') return { already: true };
  const H = 'https://agent.dbpia.co.kr/vetter_conversations';
  const PLCT = ['025001', '025002', '025015', '025003', '025005', '025004', '025000'];
  const Q = window.__Q.slice();
  const START = 137;
  const MAX_IN_FLIGHT = 3;
  const J = window.__job2 = { state: 'running', sent: 0, done: 0, failed: 0, part: 0, errors: [], halted: null, next: START, senderDone: false };
  const pending = [];
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  let buf = [];

  const pushPart = (rows) => {
    if (!rows.length) return;
    J.part++;
    const blob = new Blob([rows.map((x) => JSON.stringify(x)).join('\n') + '\n'], { type: 'application/x-ndjson' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'ai_r2_' + String(J.part).padStart(3, '0') + '.jsonl';
    document.body.appendChild(a); a.click(); a.remove();
  };

  const sender = async () => {
    while (J.next < Q.length && !J.halted) {
      while (pending.length >= MAX_IN_FLIGHT && !J.halted) await sleep(5000);
      if (J.halted) break;
      const k = J.next++;
      const q = Q[k];
      try {
        const c = await fetch(H + '/', { method: 'POST', credentials: 'include', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ title: q }) });
        if (!c.ok) { J.errors.push({ k, e: 'CREATE_' + c.status }); if ([403, 429].includes(c.status)) J.halted = 'CREATE_' + c.status; continue; }
        const id = Number((await c.json()).id);
        const ch = await fetch(H + '/chat_message', { method: 'POST', credentials: 'include', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ message: q, plct_types: PLCT, is_criticized: false, client_type: 'web', conversation_id: id, referer: '' }) });
        if (!ch.ok) { J.errors.push({ k, e: 'CHAT_' + ch.status }); if ([403, 429].includes(ch.status)) J.halted = 'CHAT_' + ch.status; continue; }
        ch.text();
        pending.push({ k, q, id: String(id), t: Date.now(), last: '', stable: 0 });
        J.sent++;
      } catch (e) { J.errors.push({ k, e: 'EXC ' + e.message }); }
      await sleep(1500);
    }
    J.senderDone = true;
  };

  const collector = async () => {
    while (true) {
      for (const p of pending.slice()) {
        let j = null;
        try { j = await fetch(H + '/' + p.id, { credentials: 'include' }).then((r) => r.json()); } catch (e) { continue; }
        const asst = (j.messages || []).filter((m) => m.role === 'assistant');
        const content = asst.length ? String(asst[asst.length - 1].content || '') : '';
        if (content && content.length > 200 && content === p.last) p.stable++; else p.stable = 0;
        p.last = content;
        if (p.stable >= 2) {
          const seen = new Set(), links = [];
          for (const m of content.matchAll(/<CIT\s+([A-Za-z0-9]+)_\d+>/g)) {
            if (seen.has(m[1])) continue;
            seen.add(m[1]);
            links.push({ href: 'https://www.dbpia.co.kr/journal/articleDetail?nodeId=' + m[1], title: '' });
          }
          buf.push({ question: p.q, answer: content, links, conversationId: p.id, askedAt: new Date().toISOString() });
          J.done++;
          pending.splice(pending.indexOf(p), 1);
          if (buf.length >= 20) { pushPart(buf); buf = []; }
        } else if (Date.now() - p.t > 25 * 60 * 1000) {
          J.failed++; J.errors.push({ k: p.k, e: 'TIMEOUT' });
          pending.splice(pending.indexOf(p), 1);
        }
      }
      if (J.senderDone && pending.length === 0) break;
      await sleep(15000);
    }
    pushPart(buf); buf = [];
    J.state = J.halted ? 'halted' : 'finished';
  };

  sender();
  collector();
  return { started: true, from: START, total: Q.length };
})()
