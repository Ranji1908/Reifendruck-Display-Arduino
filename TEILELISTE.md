# Teileliste – Reifendruck- und Neigungsanzeige mit M5Stack Dial

Stand: November 2026. Preise schwanken stark je nach Händler, bitte vor dem Kauf vergleichen.

## Pflicht

| # | Teil | Wofür | Worauf achten | ca. Preis |
|---|---|---|---|---|
| 1 | **M5Stack Dial v1.1** (ESP32-S3) | Display, Drehknopf, Summer, Bluetooth – das ganze Gerät | Aktuelle Version heißt **v1.1** (das erste Modell wurde eingestellt). Rundes 1,28"-Touchdisplay, ca. Ø 45 × 32 mm. Laut Hersteller **6–36 V Eingang** auf der Rückseite – direkt ans 12-V-Bordnetz. | 35–45 € |
| 2 | **4× BLE-Ventilkappen-Reifensensoren** | Druck + Temperatur je Rad | Die Firmware erkennt 6 verbreitete Typen (siehe unten). **Messbereich mind. 6 bar (87 psi)** – der Sprinter fährt hinten über 4 bar! Am besten Sets mit Kontermuttern (Diebstahlschutz). | 25–45 € |
| 3 | **Stromanschluss** | Dial an Zündungsplus | Sicherungsabgriff („Add-A-Fuse“) passend zum Sicherungstyp im Sprinter (Mini oder Low-Profile-Mini – **vorher nachschauen**), Sicherung **2 A**, ca. 2 m Kabel 2 × 0,5 mm², Ringöse für Masse, Schrumpfschlauch. Stecker für den 6–36-V-Anschluss des Dial prüfen (Doku/Lieferumfang). | 10 € |
| 4 | **52-mm-Instrumentenhalter** (1-fach, „Zusatzinstrument Halter Armaturenbrett, Aufbau“) | fertiges Gehäuse fürs Armaturenbrett | Genormtes Autozubehör für Zusatzinstrumente, gibt es gerade, schräg, für die A-Säule. Der Dial (Ø 45 mm) sitzt darin mit einem Ring aus Moosgummi-Klebeband. Hinten offen bzw. mit Kabelloch. Beispiele: Riemann-Stil 1er-Halter ca. 9,50 €, Hoffmann-Speedster „Instrumentengehäuse 52 mm kurz“ ca. 15 €. | 10–15 € |
| 5 | **Moosgummi-Dichtband** 3 mm stark, selbstklebend (Baumarkt) | Dial im Halter festklemmen, Spalt auffüllen | 1–2 Lagen um den Dial wickeln, bis er stramm sitzt. Eine Scheibe davon hinter den Dial legen, wenn er weiter vorne sitzen soll. | 3 € |

## Für den Neigungsmesser (sehr empfohlen)

| # | Teil | Wofür | Worauf achten | ca. Preis |
|---|---|---|---|---|
| 6 | **M5Stack Unit IMU (MPU6886)**, Artikel U095 | Seiten- und Längsneigung, Stellplatz-Ausrichtung | Einfach per beiliegendem Grove-Kabel in **Port A** des Dial stecken, **kein Löten**. Ist oft ausverkauft – Alternative: jedes MPU-6050-Modul mit Grove-Adapterkabel. | 7–10 € |
| 7 | Grove-Kabel 20 cm (optional) | falls die IMU nicht direkt hinter dem Dial sitzt | M5Stack „Grove Cable 20cm“ | 2 € |

## Einbau ohne Basteln am Gehäuse

1. Halter mit den beiliegenden Schrauben oder 3M-VHB-Klebeband aufs Armaturenbrett setzen
   (z. B. oben auf die Ablage oder an die A-Säule). Fläche vorher mit Isopropanol entfetten.
2. Ein kleines Loch (Ø 10–14 mm, Stufenbohrer) hinter dem Halter für die Kabel, Gummitülle rein.
3. Unit IMU mit doppelseitigem Klebeband hinten in den Halter oder hinter die Verkleidung kleben –
   sie muss fest sitzen, nicht baumeln.
4. Moosgummi um den Dial wickeln und ihn in den Halter drücken.

**Drehring verdeckt?** Sitzt der Dial so tief, dass man den Ring nicht greifen kann, ist das egal:
Die Firmware lässt sich komplett **per Touch** bedienen (tippen, lange tippen, wischen).

*Wer doch einen 3D-Drucker hat:* Passgenaue Halter liegen in `hardware/` – optional.

## Werkzeug

Multimeter (Zündungsplus finden), Abisolierzange, Crimpzange, Verkleidungskeile, Stufenbohrer, USB-C-Kabel zum Flashen.

## Welche Reifensensoren?

Die Firmware erkennt automatisch diese Bauarten (Formate aus dem Open-Source-Projekt [tpms_ble](https://github.com/bkbilly/tpms_ble)):

| Typ | Erkennbar an | Hinweis |
|---|---|---|
| A | App **„TPMSII“**, Bluetooth-Name `TPMS1_…`–`TPMS4_…` | sehr verbreitet, meist günstigste Wahl |
| B | App **„SYTPMS“** | |
| D | App **„ITPMS (K)“** / „LYTPMS“, Marke WODHMIEY | |
| F | „HRTPMS“ | |
| G | App **„Ai TPMS“** (AI-8000), Name `AI-…` | |
| H | „TC.TPMS“ | |

Am einfachsten: In der Produktbeschreibung steht, **welche Handy-App** man nutzen soll. Ist es eine der Apps oben, passt der Sensor. Prüfen kannst du es vorab mit der App **nRF Connect** (Gerätename/Daten).
Kommt ein unbekannter Typ, `DEBUG_BLE 1` setzen und das Log schicken – dann lässt sich ein Dekoder ergänzen.

## Gesamtkosten

| Ausbau | ca. |
|---|---|
| Nur Reifendruck | 85–115 € |
| Reifendruck + Neigung | 95–125 € |
