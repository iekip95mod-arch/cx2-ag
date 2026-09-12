require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
classification = workflow.fetch('concurrency').fetch('group').delete_prefix('agent-review-${{ github.event.pull_request.number }}-')
raise 'Review run names must identify requested and metadata runs by PR' unless workflow['run-name'] == 'Review PR #${{ github.event.pull_request.number }} (' + classification + ')'
selection = workflow.fetch('jobs').fetch('select-reviewer').fetch('steps').find { |step| step['id'] == 'reviewer' }.fetch('run')
%w[agent agent-codex agent-gemini].each do |name|
  worker = YAML.load_file(File.join(root, ".github/workflows/#{name}.yml"))
  expected = "#{name}-" + '${{ github.event.issue.number || github.event.pull_request.number || github.run_id }}'
  raise "#{name}: work must be isolated by issue or PR" unless worker.fetch('concurrency') == { 'group' => expected, 'cancel-in-progress' => false }
end
fixtures = [
  ['opened', 'codex/change', [], nil, 'codex'],
  ['opened', 'agent/change', [], nil, 'claude'],
  ['ready_for_review', 'codex/change', [], nil, 'codex'],
  ['opened', 'codex/change', ['claude-review'], nil, 'claude'],
  ['opened', 'agent/change', ['codex-review'], nil, 'codex'],
  ['labeled', 'codex/change', ['claude-review', 'codex-review'], 'claude-review', 'claude'],
  ['labeled', 'agent/change', ['claude-review', 'codex-review'], 'codex-review', 'codex'],
  ['opened', 'codex/change', ['claude-review', 'codex-review'], nil, nil],
  ['labeled', 'codex/change', [], 'unrelated', 'codex'],
  ['labeled', 'agent/change', ['codex-review'], 'tooling', 'codex'],
  ['labeled', 'codex/change', ['claude-review'], 'tooling', 'claude'],
  ['labeled', 'codex/change', ['claude-review', 'codex-review'], 'tooling', nil]
]
workspace = File.join(root, '.Internal/workspaces/review-routing-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)
fixtures.each_with_index do |(action, branch, labels, requested_label, expected), index|
  directory = File.join(run_directory, index.to_s)
  FileUtils.mkdir_p(directory)
  event_file = File.join(directory, 'event.json')
  output_file = File.join(directory, 'output')
  File.write(event_file, { action: action, label: { name: requested_label }, pull_request: { head: { ref: branch }, labels: labels.map { |name| { name: name } } } }.to_json)
  File.write(output_file, '')
  environment = { 'PATH' => ENV.fetch('PATH'), 'GITHUB_EVENT_PATH' => event_file, 'GITHUB_OUTPUT' => output_file }
  _stdout, _stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', selection, unsetenv_others: true)
  if expected
    requested = action != 'labeled' || ['claude-review', 'codex-review'].include?(requested_label)
    raise "case #{index}: wrong reviewer or review request" unless status.success? && File.read(output_file) == "reviewer=#{expected}\nrequested=#{requested}\n"
  else
    raise "case #{index}: invalid request accepted" if status.success? || !File.read(output_file).empty?
  end
end
puts "#{fixtures.length} reviewer routing cases passed"
