#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// RP2040 Zero köprü: native USB = device (Mac), PIO-USB = host (mouse)
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUSB_RHPORT1_MODE   OPT_MODE_HOST
#define CFG_TUSB_OS             OPT_OS_PICO

#define BOARD_TUD_RHPORT        0
#define BOARD_TUH_RHPORT        1

// Host, PIO-USB üzerinden
#define CFG_TUH_RPI_PIO_USB     1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE (Mac tarafı)
//--------------------------------------------------------------------
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_HID             2   // 0: mouse, 1: ayar kanalı (WebHID)
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_HID_EP_BUFSIZE  32   // ayar feature raporu 24B — kontrol tamponu buna göre

//--------------------------------------------------------------------
// HOST (mouse tarafı)
//--------------------------------------------------------------------
#define CFG_TUH_ENABLED         1
#define CFG_TUH_HUB             0
#define CFG_TUH_HID             4      // mouse composite: 2 interface + pay
#define CFG_TUH_DEVICE_MAX      1
#define CFG_TUH_ENUMERATION_BUFSIZE 512

#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif
