require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
%w[review codex-review].each do |name|
  steps = workflow.fetch('jobs').fetch(name).fetch('steps')
  token = steps.find { |step| step['id'] == 'bot' }
  raise 'Reviewers must mint their assigned App token' unless token.fetch('uses').start_with?('actions/create-github-app-token@')
  raise 'Reviewer token must use its leased App and secret' unless token.fetch('with').fetch('app-id') == '${{ needs.select-reviewer.outputs.app_id }}' && token.fetch('with').fetch('private-key') == '${{ secrets[needs.select-reviewer.outputs.secret_name] }}'
  raise 'Reviewer token must target only this repository' unless token.fetch('with').fetch('repositories') == '${{ github.event.repository.name }}'
  raise 'Model execution must not create assignment events' if steps.any? { |step| step.fetch('run', '').include?('--dispatch-review') || step.fetch('run', '').include?('--add-label') }

end
claude = workflow.fetch('jobs').fetch('review').fetch('steps').find { |step| step['uses']&.start_with?('anthropics/claude-code-action@') }.fetch('with')
raise 'Claude review must use the assigned GitHub identity' unless claude.fetch('github_token') == '${{ steps.bot.outputs.token }}'
raise 'Claude review must use subscription authentication only' if claude.key?('anthropic_api_key')
raise 'Claude must only allow the verified requesting executor' unless claude.fetch('allowed_bots') == '${{ needs.select-reviewer.outputs.allowed_bots }}'
selection = workflow.fetch('jobs').fetch('select-reviewer')
raise 'Review execution must use verified assignment metadata' unless selection.fetch('outputs').fetch('allowed_bots') == '${{ steps.identity.outputs.allowed_bots }}'
raise 'Review execution must validate the assignment event' unless selection.fetch('steps').find { |step| step['id'] == 'identity' }.fetch('run') == 'node .github/scripts/wait-for-review.mjs --trust-assignment'
raise 'Review execution must not allocate identities' if selection.fetch('steps').any? { |step| step.fetch('run', '').include?(' allocate ') }
request = YAML.load_file(File.join(root, '.github/workflows/agent-review-request.yml')).fetch('jobs').fetch('assign')
raise 'Validate the requester before reserving a reviewer' unless request.fetch('steps').index { |step| step['id'] == 'requester' } < request.fetch('steps').index { |step| step['id'] == 'identity' }
raise 'Execute the requester verifier' unless request.fetch('steps').find { |step| step['id'] == 'requester' }.fetch('run') == 'node .github/scripts/wait-for-review.mjs --trust-requester'
raise 'Assignment must dispatch with the reviewer App token' unless request.fetch('steps').last.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
raise 'Assignment must produce the real label event' unless request.fetch('steps').last.fetch('run') == 'node .github/scripts/wait-for-review.mjs --dispatch-review'
codex_publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }
raise 'Codex review must be published by the assigned identity' unless codex_publication.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }.fetch('run')
workspace = File.join(root, '.Internal/workspaces/codex-review-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)
fixtures = [
  ['approval', 'reviewed-sha', { verdict: 'APPROVED', body: 'Checks passed' }, 'APPROVE'],
  ['changes', 'reviewed-sha', { verdict: 'CHANGES_REQUESTED', body: 'A regression was reproduced' }, 'REQUEST_CHANGES'],
  ['stale', 'newer-sha', { verdict: 'APPROVED', body: 'Checks passed' }, nil],
  ['invalid-verdict', 'reviewed-sha', { verdict: 'UNKNOWN', body: 'Checks passed' }, nil],
  ['empty-body', 'reviewed-sha', { verdict: 'APPROVED', body: '' }, nil],
  ['wrong-app', 'reviewed-sha', { verdict: 'APPROVED', body: 'Checks passed' }, nil]
]
fixtures.each do |name, current_sha, review, expected_event|
  directory = File.join(run_directory, name)
  FileUtils.mkdir_p(directory)
  File.write(File.join(directory, 'codex-review.json'), review.to_json)
  gh = File.join(directory, 'gh')
  File.write(gh, <<~SH)
    #!/bin/bash
    set -euo pipefail
    if [ "$*" = "api repos/repository/pulls/84 --jq .head.sha" ]; then
      printf '%s\n' "$CURRENT_SHA"
    elif [ "$1" = api ] && [ "$2" = repos/repository/pulls/84/reviews ] && [ "$3" = --input ]; then
      cp "$4" "$RUNNER_TEMP/posted.json"
    elif [ "$*" = "label create reviewer:assigned-reviewer --repo repository --description Assigned reviewer identity --color 8250df --force" ]; then
      touch "$RUNNER_TEMP/label-created"
    elif [ "$*" = "pr edit 84 --repo repository --add-label reviewer:assigned-reviewer" ]; then
      test -f "$RUNNER_TEMP/label-created"
      touch "$RUNNER_TEMP/label-applied"
    else
      exit 1
    fi
  SH
  File.chmod(0o700, gh)
  environment = { 'PATH' => "#{directory}:#{ENV.fetch('PATH')}", 'RUNNER_TEMP' => directory, 'CURRENT_SHA' => current_sha, 'HEAD_SHA' => 'reviewed-sha', 'REPO' => 'repository', 'PR' => '84', 'APP_SLUG' => 'assigned-reviewer', 'EXPECTED_LOGIN' => 'assigned-reviewer[bot]' }
  environment['APP_SLUG'] = 'other-reviewer' if name == 'wrong-app'
  _stdout, _stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', publication, unsetenv_others: true)
  posted_file = File.join(directory, 'posted.json')
  if expected_event
    raise "#{name}: review not posted" unless status.success? && File.exist?(posted_file)
    posted = JSON.parse(File.read(posted_file))
    raise "#{name}: wrong review" unless posted == { 'commit_id' => 'reviewed-sha', 'event' => expected_event, 'body' => review.fetch(:body) }
  else
    raise "#{name}: invalid review posted" if status.success? || File.exist?(posted_file)
  end
end
puts "#{fixtures.length} review publication cases passed"
