# Journal des modifications

[English version](CHANGELOG.md)

Le projet est encore en préversion. Jusqu'au premier tag de version, les
entrées décrivent l'état actuel de la branche `main`.

## À venir

### Ajouts

- Prise en charge native de l'écran, du tactile capacitif, de la microSD et de
  la PSRAM de la JC3248W535.
- Jeu en portrait, en 320 x 200, avec la barre d'état DOOM live détachée.
- Commandes tactiles à deux points et mode strafe persistant.
- Écrans de sélection des armes et des cheats utilisant les ressources du
  WAD.
- Effets sonores sur huit canaux, musique MUS/OPL3 et coupure instantanée de
  l'audio.
- Partition applicative de 4 Mio.
- Définition matérielle centralisée de la carte.
- Build public, documentation de l'architecture, du matériel, du dépannage,
  des licences et des contributions.

### Corrections

- Les icônes des armes et des cheats ne disparaissent plus après une longue
  démo ou un changement de niveau. Leurs illustrations utilisent désormais
  un cache PSRAM stable et dédupliqué, indépendant du cache WAD purgeable de
  DOOM.

### Limitations connues

- Jeu solo uniquement ; aucun gameplay Wi-Fi ou Bluetooth pour le moment.
- Seule la carte GUITION portant la référence `JC3248W535` a été validée.
- Les sauvegardes n'ont pas encore été validées dans la matrice de tests
  publique.
