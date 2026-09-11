#ifndef NPS_TEST_CONTAINER_WORKLOAD_H
#define NPS_TEST_CONTAINER_WORKLOAD_H

#include <cstddef>
#include <cstdint>

namespace nps::test {
struct ContainerBatch {
    bool valid = false;
    uint32_t checksum = 0;
    size_t object_bytes = 0;
    size_t heap_capacity_bytes = 0;
};
ContainerBatch container_batch(unsigned kind, unsigned frames, unsigned seed);
}
#endif
