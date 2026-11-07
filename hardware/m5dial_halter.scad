// Halterungen fuer den M5Stack Dial im Armaturenbrett – parametrisch (OpenSCAD)
//
// Teile (unten bei "teil" waehlen und als STL exportieren):
//   "testring"    – 10-Minuten-Druck zum Pruefen der Passung. IMMER ZUERST drucken!
//   "aufbau"      – flacher Aufbau-Halter zum Aufkleben (VHB-Klebeband),
//                    Kabel gehen nach unten durch ein kleines Loch ins Armaturenbrett.
//                    Optional schraeg (neigung), damit das Display zum Fahrer zeigt.
//   "einbau"      – Einbau-Blende fuer ein rundes Loch in einer Blende/Blindabdeckung,
//                    vorne buendig, mit Senkkopf-Schraubloechern.
//   "schablone"   – Bohr-/Anreissschablone fuer das Einbauloch.
//
// WICHTIG: Der Drehring oben am Dial muss frei bleiben – die Halter umfassen nur das
// Unterteil. Miss vor dem Druck mit dem Messschieber nach und trag die Werte unten ein.

teil = "aufbau";  // [testring, aufbau, einbau, schablone]

/* [Masse des Dial – bitte nachmessen!] */
dial_d       = 45.0;  // Aussendurchmesser des Gehaeuse-UNTERTEILS
dial_h       = 32.3;  // Gesamthoehe
ring_frei    = 12.0;  // Hoehe von oben, die frei bleiben muss (Drehring + Luft)
spiel        = 0.30;  // Luft rundum (PLA/PETG: 0.25..0.4)
quetschrippen = 0.45; // Uebermass der 6 Klemmrippen fuer Presssitz (0 = keine)

/* [Aufbau-Halter] */
wand         = 2.4;   // Wandstaerke
boden        = 3.0;   // Bodenstaerke
kabelraum    = 10.0;  // Platz unter dem Dial fuer Grove-Stecker und Stromkabel
kabel_loch_d = 14.0;  // Loch im Boden (Kabel ins Armaturenbrett)
fuss_d       = 62.0;  // Durchmesser der Klebeflaeche (VHB-Pad)
neigung      = 0;     // Grad: Halter kippt Richtung Fahrer (z.B. 15)
usb_schlitz  = true;  // Seitlicher Schlitz fuer USB-C (zum Flashen ohne Ausbau)
usb_b        = 12.0;  // Schlitzbreite
usb_h        = 7.0;   // Schlitzhoehe
kabel_seitlich = false; // statt Bodenloch: Kabelauslass seitlich am Fuss

/* [Einbau-Blende] */
blech_t      = 3.0;   // Staerke der Blende / Verkleidung, in die eingebaut wird
flansch_b    = 6.0;   // Breite des sichtbaren Rands vorne
flansch_t    = 2.0;   // Dicke des Rands
schrauben    = 3;     // Anzahl Senkkopfschrauben (0 = nur kleben)
schraube_d   = 3.0;   // Schraubendurchmesser (z.B. 2,9 mm Blechschraube oder M3)

$fn = 120;

// ---------------------------------------------------------------- Ableitungen
innen_d   = dial_d + 2 * spiel;
sitz_h    = dial_h - ring_frei;          // so tief steckt der Dial im Halter
aussen_d  = innen_d + 2 * wand;
tubus_d   = innen_d + 2 * wand;          // = Durchmesser des Einbaulochs
flansch_d = tubus_d + 2 * flansch_b;

module rippen(h) {
  if (quetschrippen > 0)
    for (a = [0 : 60 : 359])
      rotate(a) translate([innen_d / 2 - quetschrippen, -0.8, 0]) cube([quetschrippen + 0.5, 1.6, h]);
}

// Huelse, in die der Dial gesteckt wird (Innen-Kontur mit Rippen)
module huelse(h) {
  difference() {
    cylinder(d = aussen_d, h = h);
    translate([0, 0, -1]) cylinder(d = innen_d, h = h + 2);
  }
  rippen(h);
}

// ---------------------------------------------------------------- Testring
module testring() {
  difference() {
    huelse(6);
    // Ausschnitt markiert die Seite, an der die USB-Buchse sitzt
    translate([aussen_d / 2 - 3, -usb_b / 2, 3]) cube([10, usb_b, 10]);
  }
}

// ---------------------------------------------------------------- Aufbau-Halter
module aufbau() {
  auflage = 1.6;  // Ringschulter, auf der der Dial aufliegt
  becher_h = boden + kabelraum + sitz_h;
  // Keil fuer die Neigung: der Becher steht auf einem schraegen Sockel
  keil_h = tan(neigung) * fuss_d;

  difference() {
    union() {
      // Fuss / Klebeflaeche + Keil
      hull() {
        cylinder(d = fuss_d, h = 1.2);
        translate([0, 0, keil_h / 2]) rotate([neigung, 0, 0]) cylinder(d = aussen_d, h = boden);
      }
      // Becher
      translate([0, 0, keil_h / 2]) rotate([neigung, 0, 0]) {
        difference() {
          cylinder(d = aussen_d, h = becher_h);
          translate([0, 0, boden]) cylinder(d = innen_d - 2 * auflage, h = becher_h);  // Kabelraum
          translate([0, 0, boden + kabelraum]) cylinder(d = innen_d, h = becher_h);    // Sitz
          if (usb_schlitz)
            translate([0, aussen_d / 2, boden + kabelraum + usb_h / 2 + 1])
              cube([usb_b, aussen_d, usb_h + 2], center = true);
        }
        translate([0, 0, boden + kabelraum]) rippen(sitz_h);
      }
    }
    // Kabeldurchfuehrung
    if (!kabel_seitlich) {
      translate([0, 0, -1]) cylinder(d = kabel_loch_d, h = keil_h + boden + 5);
    } else {
      translate([0, 0, boden + kabelraum / 2 + keil_h / 2])
        rotate([90, 0, 0]) cylinder(d = 8, h = fuss_d);
    }
    // Unterseite plan
    translate([0, 0, -50]) cube(100, center = true);
  }
}

// ---------------------------------------------------------------- Einbau-Blende
module einbau() {
  tubus_h = sitz_h - flansch_t;  // Dial-Unterteil steckt komplett in Flansch + Tubus
  difference() {
    union() {
      // sichtbarer Flansch (oben = Fahrzeug-Innenraum)
      translate([0, 0, tubus_h]) cylinder(d1 = flansch_d - 1.5, d2 = flansch_d - 3.5, h = flansch_t);
      translate([0, 0, tubus_h - 0.01]) cylinder(d = flansch_d - 1.5, h = 0.01 + 0.2);
      // Tubus durch das Loch
      cylinder(d = tubus_d, h = tubus_h + 0.1);
    }
    translate([0, 0, 2]) cylinder(d = innen_d, h = 100);       // Sitz
    translate([0, 0, -1]) cylinder(d = innen_d - 4, h = 100);  // Auflage-Schulter 2 mm
    // Senkkopfschrauben im Flansch
    if (schrauben > 0)
      for (a = [90 : 360 / schrauben : 449])
        rotate(a) translate([tubus_d / 2 + flansch_b / 2, 0, 0]) {
          cylinder(d = schraube_d + 0.3, h = 100);
          translate([0, 0, tubus_h + flansch_t - schraube_d * 0.6])
            cylinder(d1 = schraube_d, d2 = schraube_d * 2.1, h = schraube_d * 0.6 + 0.01);
        }
  }
  translate([0, 0, 2]) rippen(tubus_h - 2);
  // Hinweis: blech_t bestimmt nur die Schraubenlaenge; der Tubus ragt hinten heraus.
  echo(str("Einbauloch bohren/saegen: Durchmesser ", tubus_d, " mm"));
  echo(str("Tubus ragt hinter der Blende ", tubus_h - blech_t, " mm heraus"));
}

// ---------------------------------------------------------------- Schablone
module schablone() {
  difference() {
    cylinder(d = flansch_d + 10, h = 1.2);
    translate([0, 0, -1]) cylinder(d = tubus_d, h = 5);           // Innenkontur = Loch
    translate([0, 0, -1]) cylinder(d = 2, h = 5);
    if (schrauben > 0)
      for (a = [90 : 360 / schrauben : 449])
        rotate(a) translate([tubus_d / 2 + flansch_b / 2, 0, -1]) cylinder(d = 2, h = 5);
  }
  // Steg mit Mittelpunkt zum Ankoernen
  difference() {
    translate([-tubus_d / 2, -2, 0]) cube([tubus_d, 4, 1.2]);
    translate([0, 0, -1]) cylinder(d = 2, h = 5);
  }
}

if (teil == "testring") testring();
else if (teil == "aufbau") aufbau();
else if (teil == "einbau")  // Flansch nach unten = sichtbare Seite glatt auf dem Druckbett
  translate([0, 0, sitz_h]) rotate([180, 0, 0]) einbau();
else if (teil == "schablone") schablone();
