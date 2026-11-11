#!/bin/bash
# Köprünün flash'taki kalıcı olay kaydını (kara kutu) oku. Elektrik kesilse de silinmez.
#   ./karakutu.sh [adet | hepsi | saat | ham]
cd "$(dirname "$0")" && swift karakutu.swift "$@"
