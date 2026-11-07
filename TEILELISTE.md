# Teileliste – Reifendruck- und Neigungsanzeige mit M5Stack Dial

Stand: November 2026. Preise schwanken stark je nach Händler, bitte vor dem Kauf vergleichen.

## Pflicht

| # | Teil | Wofür | Worauf achten | ca. Preis |
|---|---|---|---|---|
| 1 | **M5Stack Dial v1.1** (ESP32-S3) | Display, Drehknopf, Summer, Bluetooth – das ganze Gerät | Aktuelle Version heißt **v1.1** (das erste Modell wurde eingestellt). Rundes 1,28"-Touchdisplay, ca. Ø 45 × 32 mm. Laut Hersteller **6–36 V Eingang** auf der Rückseite – direkt ans 12-V-Bordnetz. | 35–45 € |
| 2 | **4× BLE-Ventilkappen-Reifensensoren** | Druck + Temperatur je Rad | Die Firmware erkennt 6 verbreitete Typen (siehe unten). **Messbereich mind. 6 bar (87 psi)** – der Sprinter fährt hinten über 4 bar! Am besten Sets mit Kontermuttern (Diebstahlschutz). | 25–45 € |
| 3 | **Stromanschluss** | Dial an Zündungsplus | Sicherungsabgriff („Add-A-Fuse“) passend zum Sicherungstyp im Sprinter (Mini oder Low-Profile-Mini – **vorher nachschauen**), Sicherung **2 A**, ca. 2 m Kabel 2 × 0,5 mm², Ringöse für Masse, Schrumpfschlauch. Stecker für den 6–36-V-Anschluss des Dial prüfen (Doku/Lieferumfang). | 10 € |
| 4 | **Halter aus dem 3D-Drucker** | sauberer Einbau | Dateien in `hardware/stl/`. **Material: ASA, ABS oder mind. PETG – kein PLA!** Hinter der Scheibe wird es im Sommer über 70 °C, PLA wird weich. Kein Drucker? Bei Druckdiensten (z. B. JLC3DP, Craftcloud) oder im Makerspace. | 0–15 € |

## Für den Neigungsmesser (sehr empfohlen)

| # | Teil | Wofür | Worauf achten | ca. Preis |
|---|---|---|---|---|
| 5 | **M5Stack Unit IMU (MPU6886)**, Artikel U095 | Seiten- und Längsneigung, Stellplatz-Ausrichtung | Einfach per beiliegendem Grove-Kabel in **Port A** des Dial stecken, **kein Löten**. Ist oft ausverkauft – Alternative: jedes MPU-6050-Modul mit Grove-Adapterkabel. | 7–10 € |
| 6 | Grove-Kabel 20 cm (optional) | falls die IMU nicht direkt hinter dem Dial sitzt | M5Stack „Grove Cable 20cm“ | 2 € |

## Befestigung – je nach Einbauart

**Variante A – Aufbau (aufkleben, nur ein kleines Kabelloch):**
- Halter `aufbau.stl` (gerade) oder `aufbau_15grad.stl` (zum Fahrer geneigt)
- **3M VHB**-Klebepad, rund ≥ 60 mm oder Streifen (z. B. 3M VHB 5952) – Fläche vorher mit Isopropanol entfetten
- Stufenbohrer für das Kabelloch (Ø 10–14 mm), Kantenschutz/Gummitülle

**Variante B – Einbau (bündig in eine Blende):**
- Halter `einbau.stl` + `schablone.stl` (Bohrschablone)
- 3 Senkkopf-Blechschrauben 2,9 × 9,5 mm (DIN 7982) **oder** Kleber
- Lochsäge/Stufenbohrer bzw. Dremel. Tipp: nicht ins Armaturenbrett selbst, sondern in eine **Blindabdeckung** oder ein Ablagefach-Deckel schneiden – die kann man günstig nachkaufen, falls etwas schiefgeht.

## Werkzeug

Messschieber (Pflicht zum Nachmessen vor dem Druck!), Multimeter (Zündungsplus finden), Abisolierzange, Crimpzange, Verkleidungskeile, USB-C-Kabel zum Flashen.

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
| Nur Reifendruck | 70–100 € |
| Reifendruck + Neigung | 80–110 € |
