require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
job = workflow.fetch('jobs').fetch('review-approved')
raise 'Approval gate must run when dependencies fail or skip' unless job.fetch('if') == '${{ always() }}'
raise 'Approval gate must wait for selection and both reviewers' unless job.fetch('needs').sort == ['codex-review', 'review', 'select-reviewer']
gate = job.fetch('steps').first.fetch('run')
%w[review codex-review].each do |name|
  raise 'Ordinary labels must not spend a review' unless workflow.fetch('jobs').fetch(name).fetch('if').include?("needs.select-reviewer.outputs.requested == 'true'")
end
raise 'Ordinary labels must not cancel requested reviews' unless workflow.fetch('concurrency').fetch('group').include?("'metadata' || 'requested'")
raise 'Review cancellation must be isolated by PR' unless workflow.fetch('concurrency').fetch('group').include?('${{ github.event.pull_request.number }}')
approval = { id: 2, commit_id: 'reviewed-sha', user: { login: 'github-actions[bot]', type: 'Bot' }, state: 'APPROVED' }
fixtures = [
  ['codex', 'codex', 'reviewed-sha', [approval], 'success', true],
  ['claude', 'claude', 'reviewed-sha', [approval.merge(user: { login: 'claude[bot]', type: 'Bot' })], 'success', true],
  ['wrong-provider', 'claude', 'reviewed-sha', [approval], 'success', false],
  ['no-review', 'codex', 'reviewed-sha', [], 'success', false],
  ['old-review', 'codex', 'reviewed-sha', [approval.merge(id: 1)], 'success', false],
  ['old-commit', 'codex', 'reviewed-sha', [approval.merge(commit_id: 'older-sha')], 'success', false],
  ['new-push', 'codex', 'newer-sha', [approval], 'success', false],
  ['changes-requested', 'codex', 'reviewed-sha', [approval, approval.merge(id: 3, state: 'CHANGES_REQUESTED')], 'success', false],
  ['failed-reviewer', 'codex', 'reviewed-sha', [approval], 'failure', false],
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
  File.write(File.join(directory, 'reviews.json'), [reviews].to_json)
  gh = File.join(directory, 'gh')
  File.write(gh, <<~SH)
    #!/bin/bash
    set -euo pipefail
    if [ "$*" = "api repos/repository/pulls/84 --jq .head.sha" ]; then
      printf '%s\n' "$CURRENT_SHA"
    elif [ "$*" = "api --paginate --slurp repos/repository/pulls/84/reviews" ]; then
      cat "$FIXTURE_DIR/reviews.json"
    else
      exit 1
    fi
  SH
  File.chmod(0o700, gh)
  environment = {
    'PATH' => "#{directory}:#{ENV.fetch('PATH')}", 'FIXTURE_DIR' => directory,
    'REVIEWER' => reviewer, 'CURRENT_SHA' => current_sha, 'HEAD_SHA' => 'reviewed-sha',
    'BEFORE' => '[1]', 'CLAUDE_RESULT' => provider_status, 'CODEX_RESULT' => provider_status,
    'SELECT_RESULT' => selection_status || 'success',
    'REVIEW_REQUESTED' => requested || 'true',
    'REPO' => 'repository', 'PR' => '84'
  }
  _stdout, _stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', gate, unsetenv_others: true)
  raise "#{name}: incorrect approval gate result" unless status.success? == expected
end
puts "#{fixtures.length} approval gate cases passed"
