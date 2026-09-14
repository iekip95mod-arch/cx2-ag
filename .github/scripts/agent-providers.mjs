export const providers = ['codex', 'claude', 'gemini'];

export function workerWorkflow(provider) {
  if (!providers.includes(provider)) throw Error('Unknown executor provider');
  return provider === 'claude' ? 'agent.yml' : `agent-${provider}.yml`;
}
