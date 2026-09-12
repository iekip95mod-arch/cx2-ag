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
raise 'Issue work must use the scoped publishing token' unless claim.fetch('env').fetch('GH_TOKEN') == '${{ secrets.CODEX_GITHUB_TOKEN }}'
raise 'Issue work must claim its branch before running Codex' unless claim.fetch('run') == 'node .github/scripts/prepare-codex-worker.mjs'
execution = worker.fetch('steps').find { |step| step['name'] == 'Run Codex with saved login' }
raise 'Codex must inherit its publishing credential' unless execution.fetch('run').include?('shell_environment_policy.ignore_default_excludes=true')
raise 'Codex must run inside its assigned checkout' unless execution.fetch('run').include?('--cd "$WORKER_DIRECTORY"')
raise 'General responses must not receive publishing credentials' unless execution.fetch('env').fetch('GH_TOKEN') == "${{ steps.worker.outputs.issue && secrets.CODEX_GITHUB_TOKEN || '' }}"
raise 'Default Actions token must remain read-only for contents' unless worker.fetch('permissions').fetch('contents') == 'read'
puts '6 worker credential contracts passed'
