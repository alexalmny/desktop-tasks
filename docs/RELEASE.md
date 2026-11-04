Première preview native C++/Qt Widgets : deux colonnes, filtres de listes, cases à cocher, glisser-déposer, ajout rapide, cache et file persistante hors ligne, OAuth système/PKCE et coffre natif.

- **Linux x64** : paquet `.deb` pour Ubuntu/KDE neon basé sur 24.04. Compilation et tests de protocole natifs ; rendu et mesure hors écran. Pas de validation interactive Plasma X11/Wayland.
- **Windows x64** : ZIP à extraire intégralement. Compilation, tests et rendu du véritable processus sur **Windows Server 2022 CI** ; pas une validation Windows 10.
- Aucun test avec un compte Google réel n’a été exécuté. Importez votre client OAuth Desktop puis connectez Google selon `docs/OAUTH.md`. Un coffre système est obligatoire.
- `performance-*.json` contient les mesures réelles de processus avec 100 tâches fictives, sans réseau ni authentification. Ces nombres ne sont pas ceux d’une session synchronisée réelle.
- Sources de l’application, Qt et QtKeychain et SHA256SUMS inclus. Les archives `qtbase-*` et `qtkeychain-*` sont les sources des bibliothèques, **pas nécessaires pour installer**.

### Test immédiat sans compte Google

- **Windows** : téléchargez `Desktop-Tasks-Windows-x64.zip`, extrayez **tout** le ZIP, puis lancez `desktop-tasks.exe`. Pour une démo isolée avec 100 tâches fictives : `desktop-tasks.exe --demo --window` depuis PowerShell dans ce dossier (`.\desktop-tasks.exe --demo --window`).
- **Ubuntu/Kubuntu/KDE neon 24.04 x64** : téléchargez le `.deb`, installez-le avec `sudo apt install ./desktop-tasks-0.1.0-Linux.deb`, puis lancez `desktop-tasks --demo --window`.
- La démo ne contacte pas Google et ne conserve pas ses modifications. Pour vos vraies tâches : relancez sans `--demo`, puis menu **⋮ → Importer le client OAuth…**, suivi de **Connecter Google…** ; voir [la configuration OAuth](https://github.com/alexalmny/desktop-tasks/blob/main/docs/OAUTH.md). Un client OAuth Desktop est à créer/importer : la release ne contient aucune identité Google préconfigurée.

Binaire Windows non signé. Intégration WorkerW non documentée par Microsoft ; mode `--window` disponible, relancer après redémarrage d’Explorer. Version expérimentale : testez d’abord avec une liste Google de test. Voir README pour installation et limites, notamment le traitement prudent des ajouts dont la réponse est perdue.
