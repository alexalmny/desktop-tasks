# OAuth Google Tasks

Aucun serveur personnel, clé de service ou mot de passe Google dans le widget. L’API exige le consentement OAuth de chaque compte. Un client desktop est public : sa valeur `client_secret` distribuée ne garantit pas sa confidentialité ; PKCE et le retour local protègent le flux. Ne pas publier pour autant vos fichiers de configuration ou jetons.

## Préparer un projet

Dans un projet Google Cloud dédié dont vous êtes propriétaire :
```sh
gcloud auth list
gcloud services enable tasks.googleapis.com --project=VOTRE_PROJET
```
L’activation API peut se faire avec gcloud. **La création d’un client Desktop passe par Google Auth Platform / Cloud Console** : les commandes `gcloud iap oauth-*` concernent IAP, elles ne conviennent pas à cette application. Il ne faut pas réutiliser les identifiants d’un autre outil ni ses refresh tokens.

1. Ouvrir https://console.cloud.google.com/auth/overview et sélectionner le projet.
2. Configurer **Branding** : nom de l’application, adresse de support et contact développeur. Ne demander ni scope Gmail, ni Drive, ni Calendar.
3. **Audience** : externe pour un compte Google personnel. En mode **Testing**, ajouter l’adresse Google de l’utilisateur aux test users. En mode Testing, le refresh token pour le scope Tasks expire généralement après **7 jours** : reconnexion nécessaire.
4. **Data Access** : déclarer `https://www.googleapis.com/auth/tasks` (lecture et écriture). Ce scope sensible peut nécessiter une vérification Google pour une distribution publique sans avertissement. Un binaire open source ne dispense pas de cette procédure.
5. **Clients → Create client → Desktop app / Application de bureau**. Télécharger le JSON (`installed` à la racine). Ce n’est ni un client Web ni Android. Aucun URI web hébergé ni domaine serveur à enregistrer pour le retour loopback desktop.
6. Importer localement le JSON dans le widget, puis **Connecter Google**. Le navigateur doit s’ouvrir sur la **même machine** que le widget (le retour localhost d’une VM n’est pas celui de votre PC).

Ne pas communiquer mots de passe, codes 2FA, auth codes ou refresh tokens dans des tickets, conversations ou captures. Les conserver uniquement dans le coffre système. Les fichiers JSON du client ne doivent pas être attachés aux issues.

## Dépanner

- `access_denied` : utilisateur non déclaré en test, consentement refusé ou politique Workspace.
- `invalid_grant` : autorisation expirée/révoquée, reconnexion via le menu ; en Testing vérifier la règle des sept jours.
- `redirect_uri_mismatch` : vérifier qu’il s’agit d’un client **Desktop** ; ne pas saisir manuellement de callback fixe.
- API 403 : Tasks API activée dans le projet du client, scope autorisé, quotas et politiques du domaine.
- Coffre indisponible : ouvrir/déverrouiller KWallet ou Secret Service. Le widget refuse délibérément le repli en clair. Une session Xvfb seule ne fournit pas de coffre utilisable.
- Timeout après consentement : vérifier navigateur local, pare-feu local et port loopback ; relancer la connexion. Aucun port entrant Internet n’est nécessaire.

Références : [OAuth applications natives](https://developers.google.com/identity/protocols/oauth2/native-app), [liste Tasks](https://developers.google.com/workspace/tasks/reference/rest/v1/tasks/list), [consentement et publication](https://developers.google.com/identity/protocols/oauth2/production-readiness/policy-compliance).
