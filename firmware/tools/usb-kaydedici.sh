#!/bin/bash
# Dock'taki USB cihazlarının takılma/kopma anlarını kalıcı dosyaya yazar.
# macOS kendi günlüğünü ~2 gün tutuyor, trackball'ın günlüğü de elektrik
# tamamen gidince siliniyor; bu kayıt ikisinin açığını kapatıyor.
# LaunchAgent (com.erenworks.usb-kaydedici) oturum açılınca başlatır.
KAYIT="$HOME/Library/Logs/usb-olaylari.log"
SINIR=$((5 * 1024 * 1024))   # 5 MB'ı geçince eskisini .1'e taşı

dondur() {
  if [ -f "$KAYIT" ] && [ "$(stat -f%z "$KAYIT")" -gt "$SINIR" ]; then
    mv -f "$KAYIT" "$KAYIT.1"
  fi
}
dondur
echo "$(date '+%Y-%m-%d %H:%M:%S')  ---- kaydedici başladı ----" >> "$KAYIT"

son_hata=""
sayac=0
/usr/bin/log stream --style compact --predicate 'process == "kernel" AND (eventMessage CONTAINS "hardware connection lost" OR eventMessage CONTAINS "enumerated 0x" OR eventMessage CONTAINS "transaction error" OR eventMessage CONTAINS[c] "overcurrent" OR eventMessage CONTAINS[c] "over-current")' 2>/dev/null |
while IFS= read -r satir; do
  case "$satir" in 20[0-9][0-9]-*) ;; *) continue ;; esac   # başlık satırlarını atla
  zaman=$(echo "$satir" | cut -c1-23)
  case "$satir" in
    *"hardware connection lost"*)
      ad=$(echo "$satir" | sed -nE 's/.*destroying [^ ]+ \(([^)]*)\).*/\1/p')
      echo "$zaman  KOPTU      $ad" >> "$KAYIT" ;;
    *"enumerated 0x"*)
      ad=$(echo "$satir" | sed -nE 's/.*enumerated [^ ]+ \(([^/)]*).*/\1/p' | sed 's/ *$//')
      echo "$zaman  BAĞLANDI   $ad" >> "$KAYIT" ;;
    *"transaction error"*)
      # sadece trackball'ın hataları, saniyede bir satır (patlamalar dosyayı şişirmesin)
      case "$satir" in *"Trackball Bridge"*)
        sn=$(echo "$zaman" | cut -c1-19)
        [ "$sn" != "$son_hata" ] && echo "$zaman  HATA       Trackball Bridge (iletim hatası)" >> "$KAYIT"
        son_hata="$sn" ;;
      esac ;;
    *[Oo]vercurrent*|*[Oo]ver-current*)
      echo "$zaman  AŞIRI AKIM $(echo "$satir" | cut -c60-200)" >> "$KAYIT" ;;
  esac
  sayac=$((sayac + 1))
  [ $((sayac % 500)) -eq 0 ] && dondur
done
