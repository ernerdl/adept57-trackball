#!/bin/bash
# Köprünün kendi tuttuğu olay günlüğünü oku (cihaz içi, resetlerde silinmez)
cd "$(dirname "$0")" && swift gunluk.swift
