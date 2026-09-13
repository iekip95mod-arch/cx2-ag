require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-codex.yml'))
credential_step = workflow.fetch('jobs').fetch('respond').fetch('steps').find { |step| step['id'] == 'key' }
raise 'Missing credential step' unless credential_step

fixtures = [
  ['subscription', { tokens: { access_token: 'subscription-fixture' }, OPENAI_API_KEY: nil }.to_json, :accept],
  ['subscription-only', { tokens: { access_token: 'subscription-fixture' } }.to_json, :accept],
  ['missing', '', :skip],
  ['api-only', { OPENAI_API_KEY: 'api-fixture' }.to_json, :reject],
  ['mixed', { tokens: { access_token: 'subscription-fixture' }, OPENAI_API_KEY: 'api-fixture' }.to_json, :reject],
  ['empty-token', { tokens: { access_token: '' } }.to_json, :reject],
  ['empty-object', '{}', :reject],
  ['null', 'null', :reject],
  ['malformed', 'malformed-fixture', :reject]
]

workspace = File.join(root, '.Internal/workspaces/codex-auth-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)
fixtures.each do |name, credential, expectation|
  directory = File.join(run_directory, name)
  FileUtils.mkdir_p(directory)
  output_file = File.join(directory, 'output')
  summary_file = File.join(directory, 'summary')
  File.write(output_file, '')
  File.write(summary_file, '')
  environment = {
    'PATH' => ENV.fetch('PATH'),
    'HOME' => directory,
    'AUTH_JSON' => credential,
    'RUNNER_TEMP' => directory,
    'GITHUB_OUTPUT' => output_file,
    'GITHUB_STEP_SUMMARY' => summary_file
  }
  stdout, stderr, status = Open3.capture3(environment, 'bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', credential_step.fetch('run'), unsetenv_others: true, chdir: root)
  outputs = File.read(output_file)
  summary = File.read(summary_file)
  auth_file = File.join(directory, 'codex-home/auth.json')
  case expectation
  when :accept
    raise "#{name}: login not accepted" unless status.success? && outputs.lines.include?("have=true\n")
    raise "#{name}: wrong credential home" unless outputs.lines.include?("home=#{File.dirname(auth_file)}\n")
    raise "#{name}: credential bytes changed" unless File.binread(auth_file) == credential
    raise "#{name}: credential permissions too broad" unless (File.stat(auth_file).mode & 0o777) == 0o600
  when :skip
    raise "#{name}: missing login did not skip cleanly" unless status.success? && outputs.lines.include?("have=false\n") && summary.include?('did not run')
    raise "#{name}: credentials unexpectedly written" if File.exist?(auth_file)
  when :reject
    raise "#{name}: invalid login accepted" if status.success? || outputs.include?('have=true') || File.exist?(auth_file)
    raise "#{name}: missing diagnostic" unless (stdout + stderr).include?('::error::')
  end
  if (stdout + stderr + outputs + summary).match?(/subscription-fixture|api-fixture|malformed-fixture/)
    raise "#{name}: credentials leaked into logs or outputs"
  end
end
puts "#{fixtures.length} credential cases passed"

worker = workflow.fetch('jobs').fetch('respond')
claim = worker.fetch('steps').find { |step| step['id'] == 'worker' }
raise 'Issue work must use the scoped publishing token' unless claim.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot-token.outputs.token }}'
raise 'Issue work must claim its branch before running Codex' unless claim.fetch('run') == 'node .github/scripts/prepare-codex-worker.mjs'
execution = worker.fetch('steps').find { |step| step['name'] == 'Run Codex with saved login' }
raise 'Codex must stop superseded issue work before model execution' unless execution.fetch('if').include?("steps.start.outputs.proceed == 'true'")
%w[agent-codex.yml agent.yml].each do |file|
  steps = YAML.load_file(File.join(root, '.github/workflows', file)).fetch('jobs').fetch('respond').fetch('steps')
  startup = steps.find { |step| step['id'] == 'start' }
  raise 'Both workers must validate and acknowledge before execution' unless startup && startup.fetch('run').include?('worker-start.mjs')
  publication = steps.find { |step| step['name'] == 'Ensure the first commit has a linked draft PR' }
  raise 'Stale work must not publish from final cleanup' unless publication.fetch('if').include?("steps.start.outputs.proceed == 'true'")
  model = steps.find { |step| step['name'] == (file == 'agent.yml' ? 'Run the agent' : 'Run Codex with saved login') }
  raise 'Both workers must honor startup validation' unless model.fetch('if').include?("steps.start.outputs.proceed == 'true'")
end
raise 'Codex must inherit its publishing credential' unless execution.fetch('run').include?('shell_environment_policy.ignore_default_excludes=true')
raise 'Codex must run inside its assigned checkout' unless execution.fetch('run').include?('--cd "$WORKER_DIRECTORY"')
raise 'General responses must not receive publishing credentials' unless execution.fetch('env').fetch('GH_TOKEN') == "${{ steps.worker.outputs.issue && steps.bot-token.outputs.token || '' }}"
raise 'Default Actions token must remain read-only for contents' unless worker.fetch('permissions').fetch('contents') == 'read'
%w[agent-codex.yml agent.yml].each do |name|
  executor = YAML.load_file(File.join(root, '.github/workflows', name))
  jobs = executor.fetch('jobs')
  allocator = jobs.fetch('allocate')
  raise "#{name}: lease allocation needs isolated contents write" unless allocator.fetch('permissions').fetch('contents') == 'write'
  raise "#{name}: default token reaches the model job with contents write" unless jobs.fetch('respond').fetch('permissions').fetch('contents') == 'read'
  steps = jobs.fetch('respond').fetch('steps')
  mint = steps.find { |step| step['id'] == 'bot-token' }
  raise "#{name}: token must be limited to this repository" unless mint.fetch('with').fetch('repositories') == '${{ github.event.repository.name }}'
  raise "#{name}: wrong private key source" unless mint.fetch('with').fetch('private-key').include?('needs.allocate.outputs.secret_name == ') && !mint.fetch('with').fetch('private-key').include?('secrets[')
  bootstrap = steps.find { |step| step['id'] == 'worker' }
  raise "#{name}: bootstrap must validate minted App slug" unless bootstrap.fetch('env').fetch('BOT_APP_SLUG') == '${{ steps.bot-token.outputs.app-slug }}'
  raise "#{name}: publication recovery is missing" unless steps.any? { |step| step['run'].to_s.include?('publish-worker-pr.mjs') }
end
claude = YAML.load_file(File.join(root, '.github/workflows/agent.yml'))
claude_step = claude.fetch('jobs').fetch('respond').fetch('steps').find { |step| step['name'] == 'Run the agent' }
raise 'Claude must publish using its leased App' unless claude_step.fetch('with').fetch('github_token') == '${{ steps.bot-token.outputs.token }}'
raise 'Claude must use its named bot commit identity' unless claude_step.fetch('with').fetch('bot_name') == '${{ needs.allocate.outputs.login }}' && claude_step.fetch('with').fetch('bot_id') == '${{ needs.allocate.outputs.user_id }}'
raise 'Claude must use subscription OAuth without API billing' if File.read(File.join(root, '.github/workflows/agent.yml')).include?('ANTHROPIC_API_KEY')
raise 'Claude comments must name Claude explicitly' unless claude.fetch('jobs').fetch('resolve').fetch('if').include?("contains(github.event.comment.body, '@claude')")
raise 'Claude must allow only the internal Actions bot on dispatch' unless claude_step.fetch('with').fetch('allowed_bots') == "${{ github.event_name == 'workflow_dispatch' && 'github-actions[bot]' || '' }}"
feedback = YAML.load_file(File.join(root, '.github/workflows/agent-review-feedback.yml'))
raise 'Feedback must subscribe to submitted reviews and completed CI' unless feedback.fetch(true).keys.sort == %w[pull_request_review workflow_run] && feedback.fetch(true).fetch('pull_request_review') == { 'types' => ['submitted'] } && feedback.fetch(true).fetch('workflow_run').fetch('types') == ['completed']
feedback_job = feedback.fetch('jobs').fetch('continue-executor')
raise 'Feedback must be able to dispatch workflows' unless feedback_job.fetch('permissions') == { 'contents' => 'read', 'issues' => 'read', 'pull-requests' => 'read', 'actions' => 'write' }
feedback_checkout = feedback_job.fetch('steps').find { |step| step['uses'].to_s.start_with?('actions/checkout@') }
raise 'Feedback must run trusted main code without checkout credentials' unless feedback_checkout.fetch('with') == { 'ref' => 'main', 'persist-credentials' => false }
feedback_dispatch = feedback_job.fetch('steps').find { |step| step['name'] == 'Dispatch the assigned executor' }
raise 'Feedback must execute the validated dispatcher' unless feedback_dispatch.fetch('run') == 'node .github/scripts/review-feedback.mjs' && feedback_dispatch.fetch('env').fetch('GH_TOKEN') == '${{ secrets.GITHUB_TOKEN }}'
general = claude.fetch('jobs').fetch('respond').fetch('steps').find { |step| step['name'] == 'Answer an unassigned request' }
raise 'Claude task-only requests must execute rather than skip' unless general && general.fetch('run').include?('claude -p')
raise 'Claude task-only requests must not receive publishing credentials' unless general.fetch('env').fetch('GH_TOKEN') == ''
raise 'Claude installs its CLI for task-only requests' unless claude.fetch('jobs').fetch('respond').fetch('steps').find { |step| step['name'] == 'Install Claude CLI' }.fetch('if') == "steps.key.outputs.have == 'true'"
general_directory = Dir.mktmpdir('general-', run_directory)
general_bin = File.join(general_directory, 'bin')
FileUtils.mkdir_p(general_bin)
File.write(File.join(general_bin, 'claude'), "#!/usr/bin/env ruby\nrequire 'json'\nFile.write(ENV.fetch('CAPTURE'), JSON.generate({args: ARGV, task: STDIN.read}))\nputs 'General response fixture'\n")
FileUtils.chmod(0o755, File.join(general_bin, 'claude'))
general_env = {
  'PATH' => "#{general_bin}:#{ENV.fetch('PATH')}", 'RUNNER_TEMP' => general_directory,
  'GITHUB_STEP_SUMMARY' => File.join(general_directory, 'summary'), 'CAPTURE' => File.join(general_directory, 'capture'),
  'WORKER_MODEL' => 'opus', 'WORKER_EFFORT' => 'high', 'TASK' => "Explain the repository's tests", 'GH_TOKEN' => '',
  'AGENT_TIME_BUDGET' => 'Finish before the supplied UTC cutoff'
}
stdout, stderr, status = Open3.capture3(general_env, 'bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', general.fetch('run'), unsetenv_others: true, chdir: root)
raise "Claude task-only execution failed: #{stderr}" unless status.success?
captured = JSON.parse(File.read(general_env.fetch('CAPTURE')))
raise 'Claude task-only deadline or request changed' unless captured.fetch('task') == general_env.fetch('AGENT_TIME_BUDGET') + "\n\n" + general_env.fetch('TASK') + "\n"
raise 'Claude task-only model or effort differs from disclosure' unless captured.fetch('args')[0, 5] == ['-p', '--model', 'opus', '--effort', 'high']
raise 'Claude task-only response was dropped' unless File.read(general_env.fetch('GITHUB_STEP_SUMMARY')).include?('General response fixture')
raise 'Codex model and effort must be passed explicitly' unless execution.fetch('run').include?('--model "$WORKER_MODEL"') && execution.fetch('run').include?('model_reasoning_effort=')
raise 'Claude model and effort must be passed explicitly' unless claude_step.fetch('with').fetch('claude_args').include?('--model ${{ env.WORKER_MODEL }}') && claude_step.fetch('with').fetch('claude_args').include?('--effort ${{ env.WORKER_EFFORT }}')
installed = feedback_job.fetch('steps').find { |step| step['id'] == 'installed' }
raise 'Feedback dispatch must skip until trusted main contains its helper' unless feedback_dispatch.fetch('if') == "steps.installed.outputs.ready == 'true'"
bootstrap_directory = Dir.mktmpdir('bootstrap-', run_directory)
bootstrap_env = { 'PATH' => ENV.fetch('PATH'), 'GITHUB_OUTPUT' => File.join(bootstrap_directory, 'outputs'), 'GITHUB_STEP_SUMMARY' => File.join(bootstrap_directory, 'summary') }
stdout, stderr, status = Open3.capture3(bootstrap_env, 'bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', installed.fetch('run'), unsetenv_others: true, chdir: bootstrap_directory)
raise "Bootstrap skip failed: #{stderr}" unless status.success? && File.read(bootstrap_env.fetch('GITHUB_OUTPUT')) == "ready=false\n"
puts 'Worker model, task-only execution and feedback bootstrap contracts passed'
