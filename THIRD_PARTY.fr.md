# Logiciels et ressources tiers

[English version](THIRD_PARTY.md)

## LinuxDOOM 1.10

- Source : [id-Software/DOOM](https://github.com/id-Software/DOOM)
- Copyright : id Software et les détenteurs de droits mentionnés dans les
  sources
- Licence : GNU GPL version 2 ; voir `LICENSE.TXT`

Aucun IWAD, PWAD, morceau de musique, son ou autre contenu commercial de DOOM
n'est inclus. Chaque utilisateur doit fournir son propre IWAD compatible,
obtenu légalement.

## Composant Espressif AXS15231B

- Paquet : `espressif/esp_lcd_axs15231b` 2.1.0
- Source : [ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_axs15231b/versions/2.1.0)
- Licence : Apache-2.0

Le gestionnaire de composants d'ESP-IDF télécharge ce composant ; il n'est
pas embarqué dans le dépôt. Le graphe résolu de ses dépendances est enregistré
dans `ESP32/dependencies.lock`.

## LittleMUS

- Emplacement : `ESP32/components/doom_music/musplayer.*`
- Révision upstream enregistrée par le composant :
  `c551f1fba021343bc54f06381d828d022461f223`
- Copyright : Andrew Towers
- Licence : MIT ; voir `ESP32/components/doom_music/LICENSE.LittleMUS`

## Woody-OPL

- Emplacement : `ESP32/components/doom_music/woody_opl.*`
- Révision upstream enregistrée par le composant :
  `c3f6674e4394fd9a83fe52722cfc63e1a9a8e29c`
- Copyright : équipe DOSBox, Ken Silverman et contributeurs
- Licence : LGPL-2.1-or-later ; voir
  `ESP32/components/doom_music/LICENSE.Woody-OPL`

Woody-OPL est l'émulateur compilé dans le firmware actuel.

## Nuked OPL3

- Emplacement : `ESP32/components/doom_music/opl3.*`
- Copyright : Nuke.YKT et contributeurs
- Licence : LGPL-2.1-or-later ; voir
  `ESP32/components/doom_music/LICENSE.Nuked-OPL3`

Cet émulateur alternatif est conservé pour permettre des expérimentations,
mais il n'est pas inclus dans le build actuel du composant.

## Médias de la documentation

Les photos et vidéos de `docs/media` ont été capturées pendant le
développement de ce port et ne font pas partie du code distribué sous GPL.
Elles montrent un rendu produit à partir de données DOOM fournies par
l'utilisateur. Les noms, illustrations et marques du jeu restent la propriété
de leurs détenteurs respectifs.
