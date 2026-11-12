// Kara kutu okuyucu: köprünün FLASH'ta tuttuğu kalıcı olay kaydını yazar.
// Elektrik kesilse de silinmez. Kullanım:
//   karakutu            son 60 kayıt
//   karakutu 200        son 200 kayıt
//   karakutu hepsi      tüm kayıtlar
//   karakutu saat       yalnızca cihaza gerçek saati bildir (kayıtlara tarih düşsün)
//   karakutu ham        makinece okunur: "sıra epoch güç t_sn olay detay" (epoch bilinmiyorsa 0)
import IOKit.hid
import Foundation

let adlar: [UInt8: String] = [
    1: "köprü açıldı (ELEKTRİK GELDİ — soğuk açılış)",
    2: "köprü açıldı (reset sonrası)",
    3: "Mac bağlandı",
    4: "Mac BAĞLANTISI KOPTU",
    7: "G403 bağlandı",
    8: "G403 KOPTU",
    9: "denetçi resetledi (host yok)",
    10: "ayar kaydı → reset (detay 1: komutla yeniden başlatma)",
    11: "köprü açıldı (GERİLİM ÇÖKTÜ — besleme dalgalandı)",
    12: "G403 veri hattı hazır (bekleme: detay×100ms)",
    14: "ÖNCEKİ AÇILIŞ TAKILDI — erken watchdog kurtardı",
    15: "saat eşitlendi",
    16: "tanı: hat durumu (bit0 D+ 1 D- 2 PIO bağlı 3 FS 4 host kuruldu 5 core1 dönüyor 6 tanıma bitti 7 açılışta D+ gerçek)",
    17: "tanı: takılma/çıkma olayı (üst/alt 4 bit)",
    18: "tanı: ilk hatadan önceki transfer sayısı",
    19: "tanı: ilk hata türü / hata sayısı (üst/alt 4 bit; 1 başarısız 2 stall 3 zaman aşımı)",
    20: "tanı: ilk iletim hatasında hat (bit0-1 adım 0 istek 1 cevap alma 2 gönderme; 2 adresli; 3 paket başladı; 4 bitti; 5 dolgu hatası; 6 eşleme doğru; 7 uç≠0)",
    21: "tanı: o anda gelen paket türü baytı (0 = hiçbir şey)",
    22: "tanı: cevapsız / bozuk hata sayısı (üst/alt 4 bit)",
]
let SEKTOR = 8, SEKTOR_BOY = 4096, SAYFA = 256, YUVA = 16, YUVA_SAYI = 240   // ilk sayfa başlık
let arg = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "60"

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(mgr, ["VendorID": 0x2e8a, "ProductID": 0x107b, "PrimaryUsagePage": 0xFF60] as CFDictionary)
IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
guard let set = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, let dev = set.first else {
    print("ayar kanalı bulunamadı — trackball bağlı mı?"); exit(1)
}
func get() -> [UInt8]? {
    var b = [UInt8](repeating: 0, count: 24); var l = b.count
    guard IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, 0, &b, &l) == kIOReturnSuccess else { return nil }
    return (b[0] == 0xA5 || b[0] == 0xA6 || b[0] == 0xA7) ? b : Array(b[1...]) + [0]
}
func gonder(_ v: [UInt8]) {
    var c = [UInt8](repeating: 0, count: 24)
    for (i, x) in v.enumerated() { c[i] = x }
    _ = IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, 0, &c, c.count)
}
// Gerçek saati bildir: cihaz bundan sonraki kayıtlara tarih düşer
let e = UInt32(Date().timeIntervalSince1970)
gonder([0x09, UInt8(e & 0xFF), UInt8((e >> 8) & 0xFF), UInt8((e >> 16) & 0xFF), UInt8((e >> 24) & 0xFF)])
if arg == "saat" { exit(0) }

func oku(_ ofs: Int, _ n: Int) -> [UInt8]? {
    var cikti: [UInt8] = []; var deneme = 0
    gonder([0x08, UInt8(ofs & 0xFF), UInt8((ofs >> 8) & 0xFF), UInt8((ofs >> 16) & 0xFF)])
    while cikti.count < n {
        guard let b = get(), b[0] == 0xA7 else { return nil }
        let o = Int(b[1]) | Int(b[2]) << 8 | Int(b[3]) << 16
        if o != ofs + cikti.count {            // araya başka bir okuyucu girmiş: konumu yeniden ver
            deneme += 1; if deneme > 20 { return nil }
            let h = ofs + cikti.count
            gonder([0x08, UInt8(h & 0xFF), UInt8((h >> 8) & 0xFF), UInt8((h >> 16) & 0xFF)])
            continue
        }
        cikti += b[4..<24]
    }
    return Array(cikti.prefix(n))
}
func u32(_ b: [UInt8], _ o: Int) -> UInt32 { UInt32(b[o]) | UInt32(b[o+1]) << 8 | UInt32(b[o+2]) << 16 | UInt32(b[o+3]) << 24 }

// Sektör başlıkları → geçerli olanları sıra numarasına göre diz
var sektorler: [(no: Int, seq: UInt32)] = []
for s in 0..<SEKTOR {
    guard let h = oku(s * SEKTOR_BOY, 12) else { gonder([0x07, 0]); print("kara kutu okunamadı — firmware eski olabilir"); exit(2) }
    let seq = u32(h, 4)
    if u32(h, 0) == 0x4B4B5432 && u32(h, 8) == ~seq && seq != 0 { sektorler.append((s, seq)) }
}
sektorler.sort { $0.seq < $1.seq }
struct Kayit { var t: UInt32; var epoch: UInt32; var sira: UInt32; var ev: UInt8; var detay: UInt8; var guc: UInt8 }
var kayitlar: [Kayit] = []; var bozuk = 0
for s in sektorler {
    guard let d = oku(s.no * SEKTOR_BOY + SAYFA, YUVA_SAYI * YUVA) else { break }
    var bosSayac = 0
    for y in 0..<YUVA_SAYI {
        let r = Array(d[(y * YUVA)..<(y * YUVA + YUVA)])
        if r.allSatisfy({ $0 == 0xFF }) { bosSayac += 1; continue }
        var c: UInt8 = 0xA5; for i in 0..<(YUVA - 1) { c ^= r[i] }
        if c != r[15] || r[12] == 0 || r[12] == 0xFF { bozuk += 1; continue }
        kayitlar.append(Kayit(t: u32(r, 0), epoch: u32(r, 4), sira: u32(r, 8), ev: r[12], detay: r[13], guc: r[14]))
    }
}
gonder([0x07, 0])    // ayar kanalını normal moda döndür

// Saati bilinmeyen kayıtlar: aynı elektrik oturumunda saati bilinen bir kayıttan türet
var taban: [UInt8: Int] = [:]
for k in kayitlar where k.epoch != 0 { taban[k.guc] = Int(k.epoch) - Int(k.t) }
func epoch(_ k: Kayit) -> Int { k.epoch != 0 ? Int(k.epoch) : (taban[k.guc].map { $0 + Int(k.t) } ?? 0) }

let secim = (arg == "hepsi" || arg == "ham") ? kayitlar : Array(kayitlar.suffix(Int(arg) ?? 60))
if arg == "ham" {
    for k in secim { print("\(k.sira) \(epoch(k)) \(k.guc) \(k.t) \(k.ev) \(k.detay)") }
    exit(0)
}
let f = DateFormatter(); f.dateFormat = "dd.MM HH:mm:ss"
print("KARA KUTU: \(kayitlar.count) kayıt, \(sektorler.count) sektör" + (bozuk > 0 ? ", \(bozuk) yarım kalmış yazma atlandı" : "") + " — \(secim.count) tanesi gösteriliyor")
var sonGuc: Int = -1
for k in secim {
    if Int(k.guc) != sonGuc { print("══ elektrik oturumu \(k.guc) ══"); sonGuc = Int(k.guc) }
    let ep = epoch(k)
    let zaman = ep > 0 ? f.string(from: Date(timeIntervalSince1970: TimeInterval(ep))) : String(format: "saat yok +%5d sn", k.t)
    let ayrac = (k.ev == 1 || k.ev == 2 || k.ev == 11) ? "──" : "  "
    print("\(ayrac)\(zaman)  \(adlar[k.ev] ?? "bilinmeyen(\(k.ev))")\(k.detay != 0 ? " [\(k.detay)]" : "")")
}
