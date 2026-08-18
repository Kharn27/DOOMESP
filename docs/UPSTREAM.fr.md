# LinuxDOOM upstream et modifications locales

[English version](UPSTREAM.md)

## Provenance

Le moteur repose sur la
[publication officielle des sources de DOOM](https://github.com/id-Software/DOOM)
par id Software, plus précisément sur l'arborescence `linuxdoom-1.10`. Les
notes originales restent dans `README.TXT` et le texte de la GNU GPL version
2 dans `LICENSE.TXT`.

Le dépôt conserve le répertoire historique au lieu d'importer ces fichiers
dans le composant applicatif ESP-IDF. L'origine upstream reste ainsi visible
et les remplacements ordinaires de la plateforme (`i_video`, `i_sound` et
`i_net`) demeurent à l'extérieur du moteur.

## Sélection des sources au build

`ESP32/components/doom/CMakeLists.txt` compile les sources C du moteur, mais
retire les implémentations PC originales :

- `i_main.c`
- `i_net.c`
- `i_sound.c`
- `i_video.c`

Leurs équivalents ESP32 se trouvent dans `ESP32/src`. `i_system.c` reste dans
le moteur, car il porte l'allocateur de zone original, l'horloge et le contrat
d'erreur fatale ; ses branches propres à la cible utilisent les services
d'ESP-IDF.

## Patchs intentionnels du moteur

Les modifications de `linuxdoom-1.10` appartiennent à quatre catégories.

### Portabilité et correction pour les compilateurs modernes

Le code de 1997 repose sur plusieurs déclarations `int` implicites, sur le
signe de `char`, sur d'anciennes conversions de pointeurs et sur des effets de
bord dans les expressions que les toolchains embarquées modernes refusent ou
compilent incorrectement. L'arborescence locale rend ces types et opérations
explicites tout en préservant le protocole et le comportement du jeu.

### Mémoire et temps sur système embarqué

- `doomtype.h` fournit l'annotation `DOOM_EXT_RAM_BSS`.
- `i_system.c` place en PSRAM la zone DOOM de 6 Mio et les allocations basses,
  dérive les tics à 35 Hz de l'horloge milliseconde de l'ESP32, emploie les
  délais FreeRTOS et s'arrête proprement après une erreur fatale au lieu de
  quitter un système d'exploitation inexistant.
- Les grandes tables statiques qui n'exigent pas la RAM interne peuvent
  employer l'annotation de RAM externe.

### Système de fichiers et gestion des IWAD

- `w_wad.c` fait passer les opérations de fichiers par le système de fichiers
  monté de la plateforme, gère les lectures partielles, évite les grosses
  allocations sur la pile et valide les limites du répertoire et des lumps
  WAD.
- `d_main.c` recherche les IWAD compatibles dans `/sdcard` et y enregistre la
  configuration par défaut.

### Présentation et commandes portables

- `d_main.c` et `i_video.h` ajoutent les hooks ciblés permettant de détacher
  la barre d'état dans la disposition portrait.
- `m_misc.c` sélectionne et impose par défaut la vue complète de 320 x 200
  afin que la barre d'état live séparée reste toujours visible.
- `m_menu.c` accepte le joystick tactile piloté par événements sans le délai
  prévu pour les joysticks de bureau interrogés en continu.
- La gestion des démos accepte le format 1.9 intégré aux IWAD courants.

## Politique concernant les patchs

Les contributions doivent garder la logique de plateforme hors du moteur
dès qu'une frontière `I_*` existante permet de l'exprimer. Lorsqu'une
modification du moteur est nécessaire :

1. La garder minimale.
2. Protéger si possible le comportement propre à la cible avec `DOOM_ESP32`.
3. Expliquer pourquoi une solution dans `I_*` ou la couche plateforme ne
   suffit pas.
4. Préserver la sémantique du gameplay et des démos, sauf correction d'un bug
   de portabilité documenté.
5. Compiler le firmware `jc3248w535` complet avant de proposer la
   modification.

Cette politique est plus facile à maintenir qu'un fork opaque séparé ou
qu'un gros patch réappliqué à chaque build.
