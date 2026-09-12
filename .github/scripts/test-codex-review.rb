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
  label = steps.find { |step| step['name'] == 'Identify the assigned reviewer on the PR' }
  raise 'Reviewer labels must use the assigned App token' unless label.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
  raise 'Verify identity before labelling the PR' unless label.fetch('run').lines.first.strip == 'test "$APP_SLUG[bot]" = "$EXPECTED_LOGIN"'
  raise 'Reviewer label must identify the selected App' unless label.fetch('run').include?('--add-label "reviewer:$APP_SLUG"')
end
claude = workflow.fetch('jobs').fetch('review').fetch('steps').find { |step| step['uses']&.start_with?('anthropics/claude-code-action@') }.fetch('with')
raise 'Claude review must use the assigned GitHub identity' unless claude.fetch('github_token') == '${{ steps.bot.outputs.token }}'
raise 'Claude review must use subscription authentication only' if claude.key?('anthropic_api_key')
raise 'Claude must only allow the verified requesting executor' unless claude.fetch('allowed_bots') == '${{ needs.select-reviewer.outputs.allowed_bots }}'
selection = workflow.fetch('jobs').fetch('select-reviewer')
raise 'Requester identity must come from lease verification' unless selection.fetch('outputs').fetch('allowed_bots') == '${{ steps.requester.outputs.allowed_bots }}'
raise 'Validate the requester before reserving a reviewer' unless selection.fetch('steps').index { |step| step['id'] == 'requester' } < selection.fetch('steps').index { |step| step['id'] == 'identity' }
raise 'Execute the requester verifier' unless selection.fetch('steps').find { |step| step['id'] == 'requester' }.fetch('run') == 'node .github/scripts/wait-for-review.mjs --trust-requester'
raise 'Metadata labels must not request another review' unless selection.fetch('steps').find { |step| step['id'] == 'requester' }.fetch('if') == "steps.reviewer.outputs.requested == 'true'"
codex_publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }
raise 'Codex review must be published by the assigned identity' unless codex_publication.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }.fetch('run')
identification = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Identify the assigned reviewer on the PR' }.fetch('run')
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
  if ['approval', 'wrong-app'].include?(name)
    _stdout, _stderr, label_status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', identification, unsetenv_others: true)
    expected_label = name == 'approval'
    raise "#{name}: incorrect reviewer label result" unless label_status.success? == expected_label && File.exist?(File.join(directory, 'label-applied')) == expected_label
  end
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
