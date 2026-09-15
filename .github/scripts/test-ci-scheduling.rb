require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(ARGV.fetch(0, File.join(root, '.github/workflows/check.yml')))
events = workflow.fetch('on', workflow[true])
jobs = workflow.fetch('jobs')
failures = []
{ 'fast' => 'ubuntu-24.04', 'full' => 'ubuntu-24.04-arm' }.each do |name, runner|
  failures << "#{name} must use #{runner}" unless jobs.fetch(name)['runs-on'] == runner
end
reviews = YAML.load_file(File.join(root, '.github/workflows/agent-review.yml')).fetch('jobs')
['review', 'codex-review'].each do |name|
  failures << "#{name} must use Ubuntu ARM" unless reviews.fetch(name)['runs-on'] == 'ubuntu-24.04-arm'
end
full = jobs.fetch('full')
failures << 'Full must prepare the complete SDK without allowing prerequisite failure' unless full.fetch('steps').any? { |step| step['run'] == 'bash .github/scripts/prepare-review.sh' && !step['continue-on-error'] }
failures << 'Full must require the bridge suite unconditionally' unless full.fetch('steps').any? { |step| step['name'] == 'Check the bridge suite registered' && !step.key?('if') }
['check.yml', 'agent-review.yml'].each do |file|
  source = File.read(File.join(root, '.github/workflows', file))
  failures << "#{file} must not schedule macOS or install with Homebrew" if source.match?(/runs-on: macos|brew install/)
end
['agent-codex.yml', 'agent.yml', 'agent-gemini.yml'].each do |file|
  executor = YAML.load_file(File.join(root, '.github/workflows', file)).fetch('jobs').fetch('respond')
  failures << "#{file} executor must use Ubuntu" unless executor['runs-on'] == 'ubuntu-24.04'
  install = executor.fetch('steps').find { |step| step['name'] == 'Install prerequisites' }.fetch('run')
  failures << "#{file} must install Linux prerequisites" unless install.include?('apt-get install') && install.include?('libgmp-dev') && install.include?('lua5.1') && install.include?('update-alternatives --set lua-interpreter /usr/bin/lua5.1') && !install.include?('brew')
end
fast_scripts = jobs.fetch('fast').fetch('steps').filter_map { |step| step['run'] }.join("\n")
failures << 'Fast must install Linux build prerequisites' unless fast_scripts.include?('apt-get install') && fast_scripts.include?('libgmp-dev') && fast_scripts.include?('lua5.1') && fast_scripts.include?('update-alternatives --set lua-interpreter /usr/bin/lua5.1') && !fast_scripts.include?('brew')
failures << 'CI must use a read-only token by default' unless workflow['permissions'] == { 'contents' => 'read' }
failures << 'CI cancellation must be isolated by ref and event' unless workflow['concurrency'] == { 'group' => '${{ github.workflow }}-${{ github.ref }}-${{ github.event_name }}', 'cancel-in-progress' => true }
failures << 'Pushes to main must run CI' unless events.fetch('push', {})['branches'] == ['main']
failures << 'Pull requests must run CI when opened, updated, reopened or ready for review' unless events.dig('pull_request', 'types') == ['opened', 'synchronize', 'reopened', 'ready_for_review']
failures << 'Manual runs must remain available' unless events.key?('workflow_dispatch')
failures << 'Retired jobs must not run' unless (jobs.keys & ['linux-parity', 'device', 'emulator', 'codeql']).empty?
failures << 'Draft PRs must skip approval waiting while ready PRs and main keep their gates' unless jobs.fetch('review-ready')['if'] == "github.event_name != 'pull_request' || github.event.pull_request.draft == false"
failures << 'Fast must be the first gate' unless jobs.fetch('fast')['needs'].nil? && jobs.fetch('fast')['if'].nil?
['full'].each do |name|
  job = jobs.fetch(name)
  failures << "#{name} must wait for fast and current-head approval" unless Array(job['needs']).sort == ['fast', 'review-ready'] && job['if'].nil?
end
review = jobs.fetch('review-ready')
failures << 'Review readiness must follow fast without waiting on the suites it gates' unless Array(review['needs']) == ['fast'] && !review['continue-on-error']
failures << 'Review readiness must use a read-only token' unless review['permissions'] == { 'contents' => 'read', 'actions' => 'read', 'pull-requests' => 'read' }
gate = review.fetch('steps').find { |step| step['run'] == 'node .github/scripts/wait-for-review.mjs' }
failures << 'Review readiness must target the PR head and number' unless gate && gate.dig('env', 'PR_NUMBER') == '${{ github.event.pull_request.number }}' && gate.dig('env', 'PR_HEAD_SHA') == '${{ github.event.pull_request.head.sha }}' && !gate.key?('if') && !gate['continue-on-error']
abort(failures.join("\n")) unless failures.empty?
puts 'CI scheduling contracts passed'
