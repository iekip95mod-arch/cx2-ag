#include "container_workload.h"
#include "nps/ui/canvas.h"

#include <EASTL/fixed_vector.h>
#include <type_traits>
#include <vector>

namespace nps::test {
namespace {
using Etl = etl::vector<ui::Fill, 64>;
using Eastl = eastl::fixed_vector<ui::Fill, 64, false>;
using Stl = std::vector<ui::Fill>;

template<class Container>
uint32_t populate(Container &fills, unsigned frame) {
    fills.clear();
    for (int index = 0; index < 64; ++index)
        fills.push_back({{index, static_cast<int>(frame % 200), index + 1, 1}, {37, 57, 87}});
    auto *commands = fills.data();
    __asm__ volatile("" : : "g"(commands) : "memory");
    uint32_t checksum = 0;
    for (const auto &fill : fills)
        checksum += static_cast<uint32_t>(fill.bounds.x + fill.bounds.y + fill.bounds.width + fill.bounds.height +
                                         fill.color.red + fill.color.green + fill.color.blue);
    return checksum;
}

template<class Container, bool Reuse>
ContainerBatch measure(unsigned frames, unsigned seed) {
    ContainerBatch batch{true, 0, sizeof(Container), 0};
    if constexpr (Reuse) {
        Container fills;
        fills.reserve(64);
        if constexpr (std::is_same_v<Container, Stl>) batch.heap_capacity_bytes = fills.capacity() * sizeof(ui::Fill);
        for (unsigned frame = 0; frame < frames; ++frame) batch.checksum += populate(fills, frame + seed);
    } else {
        for (unsigned frame = 0; frame < frames; ++frame) {
            Container fills;
            fills.reserve(64);
            if constexpr (std::is_same_v<Container, Stl>) batch.heap_capacity_bytes = fills.capacity() * sizeof(ui::Fill);
            batch.checksum += populate(fills, frame + seed);
        }
    }
    return batch;
}
}

ContainerBatch container_batch(unsigned kind, unsigned frames, unsigned seed) {
    if (frames == 0 || frames > 20000 || seed >= 200) return {};
    switch (kind) {
    case 1: return measure<Etl, false>(frames, seed);
    case 2: return measure<Eastl, false>(frames, seed);
    case 3: return measure<Stl, false>(frames, seed);
    case 4: return measure<Etl, true>(frames, seed);
    case 5: return measure<Eastl, true>(frames, seed);
    case 6: return measure<Stl, true>(frames, seed);
    default: return {};
    }
}
}
