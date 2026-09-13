const phases = ['queued', 'preparing', 'running', 'publishing', 'succeeded', 'failed', 'cancelled'];

function modelDescription(model) {
  return /^(opus|sonnet|haiku|fable|opusplan|default)(\[1m\])?$/.test(model)
    ? `${model} (configured alias, resolved model unverified)`
    : model;
}

function workflowCoordinates(body) {
  const match = body.match(/Workflow: https:\/\/github\.com\/iekip95mod-arch\/cx2-ag\/actions\/runs\/([1-9][0-9]*)\/attempts\/([1-9][0-9]*)/);
  if (!match) throw Error('Invalid existing progress workflow');
  return { run: BigInt(match[1]), attempt: BigInt(match[2]) };
}

function progressBody({ role, login, model, effort, repository, run, attempt, phase, detail, previous = '' }) {
  const sameAttempt = previous.includes(`Workflow: https://github.com/${repository}/actions/runs/${run}/attempts/${attempt}`);
  const executing = ['running', 'publishing', 'succeeded'].includes(phase) || sameAttempt && previous.includes('- [x] Agent execution started');
  const publishing = ['publishing', 'succeeded'].includes(phase) || sameAttempt && previous.includes('- [x] Publishing result');
  const terminal = ['succeeded', 'failed', 'cancelled'].includes(phase);
  const label = phase === 'succeeded' ? 'Completed' : phase[0].toUpperCase() + phase.slice(1);
  return `<!-- cx2-agent-progress:${role}:${login} -->
### ${role === 'executor' ? 'Executor' : 'Reviewer'} progress

- [x] Queued
- [${executing ? 'x' : ' '}] Agent execution started
- [${publishing ? 'x' : ' '}] Publishing result
- [${terminal && phase === 'succeeded' ? 'x' : ' '}] Finished successfully

Status: **${label}**

${detail}

Identity: ${login}
Configured model: ${modelDescription(model)}
Configured effort: ${effort}
Workflow: https://github.com/${repository}/actions/runs/${run}/attempts/${attempt}`;
}

export async function publishProgress(options, api) {
  const { repository, number, role, login, userId, model, effort, run, attempt = 1, phase, detail } = options;
  if (repository !== 'iekip95mod-arch/cx2-ag' || !Number.isSafeInteger(number) || number < 1) throw Error('Invalid progress target');
  if (!['executor', 'reviewer'].includes(role) || !/^[a-z0-9-]+\[bot\]$/.test(login) || !Number.isSafeInteger(userId) || userId < 1) throw Error('Invalid progress identity');
  if (!/^[A-Za-z0-9._:[\]/-]+$/.test(model) || !/^[a-z]+$/.test(effort) || !/^[1-9][0-9]*$/.test(String(run)) || !/^[1-9][0-9]*$/.test(String(attempt)) || !phases.includes(phase)) throw Error('Invalid progress state');
  if (typeof detail !== 'string' || !detail.trim() || detail.includes('@')) throw Error('Progress detail must be controlled text');
  const marker = `<!-- cx2-agent-progress:${role}:${login} -->`;
  const matching = [];
  for (let page = 1; page <= 10; page++) {
    const comments = await api('GET', `repos/${repository}/issues/${number}/comments?per_page=100&page=${page}`);
    if (!Array.isArray(comments)) throw Error('Invalid progress comment history');
    matching.push(...comments.filter(comment => comment.body?.includes(marker) && comment.user?.type === 'Bot' && comment.user.login === login && comment.user.id === userId));
    if (comments.length < 100) break;
    if (page === 10) throw Error('Progress comment history exceeds the lookup limit');
  }
  if (matching.length > 1) throw Error('Multiple progress comments exist for this identity and role');
  if (!matching[0] && options.updateOnly) return false;
  if (matching[0]) {
    const previous = workflowCoordinates(matching[0].body);
    const stale = previous.run > BigInt(run) || previous.run === BigInt(run) && previous.attempt > BigInt(attempt);
    if (stale) return matching[0];
  }
  const body = progressBody({ ...options, previous: matching[0]?.body });
  if (matching[0]?.body === body) return matching[0];
  if (matching[0]) return api('PATCH', `repos/${repository}/issues/comments/${matching[0].id}`, { body });
  return api('POST', `repos/${repository}/issues/${number}/comments`, { body });
}
