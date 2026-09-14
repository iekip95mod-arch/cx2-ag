require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml'))
emulator_contract = ['Calculator execution is emulator only', 'Physical handheld runs are outside scope', 'unfinished physical-test checkboxes', 'block', 'absence', 'package', 'Distinguish']
%w[review codex-review].each do |name|
  steps = workflow.fetch('jobs').fetch(name).fetch('steps')
  history = steps.find { |step| step['name'] == 'Prepare cumulative review history' }
  raise 'Reviewers need prior findings on the exact head' unless history && history.fetch('run') == 'node .github/scripts/review-history.mjs' && history.fetch('env').fetch('HEAD_SHA') == '${{ github.event.pull_request.head.sha }}'
  prompt = name == 'review' ? steps.find { |step| step['name'] == 'Review the pull request' }.fetch('env').fetch('REVIEW_PROMPT') : steps.find { |step| step['name'] == 'Review with subscription login' }.fetch('run')
  raise 'Every review must cover the full PR and track earlier findings' unless prompt.include?('entire cumulative PR') && prompt.include?('review-history.json') && prompt.include?('superseded') && prompt.include?('suggestion block')
  raise 'Reviewer environment blockers must not request code changes' unless prompt.include?('BLOCKED') && prompt.include?('CI evidence') && prompt.include?('review-prerequisites.log')
  raise "#{name}: reviewer prompt must make emulator validation the final device stage" unless emulator_contract.all? { |text| prompt.include?(text) }
  preparation = steps.find { |step| step['name'] == 'Prepare complete reviewer prerequisites' }
  raise 'Both reviewers must prepare SDK and Lua before the model runs' unless preparation && preparation.fetch('run').include?('prepare-review.sh') && preparation['continue-on-error'] == true
  restore = steps.find { |step| step['name'] == 'Restore the reviewer cross toolchain' }
  save = steps.find { |step| step['name'] == 'Save the complete reviewer cross toolchain' }
  raise 'Reviewer cache restoration must not save partial prerequisites' unless restore && restore.fetch('uses') == 'actions/cache/restore@v6' && restore.fetch('id') == 'reviewer-toolchain-cache'
  raise 'Reviewer cache save must follow successful preparation' unless save && save.fetch('uses') == 'actions/cache/save@v6' && save.fetch('if') == "steps.reviewer-prerequisites.outcome == 'success' && steps.reviewer-prerequisites.outputs.native != 'false' && steps.reviewer-toolchain-cache.outputs.cache-hit != 'true'"
  raise 'Reviewer cache save must use the restored primary key' unless save.fetch('with').fetch('key') == '${{ steps.reviewer-toolchain-cache.outputs.cache-primary-key }}'
  raise 'Reviewer cache lifecycle is out of order' unless steps.index(restore) < steps.index(preparation) && steps.index(preparation) < steps.index(save)
  raise 'Review history needs CI read permission without exposing a token to Codex' unless workflow.fetch('jobs').fetch(name).fetch('permissions')['actions'] == 'read'
  token = steps.find { |step| step['id'] == 'bot' }
  raise 'Reviewer reporting must survive missing model credentials' unless token['if'] == 'always()'
  checkout = steps.find { |step| step['uses'].to_s.start_with?('actions/checkout@') }
  raise 'Reviewer cleanup must have its scripts without model credentials' if checkout['if']
  missing = steps.find { |step| step['name'] == 'Require reviewer credentials' }
  raise 'Both providers must fail explicitly without model credentials' unless missing && missing['if'] == "steps.key.outputs.have != 'true'" && missing['run'].include?('exit 1')
  raise 'Reviewers must mint their assigned App token' unless token.fetch('uses').start_with?('actions/create-github-app-token@')
  raise 'Reviewer token must use its leased App and secret' unless token.fetch('with').fetch('app-id') == '${{ needs.select-reviewer.outputs.app_id }}' && token.fetch('with').fetch('private-key').include?('needs.select-reviewer.outputs.secret_name == ') && !token.fetch('with').fetch('private-key').include?('secrets[')
  raise 'Reviewer token must target only this repository' unless token.fetch('with').fetch('repositories') == '${{ github.event.repository.name }}'
  raise 'Model execution must not create assignment events' if steps.any? { |step| step.fetch('run', '').include?('--dispatch-review') || step.fetch('run', '').include?('--add-label') }
  model_step = steps.index { |step| step['name'] == (name == 'review' ? 'Review the pull request' : 'Review with subscription login') }
  progress_step = steps.index { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-progress' }
  disclosure_step = steps.index { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-disclosure' }
  raise 'A persistent progress update must precede model invocation' unless progress_step && progress_step < model_step
  raise 'Runtime disclosure must follow the formal review' unless disclosure_step && disclosure_step > model_step
  raise 'Runtime notes must use the assigned identity' unless [progress_step, disclosure_step].all? { |index| steps[index].fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}' }
  final = steps.find { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-final' }
  publishing = steps.find { |step| step.fetch('run', '') == 'node .github/scripts/wait-for-review.mjs --review-publishing' }
  raise 'Reviewer lifecycle must record publishing and final states' unless publishing && final && final.fetch('if').include?('always()')
  expected_phase = "${{ job.status == 'success' && 'succeeded' || job.status == 'failure' && 'failed' || job.status }}"
  raise 'Failed reviewer status is not translated to the progress phase' unless final.fetch('env').fetch('PROGRESS_PHASE') == expected_phase

end
request_workflow = YAML.load_file(File.join(root, '.github/workflows/agent-review-request.yml'))
[request_workflow, workflow].each do |review_workflow|
  lock = review_workflow.fetch('concurrency')
  raise 'Queued and terminal reviewer progress must share a serialized PR lane' unless lock.fetch('group').start_with?('agent-review-${{ github.event.pull_request.number }}-') && lock.fetch('cancel-in-progress') == false && lock.fetch('queue') == 'max'
end
claude = workflow.fetch('jobs').fetch('review').fetch('steps').find { |step| step['name'] == 'Review the pull request' }.fetch('env')
claude_prompt = claude.fetch('REVIEW_PROMPT')
raise 'Claude must be told that reviewer prerequisites include the cross toolchain and host Lua' unless claude_prompt.include?('prepares the cross toolchain and host Lua')
raise 'Claude must not be told that prepared reviewer prerequisites are unavailable' if claude_prompt.include?('never unpacks the cross toolchain') || claude_prompt.include?('for want of host Lua')
raise 'Claude review must use subscription OAuth' unless claude.fetch('CLAUDE_CODE_OAUTH_TOKEN') == '${{ secrets.CLAUDE_CODE_OAUTH_TOKEN }}'
raise 'Claude review must use subscription authentication only' if claude.key?('anthropic_api_key')
raise 'Claude review must use the CLI adapter' unless workflow.fetch('jobs').fetch('review').fetch('steps').find { |step| step['id'] == 'review' }.fetch('run') == 'node .github/scripts/run-claude-review.mjs'
review_steps = workflow.fetch('jobs').fetch('review').fetch('steps')
restored = review_steps.find { |step| step['name'] == 'Restore agent configuration from the base branch' }
raise 'A branch must not steer its own reviewer through agent configuration' unless restored && %w[.claude CLAUDE.md AGENTS.md .mcp.json].all? { |path| restored.fetch('run').include?(path) } && restored.fetch('run').include?('git checkout "$BASE_SHA"') && restored.fetch('run').include?('rm -rf')
raise 'Agent configuration must be restored from the base commit' unless restored.fetch('env').fetch('BASE_SHA') == '${{ github.event.pull_request.base.sha }}'
raise 'Agent configuration must be restored after checkout and before the model reads it' unless review_steps.index { |step| step['uses'].to_s.start_with?('actions/checkout@') } < review_steps.index(restored) && review_steps.index(restored) < review_steps.index { |step| step['id'] == 'review' }
raise 'A reviewer told to file separate issues needs a credential that can' unless claude['REVIEW_GH_TOKEN'] == '${{ secrets.GITHUB_TOKEN }}'
raise 'Reviewer gh tooling must not receive the assigned App token' if claude['REVIEW_GH_TOKEN'].to_s.include?('steps.bot.outputs.token')
raise 'Claude review must not depend on the workflow equality guard' if workflow.fetch('jobs').fetch('review').fetch('steps').any? { |step| step['id'] == 'mine' || step['uses'].to_s.start_with?('anthropics/') }
selection = workflow.fetch('jobs').fetch('select-reviewer')
raise 'Review execution must use verified assignment metadata' unless selection.fetch('outputs').fetch('allowed_bots') == '${{ steps.identity.outputs.allowed_bots }}'
raise 'Review execution must validate the assignment event' unless selection.fetch('steps').find { |step| step['id'] == 'identity' }.fetch('run') == 'node .github/scripts/wait-for-review.mjs --trust-assignment'
raise 'Review execution must not allocate identities' if selection.fetch('steps').any? { |step| step.fetch('run', '').include?(' allocate ') }
request = YAML.load_file(File.join(root, '.github/workflows/agent-review-request.yml')).fetch('jobs').fetch('assign')
trust = request.fetch('steps').index { |step| step['name'] == 'Require a trusted PR author' }
checkouts = request.fetch('steps').select { |step| step['uses']&.start_with?('actions/checkout@') }
raise 'Credentialed assignment scripts must stay on the trusted initial checkout' unless trust && checkouts.length == 1 && checkouts[0].fetch('with').fetch('ref').include?('github.event.pull_request.base.sha')
raise 'Validate the requester before reserving a reviewer' unless request.fetch('steps').index { |step| step['id'] == 'requester' } < request.fetch('steps').index { |step| step['id'] == 'identity' }
raise 'Execute the requester verifier' unless request.fetch('steps').find { |step| step['id'] == 'requester' }.fetch('run') == 'node .github/scripts/wait-for-review.mjs --trust-requester'
queued = request.fetch('steps').find { |step| step['name'] == 'Show queued reviewer work' }
dispatch = request.fetch('steps').find { |step| step['name'] == 'Deliver the reviewer assignment' }
raise 'Reviewer assignment needs room for progress publication and delivery' unless request.fetch('timeout-minutes') >= 10
raise 'Assignment must publish queued progress with the reviewer App token' unless queued&.fetch('env')&.fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}' && queued.fetch('run').include?('node .github/scripts/wait-for-review.mjs --review-queued')
raise 'Assignment must bootstrap safely from a base branch without progress support' unless queued.fetch('run').include?('grep -Fq -- "--review-queued"')
raise 'Assignment must dispatch with the reviewer App token' unless dispatch&.fetch('env')&.fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}' && dispatch.fetch('run') == 'node .github/scripts/wait-for-review.mjs --dispatch-review'
raise 'Assignment must queue progress before producing the real label event' unless request.fetch('steps').index(queued) < request.fetch('steps').index(dispatch)
assignment_final = request.fetch('steps').find { |step| step['name'] == 'Record failed reviewer assignment' }
handoff = request.fetch('steps').find { |step| step['name'] == 'Record successful reviewer handoff' }
raise 'Successful assignments must finish their own progress without cleaning review labels' unless handoff && handoff.fetch('run').include?('--review-handoff') && !handoff.fetch('run').include?('--review-final') && handoff.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
raise 'Handoff must follow successful assignment delivery' unless handoff['if'] == "steps.identity.outputs.waiting != 'true'" && request.fetch('steps').index(handoff) > request.fetch('steps').index(dispatch)
raise 'Handoff must bootstrap safely from the previous base' unless handoff.fetch('run').include?('grep -Fq -- "--review-handoff"')
raise 'Reviewer assignment must close unsuccessful queued progress' unless assignment_final && assignment_final.fetch('if').include?('always()') && assignment_final.fetch('if').include?("job.status != 'success'") && assignment_final.fetch('run').include?('node .github/scripts/wait-for-review.mjs --review-final')
raise 'Assignment cleanup must tolerate a base branch without progress support' unless assignment_final.fetch('run').include?('grep -Fq -- "--review-final"')
raise 'Reviewer assignment failure must use the assigned identity' unless assignment_final.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
expected_phase = "${{ job.status == 'failure' && 'failed' || job.status }}"
raise 'Assignment failure is not translated to the progress phase' unless assignment_final.fetch('env').fetch('PROGRESS_PHASE') == expected_phase
raise 'Assignment cleanup must follow delivery' unless request.fetch('steps').index(assignment_final) > request.fetch('steps').index(dispatch)
claude_steps = workflow.fetch('jobs').fetch('review').fetch('steps')
claude_guard = claude_steps.index { |step| step['name'] == 'Refuse a green check over a review that was not posted' }
claude_final = claude_steps.index { |step| step['name'] == 'Record the reviewer outcome' }
raise 'Claude final progress must observe the review-presence guard' unless claude_guard && claude_final && claude_final > claude_guard
codex_publication = workflow.fetch('jobs').fetch('codex-review').fetch('steps').find { |step| step['name'] == 'Publish the review for the reviewed commit' }
raise 'Codex review must be published by the assigned identity' unless codex_publication.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
%w[review codex-review].each do |name|
  steps = workflow.fetch('jobs').fetch(name).fetch('steps')
  publish = steps.find { |step| step['name'] == 'Publish the review for the reviewed commit' }
  raise 'Both providers must use the native inline publisher' unless publish.fetch('run') == 'node .github/scripts/publish-review.mjs'
  raise 'Native reviews must use the assigned identity' unless publish.fetch('env').fetch('GH_TOKEN') == '${{ steps.bot.outputs.token }}'
  if name == 'review'
    raise 'Publish Claude native structured output' unless publish.fetch('env').fetch('REVIEW_FILE') == '${{ runner.temp }}/claude-review.json'
  else
    raise 'Publish the Codex output file' unless publish.fetch('env').fetch('REVIEW_FILE') == '${{ runner.temp }}/codex-review.json'
  end
  raise 'Failed executions cannot publish approval checkpoints' unless publish.fetch('env').fetch('REVIEW_OUTCOME') == '${{ steps.review.outcome }}'
  raise 'Disclose only after publication' unless steps.index(publish) < steps.index { |step| step.fetch('run', '').include?('--review-disclosure') }
end
raise 'Claude must return its verdict through the native schema' unless claude.fetch('REVIEW_PROMPT').include?('required structured output schema') && claude.fetch('REVIEW_SCHEMA') == '${{ steps.review-schema.outputs.json }}'
schema_step = claude_steps.find { |step| step['id'] == 'review-schema' }
raise 'Claude schema must be the shared review schema' unless schema_step && schema_step.fetch('run').include?('jq -c . .github/scripts/review-schema.json')
raise 'Claude must not post a competing summary-only verdict' if claude.fetch('REVIEW_ALLOWED_TOOLS').include?('Bash(gh pr review:*)')
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

feedback = YAML.load_file(File.join(root, '.github/workflows/agent-review-feedback.yml'))
feedback_events = feedback['on'] || feedback[true]
covered = feedback_events.fetch('workflow_run').fetch('workflows')
Dir.glob(File.join(root, '.github/workflows/*.yml')).each do |path|
  candidate = YAML.load_file(path)
  events = candidate['on'] || candidate[true]
  next unless events.is_a?(Hash) && events.key?('pull_request')
  next if candidate['name'] == 'agent-review-feedback'
  raise "Failed CI feedback does not cover #{candidate['name']}" unless covered.include?(candidate['name'])
end
