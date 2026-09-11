#include <stdio.h>
#include "../lcd.c"

int main() {
    product = 0x1c0;
    unsigned checks = 0, failures = 0;
    const uint32_t offsets[] = {0x200u, 0x3fcu, 0x800u, 0xbfcu};
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        uint32_t offset = offsets[i];
        memset(&lcd, 0x5a, sizeof lcd);
        lcd_state expected;
        memcpy(&expected, &lcd, sizeof lcd);
        const uint32_t value = 0x80402010u + offset;
        uint8_t *bytes = offset < 0x400
            ? (uint8_t *)expected.palette + (offset - 0x200)
            : expected.cursor_ram + (offset - 0x800);
        memcpy(bytes, &value, sizeof value);
        lcd_write_word(offset, value);
        ++checks;
        if (memcmp(&lcd, &expected, sizeof lcd)) {
            ++failures;
            fprintf(stderr, "FAIL LCD write changed neighboring state at %u\n", offset);
        }
        ++checks;
        if (lcd_read_word(offset) != value) {
            ++failures;
            fprintf(stderr, "FAIL LCD word read at %u\n", offset);
        }
    }
    printf("lcdtest: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
