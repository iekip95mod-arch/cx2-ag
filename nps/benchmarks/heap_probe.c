// PERF-010's free-memory reading. Run it through the debugger's exec, which keeps the Lua document
// and its modules resident. Opening it from the file browser tears them down first, see BUDGETS.md.

#include <SDL/SDL.h>
#include <os.h>
#include <stdarg.h>

#define RESULT_PATH "/documents/nps_heap.txt.tns"
#define SEARCH_CEILING (64u << 20)
#define RESOLUTION (64u << 10)
#define HOLDS_MAX 1024

static FILE *result;
static void *holds[HOLDS_MAX];

static void report(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (result) {
		va_start(ap, fmt);
		vfprintf(result, fmt, ap);
		va_end(ap);
	}
}

static unsigned probe_mallocs;

static unsigned largest_block(unsigned ceiling, unsigned resolution)
{
	unsigned lo = 0, hi = ceiling;
	void *p = malloc(ceiling);

	probe_mallocs = 1;
	if (p) {
		free(p);
		return ceiling;
	}

	while (hi - lo > resolution) {
		unsigned mid = lo + (hi - lo) / 2;
		p = malloc(mid);
		probe_mallocs++;
		if (p) {
			free(p);
			lo = mid;
		} else {
			hi = mid;
		}
	}

	return lo;
}

static void repeated_largest_block(unsigned repeats)
{
	unsigned started = SDL_GetTicks();
	unsigned first = largest_block(SEARCH_CEILING, RESOLUTION);
	unsigned stable = 1;
	unsigned elapsed;

	for (unsigned i = 1; i < repeats; i++)
		if (largest_block(SEARCH_CEILING, RESOLUTION) != first)
			stable = 0;
	elapsed = SDL_GetTicks() - started;

	report("largest_block.repeat.count = %u\n", repeats);
	report("largest_block.repeat.same_answer_every_time = %s\n", stable ? "yes" : "no");
	report("largest_block.repeat.mallocs_per_call = %u\n", probe_mallocs);
	report("largest_block.repeat.total_ms = %u\n", elapsed);
	report("largest_block.repeat.us_per_call = %u\n", elapsed * 1000 / repeats);
}

static void timed_largest_block(const char *label)
{
	unsigned started = SDL_GetTicks();
	unsigned block = largest_block(SEARCH_CEILING, RESOLUTION);
	unsigned elapsed = SDL_GetTicks() - started;

	report("largest_block.%s.bytes = %u\n", label, block);
	report("largest_block.%s.kib = %u\n", label, block >> 10);
	report("largest_block.%s.elapsed_ms = %u\n", label, elapsed);
}

static void chunked_total(const char *label, unsigned chunk)
{
	unsigned started = SDL_GetTicks();
	unsigned n = 0;
	unsigned elapsed;

	while (n < HOLDS_MAX && (holds[n] = malloc(chunk)))
		n++;
	elapsed = SDL_GetTicks() - started;

	for (unsigned i = 0; i < n; i++)
		free(holds[i]);

	report("chunked_total.%s.chunk_bytes = %u\n", label, chunk);
	report("chunked_total.%s.chunks = %u\n", label, n);
	report("chunked_total.%s.bytes = %u\n", label, n * chunk);
	report("chunked_total.%s.kib = %u\n", label, (n * chunk) >> 10);
	report("chunked_total.%s.hit_hold_limit = %s\n", label, n == HOLDS_MAX ? "yes" : "no");
	report("chunked_total.%s.elapsed_ms = %u\n", label, elapsed);
}

// Fixed chunks lose whatever does not divide the free runs, so take the largest block each time.
static void greedy_total(const char *label, unsigned resolution)
{
	unsigned started = SDL_GetTicks();
	unsigned n = 0;
	unsigned total = 0;
	unsigned smallest = 0;
	unsigned at_resolution = 0;
	unsigned ceiling = SEARCH_CEILING;
	unsigned elapsed;

	while (n < HOLDS_MAX) {
		unsigned block = largest_block(ceiling, resolution);
		// The search is a lower bound, so a small answer is not an empty heap. Let the allocation say.
		if (block < resolution)
			block = resolution;
		holds[n] = malloc(block);
		if (!holds[n]) {
			if (block <= resolution)
				break;
			ceiling = block - resolution;
			continue;
		}
		if (!n || block < smallest)
			smallest = block;
		if (block == resolution)
			at_resolution++;
		total += block;
		ceiling = block;
		n++;
	}
	elapsed = SDL_GetTicks() - started;

	for (unsigned i = 0; i < n; i++)
		free(holds[i]);

	report("greedy_total.%s.blocks = %u\n", label, n);
	report("greedy_total.%s.bytes = %u\n", label, total);
	report("greedy_total.%s.kib = %u\n", label, total >> 10);
	report("greedy_total.%s.smallest_block_bytes = %u\n", label, smallest);
	report("greedy_total.%s.blocks_at_resolution = %u\n", label, at_resolution);
	report("greedy_total.%s.blocks_above_resolution = %u\n", label, n - at_resolution);
	report("greedy_total.%s.resolution_bytes = %u\n", label, resolution);
	report("greedy_total.%s.slack_bound_bytes = %u\n", label, n * resolution);
	report("greedy_total.%s.hit_hold_limit = %s\n", label, n == HOLDS_MAX ? "yes" : "no");
	report("greedy_total.%s.elapsed_ms = %u\n", label, elapsed);
}

int main(void)
{
	result = fopen(RESULT_PATH, "w");

	report("schema = nps-heap-probe-v1\n");
	// Differs per run, so a stale result file cannot be read as a fresh one.
	report("run.image_address = %p\n", (void *)&main);
	report("scope = one ndl program, the OS heap it shares with whatever is already resident\n");
	report("target.ndl_revision = %d\n", nl_ndl_rev());
	report("target.started_from_startup = %d\n", nl_isstartup());
	report("timing.source = SDL_GetTicks\n");

	if (SDL_Init(0) != 0) {
		report("timing.initialized = no\n");
		report("timing.error = %s\n", SDL_GetError());
		if (result)
			fclose(result);
		return 1;
	}
	report("timing.initialized = yes\n");

	timed_largest_block("pass1");
	timed_largest_block("pass2");
	timed_largest_block("pass3");
	repeated_largest_block(100);

	chunked_total("mib", 1u << 20);
	timed_largest_block("after_mib_chunks");

	chunked_total("kib64", RESOLUTION);
	timed_largest_block("after_kib64_chunks");

	// Same heap at three resolutions: if the totals agree the granularity loss is small, and if the
	// finer ones read higher the coarse walk was losing a tail.
	greedy_total("kib64", RESOLUTION);
	timed_largest_block("after_greedy_kib64");

	greedy_total("kib4", 4u << 10);
	timed_largest_block("after_greedy_kib4");

	greedy_total("b512", 512u);
	timed_largest_block("after_greedy_b512");

	// A fourth pass says whether the recovery is flattening or whether each finer pass keeps finding
	// more, which decides whether any of these totals is near the truth.
	greedy_total("b64", 64u);
	timed_largest_block("after_greedy_b64");

	report("done\n");

	SDL_Quit();
	if (result)
		fclose(result);
	return 0;
}
