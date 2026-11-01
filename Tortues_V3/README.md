# Tortues V3 – suivi d'hibernation

Même matériel que la V2. Changements : fiabilité (pannes détectées, chien de garde), nouveaux seuils
à paliers, LED à 5 états, bandeau coloré, enregistrement toutes les 15 min, un fichier par saison,
affichage plus grand (10 lignes d'historique).

## Fichiers du dossier

| Fichier | Rôle |
|---|---|
| `Tortues_V3.ino` | le programme |
| `Mono16x32.c`, `Mono32x64.c` | polices d'écran (générées depuis Menlo, format UTFT) |
| `README.md` | ce document |
| `CHANGELOG.md` | historique des versions, à compléter à chaque changement de `VERSION` |

Ouvrir `Tortues_V3.ino` dans l'IDE Arduino : les deux fichiers de polices sont pris automatiquement.

## Réglages (en tête du programme)

| Constante | Valeur | Rôle |
|---|---|---|
| `TEMP_OFFSET` | 2.3 | compensation de température du capteur (voir calibration) |
| `TOUCH_RAW_*`, `TOUCH_SWAP_XY` | | calibration de la dalle tactile (voir « Boutons tactiles ») |
| `MEASURE_EVERY_MIN` | 1 | mesure, écran, LED |
| `RECORD_EVERY_MIN` | 15 | écriture sur carte SD, alignée sur :00 :15 :30 :45 |
| `CONFIRM_COUNT` | 3 | mesures consécutives avant changement de niveau (3 min) |
| `T_*`, `H_*`, `C_*` | | seuils (voir tableau) |

## Niveaux d'alerte

| Niveau | LED | Bandeau | Température | Humidité | CO2 |
|---|---|---|---|---|---|
| OK | vert fixe | vert | 4,5 – 8 | 65 – 92 | < 1000 |
| Surveillance | vert clignotant lent | jaune | 4 – 4,5 ou 8 – 9 | 55 – 65 ou 92 – 95 | 1000 – 2000 |
| Alerte | rouge fixe | orange | 3 – 4 ou 9 – 10 | < 55 ou > 95 | 2000 – 3000 |
| Urgence | rouge clignotant rapide | rouge | < 3 ou > 10 | — | > 3000 |
| Panne | rouge / vert alternés | gris | capteur muet, carte SD en erreur, horloge perdue | | |

- Montée de niveau : 3 mesures consécutives (3 min).
- Descente : la valeur doit être revenue avec une marge (0,3 °C, 2 points d'humidité, 200 ppm).
- Dans l'historique, chaque valeur hors plage OK a un fond de la couleur de son niveau.
- Badge « 24h » (colonne de droite) : pire niveau des dernières 24 h (fenêtre glissante, s'efface de lui-même).
- Ligne du bas : état (OK + nombre d'enregistrements + nom du fichier, ou le message de panne) et, à droite,
  la version du programme (constante `VERSION` en tête du programme, à incrémenter à chaque modification,
  avec une entrée dans `CHANGELOG.md`).

## Écran

- Bandeau : date/heure de la dernière mesure, valeurs en gros, fond = niveau global.
- 10 lignes d'historique, une par enregistrement (15 min) → 2 h 30 visibles.
- **UP** = vers le plus récent, **DOWN** = vers le plus ancien, **par page de 10 lignes** (2 h 30). La page
  la plus ancienne reste pleine. Maintenir le doigt = défilement continu. Compteur « page / total » sous la
  flèche DOWN (page 1 = la plus récente).
  L'historique garde sa position quand une nouvelle mesure arrive, sauf s'il est déjà sur la ligne la
  plus récente.
- **NOW** (rond) = retour à la page 1, la plus récente.
- Boutons en blanc = actifs, en gris = inactifs (moins de deux enregistrements) ; inversés pendant l'appui,
  brièvement gris en butée. L'horloge est neutre (blanc sur noir). Seuls le bandeau, les valeurs de
  l'historique et le badge 24h portent une couleur, toujours celle d'un niveau d'alerte.

## Fichier de données

Un fichier par saison : `TORT2627.BIN` pour l'hiver 2026‑2027 (saison = année si mois ≥ juillet,
sinon année − 1). Si l'horloge est perdue : `TORTLOST.BIN`.

À côté du binaire, un fichier texte `TORT2627.CSV` reçoit la même ligne à chaque enregistrement
(colonnes `date;heure;temp;hum;co2;flag`, séparateur `;`, décimale `.`, en-tête écrit à la création).
Le binaire reste la référence ; le CSV est une copie lisible directement (Excel, Numbers, pandas).

Format d'un enregistrement binaire : **20 octets, identique à la V2**, valeurs brutes (pas de moyenne).

| Champ | Type | Octets |
|---|---|---|
| jour, mois, année (complète, ex. 2026), heure, minute | int16 ×5 | 10 |
| température (°C, arrondie à 0,1, peut être négative) | float32 | 4 |
| humidité (%, arrondie à l'entier) | int16 | 2 |
| CO2 (ppm) | int16 | 2 |
| flag : bit0 = température hors plage OK, bit1 = humidité, bit2 = CO2 | int16 | 2 |

Différences avec la V2 côté données : un enregistrement toutes les 15 min au lieu de 3 ; « hors plage »
suit les nouveaux seuils (4,5–8 / 65–92 / 1000) ; **aucun enregistrement n'est écrit quand le capteur ne
répond pas** (un trou dans les heures = panne capteur) ; une fin de fichier incomplète (coupure de courant
pendant l'écriture) est tronquée au démarrage.

## Journal des événements

`TORT2627.LOG` (texte, à côté du binaire et du CSV) reçoit une ligne par événement :
`date;heure;type;detail`. Types :

| Type | Détail |
|---|---|
| DEMARRAGE | version et cause (mise sous tension, chien de garde, baisse de tension, reset/USB) |
| CAPTEUR | pas de donnée / retour OK après N min / auto-test en échec |
| SD | ouverture, écriture ou lecture impossible / retour OK (panne depuis HH:MM) / fichier tronqué |
| HORLOGE | heure perdue |
| NIVEAU | changement de niveau d'alerte, ex. `OK -> URGENCE;temp=24.4;hum=42;co2=655` |
| REDEMARRAGE | redémarrage forcé après 10 min de capteur muet |

Le début d'une panne SD n'est souvent pas inscriptible (carte absente) ; il est écrit au retour, avec
l'heure de début. Chaque ligne est aussi envoyée sur le port série, préfixée `LOG`.

## Port série (115200)

Une ligne CSV par mesure : `date_heure;temp;hum;co2;flag;niveau;enregistre` (niveau 0–4, enregistre 0/1).

## Calibration de l'offset de température (point 10)

Le capteur chauffe légèrement en mesure continue ; la constante `TEMP_OFFSET` (2,3 depuis la V3.6, calibrée le 2026-11-01 : écran 8,5–8,9 °C, référence 10,3–10,5 °C posée à côté de la sonde) retranche
cette dérive. À 5 °C, 1 °C d'erreur décale aussi l'humidité relative d'environ 7 points, donc l'offset
conditionne les deux.

**Quand** : après le téléversement de la V3, l'appareil en place dans la caisse à température
d'hibernation (froide et stable), allumé depuis au moins 1 h.

**Comment** :

1. Poser un thermomètre/hygromètre de référence juste à côté du capteur, fermer la caisse, attendre 1 h.
2. Lire la température sur l'écran (bandeau) et sur la référence.
3. Nouvel offset = offset actuel + (température écran − température référence).
   Exemple : écran 6,2 °C, référence 5,0 °C → `TEMP_OFFSET = 4.0 + 1.2 = 5.2`.
4. Modifier la constante, téléverser, attendre 30 min, vérifier que l'écran et la référence concordent.
5. Comparer l'humidité à la référence : après correction de la température elle doit être proche
   (le capteur est donné à ± 6 points). S'il reste un écart constant, c'est la limite du capteur, pas un
   réglage.

## Boutons tactiles (calibration)

Le fichier `URTouchCD.h` de la bibliothèque URTouch installée est celui d'origine, calibré pour un écran
320 × 240 : avec lui, la position x ne dépasse jamais 319 et la colonne de boutons (x ≥ 664) est
inatteignable. Depuis la V3.6 le programme lit la dalle en valeurs brutes et fait lui-même la conversion
en pixels 800 × 480 avec les constantes `TOUCH_RAW_*` ; le fichier de la bibliothèque n'a plus d'effet.
Les valeurs en place ont été mesurées le 2026-11-01 sur cette dalle (canal B = horizontale décroissante,
canal A = verticale croissante) ; la procédure ci-dessous sert si l'écran ou la dalle change.

**Comment** (moniteur série de l'IDE, 115200) :

1. Dans `touchRead()`, ajouter temporairement une ligne qui écrit `ra`, `rb`, `x`, `y` sur le port série
   (la trace `# TOUCH` de la V3.6–V3.8 a été retirée en V3.9). Appuyer au milieu de chaque bord de l'écran :
   gauche, droite, haut, bas.
2. Si un glissement du doigt de haut en bas fait surtout varier **A** (et non B), mettre
   `TOUCH_SWAP_XY = true` et recommencer.
3. Reporter : A du bord gauche dans `TOUCH_RAW_LEFT`, A du bord droit dans `TOUCH_RAW_RIGHT`,
   B du bord haut dans `TOUCH_RAW_TOP`, B du bord bas dans `TOUCH_RAW_BOTTOM`. Si une valeur de bord est
   plus grande que son opposée (axe inversé), c'est normal : la conversion en tient compte.
4. Téléverser, vérifier que UP, DOWN et NOW réagissent (le bouton s'inverse pendant l'appui), puis
   retirer la ligne de trace.

Les zones de détection couvrent toute la colonne de droite (x ≥ 664), coupée en trois bandes à mi-distance
entre les boutons (UP ≤ 175, DOWN 176–312, NOW ≥ 313), pour tolérer une calibration verticale approximative.

## Mise à l'heure

Dans `setup()`, décommenter la ligne `rtc.adjust(...)` avec la date/heure voulue, téléverser, puis
recommenter et téléverser à nouveau (sinon l'heure est remise à cette valeur à chaque démarrage).

## Panne capteur persistante

Après `RESTART_AFTER_FAULTS` mesures ratées d'affilée (10 min), la carte redémarre complètement :
bus I2C, capteur et écran sont réinitialisés. Si le capteur est vraiment mort, le cycle se répète toutes
les 10 min, l'écran continue d'afficher la panne. Ne pas brancher ou débrancher le capteur sous tension :
l'appel de courant peut réinitialiser l'écran sans que le programme le sache (écran noir, LED active) ;
dans ce cas, couper et remettre l'alimentation.

## Chien de garde

Si le programme se bloque plus de 8 s, la carte redémarre seule. Les anciens chargeurs d'amorçage de
Mega 2560 (avant 2012) bouclent indéfiniment après un tel redémarrage : à tester une fois en
débranchant le capteur en cours de fonctionnement. Si la carte ne revient pas, mettre `wdt_enable` en
commentaire.
