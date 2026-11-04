# Mesures et validation

## VM ARM64 — exécuté, pas estimé

Mesure Release GCC 11.3 / Qt **6.2.4**, Ubuntu 22.04 ARM64, noyau 6.8.0-1062-oracle. Véritable processus Qt Widgets, plugin `offscreen`, **100 tâches fictives en cache**, attente de 3 s puis échantillon de 15,003 s. Aucun jeton, compte Google, TLS ou compositeur de bureau. Résultat brut : [performance-vm-arm64.json](performance-vm-arm64.json).

| Mesure | Valeur |
|---|---:|
| RSS, pic RSS | 26,92 Mio |
| PSS (mémoire partagée répartie) | 20,99 Mio |
| Mémoire privée résidente | 19,88 Mio |
| CPU au repos sur 15 s | 0 tick mesuré, soit 0,00 % d’un cœur à cette résolution |
| Exécutable Release, non strippé | 298,7 Kio |

Sous **X11/Xvfb** avec le même exécutable et les mêmes 100 tâches : **30,38 Mio RSS**, **23,64 Mio PSS**, **22,00 Mio privés**, aucun tick CPU mesuré sur 15,015 s. [Résultat brut X11](performance-vm-x11-arm64.json). Aucun window manager ni Plasma n’est lancé. Le serveur X est exclu de la mesure. Le chargement GLX/Mesa inutile ajoutait environ 49 Mio RSS ; l’application désactive désormais l’intégration GL XCB puisqu’elle peint uniquement des widgets raster (surcharge explicite possible par variable d’environnement).

RSS inclut les pages partagées de Qt ; PSS est une meilleure approximation du coût additionnel avec KDE qui charge déjà Qt. Ces valeurs **ne mesurent pas** une synchronisation Google réelle ou une session Plasma/Windows 10. Le CPU « 0 » signifie aucune unité de temps CPU comptabilisée dans l’intervalle, pas une promesse de consommation nulle.

Reproduire sous Linux :
```sh
python3 scripts/measure.py build/desktop-tasks --output measure.json
QT_QPA_PLATFORM=xcb xvfb-run -a python3 scripts/measure.py build/desktop-tasks --output measure-x11.json
```
Le script utilise `/proc/PID/stat`, `status` et `smaps_rollup`, sans dépendance Python. Il mesure uniquement le widget, pas le serveur X/compositeur ou le navigateur système externe utilisé ponctuellement pour OAuth.

## CI et plateformes

Chaque build produit `performance-linux.json` et `performance-windows.json`, joints aux artefacts/release. Linux CI : Ubuntu 24.04 x64, rendu hors écran. Windows CI : Windows Server 2022 x64, Qt 6.8.3/MSVC, vrai processus GUI, working set/private bytes et CPU sur 15 s après 3 s de chauffe. Ce n’est **ni une compilation croisée ni un test Windows 10**. Les données de démonstration sont les mêmes sur les deux systèmes. Voir les logs Actions pour les versions exactes et résultats.

## Ce qui est vérifié

- Cache atomique, rechargement et refus d’écraser un cache invalide ; rollback après échec d’écriture.
- Coalescence des changements, édition pendant une requête, conservation de l’outbox après lecture.
- Réponses Google simulées, pagination de listes/tâches, tâches masquées terminées, erreurs HTTP et Retry-After.
- POST non rejoué par Qt après déconnexion, état incertain persistant avant envoi, absence de nouvelle tentative automatique après réponse perdue.
- PKCE vecteur RFC7636, contrôle du callback/state et import du type de client OAuth.
- Interface réelle avec 100 tâches fictives, rendu hors écran/X11 sur VM et natif Windows CI ; tests GUI de coche clavier, ajout, filtre et signal de transfert entre colonnes. Le geste souris glisser-déposer complet n’est pas automatisé.
- Parsers de mesures Python et sanitizers AddressSanitizer/UBSan sur Linux CI.

## Reste à valider manuellement

**Non testé ici** : consentement/refresh Google avec le client réel ; clé stockée dans KWallet/Credential Manager réels ; round-trip avec Android ; clavier/glisser-déposer sur Plasma X11/Wayland et Windows 10 ; persistance du rôle bureau et comportement Show Desktop/Explorer ; multi-écran/DPI ; ressources pendant synchronisation réelle. L’intégration KDE Wayland est compilée en CI, mais aucun compositeur Plasma n’y est exécuté. La VM compile et rend la variante X11/offscreen sans layer-shell Qt6.

Check-list avant usage quotidien : utiliser une liste de test, connexion, ajout puis visibilité Android, cocher/décocher dans les deux sens, couper le réseau, modifier/redémarrer/reconnecter, vérifier absence de perte et de doublon, tester les deux bureaux et mesurer une session connectée pendant au moins 10 minutes. Une liste très longue augmente RAM et coût des lectures complètes ; pas de promesse chiffrée pour 100 000 tâches.
