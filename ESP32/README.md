# DOOM sur JC3248W535

Port expérimental de LinuxDoom 1.10 pour la carte tout-en-un GUITION
JC3248W535 : ESP32-S3-WROOM-1 N16R8, écran tactile capacitif AXS15231B QSPI
320 x 480, 16 Mio de flash et 8 Mio de PSRAM.

## Préparer la carte microSD

Formater une carte microSD en FAT32 et placer à sa racine un WAD légalement
obtenu. Le firmware reconnaît, dans cet ordre :

- `doom2f.wad`
- `doom2.wad`
- `plutonia.wad`
- `tnt.wad`
- `doomu.wad`
- `doom.wad`
- `doom1.wad`

Le fichier de configuration est créé sous `/default.cfg`. Le WAD n'est pas
copié en PSRAM : ses lumps sont lus à la demande depuis `/sdcard`, puis mis en
cache par le gestionnaire mémoire de DOOM.

## Compiler et flasher

Depuis ce dossier :

```sh
platformio run -e jc3248w535
platformio run -e jc3248w535 -t upload
platformio device monitor -b 115200
```

À chaque démarrage, le moniteur série affiche le contenu de la racine de la
microSD. Ouvrir le moniteur puis appuyer sur le bouton Reset de la carte pour
relancer cette liste. La carte n'est pas exposée comme stockage USB : l'ajout
de fichiers nécessite encore un lecteur microSD externe.

Une erreur `ESP_ERR_TIMEOUT` pendant `sdmmc_card_init` signifie que la carte ne
répond pas encore au bus (carte absente, mal insérée ou liaison incorrecte) :
elle survient avant la lecture du système de fichiers et du WAD. Une carte qui
répond mais qui n'est pas en FAT32 produit une erreur de montage différente.

Le manifeste `src/idf_component.yml` récupère le pilote Espressif officiel
`esp_lcd_axs15231b` lors de la première compilation.

## Commandes tactiles provisoires

L'écran est utilisé en paysage, avec l'image 320 x 200 centrée dans la dalle
480 x 320.

- Moitié gauche : pavé directionnel relatif au centre de la zone.
- Tiers supérieur droit : tirer ; ce bouton valide aussi les menus.
- Tiers central droit : utiliser une porte ou un interrupteur.
- Tiers inférieur droit : ouvrir ou fermer le menu.

Le contrôleur AXS15231B est actuellement exploité avec un seul point : il
n'est donc pas encore possible de se déplacer et tirer simultanément. Les
zones et l'orientation devront être validées sur la carte réelle.

## État du port

- Écran QSPI 40 MHz et double tampon DMA par bandes de 16 lignes.
- PSRAM octale 80 MHz ; zone mémoire DOOM de 6 Mio en PSRAM.
- Carte microSD sur un bus SPI séparé (`CS 10`, `MOSI 11`, `CLK 12`,
  `MISO 13`).
- Jeu solo uniquement.
- Son et musique non implémentés pour le moment.

Le dossier `components/doom` compile les sources originales situées dans
`../linuxdoom-1.10` en excluant les implémentations PC des fichiers `i_*`.
