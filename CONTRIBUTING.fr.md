# Contribuer

[English version](CONTRIBUTING.md)

Les issues, validations matérielles, améliorations documentaires et
modifications du code sont les bienvenues.

## Avant d'ouvrir une issue

- Vérifie que le circuit imprimé porte la référence `JC3248W535`.
- Compile la branche `main` actuelle.
- Consulte `docs/TROUBLESHOOTING.fr.md`.
- Capture le journal série depuis le reset à 115200 bauds.
- Ne téléverse ni ne lie aucun WAD commercial.

Pour un problème d'affichage ou de tactile, ajoute une photo nette du circuit
imprimé et les coordonnées tactiles brutes écrites par le firmware. Pour un
crash, conserve le fichier ELF correspondant et joins la backtrace complète.

## Environnement de développement

Installe PlatformIO, clone le dépôt et lance :

```sh
pio run -d ESP32
```

Le résultat attendu est un build release réussi pour `jc3248w535`. Toute
modification matérielle doit aussi être flashée et testée sur la carte
physique.

## Règles concernant les sources

- Préserve l'architecture originale de LinuxDOOM et ses interfaces `I_*`.
- Place les services généraux de la carte dans `ESP32/src/platform`.
- Conserve les broches physiques dans `platform_board.h`.
- Conserve la géométrie tactile dans `platform_controls.h`.
- Pour une nouvelle cible matérielle, suis `docs/PORTING.fr.md` : ajoute un
  environnement de build distinct et sépare le transport matériel du
  comportement partagé de l'interface.
- Évite de modifier `linuxdoom-1.10` lorsqu'un backend ESP32 peut porter le
  comportement. Suis `docs/UPSTREAM.fr.md` si un patch moteur est nécessaire.
- Respecte le style C environnant ; le port utilise une indentation de quatre
  espaces.
- Garde les handlers d'interruption courts et les transactions LCD dans la
  tâche de rendu.
- Préfère la PSRAM pour les gros caches et la RAM interne/DMA pour les tampons
  sensibles au temps.

## Pull requests

Décris :

1. Le problème et le comportement attendu.
2. La révision de carte et la famille de WAD testées — sans jamais joindre le
   WAD.
3. Le résultat du build et la taille du firmware.
4. Le résultat du test physique pour toute modification de l'écran, du
   tactile, de la SD ou de l'audio.
5. Toute modification de `linuxdoom-1.10` et la raison pour laquelle elle
   était inévitable.

Les contributions à ce projet GPL-2.0 doivent employer une licence compatible
avec celle du dépôt. Conserve les licences et attributions des dépendances
tierces embarquées.
