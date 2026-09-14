require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
classification = workflow.fetch('concurrency').fetch('group').delete_prefix('agent-review-${{ github.event.pull_request.number }}-')
raise 'Review run names must identify requested and metadata runs by PR' unless workflow['run-name'] == 'Review PR #${{ github.event.pull_request.number }} (' + classification + ')'
request = YAML.load_file(File.join(root, '.github/workflows/agent-review-request.yml'))
selection = request.fetch('jobs').fetch('assign').fetch('steps').find { |step| step['id'] == 'reviewer' }.fetch('run')
raise 'Only assignment labels can trigger review execution' unless (workflow['on'] || workflow[true]).fetch('pull_request').fetch('types') == ['labeled'] && workflow.fetch('jobs').fetch('select-reviewer').fetch('if').include?("startsWith(github.event.label.name, 'reviewer:')")
raise 'Review selection must be read-only' unless workflow.fetch('jobs').fetch('select-reviewer').fetch('permissions').values.all? { |value| value == 'read' }
raise 'Assignment labels must not loop into another assignment' unless request.fetch('jobs').fetch('assign').fetch('if').include?("contains(fromJSON('[\"claude-review\", \"codex-review\", \"gemini-review\"]'), github.event.label.name)")
raise 'Assignment must not run either model' if request.fetch('jobs').values.flat_map { |job| job.fetch('steps') }.any? { |step| step.fetch('uses', '').include?('claude-code-action') || step.fetch('run', '').include?('codex exec') }
%w[agent agent-codex agent-gemini].each do |name|
  worker = YAML.load_file(File.join(root, ".github/workflows/#{name}.yml"))
  if ['agent-codex', 'agent', 'agent-gemini'].include?(name)
    provider = name == 'agent' ? 'claude' : name.delete_prefix('agent-')
    response = worker.fetch('jobs').fetch('respond')
    expected = "agent-#{provider}-" + '${{ needs.resolve.outputs.branch || github.run_id }}'
    raise 'Issue and PR aliases must share their branch queue' unless response.fetch('concurrency') == { 'group' => expected, 'cancel-in-progress' => false, 'queue' => 'max' }
    raise 'Resolve and allocate ownership before starting a worker' unless response.fetch('needs').sort == ['allocate', 'resolve']
    resolver = worker.fetch('jobs').fetch('resolve')
    raise 'Ownership resolution must be read-only' unless resolver.fetch('permissions').values.all? { |value| value == 'read' }
    step = resolver.fetch('steps').find { |entry| entry['id'] == 'target' }
    raise 'Execute the ownership resolver' unless step.fetch('run').include?('node .github/scripts/prepare-codex-worker.mjs --resolve')
    raise 'Publish the resolved branch queue' unless resolver.fetch('outputs').fetch('branch') == '${{ steps.target.outputs.branch }}'
    claim = response.fetch('steps').find { |entry| entry['id'] == 'worker' }
    raise 'Recheck the branch after entering its queue' unless claim.fetch('env').fetch('EXPECTED_BRANCH') == '${{ needs.resolve.outputs.branch }}'
  else
    expected = "#{name}-" + '${{ github.event.issue.number || github.event.pull_request.number || github.run_id }}'
    raise "#{name}: work must be isolated by issue or PR" unless worker.fetch('concurrency') == { 'group' => expected, 'cancel-in-progress' => false }
  end
end
fixtures = [
  ['opened', 'gemini/issue-42', [], nil, 'gemini'],
  ['ready_for_review', 'gemini/issue-42', ['gemini-review'], nil, 'gemini'],
  ['labeled', 'gemini/issue-42', ['gemini-review'], 'gemini-review', 'gemini'],
  ['opened', 'gemini/issue-42', ['gemini-review', 'codex-review'], nil, nil],
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
  environment = { 'PATH' => ENV.fetch('PATH'), 'GITHUB_EVENT_PATH' => event_file, 'GITHUB_OUTPUT' => output_file,
                  'CODEX_MODEL' => 'gpt-fixture', 'CODEX_EFFORT' => 'high', 'CLAUDE_MODEL' => 'opus', 'CLAUDE_EFFORT' => 'high', 'GEMINI_MODEL' => 'gemini-3.8-flash-high', 'GEMINI_EFFORT' => 'high' }
  _stdout, _stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', selection, unsetenv_others: true)
  if expected
    requested = action != 'labeled' || ['claude-review', 'codex-review', 'gemini-review'].include?(requested_label)
    model = { 'codex' => 'gpt-fixture', 'claude' => 'opus', 'gemini' => 'gemini-3.8-flash-high' }.fetch(expected)
    raise "case #{index}: wrong reviewer or review request" unless status.success? && File.read(output_file) == "reviewer=#{expected}\nrequested=#{requested}\nmodel=#{model}\neffort=high\n"
  else
    raise "case #{index}: invalid request accepted" if status.success? || !File.read(output_file).empty?
  end
end
puts "#{fixtures.length} reviewer routing cases passed"
