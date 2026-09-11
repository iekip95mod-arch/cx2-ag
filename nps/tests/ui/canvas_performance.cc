#include "nps/ui/canvas.h"

#include <EASTL/fixed_vector.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

namespace {
bool counting = false;
size_t allocations = 0;
volatile size_t observed = 0;
constexpr unsigned iterations = 2000;

template<class Work>
size_t measure(const char *label, Work work) {
    std::array<double, 7> elapsed{};
    size_t total = 0;
    for (double &sample : elapsed) {
        allocations = 0;
        counting = true;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < iterations; ++frame) observed = work(frame);
        const auto end = std::chrono::steady_clock::now();
        counting = false;
        total += allocations;
        sample = std::chrono::duration<double, std::micro>(end - start).count() / iterations;
    }
    std::sort(elapsed.begin(), elapsed.end());
    std::cout << label << ": allocations=" << total << ", us/frame min=" << elapsed.front()
              << " median=" << elapsed[3] << " max=" << elapsed.back() << '\n';
    return total;
}

template<class Container>
size_t populate(Container &fills, unsigned frame) {
    fills.clear();
    for (int i = 0; i < 64; ++i)
        fills.push_back({{i, static_cast<int>(frame % 200), i + 1, 1}, {37, 57, 87}});
    size_t checksum = 0;
    for (const auto &fill : fills) checksum += static_cast<size_t>(fill.bounds.width + fill.bounds.y);
    return checksum;
}
}

void *operator new(size_t size) {
    if (void *memory = std::malloc(size ? size : 1)) {
        if (counting) ++allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, size_t) noexcept { std::free(memory); }

int main() {
    bool complete = true;
    const size_t native = measure("canvas panel and five icons", [&](unsigned frame) {
        nps::ui::Canvas canvas(320, 240);
        canvas.panel(18, 18);
        for (unsigned icon = 0; icon < 5; ++icon)
            canvas.icon(static_cast<nps::ui::Icon>(icon), static_cast<int>(icon * 20),
                        20 + static_cast<int>(frame % 180), {37, 57, 87}, {255, 255, 255});
        complete = complete && canvas.good() && canvas.fills().size() > 4;
        return canvas.fills().size();
    });
    const size_t etl = measure("ETL 64 commands", [](unsigned frame) {
        etl::vector<nps::ui::Fill, 256> fills;
        return populate(fills, frame);
    });
    const size_t eastl = measure("EASTL 64 commands", [](unsigned frame) {
        eastl::fixed_vector<nps::ui::Fill, 128, false> fills;
        return populate(fills, frame);
    });
    const size_t fresh = measure("STL fresh 64 commands", [](unsigned frame) {
        std::vector<nps::ui::Fill> fills;
        fills.reserve(64);
        return populate(fills, frame);
    });
    std::vector<nps::ui::Fill> retained;
    retained.reserve(64);
    const size_t reused = measure("STL retained 64 commands", [&](unsigned frame) {
        return populate(retained, frame);
    });
    std::cout << "Canvas bytes=" << sizeof(nps::ui::Canvas) << ", iterations/sample=" << iterations << '\n';
    return complete && native == 0 && etl == 0 && eastl == 0 && fresh == iterations * 7 && reused == 0 ? 0 : 1;
}
