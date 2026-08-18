# Matériel

[English version](HARDWARE.md)

## Cible testée

La cible fonctionnelle est la carte GUITION noire portant la référence
**JC3248W535** sur son circuit imprimé. L'exemplaire testé rapporte :

- ESP32-S3 à 240 MHz ;
- 16 Mio de flash SPI ;
- 8 Mio de PSRAM octale à 80 MHz ;
- écran LCD AXS15231B QSPI de 320 x 480 pixels ;
- tactile capacitif à deux points par I2C ;
- lecteur microSD ;
- amplificateur audio mono NS4168.

Ne te fie pas seulement à la taille de l'écran ou à la photo du produit.
D'autres cartes équipées d'un AXS15231B peuvent relier les mêmes composants à
des GPIO différents et nécessiter une autre séquence d'initialisation.

Cette page décrit la cible testée et non un câblage ESP32-S3 générique.
Consulte le [guide de portage](PORTING.fr.md) avant d'adapter le firmware à une
autre carte ou à un autre écran.

<p align="center">
  <img src="media/board-back.jpg" width="420" alt="Dos de la carte GUITION testée montrant la référence JC3248W535, le module ESP32-S3 et le lecteur microSD">
</p>

## Brochage vérifié

La source de vérité est
[`platform_board.h`](../ESP32/src/platform/platform_board.h).

| Fonction | Périphérique | Signal | GPIO |
| --- | --- | --- | ---: |
| LCD | SPI2 QSPI | CS | 45 |
| LCD | SPI2 QSPI | CLK | 47 |
| LCD | SPI2 QSPI | D0 | 21 |
| LCD | SPI2 QSPI | D1 | 48 |
| LCD | SPI2 QSPI | D2 | 40 |
| LCD | SPI2 QSPI | D3 | 39 |
| LCD | GPIO | Rétroéclairage | 1 |
| Tactile | I2C0 | SDA | 4 |
| Tactile | I2C0 | SCL | 8 |
| Tactile | GPIO | Interruption | 3 |
| microSD | SPI3 | CS | 10 |
| microSD | SPI3 | MOSI | 11 |
| microSD | SPI3 | CLK | 12 |
| microSD | SPI3 | MISO | 13 |
| Audio | I2S0 | BCLK | 42 |
| Audio | I2S0 | LRCLK/WS | 2 |
| Audio | I2S0 | DATA | 41 |

Le LCD fonctionne avec une horloge pixel/SPI de 40 MHz. Le bus SD est
volontairement limité à 10 MHz pour rester fiable avec le routage de la
carte. L'I2C tactile fonctionne à 400 kHz. La sortie audio est un flux I2S
stéréo 16 bits à 16 kHz ; l'échantillon mono est dupliqué dans les deux slots
pour l'amplificateur.

## Haut-parleur

Utilise la sortie amplifiée de la carte, jamais un GPIO de l'ESP32. Un
haut-parleur de 8 ohms et 1,5 W a été testé ; il est déjà puissant pour un
petit boîtier. Fixe-le mécaniquement : les basses fréquences des armes peuvent
faire vibrer un haut-parleur libre contre la table ou ses propres fils.

<p align="center">
  <img src="media/audio-wiring.jpg" width="360" alt="Haut-parleur de test branché sur la sortie amplifiée de la JC3248W535">
</p>

L'icône tactile du haut-parleur coupe le mixage final. Les volumes des effets
et de la musique du menu DOOM continuent de s'appliquer indépendamment.

## Configuration de la flash et de la PSRAM

`sdkconfig.defaults` active la PSRAM octale à 80 MHz, réserve 64 Kio de
mémoire interne aux allocations DMA/internes, sélectionne un CPU à 240 MHz et
fixe la pile de la tâche principale à 24 Kio. La carte choisie dans PlatformIO
définit la topologie testée de 16 Mio de flash et 8 Mio de PSRAM.

La table de partitions personnalisée réserve 4 Mio au firmware :

| Partition | Décalage | Taille |
| --- | ---: | ---: |
| NVS | `0x9000` | 24 Kio |
| Initialisation PHY | `0xF000` | 4 Kio |
| Application factory | `0x10000` | 4 Mio |

Le reste de la flash n'est actuellement pas alloué. Il laisse de la place à
de futures partitions OTA ou de ressources sans imposer de placer les WAD en
flash.

## Alimentation et USB

Utilise un câble USB transmettant les données et une alimentation qui reste
stable lorsque le rétroéclairage et le haut-parleur fonctionnent ensemble.
Sous Linux, l'interface native USB Serial/JTAG apparaît généralement sous
`/dev/ttyACM0` ; Windows lui attribue un port `COM`.
