# Pilotes Linux embarqués sur i.MX93 FRDM

Travaux pratiques réalisés par **Imel23** pendant la formation Bootlin *Linux Kernel and Driver Development*. J’ai compilé un noyau Linux 6.12 pour la carte NXP i.MX93 FRDM, adapté son Device Tree et développé trois modules en C. [Formation Bootlin](https://bootlin.com/training/linux-kernel/)

| Réalisation | Ce qu’elle met en pratique |
| --- | --- |
| [Module Hello](modules/hello/hello_version.c) | Chargement et déchargement d’un module, paramètre `who`, version du noyau et journalisation. |
| [Pilote Nunchuk](modules/nunchuk/nunchuk.c) | Initialisation du joystick Wii sur LPI2C4 à l’adresse `0x52`, interrogation toutes les 50 ms et remontée des axes X/Y et des boutons C/Z via le sous-système Input. |
| [Pilote série LPUART](modules/serial/serial.c) | Configuration de LPUART3 et LPUART8 via leurs registres MMIO, émission à 115 200 bauds et exposition d’un périphérique `miscdevice` par port. Deux [outils](tools/) lisent et remettent à zéro le compteur d’émission via `ioctl`. |
| [Intégration carte](kernel-patches/) | Support i.MX93 FRDM, configuration ARM64, description du Nunchuk et multiplexage des broches I²C et UART. |

## Essais sur la carte

- **Nunchuk.** La capture `evtest` montre les appuis et relâchements des boutons C et Z. [Capture](docs/nunchuk-evtest.png)
- **LPUART.** Minicom reçoit `Hello!` envoyé depuis le périphérique série. Le compteur passe de 0 à 7 après l’écriture, puis revient à 0 après la remise à zéro. [Relevé du lab](docs/essais-carte.md#émission-uart-et-compteur)

Le parcours de formation atteint le chapitre **Sleeping and handling interrupts**. Dans la version du pilote série disponible ici, `read()` renvoie `-EINVAL`. La réception par interruption, le tampon circulaire et l’attente bloquante restent à intégrer au dépôt.

## Compilation

Sur la machine de développement, installer `make`, un compilateur hôte et une chaîne de compilation ARM64 avec sa libc pour les utilitaires statiques. Utiliser l’arbre de compilation correspondant au noyau démarré sur la carte, avec sa configuration et son fichier `Module.symvers`. Les [instructions d’intégration](docs/essais-carte.md#préparer-le-noyau) permettent de reconstruire cet arbre à partir de Linux 6.12.103.

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/chemin/vers/linux
```

Les modules `.ko` se trouvent sous `modules/` et les exécutables ARM64 sous `tools/`. Le [guide d’essais](docs/essais-carte.md#essayer-les-modules-sur-la-carte) indique comment les charger et les tester.

## Crédits

Le pilote série et ses utilitaires reprennent les squelettes du lab Bootlin. Le support de base de la carte dérive du Device Tree NXP fourni par Bootlin. Les mentions SPDX des sources et des patches indiquent leurs licences. Les utilitaires adaptés relèvent de la licence CC BY-SA 3.0 des supports du cours. [Dépôt des supports Bootlin](https://github.com/bootlin/training-materials), [licence des supports](https://github.com/bootlin/training-materials/blob/master/LICENSE), [GPL version 2](LICENSE)
