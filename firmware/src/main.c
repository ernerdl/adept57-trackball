// Trackball Bridge — RP2040 Zero
// Native USB (rhport0) : Mac'e HID mouse (device)
// PIO-USB   (rhport1) : Claw's Nexus X1'e host — core1'de koşar
// GPIO                : harici trackball butonları (aktif-düşük, dahili pull-up)
//
// Mouse raporu (Interface 0, Report ID 1, 7 bayt):
//   [ID=1][btn(5bit)][Xlo][Xhi][Ylo][Yhi][wheel]

#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/flash.h"
#include "pio_usb.h"
#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------
// Yapılandırma
//--------------------------------------------------------------------
#define PIN_USB_DP        28   // PIO-USB D+ (D- otomatik: DP+1 = GPIO29) — sağ üst köşe, 5V/GND yanı: derli demet
#define EXT_BTN_COUNT     6
// Kalıcı lehim düzeni (2026-08-12). FİZİKSEL YERLEŞİM 2026-10-22'DE ÖLÇÜLDÜ —
// önceki yorumlardaki yay isimleri AYNALIYDI, doğrusu:
//   GP26 = sol büyük (alt)    GP6  = sağ büyük (alt)
//   GP10 = SAĞ üst            GP7  = SAĞ orta
//   GP13 = SOL orta           GP14 = SOL üst
// Varsayılan görevler (btn_map sırası = EXT_BTN_PINS sırası):
//   bit0 SOL TIK, bit1 SAĞ TIK, bit3 GERİ, bit2 ORTA TIK, bit4 İLERİ, bit5 boş
static const uint8_t EXT_BTN_PINS[EXT_BTN_COUNT] = {26, 6, 10, 7, 13, 14};
static const uint8_t EXT_BTN_DEFAULT[EXT_BTN_COUNT] = { 0, 1,  3, 2,  4,  5};
#define DEBOUNCE_US       4000

//--------------------------------------------------------------------
// Canlı tuş haritası + flash kalıcılığı
// btn_map[i] = EXT_BTN_PINS[i]'nin HID buton biti (0=sol,1=sağ,2=orta,
// 3=geri, 4=ileri, 5+=ekstra), 0xFE = drag-scroll (bas-çevir), 0xFF = devre dışı.
// Ayar sayfası (WebHID, Interface 1 feature raporları):
//   SET: [0x01, m0..m5, 0]  → anında uygula (RAM)
//   SAVE:[0x02, ...]        → flash'a yaz + temiz yeniden başlat
//   GET: cihazdan 8 bayt: [0xA5, m0..m5, xor-checksum]
//--------------------------------------------------------------------
#define MAP_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define MAP_MAGIC 0x5442354Eu   // "TB5N" — kayıt düzeni v4 (+kombinasyon)
// Görev kodları (harita baytı):
//   0-15  : HID fare buton biti
//   0x80+ : hazır eylem tablosu (klavye kısayolu / medya tuşu)
//   0xFA  : drag lock (sol tık kilidi, bas/bırak)
//   0xFB  : Fn katmanı (basılıyken 2. harita)
//   0xFC  : scroll aç/kapa    0xFD : hız kademesi döngüsü
//   0xFE  : scroll bas-tut    0xFF : devre dışı
#define LAYER_COUNT 2
static uint8_t btn_map[LAYER_COUNT][EXT_BTN_COUNT];

// Hazır eylemler: {modifier, tuş, medya_usage}; medya!=0 ise medya tuşudur
typedef struct { uint8_t mod, key; uint16_t cons; } action_t;
static const action_t ACTIONS[] = {
    {0x08, 0x06, 0},      // 0x80 Kopyala (Cmd+C)
    {0x08, 0x19, 0},      // 0x81 Yapıştır (Cmd+V)
    {0x08, 0x1B, 0},      // 0x82 Kes (Cmd+X)
    {0x08, 0x1D, 0},      // 0x83 Geri al (Cmd+Z)
    {0x00, 0x28, 0},      // 0x84 Enter
    {0x00, 0x29, 0},      // 0x85 Esc
    {0x01, 0x52, 0},      // 0x86 Mission Control (Ctrl+Yukarı)
    {0x08, 0x2B, 0},      // 0x87 Uygulama değiştir (Cmd+Tab)
    {0, 0, 0x00CD},       // 0x88 Oynat/Duraklat
    {0, 0, 0x00E9},       // 0x89 Ses +
    {0, 0, 0x00EA},       // 0x8A Ses −
    {0, 0, 0x00E2},       // 0x8B Sessiz
    {0, 0, 0x00B5},       // 0x8C Sonraki parça
    {0, 0, 0x00B6},       // 0x8D Önceki parça
};
#define ACTION_N (sizeof(ACTIONS)/sizeof(ACTIONS[0]))
static const uint8_t SPEED_STEPS[] = {50, 75, 100, 125};
static uint8_t cfg_speed = 75;   // imleç hızı, yüzde (raw × speed / 100)
static uint8_t cfg_sdiv  = 96;   // drag-scroll böleni (küçük = hızlı kaydırma)
static uint8_t cfg_sinv  = 0;    // drag-scroll yönü (1 = ters)
static uint8_t chord_a = 0xFF, chord_b = 0xFF, chord_act = 0xFF;  // kombinasyon tanımı
static volatile bool map_save_pending = false;

typedef struct {
    uint32_t magic;
    uint8_t map[LAYER_COUNT][EXT_BTN_COUNT];
    uint8_t speed, sdiv, sinv;
    uint8_t ca, cb, cact;
    uint8_t chk;
} map_record_t;

static uint8_t cfg_checksum(const uint8_t m[LAYER_COUNT][EXT_BTN_COUNT],
                            uint8_t sp, uint8_t sd, uint8_t si,
                            uint8_t ca, uint8_t cb, uint8_t cact) {
    uint8_t c = 0x5A;
    for (int l = 0; l < LAYER_COUNT; l++)
        for (int i = 0; i < EXT_BTN_COUNT; i++) c ^= (uint8_t)(m[l][i] + i + l * 7);
    c ^= sp; c ^= (uint8_t)(sd + 1); c ^= (uint8_t)(si + 2);
    c ^= (uint8_t)(ca + 3); c ^= (uint8_t)(cb + 4); c ^= (uint8_t)(cact + 5);
    return c;
}

static void map_load(void) {
    const uint8_t *raw = (const uint8_t *)(XIP_BASE + MAP_FLASH_OFFSET);
    const map_record_t *rec = (const map_record_t *)raw;
    uint32_t magic = rec->magic;
    bool loaded = false;

    if (magic == MAP_MAGIC &&
        rec->chk == cfg_checksum(rec->map, rec->speed, rec->sdiv, rec->sinv,
                                 rec->ca, rec->cb, rec->cact)) {
        memcpy(btn_map, rec->map, sizeof(btn_map));
        cfg_speed = rec->speed; cfg_sdiv = rec->sdiv; cfg_sinv = rec->sinv;
        chord_a = rec->ca; chord_b = rec->cb; chord_act = rec->cact;
        loaded = true;
    } else if (magic == 0x5442354Du) {
        // GÖÇ: TB5M (Fn katmanlı, kombinasyonsuz) — eski kaydı taşı
        const uint8_t *m = raw + 4;
        uint8_t sp = raw[16], sd = raw[17], si = raw[18], chk = raw[19];
        uint8_t c = 0x5A;
        for (int l = 0; l < 2; l++)
            for (int i = 0; i < 6; i++) c ^= (uint8_t)(m[l*6+i] + i + l*7);
        c ^= sp; c ^= (uint8_t)(sd + 1); c ^= (uint8_t)(si + 2);
        if (c == chk) {
            memcpy(btn_map, m, 12);
            cfg_speed = sp; cfg_sdiv = sd; cfg_sinv = si;
            loaded = true;
        }
    } else if (magic == 0x5442354Cu) {
        // GÖÇ: TB5L (tek katman + hızlar)
        const uint8_t *m = raw + 4;
        uint8_t sp = raw[10], sd = raw[11], si = raw[12], chk = raw[13];
        uint8_t c = 0x5A;
        for (int i = 0; i < 6; i++) c ^= (uint8_t)(m[i] + i);
        c ^= sp; c ^= (uint8_t)(sd + 1); c ^= (uint8_t)(si + 2);
        if (c == chk) {
            memcpy(btn_map[0], m, 6);
            memset(btn_map[1], 0xFF, 6);
            cfg_speed = sp; cfg_sdiv = sd; cfg_sinv = si;
            loaded = true;
        }
    }
    if (!loaded) {
        memcpy(btn_map[0], EXT_BTN_DEFAULT, EXT_BTN_COUNT);
        memset(btn_map[1], 0xFF, EXT_BTN_COUNT);   // Fn katmanı boş başlar
    }
    if (cfg_speed < 10 || cfg_speed > 250) cfg_speed = 75;
    if (cfg_sdiv < 8) cfg_sdiv = 96;
}

static void map_flash_write_unsafe(void *param) {
    (void)param;
    static uint8_t page[FLASH_PAGE_SIZE];
    map_record_t rec = { .magic = MAP_MAGIC, .speed = cfg_speed,
                         .sdiv = cfg_sdiv, .sinv = cfg_sinv,
                         .ca = chord_a, .cb = chord_b, .cact = chord_act };
    memcpy(rec.map, btn_map, sizeof(btn_map));
    rec.chk = cfg_checksum(rec.map, rec.speed, rec.sdiv, rec.sinv,
                           rec.ca, rec.cb, rec.cact);
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &rec, sizeof(rec));
    flash_range_erase(MAP_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(MAP_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
}

// İmleç hızı (cfg_speed %), scroll hızı (cfg_sdiv) ve scroll yönü (cfg_sinv)
// artık ayar sayfasından canlı ayarlanıp flash'ta saklanıyor — aşağıda tanımlı.

// Trackball aynalama: sensör topa alttan baktığı için X ekseni ters döner.
// Yön yanlışsa bu ikisiyle oyna (1=çevir, 0=dokunma).
#define INVERT_X          0
#define INVERT_Y          1

//--------------------------------------------------------------------
// Paylaşılan durum (core1 üretir, core0 tüketir)
//--------------------------------------------------------------------
static critical_section_t state_cs;
static volatile int32_t acc_dx = 0, acc_dy = 0, acc_wheel = 0, acc_pan = 0;
static volatile uint8_t mouse_buttons = 0;   // mouse'tan gelen 5 bit
static volatile bool    mouse_dirty = false;

static uint16_t ext_buttons = 0;             // debounce edilmiş GPIO durumu (haritalanmış)
static volatile uint8_t raw_btn_mask = 0;    // ham fiziksel tuş maskesi (index=EXT_BTN_PINS sırası)
static volatile bool raw_btn_dirty = false;  // ayar sayfasına bildirilecek değişiklik var
static volatile bool drag_scroll_held = false; // scroll aktif (bas-tut veya aç/kapa)

// Görev motoru durumu
static uint8_t cur_action[EXT_BTN_COUNT] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint8_t kb_mod = 0, kb_key = 0;       // klavye raporu (tek slot)
static bool kb_dirty = false;
static int8_t kb_owner = -1;
static uint16_t cons_code = 0;               // medya raporu
static bool cons_dirty = false;
static int8_t cons_owner = -1;
static uint8_t layer_cnt = 0, ds_cnt = 0;
static bool ds_toggle = false;
static bool drag_lock = false;   // 0xFA: sol tık kilidi (bas=kilitle, tekrar bas=bırak)
static uint64_t drag_lock_son_hareket = 0;   // kilit açıkken son top hareketi

// Kombinasyon (akor): chord_a+chord_b tuşlarına birlikte basılınca chord_act çalışır.
// Üye tuşların tekil aksiyonu CHORD_US kadar bekletilir (partner gelecek mi?).
#define CHORD_US   50000
#define TAP_MIN_US 15000
#define CHORD_OWNER 6            // kb/medya sahiplik takibi için sanal indeks
static bool chord_on = false;
static bool pend[EXT_BTN_COUNT];
static uint64_t pend_t[EXT_BTN_COUNT];
static uint64_t force_rel_at[EXT_BTN_COUNT];

static bool chord_enabled(void) {
    return chord_a < EXT_BTN_COUNT && chord_b < EXT_BTN_COUNT &&
           chord_a != chord_b && chord_act != 0xFF;
}

static void action_press(int i, uint8_t a) {
    // Güvenlik (2026-10-08): drag kilidi açıkken SOL TIK'a basmak kilidi bırakır.
    // Kazara kilitlenince kullanıcının ilk yaptığı şey tıklamayı denemek oluyor;
    // eskiden hiçbir yere tıklayamayıp kilitli kalıyordu.
    if (drag_lock && a == 0) drag_lock = false;
    if (a <= 15) { ext_buttons |= (uint16_t)(1u << a); }
    else if (a >= 0x80 && a < 0x80 + ACTION_N) {
        const action_t *ac = &ACTIONS[a - 0x80];
        if (ac->cons) { cons_code = ac->cons; cons_dirty = true; cons_owner = (int8_t)i; }
        else { kb_mod = ac->mod; kb_key = ac->key; kb_dirty = true; kb_owner = (int8_t)i; }
    }
    else if (a == 0xFB) layer_cnt++;
    else if (a == 0xFC) ds_toggle = !ds_toggle;
    else if (a == 0xFD) {   // hız kademesi döngüsü
        unsigned next = 0;
        for (unsigned j = 0; j < sizeof(SPEED_STEPS); j++)
            if (cfg_speed == SPEED_STEPS[j]) { next = j + 1; break; }
        cfg_speed = SPEED_STEPS[next % sizeof(SPEED_STEPS)];
    }
    else if (a == 0xFE) ds_cnt++;
}

static void action_release(int i, uint8_t a) {
    if (a <= 15) ext_buttons &= (uint16_t)~(1u << a);
    else if (a >= 0x80 && a < 0x80 + ACTION_N) {
        const action_t *ac = &ACTIONS[a - 0x80];
        if (ac->cons) { if (cons_owner == (int8_t)i) { cons_code = 0; cons_dirty = true; cons_owner = -1; } }
        else if (kb_owner == (int8_t)i) { kb_mod = 0; kb_key = 0; kb_dirty = true; kb_owner = -1; }
    }
    else if (a == 0xFB && layer_cnt) layer_cnt--;
    else if (a == 0xFE && ds_cnt) ds_cnt--;
}
static volatile bool host_mounted = false;   // mouse el sıkıştı mı
#define RETRY_MAGIC 0xB007
#define DIAG_MAGIC  0xD1A6
#define FLASH_MAGIC 0xF1A5   // bootloader'a bilerek düştük (flash) işareti

//--------------------------------------------------------------------
// Olay günlüğü (2026-10-01): kopmaların sebebini cihazın kendisi kaydetsin.
// .uninitialized_data bölümünde durur → watchdog/reset sonrası KORUNUR,
// yalnızca elektrik tamamen kesilince sıfırlanır.
// Ayar kanalından okunur: SET [0x07, sayfa] → GET [0xA6, sayfa, adet, kayıtlar]
//--------------------------------------------------------------------
#define LOG_MAGIC 0x4C4F4732u   // "LOG2" — kesintisiz saat eklendi
#define LOG_CAP   64
enum {
    EV_BOOT_GUC = 1,    // elektrik gelip açıldı (soğuk açılış)
    EV_BOOT_WDG = 2,    // watchdog/reset sonrası açıldı
    EV_MAC_TAK  = 3,    // Mac cihazı tanıdı
    EV_MAC_KOP  = 4,    // Mac bağlantısı koptu
    EV_MAC_UYKU = 5,    // Mac hattı askıya aldı
    EV_MAC_UYAN = 6,    // Mac hattı uyandırdı
    EV_G403_TAK = 7,    // mouse kartı bağlandı
    EV_G403_KOP = 8,    // mouse kartı koptu
    EV_DNT_RST  = 9,    // denetçi: host yok → reset
    EV_KAYIT_RST= 10,   // ayar kaydı → reset
    EV_BOOT_DIP = 11,   // gerilim çöktü (RAM korundu, tam kesinti değil)
    EV_DP_HAZIR = 12,   // G403 veri hattını yukarı çekti (detay = bekleme/100ms)
    EV_KILIT    = 13,   // kilit durumu değişti (detay bit0=drag bit1=scroll bit2=Fn)
};
typedef struct { uint32_t ms; uint8_t ev; uint8_t detay; } log_kayit_t;
typedef struct {
    uint32_t magic;
    uint16_t head;      // sıradaki yazma yeri
    uint16_t toplam;    // yazılan toplam kayıt (taşma takibi)
    uint32_t taban_ms;  // resetler boyunca süren saat: bu oturumun başlangıç değeri
    uint32_t son_ms;    // en son bilinen kesintisiz saat (reset sonrası taban olur)
    log_kayit_t k[LOG_CAP];
} log_t;
static log_t __uninitialized_ram(g_log);
static uint8_t log_sayfa = 0;   // ayar kanalının okuyacağı sayfa (0 = ayarlar)

static void log_ekle(uint8_t ev, uint8_t detay) {
    if (g_log.magic != LOG_MAGIC) {   // elektrik kesilmiş: günlüğü kur
        g_log.magic = LOG_MAGIC; g_log.head = 0; g_log.toplam = 0;
        g_log.taban_ms = 0; g_log.son_ms = 0;
    }
    uint32_t g = g_log.taban_ms + to_ms_since_boot(get_absolute_time());
    g_log.son_ms = g;
    g_log.k[g_log.head] = (log_kayit_t){ g, ev, detay };
    g_log.head = (uint16_t)((g_log.head + 1) % LOG_CAP);
    if (g_log.toplam < 0xFFFF) g_log.toplam++;
}
static uint16_t diag_hostlost = 0;   // host bağlantısı koptuğu için kaç kez resetlendi
static uint16_t diag_boots = 0;      // elektrik geldiğinden beri kaçıncı açılış
static bool soguk_acilis = false;    // elektrik yeni geldi (watchdog reseti değil)
static uint16_t host_bekleme = 600;  // core1'in host'u başlatmadan önce bekleyeceği ms

//--------------------------------------------------------------------
// HOST tarafı (core1): mouse'u dinle
//--------------------------------------------------------------------
static void core1_main(void) {
    // Ayar kaydı sırasında core0 flash yazar; bu core o an güvenle durdurulabilsin
    flash_safe_execute_core_init();
    // Host'u başlatmadan önce nefes payı. Soğuk açılışta (dock sabah takılınca)
    // G403 elektriği yeni alıyor; 600ms'de hazır olmuyordu ve el sıkışma kaçıyordu
    // → denetçi 3 kez resetliyor, toplam ~15sn (2026-10-05 günlüğünden teyitli).
    // Reset sonrası açılışta mouse zaten ayakta, kısa bekleme yeterli.
    // Sabit süre TAHMİN etmek yerine cihazın hazır olmasını ÖLÇ (2026-10-13):
    // G403 USB yığınını kurunca D+ hattını yukarı çeker. 400ms kesintisiz yüksek
    // görene kadar bekle (en çok 10sn), sonra kısa oturma payı ver.
    gpio_init(PIN_USB_DP);
    gpio_set_dir(PIN_USB_DP, GPIO_IN);
    gpio_disable_pulls(PIN_USB_DP);
    uint32_t t0ms = to_ms_since_boot(get_absolute_time());
    uint32_t yuksek = 0;
    while (to_ms_since_boot(get_absolute_time()) - t0ms < 10000) {
        if (gpio_get(PIN_USB_DP)) { yuksek += 5; if (yuksek >= 400) break; }
        else yuksek = 0;
        sleep_ms(5);
    }
    uint32_t gecen = to_ms_since_boot(get_absolute_time()) - t0ms;
    log_ekle(EV_DP_HAZIR, (uint8_t)(gecen / 100 > 255 ? 255 : gecen / 100));
    sleep_ms(host_bekleme);
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(BOARD_TUH_RHPORT);
    while (true) {
        tuh_task();
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    if (!host_mounted) log_ekle(EV_G403_TAK, instance);
    host_mounted = true;
    watchdog_hw->scratch[0] = 0;   // başarı: tekrar-deneme bayrağını temizle
    // Her HID arayüzünü dinlemeye başla; ayıklama report ID ile yapılır
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr; (void)instance;
    log_ekle(EV_G403_KOP, instance);
    host_mounted = false;          // bağlantı koptu: denetçi yeniden kursun
    critical_section_enter_blocking(&state_cs);
    mouse_buttons = 0;
    mouse_dirty = true;
    critical_section_exit(&state_cs);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    uint8_t btn = 0;
    int16_t dx = 0, dy = 0;
    int8_t  wh = 0, pn = 0;
    bool ok = false;

    if (len == 8 && instance == 0) {
        // G403 Prodigy: ID'siz, [btn16][X16][Y16][wheel][pan]
        // SADECE mouse arayüzü (instance 0) — 2. arayüzün (makro/medya)
        // 8 baytlık raporları hareket sanılıyordu → "geri kayma" sıçramaları
        btn = report[0] & 0x1F;
        dx  = (int16_t)(report[2] | (report[3] << 8));
        dy  = (int16_t)(report[4] | (report[5] << 8));
        wh  = (int8_t)report[6];
        pn  = (int8_t)report[7];   // G403'ün kendi yatay tekerlek baytı
        ok = true;
    } else if (len >= 7 && report[0] == 1) {
        // Claw's Nexus X1 rapor modu: ID 1, 16-bit deltalar
        btn = report[1] & 0x1F;
        dx  = (int16_t)(report[2] | (report[3] << 8));
        dy  = (int16_t)(report[4] | (report[5] << 8));
        wh  = (int8_t)report[6];
        ok = true;
    } else if (proto == HID_ITF_PROTOCOL_MOUSE && len >= 3) {
        // Genel boot mouse yedeği (başka mouse takılırsa)
        btn = report[0] & 0x1F;
        dx  = (int8_t)report[1];
        dy  = (int8_t)report[2];
        wh  = (len > 3) ? (int8_t)report[3] : 0;
        ok = true;
    }
    // Interface 1 (klavye/media/vendor) raporları sessizce düşer

    if (ok) {
#if INVERT_X
        dx = -dx;
#endif
#if INVERT_Y
        dy = -dy;
#endif
        critical_section_enter_blocking(&state_cs);
        acc_dx += dx;
        acc_dy += dy;
        acc_wheel += wh;
        acc_pan += pn;
        mouse_buttons = btn;
        mouse_dirty = true;
        critical_section_exit(&state_cs);
    }
    tuh_hid_receive_report(dev_addr, instance);
}

//--------------------------------------------------------------------
// GPIO butonları (core0): debounce'lu tarama
//--------------------------------------------------------------------
static void buttons_init(void) {
    for (int i = 0; i < EXT_BTN_COUNT; i++) {
        gpio_init(EXT_BTN_PINS[i]);
        gpio_set_dir(EXT_BTN_PINS[i], GPIO_IN);
        gpio_pull_up(EXT_BTN_PINS[i]);
    }
}

static bool deb_state[EXT_BTN_COUNT];      // debounce edilmiş fiziksel durum
static uint64_t basildi_us[EXT_BTN_COUNT]; // tuşa basılma anı (uzun basış ölçümü)
#define DRAG_UZUN_US 70000                 // drag kilidi için gereken basılı tutma
                                           // (ölçüm 2026-10-22: normal basış 127-200ms,
                                           //  darbe hiç tetiklemiyor → 250ms gereksizdi)

static bool buttons_scan(void) {
    static uint64_t last_change_us[EXT_BTN_COUNT];
    static bool raw_state[EXT_BTN_COUNT];
    bool changed = false;
    uint64_t now = time_us_64();

    for (int i = 0; i < EXT_BTN_COUNT; i++) {
        bool pressed = !gpio_get(EXT_BTN_PINS[i]);  // aktif-düşük
        if (pressed != raw_state[i]) {
            raw_state[i] = pressed;
            last_change_us[i] = now;
        } else if (now - last_change_us[i] >= DEBOUNCE_US && pressed != deb_state[i]) {
            deb_state[i] = pressed;
            // Ham kimlik maskesi: haritadan bağımsız, ayar sayfası "hangi tuş?" görsün
            if (pressed) raw_btn_mask |= (uint8_t)(1u << i);
            else         raw_btn_mask &= (uint8_t)~(1u << i);
            raw_btn_dirty = true;
            if (pressed) {
                if (chord_enabled() && (i == chord_a || i == chord_b)) {
                    int p = (i == chord_a) ? chord_b : chord_a;
                    if (pend[p]) {
                        // Partner pencerede bekliyordu → AKOR tetiklenir
                        pend[p] = false;
                        cur_action[i] = 0xF9; cur_action[p] = 0xF9;
                        chord_on = true;
                        action_press(CHORD_OWNER, chord_act);
                    } else {
                        pend[i] = true;   // partneri bekle
                        pend_t[i] = now;
                    }
                } else {
                    // Görev BASIŞ anındaki katmandan çözülür; bırakışta aynısı geri alınır
                    uint8_t a = btn_map[layer_cnt ? 1 : 0][i];
                    cur_action[i] = a;
                    basildi_us[i] = now;
                    action_press(i, a);
                }
            } else {
                if (pend[i]) {
                    // Pencere dolmadan bırakıldı: hızlı tık — bas + kısa gecikmeyle bırak
                    pend[i] = false;
                    uint8_t a = btn_map[layer_cnt ? 1 : 0][i];
                    cur_action[i] = a;
                    action_press(i, a);
                    force_rel_at[i] = now + TAP_MIN_US;
                } else if (cur_action[i] == 0xF9) {
                    // Akor üyesi bırakıldı → akor biter (ilk bırakan bitirir)
                    if (chord_on) { action_release(CHORD_OWNER, chord_act); chord_on = false; }
                    cur_action[i] = 0xFF;
                } else if (cur_action[i] == 0xFA) {
                    // Drag kilidi: sadece UZUN basışta açılır/kapanır (2026-10-20).
                    // Kullanıcı "basmadığım halde kilit açılıyor" dedi; parazit ya da
                    // anlık temas kısa darbedir, artık kilidi tetikleyemez.
                    if (now - basildi_us[i] >= DRAG_UZUN_US) drag_lock = !drag_lock;
                    cur_action[i] = 0xFF;
                } else {
                    action_release(i, cur_action[i]);
                    cur_action[i] = 0xFF;
                }
            }
            changed = true;
        }
    }
    // Bekleyenler: pencere doldu, partner gelmedi → tekil görevini işlet
    for (int i = 0; i < EXT_BTN_COUNT; i++) {
        if (pend[i] && now - pend_t[i] >= CHORD_US) {
            pend[i] = false;
            uint8_t a = btn_map[layer_cnt ? 1 : 0][i];
            cur_action[i] = a;
            action_press(i, a);
            changed = true;
        }
        if (force_rel_at[i] && now >= force_rel_at[i]) {
            force_rel_at[i] = 0;
            action_release(i, cur_action[i]);
            cur_action[i] = 0xFF;
            changed = true;
        }
    }
    drag_scroll_held = (ds_cnt > 0) || ds_toggle;
    return changed;
}

// Klavye/medya raporlarını gönder (görev motoru kirlettiyse)
static void kb_task(void) {
    if (tud_suspended()) return;
    if (kb_dirty && tud_hid_n_ready(0)) {
        uint8_t r[8] = {kb_mod, 0, kb_key, 0, 0, 0, 0, 0};
        if (tud_hid_n_report(0, 2, r, sizeof(r))) kb_dirty = false;
    }
    if (cons_dirty && tud_hid_n_ready(0)) {
        if (tud_hid_n_report(0, 3, &cons_code, 2)) cons_dirty = false;
    }
}

// Ayar kanalına (Interface 1) ham tuş + kilit durumlarını bildir
static void cfg_task(void) {
    static uint8_t last_states = 0xFF;
    uint8_t states = (uint8_t)((drag_lock ? 1 : 0) | (ds_toggle ? 2 : 0) | (layer_cnt ? 4 : 0));
    // Kilit durumu değişimini günlüğe yaz: "basmadım ama kilit açıldı" iddiasını
    // bir daha tahminle değil kayıtla çözelim (2026-10-20)
    static uint8_t log_states = 0xFF;
    if (states != log_states) { log_ekle(EV_KILIT, states); log_states = states; }
    if (!raw_btn_dirty && states == last_states) return;
    if (tud_suspended() || !tud_hid_n_ready(1)) return;
    uint8_t r[8] = {0x5B, raw_btn_mask, states, 0, 0, 0, 0, 0};
    if (tud_hid_n_report(1, 0, r, sizeof(r))) { raw_btn_dirty = false; last_states = states; }
}

// Panik: tüm kilitli durumları temizle (drag kilidi, scroll modu, katman, tuşlar)
static void states_reset(void) {
    drag_lock = false;
    ds_toggle = false;
    ds_cnt = 0;
    layer_cnt = 0;
    ext_buttons = 0;
    chord_on = false;
    kb_mod = 0; kb_key = 0; kb_dirty = true; kb_owner = -1;
    cons_code = 0; cons_dirty = true; cons_owner = -1;
    for (int i = 0; i < EXT_BTN_COUNT; i++) {
        cur_action[i] = 0xFF;
        pend[i] = false;
        force_rel_at[i] = 0;
    }
}

//--------------------------------------------------------------------
// DEVICE tarafı (core0): raporu birleştir, Mac'e gönder
//--------------------------------------------------------------------
static int16_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32767) return -32767;
    return (int16_t)v;
}
static int8_t clamp8(int32_t v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return (int8_t)v;
}

// Ham sayımı tekerlek tıkına çevirir (kalan korunur).
// Sürükleme başında ilk tık 1/3 eşikte gelir — küçük hareket de tepki versin.
static int32_t scroll_ticks(int32_t *rem, bool *fresh, int32_t delta, int32_t div) {
    int32_t t = delta + *rem, ticks = 0;
    if (*fresh) {
        int32_t kick = div / 3 + 1;
        if (t >= kick || t <= -kick) {
            ticks = (t > 0) ? 1 : -1;
            t = 0;
            *fresh = false;
        }
    }
    ticks += t / div;
    *rem = t - (t / div) * div;
    return ticks;
}

static void hid_task(void) {
    static uint16_t last_buttons = 0xFFFF;

    // Mac cihazı uyutmuşsa: durum değişince uyandır (rapor, uyanınca gider)
    if (tud_suspended()) {
        uint16_t now_btn = (uint16_t)(mouse_buttons & 0x1F) | ext_buttons;
        if (mouse_dirty || kb_dirty || cons_dirty || now_btn != (last_buttons & 0x07FF)) {
            tud_remote_wakeup();
        }
        return;
    }

    if (!tud_hid_ready()) return;

    critical_section_enter_blocking(&state_cs);
    bool dirty = mouse_dirty;
    int32_t raw_dx = acc_dx, raw_dy = acc_dy, dw = acc_wheel, dp = acc_pan;
    uint8_t mbtn = mouse_buttons;
    if (dirty) { acc_dx = 0; acc_dy = 0; acc_wheel = 0; acc_pan = 0; mouse_dirty = false; }
    critical_section_exit(&state_cs);

    int32_t dx = 0, dy = 0;
    // Drag-scroll durumu. cfg_sinv bitleri: 0=dikey ters, 1=yatay KAPALI,
    // 2=eksen rayı KAPALI, 3=yatay ters (varsayılan 0 = yatay açık + ray açık)
    static int32_t rem_v = 0, rem_h = 0, sm_x = 0, sm_y = 0;
    static bool fresh_v = true, fresh_h = true;
    static uint8_t rail = 0;              // 0=serbest, 1=dikey ray, 2=yatay ray
    static uint64_t last_move_us = 0;
    if (drag_scroll_held) {
        // Drag-scroll: top hareketi tekerleğe/pan'a döner, imleç durur (kalan korunur)
        int32_t div = cfg_sdiv;
        bool h_on = !(cfg_sinv & 2);
        bool rails = !(cfg_sinv & 4);
        uint64_t now_us = time_us_64();
        if (raw_dx || raw_dy) {
            if (now_us - last_move_us > 250000) {   // duraklama sonrası yeni kaydırma
                sm_x = sm_y = 0; rail = 0;
                rem_v = rem_h = 0; fresh_v = fresh_h = true;
            }
            last_move_us = now_us;
            // Yumuşatılmış yön (üstel ortalama): tek rapordaki titremeye değil eğilime bakar
            sm_x = sm_x - sm_x / 4 + raw_dx;
            sm_y = sm_y - sm_y / 4 + raw_dy;
        }
        int32_t in_v = raw_dy, in_h = h_on ? raw_dx : 0;
        if (h_on && rails) {
            // Ray (trackpad mantığı): eksene ±25° yakınsa o eksene yapış,
            // 40°'yi aşınca bırak (histerezis); aradaki açılar çapraz serbest.
            int32_t ax = abs(sm_x), ay = abs(sm_y);
            if (ax + ay >= 8) {
                if (rail == 1 && ax * 6 > ay * 5) rail = 0;        // dikeyden >40°
                else if (rail == 2 && ay * 6 > ax * 5) rail = 0;   // yataydan >40°
                if (rail == 0) {
                    if (ax * 15 <= ay * 7) rail = 1;               // dikeye <25°
                    else if (ay * 15 <= ax * 7) rail = 2;          // yataya <25°
                }
            }
            if (rail == 1) { in_h = 0; rem_h = 0; }
            else if (rail == 2) { in_v = 0; rem_v = 0; }
        }
        int32_t tv = scroll_ticks(&rem_v, &fresh_v, in_v, div);
        int32_t th = scroll_ticks(&rem_h, &fresh_h, in_h, div);
        dw += (cfg_sinv & 1) ? tv : -tv;
        dp += (cfg_sinv & 8) ? -th : th;
    } else {
        sm_x = sm_y = 0; rail = 0;
        rem_v = rem_h = 0; fresh_v = fresh_h = true;
        // Hassasiyet oranı (cfg_speed %) — kalan korunur, yavaş hareket kaybolmaz
        int32_t sp = cfg_speed;
        int32_t sx = raw_dx * sp, sy = raw_dy * sp;
        dx = sx / 100;
        dy = sy / 100;
        if (dirty) {
            int32_t rem_x = (sx - dx * 100) / sp;
            int32_t rem_y = (sy - dy * 100) / sp;
            if (rem_x || rem_y) {
                critical_section_enter_blocking(&state_cs);
                acc_dx += rem_x;
                acc_dy += rem_y;
                critical_section_exit(&state_cs);
            }
        }
    }

    // Drag kilidi zaman aşımı: gerçek sürükleme kesintisiz olur. 10sn hareketsizlik
    // = kazara kilitlenmiş demektir, kendiliğinden bırak.
    if (drag_lock) {
        uint64_t simdi = time_us_64();
        if (raw_dx || raw_dy) drag_lock_son_hareket = simdi;
        else if (drag_lock_son_hareket && simdi - drag_lock_son_hareket > 10000000) drag_lock = false;
    } else {
        drag_lock_son_hareket = time_us_64();
    }

    uint16_t buttons = (uint16_t)(mbtn & 0x1F) | ext_buttons;
    if (drag_lock) buttons |= 1;   // sürükleme kilidi: sol tık basılı tutulur
#if DEBUG_EXT_TO_LEFT
    // Debug: herhangi bir harici buton = sol tık (görünür test)
    if (ext_buttons) buttons |= 1;
#endif
#if DEBUG_AUTO_CLICK
    // Debug: 2 saniyede bir kendiliğinden sol tık — TX yolunu kablodan bağımsız test eder
    static uint64_t next_toggle = 0;
    static bool auto_on = false;
    uint64_t now = time_us_64();
    if (now >= next_toggle) {
        next_toggle = now + (auto_on ? 1900000 : 100000);  // 100ms basılı, 1.9s bırakılmış
        auto_on = !auto_on;
    }
    if (auto_on) buttons |= 1;
#endif

    if (!dirty && buttons == last_buttons) return;  // gönderilecek yenilik yok

    bridge_report_t rpt = {
        .buttons = buttons,
        .x = clamp16(dx),
        .y = clamp16(dy),
        .wheel = clamp8(dw),
        .pan = clamp8(dp),
    };
    if (tud_hid_report(1, &rpt, sizeof(rpt))) {
        last_buttons = buttons;
    }
}

int main(void) {
    // PIO-USB 120MHz katı ister (12Mbps bit-bang zamanlaması)
    set_sys_clock_khz(120000, true);

    // Teşhis sayaçlarını oku/ilerlet (elektrik kesilirse ikisi de sıfırlanır)
    diag_hostlost = ((watchdog_hw->scratch[2] >> 16) == DIAG_MAGIC)
                    ? (uint16_t)watchdog_hw->scratch[2] : 0;
    diag_boots = (((watchdog_hw->scratch[3] >> 16) == DIAG_MAGIC)
                  ? (uint16_t)watchdog_hw->scratch[3] : 0) + 1;
    watchdog_hw->scratch[3] = ((uint32_t)DIAG_MAGIC << 16) | diag_boots;

    // Açılış sebebini sınıflandır. RAM'in korunup korunmadığı kritik ipucu:
    //  - watchdog  → bizim resetimiz, mouse ayakta, kısa bekleme yeter
    //  - flash     → bootloader'dan döndük, mouse sarsıldı
    //  - RAM sağlam ama watchdog değil → GERİLİM ÇÖKMESİ: G403 yarı ölü kilitlenir,
    //    kendine gelmesi saniyeler alır (2026-10-08 günlüğünden teyitli) → uzun bekle
    //  - RAM de gitmiş → gerçek soğuk açılış (dock sabah takılıyor): 2.5sn yetmedi
    //    (2026-10-13: 4 deneme gerekti) → ilk denemeyi 5sn sonra yap
    bool ram_korundu = (g_log.magic == LOG_MAGIC);
    // Kesintisiz saat: önceki oturumun son bilinen değerinden devam et
    // (+500ms: reset ve açılış arası tahmini boşluk)
    if (ram_korundu) g_log.taban_ms = g_log.son_ms + 500;
    bool flash_donus = ((watchdog_hw->scratch[1] >> 16) == FLASH_MAGIC);
    watchdog_hw->scratch[1] = 0;
    uint8_t ev;
    // Not: artık D+ hattını ölçerek bekliyoruz; bunlar sadece ek oturma payı
    if (watchdog_caused_reboot() && !flash_donus) { ev = EV_BOOT_WDG;  host_bekleme = 400;  }
    else if (flash_donus)                         { ev = EV_BOOT_WDG;  host_bekleme = 800;  }
    else if (ram_korundu)                         { ev = EV_BOOT_DIP;  host_bekleme = 800;  }
    else                                          { ev = EV_BOOT_GUC;  host_bekleme = 800;  }
    soguk_acilis = (ev != EV_BOOT_WDG);
    log_ekle(ev, (uint8_t)(diag_boots > 255 ? 255 : diag_boots));

    critical_section_init(&state_cs);
    map_load();
    buttons_init();

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    tud_init(BOARD_TUD_RHPORT);

    // Donanım watchdog: ana döngü 2 saniye beslemezse RP2040 kendini resetler.
    // Kilitlenme senaryosu (2026-09-04): core1'deki PIO-USB host hata alıp
    // critical_section'ı bırakmadan çökerse core0 sonsuza dek bekliyordu —
    // cihaz USB'de görünüp arayüz açamıyor, fişi çekmeden düzelmiyordu.
    watchdog_enable(2000, true);

    // Host bağlantı denetçisi (2026-09-23): G403 ile el sıkışma yoksa köprü
    // kendini yeniden başlatır. Eskiden bu sadece açılıştan sonraki ilk 3 saniyede
    // ve 3 denemeyle sınırlıydı; dock sabah takılınca G403 geç hazır olursa üç
    // deneme boşa gidiyor ve bir daha denenmiyordu → "tuşlar çalışıyor, top ölü".
    // scratch[0]: üst 16 bit imza, alt 16 bit ardışık deneme sayısı.
    uint32_t tries = ((watchdog_hw->scratch[0] >> 16) == RETRY_MAGIC)
                     ? (watchdog_hw->scratch[0] & 0xFFFF) : 0;
    uint32_t lost_ms = 0;
    uint32_t saat_guncelleme = 0;
    while (true) {
        watchdog_update();
        // Kesintisiz saati saniyede bir güncelle: donma olursa en fazla ~1sn kaybolur
        uint32_t yerel = to_ms_since_boot(get_absolute_time());
        if (yerel - saat_guncelleme >= 1000 && g_log.magic == LOG_MAGIC) {
            g_log.son_ms = g_log.taban_ms + yerel;
            saat_guncelleme = yerel;
        }
        tud_task();
        buttons_scan();
        hid_task();
        kb_task();
        cfg_task();
        // Ayar sayfasından "kaydet" geldi: flash'a yaz (core1 güvenle durdurulur),
        // sonra temiz yeniden başlat — host hattı da sıfırdan kurulur
        if (map_save_pending) {
            map_save_pending = false;
            log_ekle(EV_KAYIT_RST, 0);
            flash_safe_execute(map_flash_write_unsafe, NULL, 200);
            watchdog_hw->scratch[0] = 0;
            watchdog_reboot(0, 0, 100);
            while (true) tight_loop_contents();
        }
        // Host bağlantısı yoksa (hiç kurulmadıysa ya da sonradan koptuysa) yeniden kur
        if (host_mounted) {
            lost_ms = 0;
            tries = 0;
        } else {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if (lost_ms == 0) lost_ms = now_ms ? now_ms : 1;
            // İlk 3 deneme hızlı (4sn); sonrasında 60sn'de bir — kart gerçekten
            // arızalıysa sürekli reset atıp tuşları da öldürmesin
            // Yeniden deneme aralığı (2026-10-13 günlüğünden ders): host yığını
            // açılışta bazen kilitleniyor ve BEKLEMEK çözmüyor — 60sn arayan oturum
            // bulamadı, hemen sonraki reset 1.5sn'de buldu. Yani her reset bir şans;
            // uzun bekleme yerine SIK dene. İlk 2 dakika hızlı, sonrası seyrek
            // (kart gerçekten arızalıysa sonsuz reset atıp tuşları öldürmesin).
            uint32_t bekle = (tries < 20) ? (uint32_t)host_bekleme + 3000 : 30000;
            if (now_ms - lost_ms > bekle) {
                log_ekle(EV_DNT_RST, (uint8_t)(tries + 1));
                watchdog_hw->scratch[2] = ((uint32_t)DIAG_MAGIC << 16) | (diag_hostlost + 1);
                watchdog_hw->scratch[0] = ((uint32_t)RETRY_MAGIC << 16) | (tries + 1);
                watchdog_reboot(0, 0, 50);
                while (true) tight_loop_contents();
            }
        }
    }
    return 0;
}

//--------------------------------------------------------------------
// Mac tarafındaki bağlantı olayları — günlüğe yazılır
//--------------------------------------------------------------------
void tud_mount_cb(void)   { log_ekle(EV_MAC_TAK, 0); }
void tud_umount_cb(void)  { log_ekle(EV_MAC_KOP, 0); }
void tud_suspend_cb(bool remote_wakeup_en) { log_ekle(EV_MAC_UYKU, remote_wakeup_en ? 1 : 0); }
void tud_resume_cb(void)  { log_ekle(EV_MAC_UYAN, 0); }

//--------------------------------------------------------------------
// Device callback'leri: ayar kanalı (GET/SET) + bootloader sihirli paketi
//--------------------------------------------------------------------
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)report_id; (void)report_type;
    if (instance == 1 && reqlen >= 24 && log_sayfa > 0) {
        // Günlük sayfası: [0xA6][sayfa][adet][kayıt×3] — kayıt = ms(4) + olay + detay
        uint16_t adet = g_log.magic == LOG_MAGIC
                        ? (g_log.toplam < LOG_CAP ? g_log.toplam : LOG_CAP) : 0;
        uint16_t bas = (uint16_t)((log_sayfa - 1) * 3);
        buffer[0] = 0xA6; buffer[1] = log_sayfa; buffer[2] = (uint8_t)adet;
        for (int i = 3; i < 24; i++) buffer[i] = 0;
        // 21-23: şu anki kesintisiz saat (saniye) — okuyucu kayıtlara gerçek saat yazsın
        uint32_t simdi_sn = (g_log.taban_ms + to_ms_since_boot(get_absolute_time())) / 1000;
        buffer[21] = (uint8_t)simdi_sn; buffer[22] = (uint8_t)(simdi_sn >> 8);
        buffer[23] = (uint8_t)(simdi_sn >> 16);
        for (uint16_t i = 0; i < 3 && bas + i < adet; i++) {
            // en eskiden yeniye sırala
            uint16_t idx = (uint16_t)((g_log.head + LOG_CAP - adet + bas + i) % LOG_CAP);
            log_kayit_t *k = &g_log.k[idx];
            uint8_t *o = &buffer[3 + i * 6];
            o[0] = (uint8_t)k->ms; o[1] = (uint8_t)(k->ms >> 8);
            o[2] = (uint8_t)(k->ms >> 16); o[3] = (uint8_t)(k->ms >> 24);
            o[4] = k->ev; o[5] = k->detay;
        }
        return 24;
    }
    if (instance == 1 && reqlen >= 17) {
        buffer[0] = 0xA5;
        memcpy(&buffer[1], btn_map[0], EXT_BTN_COUNT);
        memcpy(&buffer[7], btn_map[1], EXT_BTN_COUNT);
        buffer[13] = cfg_speed;
        buffer[14] = cfg_sdiv;
        buffer[15] = cfg_sinv;
        buffer[16] = chord_a;
        buffer[17] = chord_b;
        buffer[18] = chord_act;
        buffer[19] = cfg_checksum(btn_map, cfg_speed, cfg_sdiv, cfg_sinv,
                                  chord_a, chord_b, chord_act);
        // Teşhis: 20=host kaybı sayacı, 21=açılış sayacı, 22-23=çalışma süresi (sn)
        uint32_t up_s = to_ms_since_boot(get_absolute_time()) / 1000;
        if (up_s > 0xFFFF) up_s = 0xFFFF;
        buffer[20] = (uint8_t)(diag_hostlost > 255 ? 255 : diag_hostlost);
        buffer[21] = (uint8_t)(diag_boots > 255 ? 255 : diag_boots);
        buffer[22] = (uint8_t)(up_s & 0xFF);
        buffer[23] = (uint8_t)(up_s >> 8);
        uint16_t n = reqlen < 24 ? reqlen : 24;
        return n;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)report_id; (void)report_type;
    // Sihirli paket → bootloader'a düş (kapalı kasada tuşsuz güncelleme).
    // Mac tarafı: tools/flash.sh bu paketi SetReport(Feature) ile yollar.
    static const uint8_t MAGIC[6] = {0xB0, 0x07, 0x5E, 0x1E, 0xC7, 0x42};
    if (bufsize >= sizeof(MAGIC) && memcmp(buffer, MAGIC, sizeof(MAGIC)) == 0) {
        watchdog_hw->scratch[1] = ((uint32_t)FLASH_MAGIC << 16);  // dönüşte "flash" bilinsin
        watchdog_disable();     // bootloader'ı watchdog resetlemesin
        reset_usb_boot(0, 0);
    }
    // Ayar kanalı (Interface 1)
    if (instance == 1 && bufsize >= 4) {
        if (buffer[0] == 0x01 && bufsize >= 7) {   // canlı harita, katman 0
            memcpy(btn_map[0], &buffer[1], EXT_BTN_COUNT);
            ext_buttons = 0;               // eski bitler takılı kalmasın
        } else if (buffer[0] == 0x04 && bufsize >= 7) {  // canlı harita, Fn katmanı
            memcpy(btn_map[1], &buffer[1], EXT_BTN_COUNT);
            ext_buttons = 0;
        } else if (buffer[0] == 0x03) {            // canlı hız ayarları
            uint8_t sp = buffer[1], sd = buffer[2];
            if (sp >= 10 && sp <= 250) cfg_speed = sp;
            if (sd >= 8) cfg_sdiv = sd;
            cfg_sinv = buffer[3] & 0x0F;
        } else if (buffer[0] == 0x06) {            // kombinasyon tanımı [a, b, görev]
            if (chord_on) { action_release(CHORD_OWNER, chord_act); chord_on = false; }
            chord_a = buffer[1]; chord_b = buffer[2]; chord_act = buffer[3];
            for (int i = 0; i < EXT_BTN_COUNT; i++) pend[i] = false;
        } else if (buffer[0] == 0x07) {            // günlük sayfası seç (0 = ayarlar)
            log_sayfa = buffer[1];
        } else if (buffer[0] == 0x05) {            // panik: kilitli durumları sıfırla
            states_reset();
        } else if (buffer[0] == 0x02) {            // kalıcı kaydet (haritalar + hızlar)
            map_save_pending = true;
        }
    }
}
