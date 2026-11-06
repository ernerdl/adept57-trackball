#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

// Mac'e sunulan rapor: 16 buton + 16-bit X/Y + tekerlek
typedef struct __attribute__((packed)) {
    uint16_t buttons;   // bit0=L bit1=R bit2=M bit3/4=mouse yan, bit5+ = harici GPIO
    int16_t  x;
    int16_t  y;
    int8_t   wheel;
    int8_t   pan;       // AC Pan: yatay kaydırma
} bridge_report_t;

#endif
