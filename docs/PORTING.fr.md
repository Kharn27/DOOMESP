# Porter DOOMESP sur une autre carte ESP32-S3

[English version](PORTING.md)

DOOMESP ne possède actuellement qu'une seule cible prise en charge et validée
sur le matériel : la GUITION JC3248W535. La prise en charge de plusieurs
cartes ESP32-S3 est une direction du projet ; `platform_board.h` ne constitue
pas encore à lui seul une abstraction multi-cartes.

L'objectif est de garder LinuxDOOM et les backends `I_*` vus par DOOM
indépendants de l'écran, du contrôleur tactile, du stockage et du matériel
audio choisis. Un nouveau port doit étendre la couche plateforme ESP32 plutôt
que multiplier les conditions propres aux cartes dans le moteur du jeu.

## Ce qui est configurable aujourd'hui

`ESP32/src/platform/platform_board.h` centralise les GPIO, les contrôleurs de
bus et les fréquences testés. Le modifier peut suffire uniquement si la
nouvelle carte conserve la même topologie matérielle :

- écran AXS15231B QSPI 320 x 480 ;
- tactile AXS15231B à deux points par I2C ;
- microSD par SDSPI ;
- sortie audio I2S standard ;
- configuration compatible de la flash et de la PSRAM octale.

Même dans ce cas favorable, il faut valider physiquement la séquence
d'initialisation du panneau, son orientation, les coordonnées tactiles et la
stabilité des bus. Il ne faut pas remplacer les valeurs de la JC3248W535 pour
ajouter une cible : la nouvelle carte doit recevoir son propre environnement
PlatformIO puis, lorsque la sélection des cartes existera, son propre profil.

## Couplages matériels actuels

| Sujet | Implémentation actuelle | Besoin possible d'une autre cible |
| --- | --- | --- |
| Broches et fréquences | `platform_board.h` | Un profil de carte distinct |
| Flash et PSRAM | `platformio.ini`, `sdkconfig.defaults` | Une définition de carte, un mode mémoire et une table de partitions |
| Contrôleur LCD | `platform_lcd.c`, composant AXS15231B d'Espressif | Un autre backend d'affichage et une autre dépendance |
| Timing du panneau | Table de commandes JC3248W535 dans `platform_lcd.c` | Une séquence validée pour le panneau exact |
| Résolution et interface | `platform_controls.h`, actuellement figé à 320 x 480 | Une autre géométrie, voire une mise à l'échelle ou un centrage |
| Transport tactile | Décodeur de paquets AXS15231B dans `platform_input.c` | Un autre backend tactile et une transformation des coordonnées |
| Stockage | Implémentation SDSPI dans `platform_fs.c` | SDMMC, flash SPI ou un autre backend de fichiers |
| Audio | Chemin I2S standard dans `i_sound.c` | De simples broches différentes ou un backend DAC/PDM/audio |

Le moteur original produit toujours une image de jeu en 320 x 200. Sa barre
d'état détachée mesure 320 x 32. Un écran dont la largeur diffère de 320
pixels demande donc une politique explicite de mise à l'échelle, de centrage
ou de recadrage dans la chaîne d'affichage ESP32 ; il ne demande pas de
réécrire le moteur de rendu dans `linuxdoom-1.10`.

## Frontière d'abstraction visée

Les fichiers actuels ont volontairement permis de valider un appareil complet
avant d'introduire plusieurs interfaces simultanément. Une future organisation
multi-cartes devrait séparer les pilotes physiques de l'interface tactile et
du comportement vu par le jeu, approximativement ainsi :

```text
ESP32/src/platform/
├── boards/
│   ├── jc3248w535.h
│   └── autre_carte.h
├── display/
│   ├── platform_display.h
│   └── display_axs15231b.c
├── touch/
│   ├── platform_touch.h
│   └── touch_axs15231b.c
├── platform_ui.c
├── platform_fs.c
└── platform_audio.c
```

Cette arborescence indique une direction de conception : ce n'est ni une API
figée ni la description de fichiers déjà présents. Les contrats utiles sont
plus importants que les noms exacts :

- un backend d'affichage initialise le contrôleur et présente des zones
  RGB565 ;
- un backend tactile remonte les identifiants et coordonnées des contacts dans
  une orientation canonique et documentée ;
- la couche d'interface dessine les commandes et transforme les contacts en
  actions logiques DOOM, sans décoder de paquets propres à un contrôleur ;
- le stockage expose des fichiers à DOOM sans que le moteur sache s'ils
  proviennent du SDSPI, du SDMMC ou d'un autre support pris en charge ;
- l'audio consomme le flux PCM final sans exposer au mixeur le choix de
  l'amplificateur ou du bus ;
- la carte est sélectionnée à la compilation : une cible embarquée n'a pas
  besoin de détection dynamique ni d'embarquer des pilotes inutilisés.

## Ordre de migration recommandé

L'abstraction doit être introduite progressivement pendant le portage d'une
vraie deuxième carte :

1. Relever précisément le module, la flash, la PSRAM, l'écran, le tactile, la
   SD, l'audio, l'alimentation et le brochage de la nouvelle cible.
2. Ajouter un environnement PlatformIO nommé et conserver `jc3248w535` comme
   cible testée par défaut.
3. Introduire la sélection de carte à la compilation et déplacer les valeurs
   existantes dans un profil JC3248W535 sans en modifier le comportement.
4. Extraire le transport LCD AXS15231B du dessin du panneau de commandes, puis
   ajouter le nouveau backend d'affichage ou profil de panneau.
5. Extraire l'acquisition tactile brute du mapping des commandes, puis
   normaliser les coordonnées et le cycle de vie des contacts du nouveau
   contrôleur.
6. Adapter l'interface seulement lorsque la résolution et l'orientation
   physiques sont stables.
7. Ajouter des variantes de stockage ou d'audio uniquement si la nouvelle
   carte en a réellement besoin.
8. Compiler chaque environnement PlatformIO pris en charge dans la CI et
   retester physiquement les fonctions touchées par la modification.

Cet ordre conserve l'appareil fonctionnel comme cible de non-régression et
évite de concevoir une abstraction spéculative autour d'une seule
implémentation.

## Checklist de validation d'un port

Une carte ne doit pas être annoncée comme prise en charge avant d'avoir testé
sur le matériel :

- le démarrage à froid et des redémarrages répétés ;
- la détection de la flash et de la PSRAM, la marge mémoire et une session de
  jeu prolongée ;
- des aplats de couleur plein écran puis des mises à jour LCD animées et
  prolongées ;
- l'orientation, l'ordre des octets, le timing de balayage et la polarité du
  rétroéclairage ;
- les quatre coins tactiles bruts, la stabilité des identifiants de contact,
  les relâchements et le déplacement avec tir simultané ;
- le montage FAT et des lectures WAD prolongées lors des changements de niveau
  et de démo ;
- les effets sonores et la musique simultanés sans rupture ;
- les menus, la mise à jour de la barre d'état, le choix des armes et des
  cheats ;
- un build release et un journal série complet depuis le reset.

Un port disposant de moins de PSRAM reste peut-être possible, mais
l'implémentation actuelle place la zone DOOM, les framebuffers, les ressources
de l'interface et les caches audio en RAM externe. Une telle cible demandera
des mesures et un travail mémoire ; changer son brochage ne suffira pas.

## Candidate future : GUITION JC4880P443

> **TODO — portage non commencé.** La JC3248W535 reste la seule cible prise en
> charge et validée physiquement. Il n'existe encore aucun fichier source P4,
> environnement de build ou promesse de compatibilité.

Une GUITION **JC4880P443** est disponible comme possible seconde cible
matérielle lorsque le port ESP32-S3 actuel sera terminé et stable. L'exemplaire
disponible est annoncé avec :

- un processeur applicatif ESP32-P4 et un coprocesseur radio ESP32-C6 ;
- un écran MIPI de 4,3 pouces ;
- 16 Mio de flash et 32 Mio de PSRAM ;
- un écran tactile capacitif, une microSD et des connexions pour haut-parleur
  et microphone ;
- des ports USB 2.0 High-Speed et Full-Speed ;
- une caméra intégrée de 2 mégapixels et une liaison MIPI-CSI.

Cette carte est volontairement enregistrée comme candidate future plutôt que
comme jalon actif. Lorsque son portage commencera, son premier périmètre devra
se limiter au P4, à l'affichage, au tactile, au stockage et à l'audio
nécessaires à DOOM. La liaison radio avec le C6, la caméra, les fonctions
H.264/JPEG, la batterie et les autres extensions de la carte devront rester
hors périmètre jusqu'à la validation du port de base.

L'écart architectural est utile : la cible RISC-V, l'affichage MIPI, la
topologie mémoire plus importante et le processeur radio séparé permettront de
vérifier que les frontières matérielles décrites plus haut sont réelles. Cette
refactorisation devra néanmoins être guidée par le futur travail de bring-up et
préserver la JC3248W535 comme cible de non-régression par défaut.
