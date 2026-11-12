// Köprüye tek komut: "durum" → "çalışma_sn host_kaybı açılış"; "reset" → yeniden başlat;
// "test <adet> <düzeltme>" → cihaz içi tanıma testini başlat; "sonuc" → test sayaçları.
// Tanıma testi (tools/tanima-testi.sh) bunu kullanır. Cihaz yoksa çıkış kodu 1.
import IOKit.hid
import Foundation
let arg = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "durum"
let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(mgr, ["VendorID": 0x2e8a, "ProductID": 0x107b, "PrimaryUsagePage": 0xFF60] as CFDictionary)
IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
guard let set = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, let dev = set.first else { exit(1) }
func gonder(_ v: [UInt8]) -> Bool {
    var c = [UInt8](repeating: 0, count: 24)
    for (i, x) in v.enumerated() { c[i] = x }
    return IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, 0, &c, c.count) == kIOReturnSuccess
}
if arg == "reset" { exit(gonder([0x0A, 0x52, 0x53, 0x54]) ? 0 : 1) }
if arg == "test" {      // test <adet> <mod> <faz>: cihaz içi tanıma testini başlat
    let n = CommandLine.arguments.count > 2 ? Int(CommandLine.arguments[2]) ?? 40 : 40
    let d = CommandLine.arguments.count > 3 ? UInt8(CommandLine.arguments[3]) ?? 0 : 0
    let f = CommandLine.arguments.count > 4 ? UInt8(CommandLine.arguments[4]) ?? 0 : 0
    exit(gonder([0x0B, 0x54, UInt8(n & 0xFF), UInt8(n >> 8), d, f]) ? 0 : 1)
}
if arg == "sonuc" {     // test sayaçları: kalan açılış bağlanan denetçi tam geç kısa0 kısa1 cevapsız iletim düzeltme var
    guard gonder([0x0C]) else { exit(1) }
    var b = [UInt8](repeating: 0, count: 24); var l = b.count
    guard IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, 0, &b, &l) == kIOReturnSuccess else { exit(1) }
    if b[0] != 0xA8 { b = Array(b[1...]) + [0] }
    guard b[0] == 0xA8 else { exit(1) }
    func w(_ i: Int) -> Int { Int(b[i]) | Int(b[i + 1]) << 8 }
    print(w(1), w(3), w(5), w(7), Int(b[9]) | Int(b[10]) << 8 | Int(b[11]) << 16, w(12), w(14), w(16), w(18), w(20), b[22], b[23])
    _ = gonder([0x07, 0])
    exit(0)
}
guard gonder([0x07, 0]) else { exit(1) }          // ayar sayfasını seç
var b = [UInt8](repeating: 0, count: 24); var l = b.count
guard IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, 0, &b, &l) == kIOReturnSuccess else { exit(1) }
if b[0] != 0xA5 { b = Array(b[1...]) + [0] }
guard b[0] == 0xA5 else { exit(1) }
print(Int(b[22]) | Int(b[23]) << 8, b[20], b[21])
