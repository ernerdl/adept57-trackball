#!/usr/bin/env python3
# Tanıma testi özeti: kara kutunun "ham" çıktısını (stdin) açılışlara böler,
# başarılı ve başarısız G403 tanıma denemelerini karşılaştırır.
#   karakutu ham | tanima-ozet.py [ilk_sıra]
import sys, collections
esik = int(sys.argv[1]) if len(sys.argv) > 1 else -1
kay = [tuple(int(x) for x in s.split()) for s in sys.stdin if len(s.split()) == 6 and s.split()[0].isdigit()]
kay = [k for k in kay if k[0] > esik]
# açılışlara böl: her açılış olay 1/2/11 ile başlar
acilis, simdi = [], None
for k in kay:
    ev, det = k[4], k[5]
    if ev in (1, 2, 11):
        simdi = {"tur": ev, "olay": {}, "bagli": False, "dnt": False, "komut": False}
        acilis.append(simdi)
    elif simdi is not None:
        if ev == 7: simdi["bagli"] = True
        elif ev == 9: simdi["dnt"] = True
        elif ev == 10 and det == 1: simdi["komut"] = True
        elif ev in (16, 17, 18, 19, 20, 21, 22): simdi["olay"].setdefault(ev, det)
ok  = [a for a in acilis if a["bagli"]]
bas = [a for a in acilis if not a["bagli"] and a["dnt"]]
print("açılış: %d   bağlanan: %d   bağlanamayıp resetlenen: %d   (%%%.0f başarısız)" %
      (len(acilis), len(ok), len(bas), 100.0 * len(bas) / max(1, len(ok) + len(bas))))
# zincirler: art arda başarısızlardan sonra gelen başarı = kaçıncı denemede bağlandı
dag, n = collections.Counter(), 0
for a in acilis:
    if a["bagli"]: dag[n + 1] += 1; n = 0
    elif a["dnt"]: n += 1
print("kaçıncı denemede bağlandı:", "  ".join("%d.→%d" % (d, c) for d, c in sorted(dag.items())))
ADIM = {0: "istek(SETUP)", 1: "cevap alma(IN)", 2: "gönderme(OUT)"}
def dok(ad, grup):
    print("\n== %s (%d) ==" % (ad, len(grup)))
    if not grup: return
    iletim = [a for a in grup if 20 in a["olay"]]
    print("iletim hatası olan: %d (%%%.0f)" % (len(iletim), 100.0 * len(iletim) / len(grup)))
    print("hat durumu (16):", dict(collections.Counter(a["olay"].get(16) for a in grup)))
    print("takılma/çıkma (17):", dict(collections.Counter(a["olay"].get(17) for a in grup)))
    print("ilk hatadan önce transfer (18):", dict(sorted(collections.Counter(a["olay"].get(18) for a in grup).items(), key=lambda x: (x[0] is None, x[0]))))
    tur = collections.Counter()
    for a in grup:
        h = a["olay"].get(19)
        tur["yok" if not h else {1: "başarısız", 2: "stall", 3: "zaman aşımı"}.get(h >> 4, "?")] += 1
    print("ilk hatanın türü (19):", dict(tur))
    if iletim:
        c = collections.Counter()
        for a in iletim:
            h = a["olay"][20]
            c["%s, %s, %s%s%s" % (ADIM[h & 3], "adresli" if h & 4 else "adres 0",
                                 ("paket başladı" + (", bitti" if h & 16 else ", BİTMEDİ")) if h & 8 else "CEVAP YOK",
                                 ", dolgu hatası" if h & 32 else "", ", uç≠0" if h & 128 else "")] += 1
        for k, v in c.most_common(): print("  %2d × %s" % (v, k))
        print("gelen paket türü baytı (21):", {("%02x" % k): v for k, v in collections.Counter(a["olay"].get(21) for a in iletim).items()})
        yok = sum(a["olay"].get(22, 0) >> 4 for a in iletim); boz = sum(a["olay"].get(22, 0) & 15 for a in iletim)
        print("tüm iletim hataları: cevapsız %d, cevap gelip çözülemeyen %d" % (yok, boz))
dok("BAĞLANAN açılışlar", ok)
dok("BAĞLANAMAYAN açılışlar", bas)
