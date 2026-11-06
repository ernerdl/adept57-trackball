#!/bin/bash
# Kapalı kasada tuşsuz firmware güncelleme:
# 1) Köprüye sihirli HID paketi yolla → kart bootloader'a düşer
# 2) RPI-RP2 diski gelince uf2'yi kopyala
set -e
UF2="${1:-$(dirname "$0")/../build/trackball_bridge.uf2}"
[ -f "$UF2" ] || { echo "uf2 bulunamadı: $UF2"; exit 1; }

echo "→ Köprüye bootloader komutu yollanıyor..."
swift - <<'EOF'
import IOKit.hid
import Foundation

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
let match: [String: Any] = ["VendorID": 0x2e8a, "ProductID": 0x107b]
IOHIDManagerSetDeviceMatching(mgr, match as CFDictionary)
IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))

let devCallback: IOHIDDeviceCallback = { _, _, _, device in
    IOHIDDeviceOpen(device, IOOptionBits(kIOHIDOptionsTypeNone))
    let magic: [UInt8] = [0xB0, 0x07, 0x5E, 0x1E, 0xC7, 0x42]
    let r = IOHIDDeviceSetReport(device, kIOHIDReportTypeFeature, 0, magic, magic.count)
    print(r == kIOReturnSuccess ? "Komut gitti" : "SetReport dönüşü: \(r) (kart yine de düşmüş olabilir)")
    exit(0)
}
IOHIDManagerRegisterDeviceMatchingCallback(mgr, devCallback, nil)
IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
CFRunLoopRunInMode(.defaultMode, 3, false)
print("Köprü bulunamadı — zaten bootloader'da olabilir, devam ediliyor")
EOF

echo "→ RPI-RP2 diski bekleniyor..."
for i in $(seq 1 30); do
  if [ -d /Volumes/RPI-RP2 ]; then
    sleep 1
    cp "$UF2" /Volumes/RPI-RP2/ 2>/dev/null || true
    echo "✓ Yüklendi: $(basename "$UF2") ($(date '+%H:%M:%S'))"
    exit 0
  fi
  sleep 1
done
echo "✗ 30 saniyede disk görünmedi — son çare: tabandaki iğne deliğinden BOOT'a basılı tutup kabloyu tak"
exit 1
