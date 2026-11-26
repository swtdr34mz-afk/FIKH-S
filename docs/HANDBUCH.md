# FIKH-S – Handbuch

FIKH-S ist ein experimenteller Synth für das M-VAVE FM-1 von **swtdr34mz**, nach dem Konzept des Forge TME VHIKK X.
Er macht Drones, Bässe, Texturen und Geräusche. Ein **Generator** (einer von achtzehn Algorithmen in zwei Bänken) läuft durch ein
Filter (**FIELD**) und einen VCA in einen **Prozessor**: ein Stereo-Delay mit einem Frequenzverschieber in der
Rückkopplung, entweder metallisch (**DUNGEON**) oder weich und diffus (**REFLEKTA**). Ganz am Ende sitzt ein
Ambient-Hall nach dem Vorbild des Walrus Audio SLÖER.

Inoffiziell: nicht von Forge TME, Walrus Audio oder M-VAVE. Es steckt kein Code, kein Klang und keine Grafik von ihnen
darin.

![FIKH-S](../site/img/screen-main.png)

## Der Bildschirm

- **Oben links SEED**: das rote Fenster zeigt den Wurf (#000 bis #127).
- **Oben rechts SCAN**: die roten Streifen zeigen, wie weit SCAN steht.
- **Die Mitte oben**: zwei Kippschalter und drei LEDs, wie am Modul. Die Schalter zeigen den Algorithmus in seiner
  Bank (3 × 3 = 9), die LEDs die Bank (oben BANK 1, Mitte BANK 2). Darunter stehen die Nummer und der Name des Algorithmus.
- **Vier Regler**: die Regler der Seite. Der goldene Ring zeigt die Skala, das rote Licht im Ring den Wert. Bei den
  bipolaren Reglern geht es von der Mitte aus.
- **BASIS**: die Note, die klingt.
- **Das Scope**: das echte Ausgangssignal.
- **Die Anzeige darunter**: beim Drehen Name und Wert, sonst die Seite.
- **Unten**: die fünf Seiten, HOLD, daneben FREEZE oder ECO (sonst klein der Modus des Halls), die Oktave und der
  Akku.

## Die Bedienung

| Bedienelement | Was es tut |
| --- | --- |
| Tasten | **BASIS**: der Grundton aller Oszillatoren. Mono, die letzte Taste gewinnt, mit Anschlag |
| HOME | Seite **GEN**: WARP, SPAN, MORPH, FUSE |
| FX | Seite **PROC**: MIX, FEED, FORM, TIME |
| ENV | Seite **FIELD**: FIELD, CELL, ATTACK, RELEASE |
| EDIT | Seite **MOTION**: GLIDE, DRIFT, WIDTH, LEVEL |
| LFO | Seite **VERB**: MIX, DECAY, X, FILTER; nochmal: **VERB 2**: MODE, DEPTH, RATE, STRETCH |
| SELECT | **SEED**: jede Raste ein neuer Wurf |
| ALGORITHM | **SCAN**: grob, damit man live schnell hinkommt |
| PRESET | der Algorithmus 1 bis 18 (BANK 1: 1–9, BANK 2: 10–18). Jeder behält seine eigenen Reglerstellungen |
| PLAY | **HOLD** an/aus: der VCA bleibt offen, ein Drone. Die Tasten ändern dann nur noch BASIS |
| REC | **WÜRFELN**: neuer SEED und neuer SCAN. 1 s halten: dazu MORPH, FUSE, FORM und TIME |
| ARP | **FREEZE** an/aus: Prozessor und Hall halten, was sie gerade haben, endlos. Neue Noten klingen trocken darüber |
| SEQ 1 s halten | der Algorithmus zurück auf seine Werkseinstellung |
| SAVE | speichern. Sonst speichert FIKH-S von selbst, sobald es still ist (ein Schreibvorgang unterbricht den Klang kurz) |
| SAVE halten + Taste | **Snapshot ablegen**: der ganze Klang (Algorithmus, alle Regler, Hall, Oktave) auf einer der ersten acht weißen Tasten |
| SEL halten + Taste | **Snapshot holen** |
| OCT- / OCT+ | die Oktave der Tasten (-2 bis +2) |
| MIDI | Noten auf allen Kanälen, Pitch Bend ±2 Halbtöne, CC 1 = MORPH |

Die bipolaren Regler (WARP, SPAN, FUSE, FEED, FIELD, CELL) sind in der Mitte neutral. Dort rasten sie ein: Zwei
Rasten bleibt der Wert stehen, erst die dritte geht weiter.

## Die Regler

| Regler | Was er tut |
| --- | --- |
| **WARP** | die Kreuzmodulation: links eine Art, rechts eine andere (je Algorithmus), in der Mitte keine |
| **SPAN** | Verstimmung und Spreizung der Oszillatoren; rechts kommt Stereo dazu |
| **MORPH** | fährt durch die Klangfarbe oder die Wellenform des Generators |
| **FUSE** | je Algorithmus, meist links Chorus oder Diffusion, rechts Sättigung oder Resonanz |
| **SEED** | würfelt Verstimmungen, Wellen-Versätze und die Abgriffe des Delays. Jede Stufe ist ein anderer Wurf, derselbe SEED klingt immer gleich |
| **SCAN** | fährt durch zufällige Klangfarben (je Algorithmus) |
| **MIX** | trocken und nass, in der Mitte beides gleich |
| **FEED** | die Rückkopplung des Delays. Links geht das verschobene Signal direkt zurück (Spiralen, Barberpole), rechts wird es mit dem rohen Delay gemischt (wandernde Kerben). Ganz außen hört es nicht mehr auf |
| **FORM** | von klar getrennten Echos zu einem diffusen Hall |
| **TIME** | die Länge des Delays, 5 ms bis 0,74 s. Beim Drehen gleitet sie, das Delay zieht die Tonhöhe mit |
| **FIELD** | das Filter: links Tiefpass, rechts Hochpass, die Mitte offen |
| **CELL** | die Frequenzverschiebung in der Rückkopplung. Um die Mitte ganz fein (langsames Schweben), außen bis ±600 Hz. Bei GRAIN CLOUD: die Tonhöhe ±12 Halbtöne |
| **ATTACK / RELEASE** | der VCA: wie er aufgeht und wie er schließt (1 ms bis 8 s / 5 ms bis 14 s) |
| **GLIDE** | wie BASIS von Note zu Note gleitet |
| **DRIFT** | jeder Oszillator wandert für sich (bis ±30 Cent), MORPH und SCAN atmen langsam mit |
| **WIDTH** | die Stereobreite |
| **LEVEL** | die Lautstärke des Algorithmus |

## Snapshots

Acht Plätze für ganze Klänge, auf den ersten acht weißen Tasten (F3, G3, A3, H3, C4, D4, E4, F4).

- **Ablegen**: SAVE halten und eine dieser Tasten drücken. Alles wird dort gespeichert: Algorithmus, alle seine
  Regler, der Hall, die Oktave.
- **Holen**: SEL halten und die Taste drücken. Ist es derselbe Algorithmus, gleiten die Regler hinüber; sonst wird
  kurz umgeblendet.
- Solange SEL oder SAVE gehalten ist, spielen die Tasten nicht. Die belegten Plätze leuchten, der zuletzt geholte
  blinkt, und die Anzeige zeigt die acht Plätze (gold belegt, rot der letzte).
- SAVE allein (ohne Taste) speichert wie bisher.

## Der Hall (VERB)

Ein Ambient-Hall nach dem Vorbild des Walrus Audio SLÖER, ganz am Ende der Kette. Er gilt für alle Algorithmen
gleich, wie ein Pedal hinter dem Synth: Beim Wechsel des Algorithmus bleibt er, wie er ist. Innen: acht
Verzögerungsleitungen, die sich gegenseitig rückkoppeln und langsam in ihrer Länge schweben; davor wird das Signal
zerstreut.

| Regler | Was er tut |
| --- | --- |
| **MIX** | wie viel Hall (ganz links aus) |
| **DECAY** | wie lange er klingt, 0,8 s bis 30 s; ganz rechts **INF**: er hört nicht auf |
| **X** | je nach MODE (siehe unten) |
| **FILTER** | der Klang des Halls, von dunkel bis hell |
| **MODE** | DREAM, DARK, RISE, LIGHT, RAIN (eine Raste, ein Modus) |
| **DEPTH** | wie stark die Leitungen schweben (viel: der Hall wird unscharf, verstimmt) |
| **RATE** | wie schnell sie schweben |
| **STRETCH** | wie eine niedrigere Abtastrate: längere, dunklere Fahnen mit Artefakten |

| MODE | Was er ist | X |
| --- | --- | --- |
| **DREAM** | üppig, weich | ein Vibrato über der Fläche. Mit FREEZE (ARP) wird sie zum Pad |
| **DARK** | tief | wie viel von einer Oktave tiefer in den Hall läuft |
| **RISE** | schwillt nach jeder Note langsam an | wie lange er zum Anschwellen braucht (0,15 s bis 6 s) |
| **LIGHT** | hell | Shimmer: eine Oktave höher, die sich in der Fahne immer weiter nach oben schraubt |
| **RAIN** | wenig zerstreut: die Abgriffe sind als Echos zu hören | links Echos, rechts weich |

## Die Algorithmen (BANK 1)

1. **METAL PAIR** (DUNGEON): zwei analoge Oszillatoren. WARP links exponentielle FM, rechts Ringmodulation. SPAN der
   Abstand des Modulators (±2 Oktaven), MORPH die Welle des Trägers, SCAN die des Modulators (Puls, Säge, Sinus).
   FUSE links Chorus, rechts Filterresonanz.
2. **SHAPER PAIR** (REFLEKTA): ein phasenmodulierter Sinus durch einen Waveshaper. WARP links der Modulationsindex
   mit Rückkopplung des Modulators, rechts die des Trägers. SPAN die Trägerfrequenz. MORPH wie stark der Shaper
   arbeitet, SCAN seine Familie (Falter, Sättigung, Tschebyschow). FUSE links Modulation des Delays mit Audiorate,
   rechts Sättigung.
3. **SINE CLUSTER** (DUNGEON): sechs Sinus in einem Cluster. SEED würfelt die Abstände, SPAN spreizt sie. MORPH
   scannt über den Cluster. WARP links Phasen-Rückkopplung, rechts Kreuz-FM. SCAN Ringmodulation der Nachbarn.
   FUSE links Chorus, rechts Rauschmodulation.
4. **WAVE QUAD** (DUNGEON): vier Wavetable-Oszillatoren. MORPH fährt durch die Tabelle, SCAN versetzt jeden
   zufällig. WARP links Bitcrush, rechts Kreuz-FM. FUSE links ein Bandpass mit einem Sinus darunter, rechts Chorus mit
   Resonanz.
5. **DIATONIC** (REFLEKTA): vier Stimmen, in Moll quantisiert. SPAN spreizt sie in Tonleiterstufen (die Töne
   gleiten), MORPH scannt über die Stimmen, SCAN würfelt ihre Wellen. FUSE links Chorus, rechts Sättigung.
6. **CHORD SCAN** (DUNGEON): vier Stimmen spielen einen Akkord. MORPH scannt durch zehn Akkorde, die Töne gleiten.
   SPAN die Lage (rechts nach oben gespreizt und Stereo, links nach unten). WARP die Wellenform, SCAN ein Phaser.
7. **PARTIALS** (DUNGEON): acht Teiltöne. SPAN links unharmonisch gestreckt (Glocken, Metall), rechts von allen
   Obertönen zu nur ungeraden (hohl, Klarinette). MORPH scannt über die Teiltöne, SCAN würfelt ihre Lautstärken.
8. **COMB SWARM** (REFLEKTA): vier Kammfilter um BASIS, angeregt von Säge und Rauschen. SPAN spreizt ihre Stimmung,
   MORPH scannt über sie und hellt die Anregung auf, SCAN verstimmt sie zufällig. WARP links verrauschte, rechts
   gesättigte Rückkopplung.
9. **GRAIN CLOUD** (REFLEKTA): ein freier Cluster aus vier Stimmen. Der Prozessor verschiebt die Tonhöhe in Körnern:
   CELL ±12 Halbtöne, FORM die Korngröße. Mit FEED links liegt der Verschieber in der Schleife (das Signal steigt
   oder fällt mit jedem Durchlauf), rechts dahinter.

## Die Algorithmen (BANK 2)

10. **ZERO CARRIER** (REFLEKTA): ein Träger bei 0 Hz. Er steht still, nur zwei Modulatoren bewegen seine Phase durch
    eine Wavetable. SPAN die Frequenz des zweiten (±1 Oktave), WARP links der erste tiefer mit Rückkopplung, rechts
    der zweite, MORPH die Welle, SCAN wo die Phase ruht.
11. **DISPERSOR** (DISPERSOR): vier Sinus, die aneinander ziehen. Im Delay sitzt eine Kette aus zwölf Allpässen: Die
    Echos zirpen wie Laser. FORM wie stark, SEED verteilt sie.
12. **NOTCH STACK** (DUNGEON): zwei übersteuerte Sinus durch vier Kerbfilter, die MORPH durch das Spektrum schiebt.
    SCAN ein Gleichrichter und Falter, FUSE links verrauscht, rechts hart übersteuert.
13. **SINE SWARM** (DUNGEON): acht Sinus, frei um BASIS verstreut. MORPH scannt, SCAN würfelt die Lautstärken.
14. **RADIO** (REFLEKTA): ein Kurzwellenempfänger. MORPH ist die Skala mit sechs Sendern (Pieptöne, Zirpen, Stimmen
    aus Formanten, schwebende Akkorde), dazwischen Rauschen und Knacken. WARP rechts pfeift die Überlagerung knapp
    neben dem Sender, links schwindet der Empfang. SCAN wie stark es rauscht, SEED andere Sender.
15. **WEATHER** (REFLEKTA): MORPH von Wind (ein Band, das in Böen wandert) über Regen (Tropfen in der Tonleiter) zu
    Insekten. WARP rechts tonaler, links turbulenter, SCAN wie stürmisch.
16. **ARCHITECT** (DUNGEON): ein Körper aus sechs Resonanzen, den MORPH umbaut: Saite, Stab, Platte, Glocke, Raum.
    Jede Note schlägt ihn an, SCAN wie oft er von selbst angeschlagen wird. WARP links gestrichen, rechts härter
    angeschlagen. SPAN links gedämpft wie Holz, rechts lang klingend und breit.
17. **FOLD BASS** (DUNGEON): zwei Sägezähne und ein Sub durch einen Wavefolder, der Bass der Bank. MORPH wie stark
    gefaltet wird, SCAN die Symmetrie, SPAN die Verstimmung des zweiten Sägezahns.
18. **SUPERSAW** (REFLEKTA): sieben verstimmte Sägezähne. SPAN die Verstimmung, MORPH vom Sägezahn zum schwebenden
    Puls, SCAN die Mitte gegen die Seiten, WARP links ein Falter, rechts ein Sub.

## Tipps

- **Ein Drone**: PLAY (HOLD), eine Taste, dann an MORPH, SCAN und FEED drehen. DRIFT um die Hälfte lässt den Cluster
  atmen.
- **Spiralen**: FEED nach links, CELL ein kleines Stück aus der Mitte.
- **Würfeln, bis es passt**: REC tippen. Gefällt ein Wurf, merkt man sich die SEED-Nummer.
- **Einfrieren und darüber spielen**: ARP (FREEZE), dann HOLD aus und mit kurzem RELEASE darüber spielen.
- **Eine Kathedrale**: VERB auf DARK, X halb, DECAY weit rechts, STRETCH etwas auf.
- **Aufsteigender Glanz**: VERB auf LIGHT, X weit auf, DECAY auf INF.

## Die Last

FIKH-S zeigt **ECO**, wenn die CPU länger als eine Sekunde über 88 % liegt. Dann rechnen SINE CLUSTER und PARTIALS mit
weniger Oszillatoren.
