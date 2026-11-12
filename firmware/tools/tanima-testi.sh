#!/bin/bash
# G403 tanıma testi (cihazın içinde koşar): köprü G403'ü N kez baştan tanır,
# her bağlanışta kendini yeniden başlatır; bağlanamazsa denetçi resetler.
# Sayaçlar köprünün RAM'inde tutulur, kara kutuya yazılmaz.
#   ./tanima-testi.sh [adet=60] [mod] [etiket] [faz=0]   (faz: okuma döngüsünden önce eklenen işlemci çevrimi)
#   mod bitleri: 1 = yarış düzeltmesi açık, 2 = kütüphanenin kendi onay işlevi, 4 = kara kutuya da yaz,
#                8 = kopya işlevde yarışı kışkırt (faz iki bakışın arasına eklenir),
#                16 = düzeltme ayarı olarak kalıcı sürümün varsayılanını kullan
N="${1:-60}"; DZ="${2:-0}"; ET="${3:-}"; FAZ="${4:-0}"; D="$(cd "$(dirname "$0")" && pwd)"; K="$D/.tbkomut"
[ "$K" -nt "$D/tbkomut.swift" ] || swiftc -O "$D/tbkomut.swift" -o "$K" || exit 1
for i in 1 2 3 4 5 6 7 8 9 10; do "$K" durum >/dev/null 2>&1 && break; /bin/sleep 1; done
"$K" test "$N" "$DZ" "$FAZ" || { echo "komut gitmedi"; exit 1; }
bas=$SECONDS; son=""
while [ $((SECONDS - bas)) -lt 900 ]; do
    /bin/sleep 2
    r=$("$K" sonuc 2>/dev/null) || continue
    son="$r"; set -- $r
    printf "\rkalan %s  " "$1"
    [ "$1" = "0" ] && [ "${12}" = "1" ] && break
done
echo; set -- $son
[ -n "$1" ] || { echo "sonuç okunamadı"; exit 1; }
python3 - "$N" "$DZ" "$ET" "$((SECONDS - bas))" "$@" <<'PY'
import sys
n, dz, et, sure = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
kalan, acilis, bagli, dnt, tam, gec, k0, k1, yok, iletim, duz, var = (int(x) for x in sys.argv[5:17])
top = tam + gec + k0 + k1 + yok
mod = (("kütüphanenin kendi işlevi" + (" + kurtarma" if duz & 1 else "")) if duz & 2 else "kopya işlev, düzeltme " + ("AÇIK" if duz & 1 else "kapalı")) + (", kara kutuya yazarak" if duz & 4 else "") + (", yarış kışkırtılarak" if duz & 8 else "") + (", düzeltme = kalıcı sürümün varsayılanı" if duz & 16 else "")
print("== tanıma testi%s: %s deneme, %s, %s sn ==" % ((" [" + et + "]") if et else "", n, mod, sure))
print("açılış %d   bağlanan %d   bağlanamayıp resetlenen %d   → açılış başına başarısızlık %%%.0f" %
      (acilis, bagli, dnt, 100.0 * dnt / max(1, bagli + dnt)))
print("onay bekleme: %d kez   tam %d   GEÇ GELEN BAYT %d (%%%.1f)   bayt yok %d   yalnız 1 bayt %d   cevap başlamadı %d" %
      (top, tam, gec, 100.0 * gec / max(1, top), k0, k1, yok))
print("kütüphanenin hatalı kapattığı işlem: %d   kalan deneme: %d" % (iletim, kalan))
PY
