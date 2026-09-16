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
# A reviewer assigned to a branch whose fast gate is red cannot merge it whatever it concludes, because
# full, review-ready and emulator all need fast. Without this the subscription run is spent on a verdict
# nobody can act on, which is what #366 cost.
request = YAML.load_file(File.join(root, '.github/workflows/agent-review-request.yml')).fetch('jobs')
gate = request['fast-gate']
failures << 'agent-review-request must read the fast gate before assigning a reviewer' unless gate && gate.dig('outputs', 'stop').to_s.include?('steps.fast.outputs.stop')
failures << 'The fast gate reader must only need permission to read checks' unless gate && gate['permissions'] == { 'checks' => 'read' }
assign = request.fetch('assign')
failures << 'assign must wait for the fast gate' unless Array(assign['needs']).include?('fast-gate')
failures << 'assign must skip when the fast gate reports a failed fast' unless assign['if'].to_s.include?("needs.fast-gate.outputs.stop != 'true'")
full = jobs.fetch('full')
failures << 'Full must prepare the complete SDK without allowing prerequisite failure' unless full.fetch('steps').any? { |step| step['run'] == 'bash .github/scripts/prepare-review.sh' && !step['continue-on-error'] }
failures << 'Full must require the bridge suite unconditionally' unless full.fetch('steps').any? { |step| step['name'] == 'Check the bridge suite registered' && !step.key?('if') }
['check.yml', 'agent-review.yml'].each do |file|
  source = File.read(File.join(root, '.github/workflows', file))
  failures << "#{file} must not schedule macOS or install with Homebrew" if source.match?(/runs-on: macos|brew install/)
end
# The images an agent is handed, in the two jobs that do the work. This fails quietly: without lfs the
# checkout leaves 130 byte pointers, which look like files and are not images, so an agent that meant
# to boot the calculator reports on it by reading instead. The emulator binary is the agent's own to
# build when it wants one.
{ 'agent.yml' => 'respond', 'agent-review.yml' => 'review' }.each do |file, job_name|
  job = YAML.load_file(File.join(root, '.github/workflows', file)).fetch('jobs').fetch(job_name)
  working = job.fetch('steps').select { |step| step['uses'].to_s.start_with?('actions/checkout') }.last
  failures << "#{file} #{job_name} must check out LFS or the images arrive as pointers" unless working && working.dig('with', 'lfs') == true
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
failures << 'Retired jobs must not run' unless (jobs.keys & ['linux-parity', 'device', 'codeql']).empty?
failures << 'Draft PRs must skip approval waiting while ready PRs and main keep their gates' unless jobs.fetch('review-ready')['if'] == "github.event_name != 'pull_request' || github.event.pull_request.draft == false"
failures << 'Fast must be the first gate' unless jobs.fetch('fast')['needs'].nil? && jobs.fetch('fast')['if'].nil?
# The suites gate on fast and on nothing else. They used to wait for review-ready too, which was right
# while an approval was required to merge and is a deadlock without one: review-ready waits for a
# verdict that no longer has to arrive, so full and emulator would never start and the branch could
# never show that it builds. Compiling is the guarantee worth keeping, so it is the one left running.
['full', 'emulator'].each do |name|
  job = jobs.fetch(name)
  failures << "#{name} must wait for fast and must not wait for a review" unless Array(job['needs']) == ['fast'] && job['if'].nil?
end

# The images are LFS tracked, so a checkout without lfs hands emurun a 130 byte pointer. That is the
# one setting that decides whether this job can boot anything at all.
emulator = jobs.fetch('emulator')
checkout = emulator.fetch('steps').find { |step| step['uses'].to_s.start_with?('actions/checkout') }
failures << 'Emulator must check out LFS content or the images arrive as pointers' unless checkout && checkout.dig('with', 'lfs') == true
emulator_scripts = emulator.fetch('steps').filter_map { |step| step['run'] }.join("\n")
failures << 'Emulator must build firebird headless' unless emulator_scripts.include?('vendor/firebird-src/headless')
failures << 'Emulator must boot the calculator through tools/emu' unless emulator_scripts.include?('tools/emu/emurun.py')
review = jobs.fetch('review-ready')
failures << 'Review readiness must follow fast without waiting on the suites it gates' unless Array(review['needs']) == ['fast'] && !review['continue-on-error']
failures << 'Review readiness must use a read-only token' unless review['permissions'] == { 'contents' => 'read', 'actions' => 'read', 'pull-requests' => 'read' }
gate = review.fetch('steps').find { |step| step['run'] == 'node .github/scripts/wait-for-review.mjs' }
failures << 'Review readiness must target the PR head and number' unless gate && gate.dig('env', 'PR_NUMBER') == '${{ github.event.pull_request.number }}' && gate.dig('env', 'PR_HEAD_SHA') == '${{ github.event.pull_request.head.sha }}' && !gate.key?('if') && !gate['continue-on-error']
abort(failures.join("\n")) unless failures.empty?
puts 'CI scheduling contracts passed'
