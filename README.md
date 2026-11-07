# Reifendruck-Display (TPMS) für den Sprinter W906

Ein kleines, minimalistisches Display fürs Armaturenbrett. Es zeigt das Fahrzeug von oben
und den Druck aller 4 Reifen. Man kann zwischen **Straße** und **Offroad** umschalten, die
Solldrücke direkt am Gerät einstellen, und es warnt per Anzeige und Summer, wenn der Druck fällt.

```
 STRASSE          ! HL NIEDRIG
       3.6   ┌──┐   3.6
  Soll 3.5  ▐│──│▌  Soll 3.5
             │──│
       3.4  ▐│  │▌  4.0        <- HL blinkt invertiert
   NIEDRIG   └──┘   Soll 4.0
```

## 1. Wie es funktioniert

```
 4× BLE-Ventilkappen-Sensor ──Bluetooth LE──►  ESP32-C3  ──I²C──►  1,3" OLED
                                                   │
                                     Drehgeber ────┤──► Summer
                                                   │
                     Zündungsplus 12 V ─► 5-V-Wandler (USB)
```

Die Sensoren senden Druck, Temperatur und Batteriestand per Bluetooth. Der ESP32 hört passiv
mit (keine Kopplung nötig), ordnet die Sensoren den Rädern zu und wertet aus.

### Warum ESP32 statt des Arduino aus der Schublade?
Ein klassischer Arduino Nano/Uno hat **kein Bluetooth**. Bluetooth-Sensoren sind aber mit Abstand
der einfachste Weg (433-MHz-Sensoren auszulesen ist ein Bastelprojekt für sich). Ein ESP32-C3
kostet ca. 3–5 €, ist kleiner als ein Nano und wird genauso mit der Arduino-IDE/PlatformIO
programmiert. Falls dein „kleiner Arduino“ ein **Arduino Nano ESP32** ist: der geht auch
(Board in `platformio.ini` anpassen, Pins in `include/config.h`).

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
senden. Dann passen sie. Sonst: siehe Abschnitt 8 „Andere Sensoren“.

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

## 6. Warnlogik

| Zustand | Bedingung (Standard) | Anzeige | Ton |
|---|---|---|---|
| OK | im Sollbereich | Wert + „Soll x.x“ | – |
| HOCH | > 20 % über Soll | „HOCH“ | – (z. B. nach Umschalten auf Offroad: „noch Luft ablassen“) |
| NIEDRIG | > 10 % unter Soll | Rad blinkt | 2 kurze Pieptöne, einmalig |
| KRITISCH | > 20 % unter Soll | Rad blinkt | Dauer-Alarm bis Quittierung |
| LECK! | ≥ 0,3 bar Verlust in 5 min | Rad blinkt | Dauer-Alarm bis Quittierung; bleibt, bis wieder Solldruck |
| k.Signal | 20 min nichts empfangen | „--“ | – |
| Batt x% | Sensorbatterie < 20 % | Hinweis | – |

- Schwellen gelten relativ zum Soll des **aktuellen Modus**.
- 0,05 bar Hysterese verhindert Flackern an der Schwelle.
- Ein quittierter Alarm meldet sich erneut, sobald es **schlimmer** wird (z. B. Warnung → Leck)
  oder nachdem der Druck zwischenzeitlich wieder in Ordnung war.
- Reifen werden warm und der Druck steigt während der Fahrt – ein Hinweis „HOCH“ nach langer
  Autobahnfahrt ist normal. Solldruck immer kalt einstellen.

## 7. Einbau im W906

- **Strom:** Sicherungsabgriff auf eine Sicherung mit Zündungsplus, damit das Display mit der
  Zündung ausgeht (sonst ~0,3 W Dauerverbrauch). Masse an einen Massepunkt. Dahinter Kfz-USB-Adapter.
  Vorher mit Multimeter prüfen, welche Sicherung nur bei Zündung Strom führt.
- **Platz:** Die kleinen Ablagen/Blindschalter in der Mittelkonsole oder die Ablage oben auf dem
  Armaturenbrett eignen sich gut. Bluetooth geht durch Kunststoff – nicht komplett in Metall einbauen,
  sonst leidet der Empfang der hinteren Räder.
- **Display drehen:** Bei Kopfüber-Einbau `DISPLAY_FLIP 1` in `include/config.h`.
- **Hitze:** Hinter der Windschutzscheibe wird es im Sommer sehr heiß – nicht direkt in die Sonne legen.
- **Werks-RDK:** Hat dein Sprinter ab Werk ein Reifendruckkontrollsystem, senden dessen Sensoren auf
  433 MHz und sind mit diesem Projekt **nicht** lesbar. Die Ventilkappen-Sensoren funktionieren
  unabhängig davon zusätzlich.
- **Ventilkappen-Sensoren:** Mit den mitgelieferten Kontermuttern gegen Diebstahl sichern. Zum
  Luftablassen fürs Gelände abschrauben, danach wieder drauf – das Display zeigt den Druck sofort an.
  Gelegentlich mit Seifenwasser auf Dichtheit prüfen.

## 8. Andere Sensoren / Fehlersuche

- `DEBUG_BLE 1` in `include/config.h` setzen, flashen und `pio device monitor` öffnen: Es werden
  alle empfangenen Bluetooth-Herstellerdaten ausgegeben (`TPMS` = erkannt). Damit lassen sich
  unbekannte Sensoren analysieren und ein weiterer Dekoder in
  `lib/TpmsCore/src/TpmsCore.cpp → decodeManufacturerData()` ergänzen.
- Kein Display: SDA/SCL vertauscht? 0,96"-Display → `DISPLAY_SH1106 0`.
- Drehknopf springt zwei Werte: `ENC_STEPS_PER_DETENT 2`. Falsche Richtung: `ENC_REVERSE 1`.
- Hinterräder mit schwachem Signal: ESP32 höher/weiter hinten platzieren, nicht hinter Metall.

## 9. Projektstruktur

```
platformio.ini            Build-Konfiguration (esp32c3, esp32dev, native-Tests)
include/config.h          Pins, Display-Typ, Bedienung
src/main.cpp              Firmware: BLE, Display, Menü, Summer
lib/TpmsCore/             Hardwareunabhängige Logik (Dekoder, Warnungen, Einstellungen)
test/test_core/           Unit-Tests der Logik: pio test -e native
```

## Haftung

Bastelprojekt, kein zugelassenes Sicherheitssystem. Es ersetzt nicht die regelmäßige
Kontrolle des Reifendrucks mit einem geeichten Manometer.
