# Bordinstrument für den Sprinter W906 – Reifendruck, Neigung, Kompass

Ein kleines Einbau-Display im **1/32-DIN-Normmaß** (Rahmen 53 × 30 mm, Ausschnitt 45 × 22,5 mm) auf
Basis des **M5Stack DinMeter v1.1** – im **Retro-Look in Bernstein-Gelb**: 7-Segment-Zahlen, Pixelschrift,
Warnungen invertiert. Reifendruck aller 4 Räder mit **Straße/Offroad**-Solldrücken und Warnungen,
dazu Neigungsmesser, Kompass, Höhe/Wetter/Uhr und Stellplatz-Ausrichtung.

- 🛒 **Was kaufen & wie einbauen?** → [TEILELISTE.md](TEILELISTE.md)

![Alle Seiten](docs/bilder/uebersicht.png)

*(Vorschau nachgezeichnet – Geometrie wie in der Firmware, Pixelschrift angenähert.)*

## Die Seiten (Knopf drehen)

| # | Seite | Inhalt | braucht |
|---|---|---|---|
| 1 | **DRUCK** | Transporter quer (Front rechts), an jeder Ecke Druck in bar und Soll. Alarm: Rad-Feld blinkt invertiert. | Reifensensoren |
| 2 | **REIFENTEMP** | Temperatur jedes Reifens und Batteriestand der Sensoren | Reifensensoren |
| 3 | **NEIGUNG** | Heck- und Seitenansicht kippen mit, Gradzahlen, Warnung ab einstellbarem Winkel | Mini IMU-Pro |
| 4 | **KOMPASS** | Kurs in Grad, Himmelsrichtung und laufendes Kompassband | Mini IMU-Pro |
| 5 | **BORD** | Höhe über NN, Uhrzeit, Luftdruck mit Wettertrend, Innentemperatur | Mini IMU-Pro (Uhr ist eingebaut) |
| 6 | **AUSRICHTEN** | Stellplatz-Hilfe: Libelle + **wie viele cm unter welches Rad** (Auffahrkeile), bis der Camper eben steht | Mini IMU-Pro |

## So funktioniert's

```
 4× BLE-Ventilkappen-Sensor ──Bluetooth──►  M5Stack DinMeter  ◄──Port A── Unit Mini IMU-Pro
                                              │                       (Lage, Kompass, Luftdruck)
                                              │
                         Zündungsplus 12 V ───┘ (6–36-V-Eingang, 2-A-Sicherung)
```

Die Sensoren senden Druck, Temperatur und Batterie per Bluetooth; das Display hört passiv mit
(keine Kopplung per App nötig). **Sechs verbreitete Sensor-Typen** werden automatisch erkannt.

## Software aufspielen

1. [VS Code](https://code.visualstudio.com/) + Erweiterung **PlatformIO** installieren, diesen Ordner öffnen.
2. DinMeter per USB-C anschließen, unten **Upload** (→) – bzw. `pio run -e dinmeter -t upload`.
3. Ohne Sensoren testen: Menü → **Demo-Modus an** (simuliert Druckverlust hinten links und Schaukeln).

## Bedienung (ein Drehknopf)

| Wo | Aktion | Wirkung |
|---|---|---|
| Anzeige | **drehen** | Seite wechseln (6 Seiten, siehe oben) |
| | **kurz drücken** | Straße ⇄ Offroad (bei aktivem Alarm: quittieren) |
| | **lang drücken** | Menü |
| Menü | drehen / kurz / lang | wählen / ändern & speichern / zurück |

**Menü:** Solldruck Straße vorn/hinten, Offroad vorn/hinten · Warn-, Kritisch-, Überdruck-Schwelle ·
Leck-Erkennung · Sensoren anlernen · Neigung nullen · Einbaulage · Warnwinkel Seite/Längs ·
Kompass kalibrieren / Korrektur / spiegeln · Höhe setzen · Uhr stellen · Radstand/Spurweite ·
Helligkeit · Lautstärke · Warnton · Demo.

> ⚠️ Die Solldrücke ab Werk (Straße 3,5/4,0 bar, Offroad 2,5/2,8 bar) sind **Platzhalter**.
> Trag die Werte aus der Reifendrucktabelle deines Sprinters ein (Tankklappe/B-Säule).

### Erste Einrichtung
1. **Sensoren anlernen:** Menü → *Sensoren anlernen*. Nacheinander VL, VR, HL, HR: Sensor auf das
   angezeigte Rad schrauben (er sendet sofort), kurz drücken = übernehmen.
2. **Neigung nullen:** Sprinter auf eine ebene Fläche stellen → Menü → *Neigung nullen* → kurz drücken.
3. **Richtung prüfen:** Mit den Vorderrädern auf eine Bordsteinkante fahren – die Seite NEIGUNG muss
   „bergauf“ zeigen. Zeigt sie „bergab“ oder Seitenneigung, im Menü die *Einbaulage* um 90°-Schritte
   ändern, bis es stimmt.
4. **Radstand** eintragen (W906: 325 / 367 / 432 cm) – wichtig für die cm-Angaben beim Ausrichten.
5. **Kompass kalibrieren:** Menü → *Kompass Kal* → auf einem Parkplatz langsam 1–2 Kreise fahren, bis
   die Anzeige ≥ 75 % zeigt, dann drücken. Danach mit einem Handy-Kompass vergleichen und bei Bedarf
   *Kompass +-* (Feinkorrektur) bzw. *Komp Spieg* (dreht falsch herum) einstellen.
6. **Höhe setzen:** Menü → *Höhe* → bekannte Höhe eintragen (z. B. vom Navi). Der Luftdruck ändert sich mit
   dem Wetter, deshalb gelegentlich nachstellen.
7. **Uhr stellen:** Menü → *Uhr Std* / *Uhr Min*.

## Warnlogik

| Zustand | Bedingung (Standard) | Anzeige | Ton |
|---|---|---|---|
| OK | im Sollbereich | weiß | – |
| HOCH | > 20 % über Soll | orange | – (z. B. nach Umschalten auf Offroad = „noch Luft ablassen“) |
| NIEDRIG | > 10 % unter Soll | orange blinkend | 2 Pieptöne, einmalig |
| KRITISCH | > 20 % unter Soll | rot blinkend | Dauer-Alarm bis Quittierung |
| LECK! | ≥ 0,3 bar Verlust in 5 min | rot blinkend | Dauer-Alarm; bleibt, bis Soll wieder erreicht |
| Neigung | ≥ 25° seitlich / 30° längs | rot | Warnton, springt auf Seite NEIGUNG |

- Schwellen gelten relativ zum Soll des **aktuellen Modus**; 0,05 bar Hysterese gegen Flackern.
- Ein quittierter Alarm meldet sich erneut, wenn es **schlimmer** wird.
- Reifen werden warm – „HOCH“ nach langer Autobahnfahrt ist normal. Solldruck kalt einstellen.
- Der Neigungsmesser ist gefiltert (ca. 1 s). Beim Bremsen/Kurvenfahren zeigt jeder
  Beschleunigungssensor kurz falsche Werte – aussagekräftig ist er langsam im Gelände und im Stand.

## Hinweise

- **Werks-RDK:** Hat dein Sprinter ab Werk Reifendruckkontrolle, sind dessen 433-MHz-Sensoren hiermit
  **nicht** lesbar. Die Ventilkappen-Sensoren funktionieren unabhängig davon zusätzlich.
- **Ventilkappen-Sensoren** mit Kontermuttern sichern. Fürs Luftablassen abschrauben, danach wieder drauf.
- **Temperatur:** Der DinMeter ist für 0–40 °C spezifiziert – Einbauort ohne direkte Sonne wählen (siehe Teileliste).

## Fehlersuche

- Sensor wird nicht gefunden: `#define DEBUG_BLE 1` oben in `include/BleTpms.h` bzw. als Build-Flag,
  dann `pio device monitor` – zeigt alle Bluetooth-Daten in der Nähe.
- „Kein Lagesensor“ / „Kein Magnetsensor“: Unit Mini IMU-Pro in **Port A** stecken und neu starten.
- Kompass ungenau: im Stahlblech-Transporter normal (±10–20°). Neu kalibrieren, Sensor weiter weg von
  Lautsprechern/Kabeln montieren. Der Kurs ist magnetisch (Abweichung in Deutschland ca. 3–4°).
- Wettertrend nur im Stand aussagekräftig – beim Fahren über Berge ändert sich der Luftdruck mit der Höhe.
- Drehknopf springt 2 Einträge pro Rastung: `ENC_COUNTS_PER_DETENT` in `src/dinmeter/main.cpp` auf 2 setzen.

## Projektstruktur

```
platformio.ini            Build-Konfiguration (dinmeter, esp32c3/esp32dev = Selbstbau-Variante, native = Tests)
src/dinmeter/main.cpp     Firmware für den M5Stack DinMeter v1.1
src/oled/main.cpp         Alternative Selbstbau-Firmware: ESP32-C3 + 1,3"-OLED + Drehgeber (siehe docs/SELBSTBAU_OLED.md)
include/BleTpms.h         Bluetooth-Empfang (gemeinsam)
lib/TpmsCore/             Logik ohne Hardware: Sensor-Dekoder, Warnungen, Neigung, Ausrichten
test/test_core/           Unit-Tests: pio test -e native
```

## Haftung

Bastelprojekt, kein zugelassenes Sicherheitssystem. Ersetzt nicht die regelmäßige Kontrolle mit
einem geeichten Manometer.
