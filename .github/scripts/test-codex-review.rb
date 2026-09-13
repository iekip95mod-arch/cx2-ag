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
  model_step = steps.index { |step| step['name'] == (name == 'review' ? 'Review the pull request' : 'Review with subscription login') }
  progress_step = steps.index { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-progress' }
  disclosure_step = steps.index { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-disclosure' }
  raise 'A non-verdict progress review must precede model invocation' unless progress_step && progress_step < model_step
  raise 'Runtime disclosure must follow the formal review' unless disclosure_step && disclosure_step > model_step
  raise 'Runtime notes must use the assigned identity' unless [progress_step, disclosure_step].all? { |index| steps[index].fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}' }

end
claude = workflow.fetch('jobs').fetch('review').fetch('steps').find { |step| step['uses']&.start_with?('anthropics/claude-code-action@') }.fetch('with')
raise 'Claude review must use the assigned GitHub identity' unless claude.fetch('github_token') == '${{ steps.bot.outputs.token }}'
raise 'Claude review must use subscription authentication only' if claude.key?('anthropic_api_key')
raise 'Claude must invoke the disclosed model and effort' unless claude.fetch('claude_args').include?('--model ${{ env.REVIEW_MODEL }}') && claude.fetch('claude_args').include?('--effort ${{ env.REVIEW_EFFORT }}')
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
%w[review codex-review].each do |name|
  steps = workflow.fetch('jobs').fetch(name).fetch('steps')
  publish = steps.find { |step| step['name'] == 'Publish the review for the reviewed commit' }
  raise 'Both providers must use the native inline publisher' unless publish.fetch('run') == 'node .github/scripts/publish-review.mjs'
  raise 'Native reviews must use the assigned identity' unless publish.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
  file = name == 'review' ? 'claude-review.json' : 'codex-review.json'
  raise 'Publish the provider output file' unless publish.fetch('env').fetch('REVIEW_FILE') == "${{ runner.temp }}/#{file}"
  raise 'Disclose only after publication' unless steps.index(publish) < steps.index { |step| step.fetch('run', '').include?('--review-disclosure') }
end
raise 'Claude must write its verdict artifact' unless claude.fetch('prompt').include?('${{ runner.temp }}/claude-review.json') && claude.fetch('claude_args').include?(',Write,')
raise 'Claude must not post a competing summary-only verdict' if claude.fetch('claude_args').include?('Bash(gh pr review:*)')
workspace = File.join(root, '.Internal/workspaces/codex-review-tests')
FileUtils.mkdir_p(workspace)
run_directory = Dir.mktmpdir('run-', workspace)

invocation = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Review with subscription login' }.fetch('run')
raise 'Codex default must match the selected repository model' unless workflow.fetch('jobs').fetch('codex-review').fetch('env') == { 'REVIEW_MODEL' => "${{ vars.CODEX_MODEL || 'gpt-5.6-sol' }}", 'REVIEW_EFFORT' => "${{ vars.CODEX_EFFORT || 'high' }}" }
directory = File.join(run_directory, 'invocation')
FileUtils.mkdir_p(directory)
File.write(File.join(directory, 'codex'), <<~SH)
  #!/bin/bash
  set -euo pipefail
  if [ "$1" = login ]; then exit 0; fi
  printf '%s\n' "$@" > "$RUNNER_TEMP/invoked-args"
SH
File.chmod(0o700, File.join(directory, 'codex'))
environment = { 'PATH' => "#{directory}:#{ENV.fetch('PATH')}", 'RUNNER_TEMP' => directory, 'BASE_SHA' => 'base', 'HEAD_SHA' => 'head', 'REVIEW_MODEL' => 'gpt-5.6-sol', 'REVIEW_EFFORT' => 'high', 'AGENT_TIME_BUDGET' => 'Finish before the supplied UTC cutoff' }
_stdout, stderr, status = Open3.capture3(environment, 'bash', '-e', '-o', 'pipefail', '-c', invocation, unsetenv_others: true)
raise "Codex invocation failed: #{stderr}" unless status.success?
args = File.readlines(File.join(directory, 'invoked-args'), chomp: true)
raise 'Codex reviewer must receive its execution deadline' unless File.read(File.join(directory, 'review-prompt.txt')).start_with?(environment.fetch('AGENT_TIME_BUDGET') + "\n")
raise 'Codex must invoke exactly the disclosed model' unless args[args.index('--model') + 1] == environment.fetch('REVIEW_MODEL')
raise 'Codex must invoke exactly the disclosed effort' unless args.include?('model_reasoning_effort="high"')
raise 'Codex must enforce the inline finding schema' unless args[args.index('--output-schema') + 1] == '.github/scripts/review-schema.json'
schema = JSON.parse(File.read(File.join(root, '.github/scripts/review-schema.json')))
raise 'Review output must include inline comments' unless schema.fetch('required').include?('comments')
raise 'Inline findings require native diff coordinates' unless schema.dig('properties', 'comments', 'items', 'required').sort == %w[body line path side]
puts 'Codex configured model and effort invocation passed'
