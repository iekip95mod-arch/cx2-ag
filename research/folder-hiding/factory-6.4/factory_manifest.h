#ifndef CX2_FACTORY_64_MANIFEST_H
#define CX2_FACTORY_64_MANIFEST_H

#include <stdint.h>

struct factory_file {
    const char *path;
    const char *locale;
    uint32_t size;
    uint32_t crc32;
};

static const struct factory_file factory_files[] = {
    { "MyLib/linalg.tns", "", UINT32_C(8532), UINT32_C(0x67f38430) },
    { "MyLib/numtheory.tns", "", UINT32_C(26343), UINT32_C(0x33dbb66e) },
    { "MyWidgets/stopwatch.tns", "", UINT32_C(16798), UINT32_C(0x4ccee25d) },
    { "PyLib/ti_hub.tns", "", UINT32_C(13845), UINT32_C(0x6922ea4a) },
    { "PyLib/ti_image.tns", "", UINT32_C(2925), UINT32_C(0xefa4d9cf) },
    { "PyLib/ti_plotlib.tns", "", UINT32_C(6331), UINT32_C(0x75a8fd7b) },
    { "PyLib/ti_rover.tns", "", UINT32_C(8159), UINT32_C(0xce9a8940) },
    { "PyLib/ti_system.tns", "", UINT32_C(2416), UINT32_C(0x73b5d229) },
    { "themes.csv", "", UINT32_C(191), UINT32_C(0x93079871) },
    { "Examples/Kom godt i gang.tns", "da", UINT32_C(103394), UINT32_C(0x8c76c80e) },
    { "Examples/Erste Schritte.tns", "de", UINT32_C(103829), UINT32_C(0x4faf7140) },
    { "Examples/Getting Started Python.tns", "en", UINT32_C(329262), UINT32_C(0xc88e7464) },
    { "Examples/Getting Started.tns", "en", UINT32_C(105543), UINT32_C(0x528eb457) },
    { "Examples/Getting Started.tns", "en_GB", UINT32_C(104322), UINT32_C(0xc9984edd) },
    { "Examples/Primeros pasos.tns", "es", UINT32_C(103739), UINT32_C(0x0f15c24e) },
    { "Examples/Aloitusohjeita.tns", "fi", UINT32_C(102827), UINT32_C(0xf174b777) },
    { "Examples/Guide d'introduction.tns", "fr", UINT32_C(104666), UINT32_C(0xaff9d00a) },
    { "Examples/Guida introduttiva.tns", "it", UINT32_C(103576), UINT32_C(0x967e4d87) },
    { "Examples/Aan de slag.tns", "nl", UINT32_C(103713), UINT32_C(0x34fca800) },
    { "Examples/Aan slag.tns", "nl_BE", UINT32_C(103586), UINT32_C(0x43b075d7) },
    { "Examples/Kom i gang.tns", "no", UINT32_C(103251), UINT32_C(0xf36f6d04) },
    { "Examples/Iniciar a Utilizar.tns", "pt", UINT32_C(103565), UINT32_C(0x1d54e8f8) },
    { "Examples/Kom igang.tns", "sv", UINT32_C(103803), UINT32_C(0x961f3f1b) },
    { "Examples/Getting_Started_ZH_CN.tns", "zh_CN", UINT32_C(99895), UINT32_C(0x14cef41f) },
    { "Examples/Getting_Started_ZH_TW.tns", "zh_TW", UINT32_C(99785), UINT32_C(0xcbdfb3be) },
};

#define FACTORY_FILE_COUNT (sizeof factory_files / sizeof factory_files[0])

#endif
