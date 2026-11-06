#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------
// HID Report Descriptor: 16 buton, 16-bit X/Y, 8-bit tekerlek
//--------------------------------------------------------------------
uint8_t const desc_hid_report[] = {
    // ---- Rapor 1: Mouse ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Min (1)
    0x29, 0x10,        //     Usage Max (16)
    0x15, 0x00,        //     Logical Min (0)
    0x25, 0x01,        //     Logical Max (1)
    0x95, 0x10,        //     Report Count (16)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x01, 0x80,  //     Logical Min (-32767)
    0x26, 0xFF, 0x7F,  //     Logical Max (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Min (-127)
    0x25, 0x7F,        //     Logical Max (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan) — yatay kaydırma
    0x15, 0x81,        //     Logical Min (-127)
    0x25, 0x7F,        //     Logical Max (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
    // ---- Rapor 2: Klavye (kısayol görevleri için) ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x05, 0x07,        //   Usage Page (Kbd/Keypad)
    0x19, 0xE0,        //   Usage Min (LCtrl)
    0x29, 0xE7,        //   Usage Max (RGui)
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08,
    0x81, 0x02,        //   Input: 8 modifier biti
    0x75, 0x08, 0x95, 0x01,
    0x81, 0x01,        //   Input: reserved
    0x05, 0x07,        //   Usage Page (Kbd/Keypad)
    0x19, 0x00, 0x29, 0x65,
    0x15, 0x00, 0x25, 0x65,
    0x75, 0x08, 0x95, 0x06,
    0x81, 0x00,        //   Input: 6 tuş dizisi
    0xC0,              // End Collection
    // ---- Rapor 3: Medya (Consumer Control) ----
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x15, 0x00, 0x26, 0xFF, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01,
    0x81, 0x00,        //   Input: 16-bit usage
    0xC0               // End Collection
};

//--------------------------------------------------------------------
// Ayar kanalı (Interface 1): vendor sayfası — Chrome WebHID buna izin verir
// Feature raporlarıyla konuşulur (8 bayt), Input sadece arayüz şartı için
//--------------------------------------------------------------------
uint8_t const desc_cfg_report[] = {
    0x06, 0x60, 0xFF,        // Usage Page (Vendor 0xFF60)
    0x09, 0x61,              // Usage (0x61)
    0xA1, 0x01,              // Collection (Application)
    0x09, 0x62,              //   Usage (0x62)
    0x15, 0x00,              //   Logical Min (0)
    0x26, 0xFF, 0x00,        //   Logical Max (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x08,              //   Report Count (8)
    0x81, 0x02,              //   Input (Data,Var,Abs)
    0x09, 0x63,              //   Usage (0x63)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x18,              //   Report Count (24)
    0xB1, 0x02,              //   Feature (Data,Var,Abs)
    0xC0                     // End Collection
};

//--------------------------------------------------------------------
// Device Descriptor
//--------------------------------------------------------------------
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,   // Raspberry Pi
    .idProduct          = 0x107B,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------
// Configuration Descriptor
//--------------------------------------------------------------------
enum { ITF_NUM_HID, ITF_NUM_CFG, ITF_NUM_TOTAL };

#define EPNUM_HID 0x81
#define EPNUM_CFG 0x82
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // Interface 0: mouse, boot değil (16 buton için report protocol), 1ms polling
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
    // Interface 1: ayar kanalı (WebHID), 10ms polling yeterli
    TUD_HID_DESCRIPTOR(ITF_NUM_CFG, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_cfg_report), EPNUM_CFG,
                       CFG_TUD_HID_EP_BUFSIZE, 10)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return (instance == ITF_NUM_CFG) ? desc_cfg_report : desc_hid_report;
}

//--------------------------------------------------------------------
// String Descriptors
//--------------------------------------------------------------------
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: dil = İngilizce (0x0409)
    "ErenWorks",                 // 1: üretici
    "Trackball Bridge",          // 2: ürün
    "TB57-001",                  // 3: seri no
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
