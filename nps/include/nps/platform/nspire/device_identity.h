#ifndef NPS_PLATFORM_NSPIRE_DEVICE_IDENTITY_H
#define NPS_PLATFORM_NSPIRE_DEVICE_IDENTITY_H

#include <cstddef>
#include <cstdint>

namespace nps {

enum class CalculatorModel : uint8_t {
    Unknown,
    Classic,
    TouchpadCX,
    CM,
    CXII,
};

enum class CasBuild : uint8_t {
    Unknown,
    NonCas,
    Cas,
};

struct DeviceIdentity {
    CalculatorModel model = CalculatorModel::Unknown;
    CasBuild cas = CasBuild::Unknown;
    unsigned hardware_type = 0;
    unsigned hardware_subtype = 0;
    unsigned os_version_index = 0;
    unsigned ndl_revision = 0;
    const char *os_name = nullptr;
    bool loaded_by_third_party_loader = false;
    bool model_agrees_with_os = false;
};

inline constexpr unsigned kOsVersionIndexCount = 50;

const char *calculator_model_name(CalculatorModel model);
const char *cas_build_name(CasBuild build);

const char *os_version_name(unsigned os_version_index);
CasBuild os_version_cas(unsigned os_version_index);
bool os_version_is_cx2(unsigned os_version_index);

DeviceIdentity interpret_device_identity(unsigned hardware_type, unsigned hardware_subtype,
                                         unsigned os_version_index, unsigned ndl_revision,
                                         bool loaded_by_third_party_loader);

}

#endif
