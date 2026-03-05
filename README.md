<p align="center">
  <img src="PushlyLogo.png" alt="Pushly Logo" width="200" />
</p>

# Pushly C++ Win32 Key Spammer

[![Download](https://img.shields.io/badge/T%C3%A9l%C3%A9charger-v1.0.0-blue?style=for-the-badge&logo=windows)](https://github.com/BlaMacfly/Pushly/releases/tag/v1.0.0)

Pushly est un logiciel C++ Win32 extrêmement léger conçu pour optimiser l'expérience de jeu (notamment sur World of Warcraft) en automatisant la frappe séquentielle d'une touche avec une protection d'humanisation.

## Fonctionnalités Principales

- **Sécurité Anti-Détection** : Délai (Jitter) randomisé de +/- 20% à chaque itération pour simuler une frappe humaine et esquiver les détections algorithmiques.
- **Paramétrage Global** : Configuration complète des Hotkeys de Démarrage et d'Arrêt opérant même si l'application tourne en arrière-plan.
- **Indicateurs Sonores** : Retours audios système très rapides (bips aigus et graves) sans nécessiter de retour visuel.
- **Dark Mode Intégré** : Interface graphique moderne complètement redessinée sur le framework natif Win32 avec prise en charge du thème sombre (Title Bar et Application).
- **Sauvegarde Automatique** : Génération d'un fichier `config.ini` portable pour une persistance immédiate de vos configurations.

## Architecture

Application monolithique en C++ pur utilisant l'API Win32 matérielle directe via `SendInput()`. N'utilise aucune bibliothèque externe lourde, garantissant une utilisation CPU et Mémoire négligeable.

## Compilation

Un script **`build.cmd`** est inclus. 
Il configure de manière autonome et sécurisée les variables d'environnement `VsDevCmd.bat` et lance `cl.exe` (compilateur Microsoft).
L'icône inclut dans les *"Resources"* natives (`resource.rc`) sera automatiquement imbriquée dans l'exécutable final.
