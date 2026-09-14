import { mkdirSync, writeFileSync } from 'node:fs';
import { join, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';

export function githubHostedGemini(environment = process.env, platform = process.platform) {
  return platform === 'linux' && environment.GITHUB_ACTIONS === 'true' && environment.RUNNER_ENVIRONMENT === 'github-hosted';
}

export function geminiSettings({ mode, home, workspace, temporary, sandbox = true }) {
  if (!['executor', 'reviewer', 'reply'].includes(mode) || ![home, workspace, temporary].every(value => typeof value === 'string' && isAbsolute(value))) throw Error('Invalid Gemini permission scope');
  const allow = [`read_file(${workspace})`, `read_file(${temporary})`];
  const deny = ['execute_url(*)', 'mcp(*)', `read_file(${join(home, '.gemini')})`];
  if (mode === 'reply') deny.push('command(*)', 'write_file(*)', 'read_url(*)');
  else {
    allow.push('command(*)', `write_file(${workspace})`, `write_file(${temporary})`);
    if (mode === 'executor') allow.push('read_url(github.com)', 'read_url(api.github.com)');
    else deny.push('read_url(*)');
  }
  return { enableTerminalSandbox: mode !== 'reply' && sandbox, toolPermission: sandbox ? 'proceed-in-sandbox' : 'request-review', artifactReviewPolicy: 'always-proceed', permissions: { allow, deny } };
}

export function prepareGemini({ credentials, mode, home, workspace, temporary, sandbox = true }) {
  if (!credentials?.trim()) throw Error('ANTIGRAVITY_OAUTH_CREDS is required. No subscription login is configured');
  JSON.parse(credentials);
  const settings = geminiSettings({ mode, home, workspace, temporary, sandbox });
  const directory = join(home, '.gemini/antigravity-cli');
  mkdirSync(directory, { recursive: true, mode: 0o700 });
  writeFileSync(join(directory, 'antigravity-oauth-token'), credentials, { mode: 0o600 });
  writeFileSync(join(directory, 'settings.json'), JSON.stringify(settings), { mode: 0o600 });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try {
    prepareGemini({ credentials: process.env.OAUTH_CREDS, mode: process.env.GEMINI_MODE, home: process.env.HOME, workspace: process.env.GITHUB_WORKSPACE, temporary: process.env.RUNNER_TEMP, sandbox: !githubHostedGemini() });
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
