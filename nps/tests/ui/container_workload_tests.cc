#include "container_workload.h"

#include <array>
#include <iostream>

int main() {
    unsigned checks = 0;
    for (unsigned kind = 1; kind <= 6; ++kind) {
        for (unsigned frames : {1u, 2u, 199u, 200u, 201u, 20000u}) {
            for (unsigned seed : {0u, 1u, 199u}) {
                uint32_t expected = 0;
                for (unsigned frame = 0; frame < frames; ++frame) expected += 15744 + 64 * ((frame + seed) % 200);
                const auto batch = nps::test::container_batch(kind, frames, seed);
                if (!batch.valid || batch.checksum != expected || batch.object_bytes == 0 ||
                    (kind % 3 == 0 ? batch.heap_capacity_bytes < 1280 : batch.heap_capacity_bytes != 0)) return 1;
                ++checks;
            }
        }
    }
    for (const auto &arguments : std::array<std::array<unsigned, 3>, 5>{{
             {0, 1, 0}, {7, 1, 0}, {1, 0, 0}, {1, 20001, 0}, {1, 1, 200}}}) {
        if (nps::test::container_batch(arguments[0], arguments[1], arguments[2]).valid) return 1;
        ++checks;
    }
    std::cout << "container workload: " << checks << " checks passed\n";
}
