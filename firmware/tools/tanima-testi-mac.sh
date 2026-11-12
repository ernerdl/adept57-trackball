#!/bin/bash
# Eski yöntem (karşılaştırma için): köprüyü Mac'ten N kez yeniden başlatır.
#   ./tanima-testi-mac.sh [adet=20] [yoklama=1|0] [bekleme_sn=5]
# yoklama=1: köprü 5 sn ayakta olana kadar 0,5 sn'de bir durum sorar (11 Kasım 15:03 testi)
# yoklama=0: hiç sormaz; her denemede sabit bekler (başarısız zincir için pay dahil)
N="${1:-20}"; YOK="${2:-1}"; BEK="${3:-5}"; D="$(cd "$(dirname "$0")" && pwd)"; K="$D/.tbkomut"
for i in $(seq 1 "$N"); do
    if [ "$YOK" = "1" ]; then
        son=$((SECONDS + 150))
        while [ $SECONDS -lt $son ]; do
            set -- $("$K" durum 2>/dev/null)
            [ -n "$1" ] && [ "$1" -ge "$BEK" ] && break
            /bin/sleep 0.5
        done
    else
        /bin/sleep "$BEK"
    fi
    "$K" reset || { /bin/sleep 2; "$K" reset; }
    printf "\r%d/%d" "$i" "$N"; /bin/sleep 1.5
done
/bin/sleep 8; echo; echo bitti
