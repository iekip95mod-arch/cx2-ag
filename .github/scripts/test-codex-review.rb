require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }.fetch('run')
workspace = File.join(root, '.Internal/workspaces/codex-review-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)
fixtures = [
  ['approval', 'reviewed-sha', { verdict: 'APPROVED', body: 'Checks passed' }, 'APPROVE'],
  ['changes', 'reviewed-sha', { verdict: 'CHANGES_REQUESTED', body: 'A regression was reproduced' }, 'REQUEST_CHANGES'],
  ['stale', 'newer-sha', { verdict: 'APPROVED', body: 'Checks passed' }, nil],
  ['invalid-verdict', 'reviewed-sha', { verdict: 'UNKNOWN', body: 'Checks passed' }, nil],
  ['empty-body', 'reviewed-sha', { verdict: 'APPROVED', body: '' }, nil]
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
    else
      exit 1
    fi
  SH
  File.chmod(0o700, gh)
  environment = { 'PATH' => "#{directory}:#{ENV.fetch('PATH')}", 'RUNNER_TEMP' => directory, 'CURRENT_SHA' => current_sha, 'HEAD_SHA' => 'reviewed-sha', 'REPO' => 'repository', 'PR' => '84' }
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
