require 'shellwords'
require 'yaml'

root = File.expand_path('../..', __dir__)
workflow = YAML.load_file(ARGV.fetch(0, File.join(root, '.github/workflows/check.yml')))
events = workflow.fetch('on', workflow[true])
jobs = workflow.fetch('jobs')
failures = []
failures << 'CI must use a read-only token by default' unless workflow['permissions'] == { 'contents' => 'read' }
failures << 'CI cancellation must be isolated by ref and event' unless workflow['concurrency'] == { 'group' => '${{ github.workflow }}-${{ github.ref }}-${{ github.event_name }}', 'cancel-in-progress' => true }
failures << 'Pushes to main must run CI' unless events.fetch('push', {})['branches'] == ['main']
failures << 'Pull requests must run CI when opened, updated, reopened or ready for review' unless events.dig('pull_request', 'types') == ['opened', 'synchronize', 'reopened', 'ready_for_review']
failures << 'Manual runs must remain available' unless events.key?('workflow_dispatch')
failures << 'Retired jobs must not run' unless (jobs.keys & ['linux-parity', 'device']).empty?
failures << 'Fast must be the first gate' unless jobs.fetch('fast')['needs'].nil? && jobs.fetch('fast')['if'].nil?
['full', 'emulator'].each do |name|
  job = jobs.fetch(name)
  failures << "#{name} must wait for fast to succeed on every event" unless Array(job['needs']) == ['fast'] && job['if'].nil?
end
codeql = jobs.fetch('codeql')
failures << 'CodeQL must wait for every execution gate and reviewer approval' unless Array(codeql['needs']).sort == ['emulator', 'fast', 'full', 'review-ready'] && codeql['if'].nil?
review = jobs.fetch('review-ready')
failures << 'Review readiness must follow execution without bypassing failed approval' unless Array(review['needs']).sort == ['emulator', 'fast', 'full'] && review['if'].nil? && !review['continue-on-error']
failures << 'Review readiness must use a read-only token' unless review['permissions'] == { 'contents' => 'read', 'actions' => 'read', 'pull-requests' => 'read' }
gate = review.fetch('steps').find { |step| step['run'] == 'node .github/scripts/wait-for-review.mjs' }
failures << 'Review readiness must target the PR head and number' unless gate && gate.dig('env', 'PR_NUMBER') == '${{ github.event.pull_request.number }}' && gate.dig('env', 'PR_HEAD_SHA') == '${{ github.event.pull_request.head.sha }}' && !gate.key?('if') && !gate['continue-on-error']
emulator_commands = jobs.fetch('emulator').fetch('steps').map { |step| step['run'] }.compact.map { |script| Shellwords.split(script) }
has_headless_tests = emulator_commands.any? do |command|
  command.first == 'make' && command.each_cons(2).include?(['-C', 'vendor/firebird-src/headless']) && command.include?('check') && command.include?('test-build')
end
failures << 'Emulator must build and run its headless regression suites' unless has_headless_tests
abort(failures.join("\n")) unless failures.empty?
puts 'CI scheduling contracts passed'
