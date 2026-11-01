# Historique des versions – Tortues

Une entrée par version. La constante `VERSION` du programme et ce fichier évoluent ensemble :
à chaque modification, incrémenter l'une et compléter l'autre.

## V3.9 – 2026-11-01

- Compteur sous la flèche DOWN : « page / total » (page 1 = la plus récente) au lieu de « ligne / total ».
- Trace série `# TOUCH` retirée (calibration confirmée).

## V3.8 – 2026-11-01

- UP et DOWN défilent par page (10 enregistrements = 2 h 30) au lieu d'un enregistrement ; la page la plus
  ancienne reste pleine. Répétition en maintien : une page toutes les 400 ms.

## V3.7 – 2026-11-01 (téléversée, boutons confirmés fonctionnels)

- Calibration tactile mesurée sur la dalle (trace série de la V3.6) : axes croisés et inversés par
  rapport à l'estimation de la V3.6 (`TOUCH_SWAP_XY = true`, gauche 3490 → droite 327,
  haut 3930 → bas 610). Avec ces valeurs les trois boutons tombent dans leurs zones.
- Trace `# TOUCH` laissée active en attendant la confirmation sur l'appareil.

## V3.6 – 2026-11-01 (téléversée, boutons toujours inopérants : calibration estimée fausse)

- **Boutons tactiles réparés.** La bibliothèque URTouch réinstallée le 30 octobre contient la
  calibration d'origine (écran 320 × 240) : la position horizontale ne dépassait jamais 319 et la
  colonne de boutons (x ≥ 664) était inatteignable. Le programme lit désormais la dalle en valeurs
  brutes et convertit lui-même en pixels 800 × 480 (constantes `TOUCH_RAW_*`, `TOUCH_SWAP_XY`).
  Valeurs de départ estimées : calibration à faire avec la trace série `# TOUCH` (voir README).
- Zones de détection élargies : toute la colonne de droite, trois bandes (UP 78–175, DOWN 176–290,
  Refresh 350–460).
- `TEMP_OFFSET` 4,0 → 2,3, d'après deux relevés au thermomètre de référence posé à côté de la sonde
  (10,5 vs 8,9 ; 10,3 vs 8,5 moyen). L'ancien réglage affichait 1,7 °C trop froid et une humidité
  gonflée d'environ 12 points.

## V3.5 – 2026-11-01 (jamais téléversée)

- `TEMP_OFFSET` 4,0 → 2,4 (premier relevé 10,5 vs 8,9). Remplacée par la V3.6 le même jour.

## V3.0 à V3.4 – 2026-10-30 / 2026-10-31

Réécriture complète, en plusieurs itérations sur la journée du 30 et la matinée du 31 ; le détail de
chaque itération n'a pas été conservé (pas de gestion de versions avant le 1er novembre).
V3.3 : première version validée sur le matériel (écran, détection des pannes capteur, chien de garde).
V3.4 : en service depuis le 31 octobre 09:35 (première ligne du journal `TORT2627.LOG`).

Ce que la V3 change par rapport à la V2 :

**Mesure et enregistrement**
- Mesure toutes les minutes (écran, LED, port série) ; enregistrement toutes les 15 min aligné sur
  :00 :15 :30 :45 (V2 : toutes les 3 min). Valeurs brutes, pas de moyenne.
- Un fichier par saison `TORTaabb.BIN` (V2 : un seul `TORTUES.BIN` cumulant les années), année sur
  quatre chiffres (V2 : deux). Format d'enregistrement 20 octets inchangé.
- Copie texte `TORTaabb.CSV` écrite à chaque enregistrement, lisible directement.
- Journal `TORTaabb.LOG` : démarrages et leur cause, pannes, changements de niveau.
- Aucun enregistrement quand le capteur ne répond pas (un trou = panne) ; fin de fichier incomplète
  tronquée au démarrage.
- Signification du flag : « hors plage OK » selon les nouveaux seuils.

**Seuils et alertes**
- Quatre niveaux + Panne (OK / Surveillance / Alerte / Urgence / Panne) au lieu d'un simple
  « dans / hors tolérance ».
- Plages OK : température 4,5–8 (V2 : 5–8), humidité 65–92 (V2 : 60–70), CO2 < 1000 (V2 : < 800).
- Changement de niveau après 3 mesures consécutives ; retour au niveau inférieur avec marge
  (0,3 °C, 2 points, 200 ppm) pour éviter les oscillations.
- LED à 5 états (vert fixe, vert clignotant, rouge fixe, rouge clignotant, alterné) au lieu de
  vert / rouge.

**Écran**
- Bandeau du haut coloré selon le niveau global ; badge « 24h » (pire niveau des dernières 24 h,
  remis à zéro par Refresh) ; version du programme en bas à droite.
- 10 lignes d'historique en grande police (polices Menlo générées) ; valeurs hors plage sur fond de
  la couleur de leur niveau ; tout le reste en blanc/gris.
- Boutons : blancs = actifs, gris = inactifs, inversés pendant l'appui, gris bref en butée.
  Maintien du doigt = défilement continu. L'historique garde sa position pendant les mesures.
  Compteur « ligne / total ». (V2 : boutons verts, défilement uniquement au-delà de 21
  enregistrements, un appui par ligne.)

**Fiabilité**
- Chien de garde 8 s : redémarrage automatique en cas de blocage.
- Détection des pannes : capteur muet (redémarrage complet après 10 min), carte SD absente ou en
  erreur (reprise automatique), horloge perdue (`TORTLOST.BIN`), auto-test du capteur au démarrage.
- Délai d'attente sur le bus I2C pour éviter un blocage définitif.
- Boucle principale non bloquante (V2 : attente active d'une minute entre deux mesures).

**Inchangé**
- Matériel et câblage, y compris la broche 4 partagée entre la carte SD et la dalle tactile.
- Compensation de température du capteur à 4,0 (jusqu'à la V3.5).
- Pas de buzzer (la V2 n'en avait pas non plus : « Long_Buzz » était un simple clignotement rouge).

## V2 – 2024-11-25 (en-tête « V.1 »)

Programme d'origine, en service pendant les hivers 2024-2025 et 2025-2026. Données dans `TORTUES 2.BIN`.
