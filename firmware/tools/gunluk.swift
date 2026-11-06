import IOKit.hid
import Foundation

let adlar: [UInt8: String] = [
    1: "köprü açıldı (ELEKTRİK GELDİ — soğuk açılış)",
    2: "köprü açıldı (reset sonrası)",
    3: "Mac bağlandı",
    4: "Mac BAĞLANTISI KOPTU",
    5: "Mac hattı uykuya aldı",
    6: "Mac hattı uyandırdı",
    7: "G403 bağlandı",
    8: "G403 KOPTU",
    9: "denetçi resetledi (host yok)",
    10: "ayar kaydı → reset",
    11: "köprü açıldı (GERİLİM ÇÖKTÜ — besleme dalgalandı)",
    12: "G403 veri hattı hazır (bekleme: detay×100ms)",
    13: "KİLİT DURUMU değişti (1=drag 2=scroll 4=Fn)",
]

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(mgr, ["VendorID": 0x2e8a, "ProductID": 0x107b, "PrimaryUsagePage": 0xFF60] as CFDictionary)
IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
guard let set = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, let dev = set.first else {
    print("ayar kanalı bulunamadı — trackball bağlı mı?"); exit(1)
}
func get() -> [UInt8]? {
    var b = [UInt8](repeating: 0, count: 24); var l = b.count
    guard IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, 0, &b, &l) == kIOReturnSuccess else { return nil }
    return (b[0] == 0xA5 || b[0] == 0xA6) ? b : Array(b[1...]) + [0]
}
func sayfaSec(_ p: UInt8) {
    var c = [UInt8](repeating: 0, count: 24); c[0] = 0x07; c[1] = p
    _ = IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, 0, &c, c.count)
}

sayfaSec(0)
guard let st = get(), st[0] == 0xA5 else { print("ayar okunamadı"); exit(1) }
let up = Int(st[22]) | (Int(st[23]) << 8)
print("DURUM: çalışma süresi \(up/60)dk \(up%60)sn | açılış sayısı=\(st[21]) | host kaybı=\(st[20])")

// 1. geçiş: tüm kayıtları topla
var kayitlar: [(ms: UInt32, ev: UInt8, detay: UInt8)] = []
var sayfa: UInt8 = 1, adet = -1
var simdiGlobal = -1   // cihazın kesintisiz saati (sn)
while sayfa <= 30 {
    sayfaSec(sayfa)
    guard let b = get(), b[0] == 0xA6 else { break }
    if adet < 0 { adet = Int(b[2]); if adet == 0 { break } }
    if simdiGlobal < 0 { simdiGlobal = Int(b[21]) | Int(b[22]) << 8 | Int(b[23]) << 16 }
    for i in 0..<3 {
        let idx = (Int(sayfa) - 1) * 3 + i
        if idx >= adet { break }
        let o = 3 + i * 6
        let ms = UInt32(b[o]) | UInt32(b[o+1]) << 8 | UInt32(b[o+2]) << 16 | UInt32(b[o+3]) << 24
        kayitlar.append((ms, b[o+4], b[o+5]))
    }
    if Int(sayfa) * 3 >= adet { break }
    sayfa += 1
}
sayfaSec(0)

// 2. geçiş: kesintisiz saat sayesinde HER kayda gerçek saat yazılır
let simdi = Date()
let f = DateFormatter(); f.dateFormat = "dd.MM HH:mm:ss"
var oturum = 0
print("GÜNLÜK (\(kayitlar.count) kayıt, en eskiden yeniye):")
for k in kayitlar {
    if k.ev == 1 || k.ev == 2 || k.ev == 11 { oturum += 1 }
    let ad = adlar[k.ev] ?? "bilinmeyen(\(k.ev))"
    let once = Double(simdiGlobal) - Double(k.ms) / 1000
    let zaman = f.string(from: simdi.addingTimeInterval(-max(once, 0)))
    let ayrac = (k.ev == 1 || k.ev == 2 || k.ev == 11) ? "──" : "  "
    print("\(ayrac)\(zaman)  [oturum \(oturum)]  \(ad)\(k.detay != 0 ? " [\(k.detay)]" : "")")
}
