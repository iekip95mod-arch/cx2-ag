const encoder = new TextEncoder();
const repository = 'iekip95mod-arch/cx2-ag';
const shaPattern = /^[a-f0-9]{40}$/;
const number = value => Number.isSafeInteger(value) && value > 0;
const reply = (status, body) => Response.json(body, { status, headers: { 'cache-control': 'no-store' } });

export async function verify(secret, bytes, signature) {
  if (!secret || !/^sha256=[a-f0-9]{64}$/.test(signature ?? '')) return false;
  const key = await crypto.subtle.importKey('raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']);
  const digest = Uint8Array.from(signature.slice(7).match(/../g), pair => parseInt(pair, 16));
  return crypto.subtle.verify('HMAC', key, digest, bytes);
}

async function authorized(request, token) {
  if (!token) return false;
  const actual = await crypto.subtle.digest('SHA-256', encoder.encode(request.headers.get('authorization') ?? ''));
  const expected = await crypto.subtle.digest('SHA-256', encoder.encode(`Bearer ${token}`));
  let difference = 0;
  const bytes = new Uint8Array(actual);
  new Uint8Array(expected).forEach((byte, index) => { difference |= byte ^ bytes[index]; });
  return difference === 0;
}

export function normalize(event, body) {
  if (body?.repository?.full_name !== repository) return null;
  if (event === 'workflow_run') {
    const run = body.workflow_run;
    const workflow = ['check', 'agent-review-request', 'agent-review', 'agent-review-feedback', 'agent-review-discussion'].find(name => run?.path === `.github/workflows/${name}.yml`);
    if (body.action !== 'completed' || !workflow) return null;
    if (!shaPattern.test(run.head_sha) || !number(run.id)) return null;
    if (!['success', 'failure', 'cancelled', 'timed_out', 'action_required', 'neutral', 'skipped', 'stale', 'startup_failure'].includes(run.conclusion)) return null;
    if (workflow === 'agent-review-discussion' && ['success', 'neutral', 'skipped'].includes(run.conclusion)) return null;
    const prs = [...new Set((run.pull_requests ?? []).map(pr => pr.number).filter(number))];
    if (!prs.length) return null;
    return { repository, event, action: body.action, prs, sha: run.head_sha, workflow, conclusion: run.conclusion, run: run.id };
  }
  const pr = body.pull_request;
  if (!number(pr?.number) || !shaPattern.test(pr?.head?.sha)) return null;
  if (event === 'pull_request_review_comment' && body.action === 'created') {
    const comment = body.comment;
    const question = /^<!-- cx2-review-answer:([1-9][0-9]*) -->\r?\n/.exec(comment?.body ?? '');
    if (!question || !number(Number(question[1])) || !number(comment.id) || !number(comment.in_reply_to_id)) return null;
    if (comment.user?.type !== 'Bot' || !/^cx2-ag-(codex|claude)-review-[a-z]+\[bot\]$/.test(comment.user.login) || !number(comment.user.id)) return null;
    if (body.sender?.id !== comment.user.id || body.sender.login !== comment.user.login || body.sender.type !== 'Bot') return null;
    return { repository, event, action: body.action, prs: [pr.number], sha: pr.head.sha, comment: comment.id, question: Number(question[1]), reviewer: comment.user.login };
  }
  if (event === 'pull_request_review' && ['submitted', 'dismissed'].includes(body.action)) {
    const state = body.review?.state?.toLowerCase();
    if (!['approved', 'changes_requested', 'dismissed'].includes(state)) return null;
    return { repository, event, action: body.action, prs: [pr.number], sha: pr.head.sha, review: state };
  }
  if (event === 'pull_request' && ['closed', 'synchronize'].includes(body.action)) {
    return { repository, event, action: body.action, prs: [pr.number], sha: pr.head.sha, merged: pr.merged === true };
  }
  return null;
}

async function bodyBytes(request) {
  const reader = request.body?.getReader();
  if (!reader) return new Uint8Array();
  const chunks = [];
  let length = 0;
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    length += value.length;
    if (length > 1048576) { await reader.cancel(); return null; }
    chunks.push(value);
  }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return bytes;
}

export default {
  async fetch(request, env) {
    try {
      const url = new URL(request.url);
      if (url.pathname === '/health' && request.method === 'GET') return reply(200, { status: 'ok' });
      if (url.pathname === '/github' && request.method === 'POST') {
        const bytes = await bodyBytes(request);
        if (!bytes) return reply(413, { error: 'Delivery too large' });
        if (!await verify(env.WEBHOOK_SECRET, bytes, request.headers.get('x-hub-signature-256'))) return reply(401, { error: 'Invalid signature' });
        const delivery = request.headers.get('x-github-delivery');
        if (!/^[a-f0-9-]{36}$/.test(delivery ?? '')) return reply(400, { error: 'Invalid delivery ID' });
        let body;
        try { body = JSON.parse(new TextDecoder().decode(bytes)); } catch { return reply(400, { error: 'Invalid JSON' }); }
        const metadata = normalize(request.headers.get('x-github-event'), body);
        if (!metadata) return reply(202, { accepted: false });
        await env.DB.prepare('INSERT INTO events (delivery, received, metadata) VALUES (?, ?, ?) ON CONFLICT(delivery) DO NOTHING')
          .bind(delivery, Date.now(), JSON.stringify(metadata)).run();
        return reply(202, { accepted: true });
      }
      if (url.pathname === '/events' && request.method === 'GET') {
        if (!await authorized(request, env.BRIDGE_TOKEN)) return reply(401, { error: 'Unauthorized' });
        const after = url.searchParams.get('after') ?? '0';
        if (!/^\d{1,15}$/.test(after)) return reply(400, { error: 'Invalid cursor' });
        const wait = url.searchParams.get('wait') === '0' ? 0 : 25000;
        const deadline = Date.now() + wait;
        do {
          const rows = await env.DB.prepare('SELECT id, delivery, received, metadata FROM events WHERE id > ? ORDER BY id LIMIT 100').bind(Number(after)).all();
          if (rows.results.length || Date.now() >= deadline) return reply(200, { events: rows.results.map(row => ({ ...row, metadata: JSON.parse(row.metadata) })) });
          await new Promise(resolve => setTimeout(resolve, 2000));
        } while (!request.signal.aborted);
        return reply(200, { events: [] });
      }
      return reply(404, { error: 'Not found' });
    } catch {
      return reply(503, { error: 'Event storage unavailable' });
    }
  }
};
