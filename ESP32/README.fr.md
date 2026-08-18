# Firmware ESP32

[English version](README.md)

Ce répertoire est le projet PlatformIO/ESP-IDF pour la cible GUITION
JC3248W535. Commence par le [README français principal](../README.fr.md) pour
préparer la microSD, flasher le firmware, connaître les commandes et consulter
les informations légales.

## Commandes

Depuis ce répertoire :

```sh
pio run
pio run -t upload
pio device monitor --baud 115200
```

Ou depuis la racine du dépôt :

```sh
pio run -d ESP32
pio run -d ESP32 -t upload
```

Le seul environnement actuel est `jc3248w535`. Il fige la version de la
plateforme Espressif validée et sélectionne la partition applicative
personnalisée de 4 Mio.

## Frontières du code

- `src/i_*.c` : implémentations des interfaces historiques de plateforme de
  DOOM.
- `src/platform/` : services de la carte, définition matérielle, décodage du
  tactile et panneau de commandes personnalisé.
- `components/doom/` : compile `../linuxdoom-1.10` en excluant les backends
  PC `i_*` originaux.
- `components/doom_music/` : lecteur MUS et émulateurs OPL embarqués.

Consulte [Architecture](../docs/ARCHITECTURE.fr.md) pour le flux complet des
données et [Matériel](../docs/HARDWARE.fr.md) pour le brochage testé. Les
limites actuelles et la trajectoire prévue vers plusieurs backends de cartes
sont décrites dans le guide de [Portage](../docs/PORTING.fr.md).
