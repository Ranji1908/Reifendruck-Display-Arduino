# Selbstbau-Variante: ESP32-C3 + 1,3"-OLED + Drehgeber

> Alternative zum M5Stack DinMeter (Firmware: `src/oled/main.cpp`, Build: `pio run -e esp32c3 -t upload`).
> Unterstützt dieselben 6 Sensortypen, aber keinen Neigungsmesser.

## 2. Einkaufsliste (ca. 40–60 €)

| Teil | Hinweis | ca. Preis |
|---|---|---|
| **4× BLE-TPMS-Ventilkappensensoren** | Suche „BLE TPMS external sensor“ / „Reifendruck Sensor Bluetooth App“. Kompatibel sind die verbreiteten Sensoren mit Bluetooth-Namen `TPMS1_xxxxxx` … `TPMS4_xxxxxx` (Adresse `80:EA:CA:…`). **Messbereich mind. 6 bar!** Der Sprinter fährt hinten deutlich über 4 bar – manche billigen Motorrad-Sets gehen nur bis 3,5 bar. | 25–40 € |
| ESP32-C3 SuperMini | USB-C, winzig | 3–5 € |
| 1,3" OLED 128×64, **SH1106**, I²C (4 Pins) | 0,96" SSD1306 geht auch (siehe Konfiguration) | 5–8 € |
| Drehgeber KY-040 (mit Taster) | Ein Knopf für alles | 2 € |
| Aktiver Summer 5 V + NPN-Transistor (BC547/2N2222) + 1 kΩ | oder fertiges „aktiver Buzzer Modul“ (3,3-V-tauglich) | 2 € |
| 12 V → 5 V Versorgung | Am einfachsten: guter Kfz-USB-Adapter + kurzes USB-C-Kabel. Alternativ DC-DC-Modul mit **Eingang bis ≥ 30 V** (Spannungsspitzen im Bordnetz!) | 5–10 € |
| Sicherungsabgriff („Add-A-Fuse“) passend zu den Sicherungen + 2 A Sicherung | auf einen Platz mit **Zündungsplus** (Klemme 15) | 5 € |
| Kleines Gehäuse (3D-Druck) oder Einbau in eine Blindabdeckung | | – |

**Wichtig vorab:** Prüf in der Bluetooth-App **nRF Connect** (Android/iOS), ob deine Sensoren als
`TPMS1_…` usw. auftauchen und „Manufacturer data“ mit **18 Byte, beginnend mit `0x0001`**
senden. Dann passen sie. Sonst: `DEBUG_BLE 1` in `include/config.h` setzen und das serielle Log prüfen.

## 3. Verkabelung (ESP32-C3 SuperMini)

| Bauteil | Pin am Bauteil | ESP32-C3 |
|---|---|---|
| OLED | VCC | 3V3 |
| | GND | GND |
| | SDA | **GPIO 5** |
| | SCL | **GPIO 6** |
| Drehgeber KY-040 | + | 3V3 |
| | GND | GND |
| | CLK | **GPIO 3** |
| | DT | **GPIO 4** |
| | SW | **GPIO 10** |
| Summer (über Transistor) | Basis über 1 kΩ | **GPIO 7** |
| | Summer + | 5V |
| | Summer − → Kollektor, Emitter → GND | |
| Versorgung | 5 V über USB-C **oder** 5V-Pin | 5V / GND |

```
GPIO7 ──[1kΩ]──┐
               B│ BC547
  5V ──(+Summer−)── C
                    E ── GND
```

Für ein klassisches ESP32-DevKit sind andere Pins vorbelegt (`env:esp32dev` in `platformio.ini`).

## 4. Software aufspielen

1. [VS Code](https://code.visualstudio.com/) + Erweiterung **PlatformIO** installieren.
2. Diesen Ordner öffnen.
3. ESP32-C3 per USB anschließen, unten in der Statusleiste **Upload** (→) klicken
   bzw. im Terminal: `pio run -e esp32c3 -t upload`
4. Optional Log ansehen: `pio device monitor`

Ohne Sensoren testen: Menü → **Demo-Modus an**. Dann laufen simulierte Werte, und am hinteren
linken Rad wird regelmäßig ein Luftverlust simuliert (Warnung → Leck-Alarm).

## 5. Bedienung

Alles mit einem Drehknopf:

| Wo | Aktion | Wirkung |
|---|---|---|
| Hauptanzeige | **kurz drücken** | Straße ⇄ Offroad (bei aktivem Alarm: Alarm quittieren) |
| | **drehen** | Druck- ⇄ Temperaturansicht |
| | **lang drücken** | Menü |
| Menü | drehen / kurz | auswählen / Wert ändern / speichern |
| | lang | zurück |

**Menüpunkte:** Solldruck Straße vorn/hinten, Offroad vorn/hinten (0,1-bar-Schritte),
Warnschwelle, Kritisch-Schwelle, Hinweis bei Überdruck, Leck-Erkennung (Verlust/Zeit),
Sensoren anlernen, Helligkeit, Warnton an/aus, Demo-Modus. Alles wird dauerhaft gespeichert.

> ⚠️ Die voreingestellten Solldrücke (Straße 3,5 / 4,0 bar, Offroad 2,5 / 2,8 bar) sind **Platzhalter**.
> Trag die Werte aus der Reifendrucktabelle deines Sprinters ein (Tankklappe bzw. B-Säule) –
> sie hängen von zul. Gesamtgewicht, Reifengröße und Beladung ab. Offroad-Werte nicht zu niedrig
> wählen: Transporter-Reifen (C-Reifen) vertragen weniger Luftablassen als Geländewagenreifen.

### Sensoren anlernen
1. Menü → **Sensoren anlernen**. Am besten vorher alle Sensoren abschrauben.
2. Das Display fragt nacheinander **VL, VR, HL, HR**. Sensor auf das angezeigte Rad schrauben –
   beim Aufschrauben sendet er sofort.
3. Erscheint „Sensor AB:CD:EF … bar“, **kurz drücken** = übernehmen. Weiter zum nächsten Rad.
   Kurz drücken ohne Fund = Rad überspringen (alte Zuordnung bleibt), lang = Abbruch.

Bereits einem anderen Rad zugeordnete Sensoren werden dabei ignoriert.

