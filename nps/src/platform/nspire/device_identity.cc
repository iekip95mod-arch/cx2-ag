#include "nps/platform/nspire/device_identity.h"

namespace nps {
namespace {

struct OsVersion {
    const char *name;
    CasBuild cas;
    bool cx2;
};

// Copied from ndl/src/resources/utils.c, where ut_read_os_version_index assigns each index and
// names each build in a comment. 14 and 15 are gaps in that table, not builds.
constexpr OsVersion kOsVersions[kOsVersionIndexCount] = {
    {"3.1.0 non-CAS", CasBuild::NonCas, false},
    {"3.1.0 CAS", CasBuild::Cas, false},
    {"3.1.0 non-CAS CX", CasBuild::NonCas, false},
    {"3.1.0 CAS CX", CasBuild::Cas, false},
    {"3.1.0 CM-C", CasBuild::NonCas, false},
    {"3.1.0 CAS CM-C", CasBuild::Cas, false},
    {"3.6.0 non-CAS", CasBuild::NonCas, false},
    {"3.6.0 CAS", CasBuild::Cas, false},
    {"3.6.0 non-CAS CX", CasBuild::NonCas, false},
    {"3.6.0 CAS CX", CasBuild::Cas, false},
    {"3.9.0 non-CAS", CasBuild::NonCas, false},
    {"3.9.0 CAS", CasBuild::Cas, false},
    {"3.9.0 non-CAS CX", CasBuild::NonCas, false},
    {"3.9.0 CAS CX", CasBuild::Cas, false},
    {nullptr, CasBuild::Unknown, false},
    {nullptr, CasBuild::Unknown, false},
    {"3.9.1 non-CAS CX", CasBuild::NonCas, false},
    {"3.9.1 CAS CX", CasBuild::Cas, false},
    {"4.0.0.235 non-CAS CX", CasBuild::NonCas, false},
    {"4.0.0.235 CAS CX", CasBuild::Cas, false},
    {"4.0.3.93 non-CAS CX", CasBuild::NonCas, false},
    {"4.0.3.93 CAS CX", CasBuild::Cas, false},
    {"4.2.0.532 non-CAS CX", CasBuild::NonCas, false},
    {"4.2.0.532 CAS CX", CasBuild::Cas, false},
    {"4.3.0.702 non-CAS CX", CasBuild::NonCas, false},
    {"4.3.0.702 CAS CX", CasBuild::Cas, false},
    {"4.4.0.532 non-CAS CX", CasBuild::NonCas, false},
    {"4.4.0.532 CAS CX", CasBuild::Cas, false},
    {"4.5.0.1180 non-CAS CX", CasBuild::NonCas, false},
    {"4.5.0.1180 CAS CX", CasBuild::Cas, false},
    {"4.5.1.12 non-CAS CX", CasBuild::NonCas, false},
    {"4.5.1.12 CAS CX", CasBuild::Cas, false},
    {"4.5.3.14 non-CAS CX", CasBuild::NonCas, false},
    {"4.5.3.14 CAS CX", CasBuild::Cas, false},
    {"5.2.0.771 non-CAS CX II", CasBuild::NonCas, true},
    {"5.2.0.771 non-CAS CX II-T", CasBuild::NonCas, true},
    {"5.2.0.771 CAS CX II", CasBuild::Cas, true},
    {"4.5.4.48 non-CAS CX", CasBuild::NonCas, false},
    {"4.5.4.48 CAS CX", CasBuild::Cas, false},
    {"5.3.0.564 non-CAS CX II", CasBuild::NonCas, true},
    {"5.3.0.564 non-CAS CX II-T", CasBuild::NonCas, true},
    {"5.3.0.564 CAS CX II", CasBuild::Cas, true},
    {"4.5.5 non-CAS CX", CasBuild::NonCas, false},
    {"4.5.5 CAS CX", CasBuild::Cas, false},
    {"6.2.0.333 non-CAS CX II", CasBuild::NonCas, true},
    {"6.2.0.333 non-CAS CX II-T", CasBuild::NonCas, true},
    {"6.2.0.333 CAS CX II", CasBuild::Cas, true},
    {"6.4.0.74 non-CAS CX II", CasBuild::NonCas, true},
    {"6.4.0.74 non-CAS CX II-T", CasBuild::NonCas, true},
    {"6.4.0.74 CAS CX II", CasBuild::Cas, true},
};

bool known_index(unsigned os_version_index) {
    return os_version_index < kOsVersionIndexCount && kOsVersions[os_version_index].name != nullptr;
}

}

const char *calculator_model_name(CalculatorModel model) {
    switch (model) {
        case CalculatorModel::Classic:
            return "classic";
        case CalculatorModel::TouchpadCX:
            return "cx";
        case CalculatorModel::CM:
            return "cm";
        case CalculatorModel::CXII:
            return "cx2";
        case CalculatorModel::Unknown:
            break;
    }
    return "unknown";
}

const char *cas_build_name(CasBuild build) {
    switch (build) {
        case CasBuild::NonCas:
            return "non-cas";
        case CasBuild::Cas:
            return "cas";
        case CasBuild::Unknown:
            break;
    }
    return "unknown";
}

const char *os_version_name(unsigned os_version_index) {
    return known_index(os_version_index) ? kOsVersions[os_version_index].name : "unknown";
}

CasBuild os_version_cas(unsigned os_version_index) {
    return known_index(os_version_index) ? kOsVersions[os_version_index].cas : CasBuild::Unknown;
}

bool os_version_is_cx2(unsigned os_version_index) {
    return known_index(os_version_index) && kOsVersions[os_version_index].cx2;
}

DeviceIdentity interpret_device_identity(unsigned hardware_type, unsigned hardware_subtype,
                                         unsigned os_version_index, unsigned ndl_revision,
                                         bool loaded_by_third_party_loader) {
    DeviceIdentity identity;
    identity.hardware_type = hardware_type;
    identity.hardware_subtype = hardware_subtype;
    identity.os_version_index = os_version_index;
    identity.ndl_revision = ndl_revision;
    identity.loaded_by_third_party_loader = loaded_by_third_party_loader;
    identity.os_name = os_version_name(os_version_index);
    identity.cas = os_version_cas(os_version_index);

    if (hardware_subtype == 2)
        identity.model = CalculatorModel::CXII;
    else if (hardware_subtype == 1)
        identity.model = CalculatorModel::CM;
    else if (hardware_subtype == 0)
        identity.model = hardware_type == 0 ? CalculatorModel::Classic : CalculatorModel::TouchpadCX;

    // nl_hwsubtype reads the ASIC model register and the OS index is read out of OS memory, so
    // these two are independent and agreement means something. nl_hwtype is not a third reading:
    // ndl derives it from the OS index, so it can only ever repeat what the table already says.
    // Both readings have to be recognised before they can corroborate each other: an unmapped
    // hardware subtype leaves the model unknown, and unknown is not CX II, which would otherwise
    // read as agreement with every OS index that is not a CX II build.
    identity.model_agrees_with_os =
        known_index(os_version_index) && identity.model != CalculatorModel::Unknown &&
        os_version_is_cx2(os_version_index) == (identity.model == CalculatorModel::CXII);
    return identity;
}

}
