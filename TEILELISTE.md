# Teileliste – Reifendruck- und Neigungsanzeige

Stand: November 2026. Preise schwanken je nach Händler – vor dem Kauf vergleichen.

## Das Display: M5Stack DinMeter v1.1

Ein Einbau-Messgerät im **1/32-DIN-Normmaß** – wie ein kleines Zusatzinstrument: vorne ein
schmaler Rahmen (**53 × 30 mm**), dahinter verschwindet das Gerät in einem rechteckigen Ausschnitt
von **45 × 22,5 mm** (32 mm Einbautiefe). 1,14"-Farbdisplay (240 × 135), Drehknopf mit Taster,
Summer, Bluetooth – und **6–36 V Eingang**: direkt ans Bordnetz, kein USB-Adapter nötig.

![Vorschau Modern](docs/bilder/v2_modern/1_reifen.png) ![Vorschau Retro](docs/bilder/v1_retro/1_reifen.png)

## Einkaufsliste (vollständig)

### Elektronik

| ✓ | Menge | Teil | Artikel / Suchbegriff | Hinweis | ca. Preis |
|---|---|---|---|---|---|
| ☐ | 1 | **M5Stack DinMeter v1.1** | K134-V11 | das Display mit Drehknopf, Summer, Uhr, Bluetooth | 25–35 € |
| ☐ | 1 | **M5Stack Unit Mini IMU-Pro** | U171 (BMI270 + BMM150 + BMP280) | Neigung, Kompass, Höhe, Luftdruck, Innentemperatur. 5-cm-Kabel liegt bei | 7–10 € |
| ☐ | 1 | **Grove-Kabel 20 cm** (HY2.0-4P) | „M5Stack Grove Cable 20cm“ | damit die IMU nicht direkt am Display kleben muss (weiter weg von Störquellen) | 2–3 € |
| ☐ | 4 | **BLE-Ventilkappen-Reifensensoren** (1 Set) | App „TPMSII“, „SYTPMS“, „ITPMS (K)“ oder „Ai TPMS“ | **Messbereich mind. 6 bar / 87 psi**, mit Kontermuttern, CR1632/CR2032-Batterien meist dabei | 25–45 € |

> **Mini IMU-Pro ausverkauft?** Alternative, die die Firmware genauso erkennt:
> **6-Axis IMU Unit (MPU6886, U095)** für Neigung + Ausrichten, optional zusätzlich **ENV IV Unit
> (SHT40 + BMP280, U001-D)** für Höhe, Luftdruck/Wetter und Innentemperatur. Beide zusammen an Port A
> brauchen einen **Grove-Verteiler (M5Stack 1-zu-3 HUB Unit)**. Der **Kompass** geht nur mit der
> Mini IMU-Pro (Magnetfeldsensor) – die kann man später nachrüsten.

### Strom

| ✓ | Menge | Teil | Hinweis | ca. Preis |
|---|---|---|---|---|
| ☐ | 1 | **Sicherungsabgriff („Add-A-Fuse“)** | Bauform passend zum Sprinter: **vorher eine Sicherung ziehen und vergleichen** (Mini oder Low-Profile-Mini) | 3–5 € |
| ☐ | 1 | **Flachsicherung 2 A** in derselben Bauform | für das Display | 1 € |
| ☐ | 2 m | **Fahrzeugleitung 0,5 mm²**, rot + schwarz (oder 2-adrig) | Plus und Masse | 3 € |
| ☐ | 1 | **Ringkabelschuh M6** (isoliert) | Masse an einen Massepunkt (Schraube an der Karosserie) | 1 € |
| ☐ | 4 | **Stoßverbinder** oder Lötverbinder, dazu Schrumpfschlauch | Kabel verlängern/verbinden | 3 € |
| ☐ | 1 | **Stecker/Klemme für den 6–36-V-Eingang** des DinMeter | **Bauform im Lieferumfang bzw. in der M5Stack-Doku prüfen** – konnte ich nicht sicher klären. Notlösung: Strom über USB-C mit einem Kfz-USB-Adapter | – |

### Einbau

| ✓ | Menge | Teil | Hinweis | ca. Preis |
|---|---|---|---|---|
| ☐ | 1 | **Träger-Blende**: 1-DIN-Blindblende (Radioschacht) oder Blindabdeckung/Fachdeckel | darin den Ausschnitt 45 × 22,5 mm machen | 5–15 € |
| ☐ | 1 | **3M VHB** doppelseitiges Klebeband (schmal) | Display in der Blende und IMU hinter der Verkleidung fixieren | 5 € |
| ☐ | 1 | **Gummitülle / Kantenschutz** | falls Kabel durch eine Bohrung laufen | 1 € |
| ☐ | 5 | **Kabelbinder** (klein) | Kabel ordentlich verlegen | 1 € |
| ☐ | 1 | **USB-C-Kabel mit Datenleitung** | Firmware einmalig vom PC aufspielen (reine Ladekabel gehen nicht) | – (meist vorhanden) |

**Gesamt: ca. 85–130 €.**

### Werkzeug

Multimeter (Zündungsplus-Sicherung finden), Abisolierzange, Crimpzange, Verkleidungskeile,
Cutter + kleine Feile + Bohrer (Ausschnitt), Isopropanol zum Entfetten, PC mit VS Code + PlatformIO.

## Einbau

1. **Ort wählen:** z. B. freier DIN-Schacht, Blindabdeckung in der Schalterleiste oder eine Fachabdeckung.
   Hinter der Blende müssen **ca. 35 mm Tiefe** plus Platz für die Kabel frei sein.
2. **Ausschnitt 45 × 22,5 mm** anzeichnen, mit kleinem Bohrer vorbohren, mit Cutter/Feile auf Maß bringen.
   Der Rahmen vorne deckt kleine Ungenauigkeiten ab. Lieber erst etwas zu klein, dann nachfeilen.
3. Gerät von vorne einsetzen und mit den **Schraublöchern** oder **doppelseitigem Klebeband** fixieren
   (beides sieht der Hersteller vor).
4. Strom von Zündungsplus über die 2-A-Sicherung an den 6–36-V-Eingang, Masse an einen Massepunkt.
5. Die **Unit Mini IMU-Pro** fest hinter die Verkleidung kleben – sie muss starr sitzen.
   Für den Kompass möglichst **weg von Lautsprechern, Motoren und dicken Stromkabeln** (Magnetfelder).
6. Danach im Menü: Sensoren anlernen, Neigung nullen (siehe README).

> ⚠️ **Temperatur:** M5Stack gibt als Betriebstemperatur **0–40 °C** an. Im Auto wird es im Sommer
> wärmer und im Winter kälter. Deshalb einen Platz **ohne direkte Sonne** wählen (nicht oben auf dem
> Armaturenbrett, eher unten in der Mittelkonsole oder im DIN-Schacht). Einen Akku brauchst du nicht
> (es gibt nur einen optionalen Anschluss dafür). Bei Kälte kann das Display träge reagieren.

## Konkrete Empfehlung Reifensensoren

| Wahl | Produkt | App | Format | Warum | Achtung |
|---|---|---|---|---|---|
| **1** | z. B. **WISTEK 4-Sensor TPMS** (Amazon, ASIN B09Z2P6M48) oder baugleiche „Bluetooth TPMS 4 externe Sensoren“ | **TPMSII** | Typ A | verbreitetstes Format, Dekoder mit echtem Mitschnitt getestet | Messbereich laut Anleitung **bis 6,4 bar (92,6 psi)** – reicht, wenn dein Solldruck hinten ≤ ca. 5,5 bar ist |
| 2 | **WODHMIEY Reifendruck-Kontrollsystem** (Amazon.de, ASIN B0GWH88YSV) | ITPMS (K) / LYTPMS | Typ D | im Referenzprojekt mit echtem Mitschnitt belegt, auf Amazon.de verfügbar | Messbereich vor dem Kauf auf der Produktseite prüfen |
| 3 | AliExpress-Sets laut Referenzprojekt: [TPMSII](https://www.aliexpress.com/item/1005006129840804.html), [SYTPMS](https://www.aliexpress.com/item/1005006755884183.html) | TPMSII / SYTPMS | Typ A / B | günstig | lange Lieferzeit; bei 4-Sensor-Variante bestellen, nicht Motorrad (2 Sensoren) |

**Wichtig:** Händler verkaufen äußerlich gleiche Sensoren mit unterschiedlicher Elektronik. Entscheidend ist
die **App in der Beschreibung**. Nach dem Auspacken mit der App testen – und wenn das Display sie nicht
findet: `DEBUG_BLE 1` setzen und das Log schicken, dann ergänze ich den Dekoder. Bei Amazon ist die
Rücksendung einfacher als bei AliExpress.

Markensets mit eigener App (z. B. SuperSense, Maxsensor, FOBO) funken mit eigenen Protokollen und
werden **nicht** erkannt.

## Welche Reifensensoren?

Die Firmware erkennt automatisch diese Bauarten (Formate aus dem Open-Source-Projekt [tpms_ble](https://github.com/bkbilly/tpms_ble)):

| Typ | Erkennbar an |
|---|---|
| A | App **„TPMSII“**, Bluetooth-Name `TPMS1_…`–`TPMS4_…` – sehr verbreitet, meist günstigste Wahl |
| B | App **„SYTPMS“** |
| D | App **„ITPMS (K)“** / „LYTPMS“, Marke WODHMIEY |
| F | „HRTPMS“ |
| G | App **„Ai TPMS“** (AI-8000), Name `AI-…` |
| H | „TC.TPMS“ |

In der Produktbeschreibung steht, **welche Handy-App** man nutzen soll – ist es eine davon, passt der Sensor.
Unbekannter Typ? `DEBUG_BLE 1` setzen und das Log schicken, dann lässt sich ein Dekoder ergänzen.
