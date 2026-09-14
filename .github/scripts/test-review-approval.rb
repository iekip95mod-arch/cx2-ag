require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
job = workflow.fetch('jobs').fetch('review-approved')
raise 'Approval gate must run for failed assignment reviews only' unless job.fetch('if') == "${{ always() && startsWith(github.event.label.name, 'reviewer:') }}"
raise 'Approval gate must wait for selection and all reviewers' unless job.fetch('needs').sort == ['codex-review', 'gemini-review', 'review', 'select-reviewer']
gate_step = job.fetch('steps').find { |step| step['name'] == 'Require a fresh approval from the selected reviewer' }
gate = gate_step.fetch('run')
raise 'Approval gate must execute the shared lease verifier' unless gate == 'node .github/scripts/wait-for-review.mjs --approval-gate'
raise 'Approval lookup needs the durable lease' unless job.fetch('permissions').fetch('contents') == 'read'
%w[review codex-review gemini-review].each do |name|
  raise 'Ordinary labels must not spend a review' unless workflow.fetch('jobs').fetch(name).fetch('if').include?("needs.select-reviewer.outputs.requested == 'true'")
end
raise 'Ordinary labels must not cancel requested reviews' unless workflow.fetch('concurrency').fetch('group').include?("startsWith(github.event.label.name, 'reviewer:') && 'requested' || 'metadata'")
raise 'Review cancellation must be isolated by PR' unless workflow.fetch('concurrency').fetch('group').include?('${{ github.event.pull_request.number }}')
roster = JSON.parse(File.read(File.join(root, '.github/scripts/bot-identities.json')))
roster.each_with_index { |identity, index| identity.merge!('appId' => index + 100, 'userId' => index + 200, 'clientId' => "Iv1.fixture#{index}") }
codex = roster.find { |identity| identity['provider'] == 'codex' && identity['role'] == 'reviewer' }
claude = roster.find { |identity| identity['provider'] == 'claude' && identity['role'] == 'reviewer' }
gemini = roster.find { |identity| identity['provider'] == 'gemini' && identity['role'] == 'reviewer' }
reviewed_sha = 'a' * 40
approval = { id: 2, commit_id: reviewed_sha, user: { login: codex.fetch('login'), id: codex.fetch('userId'), type: 'Bot' }, state: 'APPROVED' }
fixtures = [
  ['gemini', 'gemini', 'reviewed-sha', [approval.merge(user: { login: gemini.fetch('login'), id: gemini.fetch('userId'), type: 'Bot' })], 'success', true],
  ['gemini-failed', 'gemini', 'reviewed-sha', [approval.merge(user: { login: gemini.fetch('login'), id: gemini.fetch('userId'), type: 'Bot' })], 'failure', false],
  ['codex', 'codex', 'reviewed-sha', [approval], 'success', true],
  ['linked-branch', 'codex', 'reviewed-sha', [approval], 'success', true],
  ['claude', 'claude', 'reviewed-sha', [approval.merge(user: { login: claude.fetch('login'), id: claude.fetch('userId'), type: 'Bot' })], 'success', true],
  ['wrong-provider', 'claude', 'reviewed-sha', [approval], 'success', false],
  ['no-review', 'codex', 'reviewed-sha', [], 'success', false],
  ['old-review', 'codex', 'reviewed-sha', [approval.merge(id: 1)], 'success', false],
  ['old-commit', 'codex', 'reviewed-sha', [approval.merge(commit_id: 'older-sha')], 'success', false],
  ['new-push', 'codex', 'newer-sha', [approval], 'success', false],
  ['changes-requested', 'codex', 'reviewed-sha', [approval, approval.merge(id: 3, state: 'CHANGES_REQUESTED')], 'success', false],
  ['failed-reviewer', 'codex', 'reviewed-sha', [approval], 'failure', false],
  ['legacy-bot', 'codex', 'reviewed-sha', [approval.merge(user: { login: 'github-actions[bot]', id: 41898282, type: 'Bot' })], 'success', false],
  ['wrong-bot-id', 'codex', 'reviewed-sha', [approval.merge(user: { login: codex.fetch('login'), id: 999, type: 'Bot' })], 'success', false],
  ['human-lookalike', 'codex', 'reviewed-sha', [approval.merge(user: { login: codex.fetch('login'), id: codex.fetch('userId'), type: 'User' })], 'success', false],
  ['executor-review', 'codex', 'reviewed-sha', [approval.merge(user: { login: roster.first.fetch('login'), id: roster.first.fetch('userId'), type: 'Bot' })], 'success', false],
  ['unknown-provider', 'unknown', 'reviewed-sha', [approval], 'success', false]
]
%w[failure skipped cancelled].each do |selection_status|
  fixtures << ["selection-#{selection_status}", 'codex', 'reviewed-sha', [approval], 'success', false, selection_status]
end
fixtures.concat([
  ['ordinary-label-approved', 'codex', 'reviewed-sha', [approval.merge(id: 1)], 'skipped', true, 'success', 'false'],
  ['ordinary-label-unapproved', 'codex', 'reviewed-sha', [], 'skipped', false, 'success', 'false'],
  ['ordinary-label-changes-requested', 'codex', 'reviewed-sha', [approval, approval.merge(id: 3, state: 'CHANGES_REQUESTED')], 'skipped', false, 'success', 'false'],
  ['ordinary-label-stale', 'codex', 'newer-sha', [approval], 'skipped', false, 'success', 'false'],
  ['ordinary-label-wrong-provider', 'claude', 'reviewed-sha', [approval], 'skipped', false, 'success', 'false']
])
workspace = File.join(root, '.Internal/workspaces/review-approval-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)
fixtures.each do |name, reviewer, current_sha, reviews, provider_status, expected, selection_status, requested|
  directory = File.join(run_directory, name)
  FileUtils.mkdir_p(directory)
  scripts = File.join(directory, '.github/scripts')
  FileUtils.mkdir_p(scripts)
  %w[wait-for-review.mjs agent-progress.mjs bot-identities.mjs].each { |file| FileUtils.cp(File.join(root, '.github/scripts', file), scripts) }
  File.write(File.join(scripts, 'bot-identities.json'), roster.to_json)
  identity = { 'codex' => codex, 'claude' => claude, 'gemini' => gemini }.fetch(reviewer, codex)
  branch = name == 'linked-branch' ? 'codex/named-bot-identities' : "#{reviewer}/issue-42"
  assignment = { key: "#{reviewer}/reviewer/issue-42", provider: reviewer, role: 'reviewer', issue: 42, pr: 84, branch: branch, slug: identity.fetch('slug'), released: false }
  encoded = [{ version: 1, assignments: [assignment] }.to_json].pack('m0')
  File.write(File.join(directory, 'state.json'), { sha: 'state-sha', content: encoded }.to_json)
  File.write(File.join(directory, 'pull.json'), { state: 'open', draft: false, labels: [], user: { login: 'author', id: 10, type: 'User' }, head: { sha: current_sha == 'reviewed-sha' ? reviewed_sha : 'b' * 40, ref: branch, repo: { full_name: 'iekip95mod-arch/cx2-ag' } } }.to_json)
  File.write(File.join(directory, 'graphql.json'), { data: { repository: { pullRequest: { closingIssuesReferences: { nodes: [{ number: 42, repository: { nameWithOwner: 'iekip95mod-arch/cx2-ag' } }], pageInfo: { hasNextPage: false } } } } } }.to_json)
  File.write(File.join(directory, 'reviews.json'), [reviews].to_json)
  gh = File.join(directory, 'gh')
  File.write(gh, <<~SH)
    #!/bin/bash
    set -euo pipefail
    if [ "$*" = "api repos/iekip95mod-arch/cx2-ag/pulls/84" ]; then
      cat "$FIXTURE_DIR/pull.json"
    elif [ "$*" = "api repos/iekip95mod-arch/cx2-ag/issues/42" ]; then
      printf '%s\n' '{}'
    elif [ "$*" = "api repos/iekip95mod-arch/cx2-ag/contents/assignments.json?ref=bot-assignments" ]; then
      cat "$FIXTURE_DIR/state.json"
    elif [ "$*" = "api --paginate --slurp repos/iekip95mod-arch/cx2-ag/pulls/84/reviews?per_page=100" ]; then
      cat "$FIXTURE_DIR/reviews.json"
    elif [ "$*" = "api graphql --method POST --input -" ]; then
      ruby -rjson -e 'request = JSON.parse(STDIN.read); abort unless request.fetch("query").start_with?("query(") && request.fetch("variables").fetch("number") == 84'
      cat "$FIXTURE_DIR/graphql.json"
    else
      exit 1
    fi
  SH
  File.chmod(0o700, gh)
  environment = {
    'PATH' => "#{directory}:#{ENV.fetch('PATH')}", 'FIXTURE_DIR' => directory,
    'REVIEWER' => reviewer, 'PR_HEAD_SHA' => reviewed_sha,
    'BEFORE' => '[1]', 'CLAUDE_RESULT' => provider_status, 'CODEX_RESULT' => provider_status, 'GEMINI_RESULT' => provider_status,
    'SELECT_RESULT' => selection_status || 'success',
    'REVIEW_REQUESTED' => requested || 'true',
    'GITHUB_REPOSITORY' => 'iekip95mod-arch/cx2-ag', 'PR_NUMBER' => '84', 'GITHUB_EVENT_NAME' => 'pull_request'
  }
  _stdout, stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', gate, unsetenv_others: true, chdir: directory)
  raise "#{name}: incorrect approval gate result: #{stderr}" unless status.success? == expected
end
puts "#{fixtures.length} approval gate cases passed"

executor = roster.find { |identity| identity['role'] == 'executor' }
trusted_bot = { login: executor.fetch('login'), id: executor.fetch('userId'), type: 'Bot' }
trust_fixtures = [
  ['executor', trusted_bot, 'NONE', 'iekip95mod-arch/cx2-ag', true],
  ['owner', { login: 'owner', type: 'User' }, 'OWNER', 'iekip95mod-arch/cx2-ag', true],
  ['wrong-id', trusted_bot.merge(id: 999), 'NONE', 'iekip95mod-arch/cx2-ag', false],
  ['outside-bot', trusted_bot.merge(login: 'dependabot[bot]'), 'COLLABORATOR', 'iekip95mod-arch/cx2-ag', false],
  ['outside-human', { login: 'outside', type: 'User' }, 'NONE', 'iekip95mod-arch/cx2-ag', false],
  ['fork', trusted_bot, 'NONE', 'other/repository', false]
]
trust_script = File.join(run_directory, 'codex', '.github/scripts/wait-for-review.mjs')
trust_fixtures.each do |name, user, association, repository, expected|
  event = File.join(run_directory, "trust-#{name}.json")
  File.write(event, { pull_request: { head: { repo: { full_name: repository } }, user: user, author_association: association } }.to_json)
  environment = { 'PATH' => ENV.fetch('PATH'), 'GITHUB_EVENT_PATH' => event, 'GITHUB_REPOSITORY' => 'iekip95mod-arch/cx2-ag' }
  _stdout, stderr, status = Open3.capture3(environment, 'node', trust_script, '--trust-author', unsetenv_others: true)
  raise "#{name}: incorrect author trust result: #{stderr}" unless status.success? == expected
end
puts "#{trust_fixtures.length} author trust entrypoint cases passed"

request_directory = File.join(run_directory, 'claude')
executor = roster.find { |identity| identity['role'] == 'executor' && identity['provider'] == 'claude' }
sender = { login: executor.fetch('login'), id: executor.fetch('userId'), type: 'Bot' }
leased = { key: 'claude/executor/issue-42', provider: 'claude', role: 'executor', issue: 42, pr: 84, branch: 'claude/issue-42', slug: executor.fetch('slug'), released: false }
request_fixtures = [
  ['legacy-author', sender, sender.fetch(:login), sender.fetch(:id), [leased], true],
  ['human-requester', { login: 'iekip95mod-arch', id: 10, type: 'User' }, 'iekip95mod-arch', 10, [], true],
  ['wrong-actor', sender, 'other[bot]', sender.fetch(:id), [leased], false],
  ['wrong-id', sender.merge(id: 999), sender.fetch(:login), 999, [leased], false],
  ['no-lease', sender, sender.fetch(:login), sender.fetch(:id), [], false],
  ['released-lease', sender, sender.fetch(:login), sender.fetch(:id), [leased.merge(released: true)], false],
  ['another-executor', sender, sender.fetch(:login), sender.fetch(:id), [leased.merge(slug: roster.find { |identity| identity['role'] == 'executor' && identity['provider'] == 'claude' && identity != executor }.fetch('slug'))], false]
]
request_fixtures.each do |name, requester, actor, actor_id, assignments, expected|
  state = [{ version: 1, assignments: assignments }.to_json].pack('m0')
  File.write(File.join(request_directory, 'state.json'), { sha: 'state-sha', content: state }.to_json)
  event_file = File.join(request_directory, 'request.json')
  pull = JSON.parse(File.read(File.join(request_directory, 'pull.json'))).merge('number' => 84)
  File.write(event_file, { pull_request: pull, sender: requester }.to_json)
  output_file = File.join(request_directory, 'request-output')
  File.write(output_file, '')
  environment = { 'PATH' => "#{request_directory}:#{ENV.fetch('PATH')}", 'FIXTURE_DIR' => request_directory, 'GITHUB_EVENT_NAME' => 'pull_request', 'GITHUB_REPOSITORY' => 'iekip95mod-arch/cx2-ag', 'PR_NUMBER' => '84', 'PR_HEAD_SHA' => reviewed_sha, 'GITHUB_EVENT_PATH' => event_file, 'GITHUB_OUTPUT' => output_file, 'GITHUB_ACTOR' => actor, 'GITHUB_ACTOR_ID' => actor_id.to_s }
  _stdout, stderr, status = Open3.capture3(environment, 'node', File.join(request_directory, '.github/scripts/wait-for-review.mjs'), '--trust-requester', unsetenv_others: true)
  raise "#{name}: incorrect requester result: #{stderr}" unless status.success? == expected
  if expected
    allowed = File.read(output_file).delete_prefix('allowed_bots=').strip
    if requester.fetch(:type) == 'Bot'
      raise 'A named executor must pass the pinned action actor check' unless allowed.split(',').map { |login| login.strip.downcase.delete_suffix('[bot]') }.include?(actor.downcase.delete_suffix('[bot]'))
      raise 'Only the requesting executor may be allowed' unless allowed == sender.fetch(:login)
    else
      raise 'Human requests must not allow any bot' unless allowed.empty?
    end
  else
    raise 'Rejected requesters must not publish an allowlist' unless File.read(output_file).empty?
  end
end
puts "#{request_fixtures.length} requester lease entrypoint cases passed"

reviewer_label = "reviewer:#{claude.fetch('slug')}"
assigned_sender = { login: claude.fetch('login'), id: claude.fetch('userId'), type: 'Bot' }
reviewer_lease = { key: 'claude/reviewer/issue-42', provider: 'claude', role: 'reviewer', issue: 42, pr: 84, branch: 'claude/issue-42', slug: claude.fetch('slug'), released: false }
state = [{ version: 1, assignments: [reviewer_lease] }.to_json].pack('m0')
File.write(File.join(request_directory, 'state.json'), { sha: 'state-sha', content: state }.to_json)
assigned_pull = JSON.parse(File.read(File.join(request_directory, 'pull.json'))).merge('number' => 84, 'labels' => [{ 'name' => reviewer_label }])
File.write(File.join(request_directory, 'pull.json'), assigned_pull.to_json)
assignment_fixtures = [
  ['assigned', assigned_sender, reviewer_label, reviewed_sha, 'labeled', true],
  ['provider-request', assigned_sender, 'claude-review', reviewed_sha, 'labeled', false],
  ['arbitrary-label', assigned_sender, 'reviewer:unknown', reviewed_sha, 'labeled', false],
  ['executor-actor', sender, reviewer_label, reviewed_sha, 'labeled', false],
  ['wrong-sender-id', assigned_sender.merge(id: 999), reviewer_label, reviewed_sha, 'labeled', false],
  ['stale-assignment', assigned_sender, reviewer_label, 'b' * 40, 'labeled', false],
  ['not-an-assignment', assigned_sender, reviewer_label, reviewed_sha, 'ready_for_review', false]
]
assignment_fixtures.each do |name, actor, label, event_sha, action, expected|
  event_file = File.join(request_directory, 'assignment.json')
  event_pull = assigned_pull.merge('head' => assigned_pull.fetch('head').merge('sha' => event_sha))
  File.write(event_file, { action: action, label: { name: label }, pull_request: event_pull, sender: actor }.to_json)
  output_file = File.join(request_directory, 'assignment-output')
  File.write(output_file, '')
  environment = { 'PATH' => "#{request_directory}:#{ENV.fetch('PATH')}", 'FIXTURE_DIR' => request_directory, 'GITHUB_EVENT_NAME' => 'pull_request', 'GITHUB_REPOSITORY' => 'iekip95mod-arch/cx2-ag', 'PR_NUMBER' => '84', 'PR_HEAD_SHA' => event_sha, 'GITHUB_EVENT_PATH' => event_file, 'GITHUB_OUTPUT' => output_file, 'GITHUB_ACTOR' => actor.fetch(:login), 'GITHUB_ACTOR_ID' => actor.fetch(:id).to_s }
  _stdout, stderr, status = Open3.capture3(environment, 'node', File.join(request_directory, '.github/scripts/wait-for-review.mjs'), '--trust-assignment', unsetenv_others: true)
  raise "#{name}: incorrect assignment result: #{stderr}" unless status.success? == expected
  if expected
    outputs = File.read(output_file).lines.to_h { |line| line.strip.split('=', 2) }
    raise 'Claude must allow exactly its assigned reviewer actor' unless outputs.fetch('allowed_bots') == claude.fetch('login') && outputs.fetch('user_id') == claude.fetch('userId').to_s && outputs.fetch('reviewer') == 'claude'
  else
    raise 'Rejected assignments must not publish App metadata' unless File.read(output_file).empty?
  end
end
puts "#{assignment_fixtures.length} assignment event entrypoint cases passed"
