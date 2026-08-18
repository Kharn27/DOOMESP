# Dépannage

[English version](TROUBLESHOOTING.md)

Ouvre le moniteur série à 115200 bauds puis redémarre la carte. Le journal
indique la détection de la flash et de la PSRAM, l'initialisation du LCD,
l'état de la SD, l'IWAD choisi, la mémoire, le tactile et l'audio.

```sh
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Adapte le port si nécessaire. Ferme le moniteur avant de flasher : un seul
processus peut posséder le port série.

## Port d'upload absent ou occupé

- Utilise un câble USB transmettant les données.
- Rebranche la carte et consulte le Gestionnaire de périphériques ou lance
  `ls /dev/ttyACM*`.
- Ferme tous les moniteurs série avant l'upload.
- Indique `--upload-port /dev/ttyACM0` ou le port `COM` Windows approprié.
- Sous Linux, installe les règles udev de PlatformIO ou vérifie que
  l'utilisateur a accès au périphérique.

## `sdmmc_card_init failed` / `ESP_ERR_TIMEOUT`

La carte n'a pas répondu à la séquence d'initialisation SDSPI. Cette erreur se
produit avant la lecture du système de fichiers ou du WAD.

1. Insère complètement la carte.
2. Coupe puis rétablis l'alimentation plutôt que de relancer seulement le
   moniteur.
3. Essaie une autre carte microSD.
4. Vérifie que le circuit imprimé porte la référence `JC3248W535`.

## La carte répond, mais FAT ne peut pas être monté

Reformate la carte en FAT32. Le firmware ne formate jamais automatiquement
une carte : un échec de montage ne peut donc pas en effacer le contenu.

## `No IWAD found in /sdcard`

Place un fichier compatible à la racine de la carte, pas dans un dossier :

```text
doom2f.wad  doom2.wad  plutonia.wad  tnt.wad
doomu.wad   doom.wad   doom1.wad
```

Utilise un IWAD obtenu légalement et respecte exactement le nom en minuscules.
Le numéro de version d'un WAD n'est pas son nom de fichier : par exemple, The
Ultimate DOOM utilise normalement `doomu.wad`.

## Écran allumé mais noir

Commence par examiner le journal série. Un écran éclairé mais vide signifie
souvent que le LCD a démarré, puis que l'application s'est arrêtée pendant
l'initialisation de la SD ou de l'IWAD. Si le journal atteint `D_DoomMain`
mais que l'image reste noire, vérifie la révision exacte de la carte et joins
le journal de démarrage complet à une issue.

## Glitches, bandes dupliquées ou image décalée

Cette cible exige l'initialisation AXS15231B propre à la JC3248W535 et des
transferts séquentiels des bandes QSPI. Ne la remplace pas par une
configuration AXS15231B générique. Vérifie l'utilisation de
`platform = espressif32@6.12.0` et du composant en version 2.1.0, puis effectue
un build propre :

```sh
pio run -d ESP32 -t clean
pio run -d ESP32
```

## Le tactile est tourné ou ne correspond pas au dessin

Le mapping actuel suppose l'orientation portrait native de 320 x 480 pixels.
Un décalage indique probablement une autre révision de carte ou de dalle.
Ajoute à l'issue les coordonnées tactiles brutes du journal série et une
photo du circuit imprimé.

## Deux commandes fonctionnent, mais pas un troisième doigt

C'est une limite matérielle de la configuration tactile testée. Utilise le
bouton persistant `STF` : avancer, faire un pas latéral et tirer ne demande
alors que deux contacts.

## Aucun son

- Branche un haut-parleur sur la sortie amplifiée, jamais directement sur un
  GPIO de l'ESP32.
- Touche l'icône du haut-parleur et vérifie que le son n'est pas coupé.
- Contrôle les volumes des effets et de la musique dans le menu de DOOM.
- Recherche `NS4168 ready` et `GENMIDI OPL instrument bank ready` dans le
  journal.
- La référence testée est un haut-parleur de 8 ohms et 1,5 W.

## Erreurs de dépendances pendant le build

Le premier build nécessite Internet. Ne supprime ni `dependencies.lock` ni
les versions figées pendant le diagnostic. Si le cache des composants gérés
est endommagé, supprime `ESP32/managed_components` puis relance le build ;
ESP-IDF téléchargera de nouveau les dépendances verrouillées.

## Signaler un crash

Copie le journal complet depuis le reset jusqu'à la backtrace. Conserve le
fichier `.pio/build/jc3248w535/firmware.elf` correspondant : le décodeur
d'exceptions de PlatformIO l'utilise pour traduire les adresses en lignes de
code. Ne joins jamais de WAD commercial à une issue.
