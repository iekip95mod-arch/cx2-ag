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
failures << 'Pull requests must run CI' unless events.key?('pull_request')
failures << 'Manual runs must remain available' unless events.key?('workflow_dispatch')
failures << 'Retired jobs must not run' unless (jobs.keys & ['linux-parity', 'device']).empty?
failures << 'Fast must be the first gate' unless jobs.fetch('fast')['needs'].nil? && jobs.fetch('fast')['if'].nil?
['full', 'emulator'].each do |name|
  job = jobs.fetch(name)
  failures << "#{name} must wait for fast to succeed on every event" unless Array(job['needs']) == ['fast'] && job['if'].nil?
end
codeql = jobs.fetch('codeql')
failures << 'CodeQL must wait for every execution gate to succeed' unless Array(codeql['needs']).sort == ['emulator', 'fast', 'full'] && codeql['if'].nil?
emulator_commands = jobs.fetch('emulator').fetch('steps').map { |step| step['run'] }.compact.map { |script| Shellwords.split(script) }
has_headless_tests = emulator_commands.any? do |command|
  command.first == 'make' && command.each_cons(2).include?(['-C', 'vendor/firebird-src/headless']) && command.include?('check') && command.include?('test-build')
end
failures << 'Emulator must build and run its headless regression suites' unless has_headless_tests
abort(failures.join("\n")) unless failures.empty?
puts '11 CI scheduling contracts passed'
