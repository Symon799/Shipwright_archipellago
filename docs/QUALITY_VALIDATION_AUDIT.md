# Jalon 0 - Audit de validation

Date : 2026-10-14. Revision auditee : `3384717c9`.
Le depot etait propre au debut de l'audit. Ce document est le seul fichier cree hors `build/`.
Le code actuel, et non les anciennes discussions, constitue la reference.

## Conclusion

La CI produit des builds, mais ne certifie pas les comportements du tracker.
Il existe deux fichiers de tests versionnes et des diagnostics locaux utiles,
pas encore une infrastructure de validation reproductible.

La priorite est de proteger les frontieres entre tracker, randomizer, Archipelago,
pause/inputs, ressources et sauvegardes. La calibration experimentale ne doit pas
absorber l'effort au detriment de ces integrations.

Recommandation : CMake/CTest pour les tests C++, `unittest` pour Python, et des
wrappers PowerShell/batch simples. Ne pas creer de framework de tests maison,
ni refondre le moteur pour commencer. Les commandes proposees ci-dessous
n'existent pas encore.

## Architecture actuelle

| Ensemble | Responsabilites et dependances observees |
| --- | --- |
| Moteur | Code C decomp dans `soh/src`, ressources et GUI libultraship, hooks `GameInteractor`, initialisation/fermeture `OTRGlobals`. DX11 Windows, OpenGL et Metal selon plateforme. |
| Randomizer / Check Tracker | `Rando::Context`, `StaticData`, inventaire, `ReachabilitySearch`, regions, options de seed, etats des locations et visibilite. Le Map Tracker utilise toujours ces donnees communes. |
| Map Tracker - donnees | `map_tracker_core.cpp` : ZIP, JSON, IDs, liens, diagnostics, choix de carte, rechargement transactionnel, generation des chargements. |
| Map Tracker - rendu | `map_tracker_render.cpp` : cache, etats/couleurs, Accessible / Checked / Total, marqueurs simples/multiples, liens, hints, logique, tooltips et curseur. |
| Map Tracker - interfaces | `randomizer_map_tracker.cpp` : quatre modes exclusifs, migration des anciens reglages, menu windowed/floating, panneau 2D anime, options, debug et inputs. |
| Pause native 3D | `map_tracker_pause.cpp` et fonctions C appelees dans les menus Kaleido : vraie page Map, transitions, memoire de page, capture ImGui convertie en geometrie Fast3D, textures tuiles, fond personnalise et panneaux 2D. |
| Inputs | Mapping via `SohInputEditorWindow`, bits custom propres au jeu, souris et curseur manette, repetition/acceleration, labels suivant le dispositif. Le code C de pause et `z_play.c` participent au blocage du gameplay. |
| Position du joueur | Approximation par liens et geometrie de scene, projections et calibrations optionnelles affines/piecewise. Des scenes sont exclues; les donjons restent un cas limite. |
| Archipelago | Singleton `ArchipelagoClient`, callbacks apclientpp, polling sur la boucle de jeu, scouts, hints, synchronisation des locations et queue des objets. Le code annonce APWorld 1.4.x. |
| Persistance | `SaveManager` sauvegarde des sections avec snapshots et taches de fond. Config/CVars pour preferences et profils de connexion par seed/slot. Sauver les preferences n'est pas sauver la progression. |
| Fermeture | Arret du client AP et du worker, liberation des textures/captures avant Context/logs; detachement des callbacks DXGI. Le proxy viewport natif ne possede pas de donnees backend ImGui. |

Fonctionnalites a conserver dans la validation : focus automatique (dont maison de
Link), Overworld/liens, liste de cartes, zoom/pan, hints et required par location,
scouts remplacant la liste provisoire, spoilers de donjons, diagnostics separes,
pause vanilla fonctionnelle, activation seulement aux moments autorises, retour
a la pause preexistante, et independance visuelle du Check Tracker.

Le couplage reste important : `gPlayState`, `gSaveContext`, Context, CVars,
`Rando::Context` et tables de regions sont utilises directement. Beaucoup de
fonctions sont internes/static. Cela limite les tests isoles, sans justifier
une extraction generale du moteur.

## Compilation et CI

- CMake >= 3.26, C++20, C23, MSVC v143 sous Windows; vcpkg statique.
- Sous-modules : libultraship, ZAPDTR, OTRExporter, wswrap et apclientpp.
- Certaines dependances sont aussi selectionnees via FetchContent. Une cible de
  test AP doit reutiliser les memes headers/versions que l'executable.
- Les batchs utilisent `build/x64-v143`, MSVC `14.43.34808`, 16 jobs et attendent
  une touche. Les tasks VS Code pointent encore principalement vers `build/x64`.
- La configuration racine Windows peut lancer bootstrap/install vcpkg : ne pas
  imposer cette etape pour quelques tests rapides sans moteur.
- `SUPPRESS_WARNINGS` est active par defaut et concerne du code large. Ne pas
  demander d'emblee un nettoyage de tous les warnings upstream/decomp.
- `generate-builds.yml` compile/package Windows, Linux et macOS sur push/PR.
  Le pack 1.1.4 est fixe par URL et SHA256; `soh.o2r` est genere sans ROM.
- `clang-format.yml` verifie le formatage sur PR; la matrice de distributions
  Linux est manuelle (`workflow_dispatch`). Aucun de ces workflows n'execute
  actuellement les tests versionnes de ce fork.
- Le packaging ajoute config, pack et ressources. La presence de ces fichiers
  dans le ZIP ne prouve pas leur decouverte apres installation, notamment avec
  DMG/AppImage et chemins de travail differents.
- Les batchs locaux ne regenerent `soh.o2r` que s'il manque. Une texture modifiee
  peut donc ne pas etre integree au package : premier correctif recommande.

## Tests existants et baseline mesuree

| Existant | Situation | Execution pendant cet audit |
| --- | --- | --- |
| `tests/map_tracker_calibration_test.cpp` | Programme standalone utilisant le vrai header de production; geometrie, schemas et selection de profils. Pas de cible CMake/CTest. | Compile avec MSVC 14.43, puis termine avec code 0; environ 0.14 s d'execution hors compilation. |
| `scripts/tests/test_export_map_tracker_references.py` | Trois tests unittest sur donnees binaires synthetiques : IA4/endian/versions, troncature, PNG/pixels/CRC. Aucune ROM necessaire. | 3/3 reussis; environ 1.22 s pour la commande complete sur cette machine. |
| 27 scripts `build/*regression.ps1` | Non versionnes car `build/` est ignore. Beaucoup extraient des fonctions/blocs par texte et generent un programme avec stubs. Certains utilisent vraiment ImGui ou Win32. | Inventories et exemples examines, pas tous reexecutes. Ne pas compter ces scripts comme suite permanente. |
| Diagnostics de fermeture | Script de debugger local, code de sortie, logs et captures avec parcours humain. | Pas relances pendant cet audit. Les validations interactives precedentes ne sont pas une suite E2E autonome. |

Attention : le test C++ utilise `assert`, parfois autour d'appels avec effets de
bord. `NDEBUG` retire les verifications et certains appels. Il faut des checks
toujours actifs dans la future cible de tests, y compris en Release.

Ces chiffres ne constituent ni un pourcentage de couverture, ni une preuve de
validation de la logique AP, de la pause ou du jeu complet.

Commandes effectivement utilisees, depuis la racine du depot :

```powershell
python -m unittest discover -s scripts/tests -v
```

Pour le C++, commande locale actuelle (le chemin nlohmann est propre a cette machine) :

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.43 >nul && cl /nologo /EHsc /std:c++20 /I. /I"C:\Users\Symon\vcpkg\installed\x64-windows-static\include" /Fe:"build\quality-audit-calibration.exe" /Fo:"build\quality-audit-calibration.obj" "tests\map_tracker_calibration_test.cpp"'
& .\build\quality-audit-calibration.exe
```

Seuls des artefacts de test ignores ont ete generes dans `build/`. Aucun asset
du jeu, sauvegarde personnelle, secret ou image officielle n'a ete copie,
telecharge ou publie. Pas de compilation complete du jeu ni de lancement E2E
pour ce jalon.

## Matrice priorisee

P0 = progression, controles ou stabilite; P1 = fonctionnalite/livraison;
P2 = confort et experimental. Cout qualitatif estime, non benchmarke :
faible = calculs/fixtures; moyen = composants initialises/reseau local;
eleve = jeu graphique/parcours/installations.

| Priorite / famille | Comportement prouve / oracle | Couche | Prerequis | Cout / valeur | CI / local |
| --- | --- | --- | --- | --- | --- |
| P0 - Visibilite AP et cache | Arrivee/remplacement des scouts donne exactement les IDs visibles attendus; options grass/shop absentes restent absentes; invalidation et recalcul observes. | Integration composants | Fixture seed/scouts, vrais StaticData et etats tracker | Moyen / tres haute | CI sans ROM apres point d'entree adapte |
| P0 - Accessibilite | Inventaires/options connus donnent des ensembles de locations attendus; cas enfant/adulte et jour/nuit, statut orange inclus dans Accessible, pas de location deja collectee accessible. | Integration logique | Contexte randomizer initialise et fixtures explicites | Moyen / tres haute | CI sans ROM si initialisation independante confirmee |
| P0 - Objets et progression AP | Sync apres chargement, replay de messages/index, doublons pertinents, ordre respecte, locations envoyees et reconciliees, pas d'objet perdu lors de reconnexion. | Integration AP puis E2E cible | Vrai apclientpp, serveur localhost scriptable, effets jeu observes | Moyen puis eleve / tres haute | Protocole/queue en CI; livraison effective en jeu local |
| P0 - Mauvaise session | Seed/slot errones ou incompatibles refuses sans mutation de la progression; erreurs de slot_data et noms inconnus signales. | Integration | Fixtures version/datapackage/slot_data | Moyen / haute | CI |
| P0 - Sauvegardes | Roundtrip des sections utiles, inventaire/index coherents, attente des taches avant fermeture; ecriture des options n'entraine pas de sauvegarde de progression. | Integration puis E2E | Vrai Config/SaveManager, fichiers temporaires, fixtures anonymisees | Moyen / tres haute | Composants CI; jeu local |
| P0 - Pause / input ownership | Eligibility vanilla respectee; vraie pause utilisable; pas de gameplay dessous; retour a la pause preexistante; fermeture rapide, mode change pendant pause, nom de fichier et Start/L/R restent corrects. | Etats deterministes + E2E | Horloge/input/scenes explicites, puis jeu | Moyen/eleve / tres haute | Etats en CI; effets reels local |
| P0 - Worker / reload | Resultats d'ancienne generation ignores, reload rate conserve le pack precedent, decode/upload separes, stop pendant requete en cours rejoint le worker et libere ses ressources. | Integration composants | Vrai loader, petits PNG/ZIP synthetiques, barriere deterministe | Moyen / tres haute | CI sans ROM; GPU verifie local |
| P0 - Ownership / fermeture | Proxy viewport sans ownership backend; ressources liberees avant logger; messages Win32 tardifs sans callback moteur; sortie avec carte 2D/3D/popup et vraie pause ouverte donne code 0. | Unitaire/integration + E2E | ImGui reel headless, Win32 pour HWND, puis jeu | Faible/eleve / tres haute | ImGui CI; HWND CI Windows; jeu local |
| P1 - Contrat pack/runtime | IDs, placements/multichecks, liens, images, doublons, erreurs utiles et compatibilite ancien pack/calibration optionnelle. Pas seulement syntaxe JSON valide. | Integration parsing | Vrai parseur, StaticData, packs synthetiques | Faible/moyen / haute | CI |
| P1 - Packaging | ZIP plat correct, bon nom, config de reference, bon pack/hash, texture courante dans soh.o2r; absent requis = echec. Aucun fichier personnel/ROM dans l'archive. | Integration Python + build | Archives natives factices pour contrat; vrais artefacts pour assemblage final | Faible/moyen / haute | CI 3 plateformes |
| P1 - Installation / chemins | Ressources/config trouvees depuis un autre cwd, apres extraction/installation AppImage/DMG; casses de noms et chemins avec espaces. | Integration + E2E plateforme | Artefacts natifs et environnements correspondants | Eleve / haute | Contrats CI; jeu local/runner prive par OS |
| P1 - Modes / preferences | Migration legacy, 4 modes exclusifs, valeurs par defaut et bindings persistants, clavier/manette sans disparition des controles standard. | Composants + checklist | Vrais CVars/Config, input explicite | Faible/moyen / haute | CI logique; hardware manuel |
| P1 - Navigation / popups | Focus initial dont maison de Link, Overworld stable, aller/retour, multicheck survolable, Select hint individuel, pas de hint via lien, zoom/pan possibles sur popup. | ImGui headless + E2E cible | Vraies draw lists/rectangles et fixture d'etat | Moyen / haute | Geometrie/interaction CI; projection 3D local |
| P1 - Editeur/runtime | Export du modele editeur puis lecture par runtime : IDs stables, liens/placements conserves, champs optionnels preserves ou perte explicitement diagnostiquee. | Integration inter-repositories | Revision editeur fixee, dependances Qt explicites, pack synthetique | Moyen / haute | Job dedie; fixtures normales sans editeur installe |
| P2 - Position du joueur | Projections, profils/contextes, exclusions, repli sans calibration, hors hull, direction, donnees invalides non fatales. | Unitaire + integration | Headers actuels, scenes/profils synthetiques | Faible/moyen / moyenne | CI; precision visuelle manuelle |
| P2 - Performance | Cache non reconstruit sans changement, textures non redecodees en navigation, ownership borne, compteurs de travail; mesures frame time Overworld sur machine de reference. | Instrumentation + benchmark | Scenario fixe et compteurs stables | Moyen/eleve / moyenne | Invariants CI; budget FPS seulement local |

Eviter de dupliquer tout le graphe logique en fixtures. Utiliser quelques cas
de regression connus, des ensembles d'IDs verifies independamment et des
invariants valides sous conditions explicites. Une reference generee par le
code teste lui-meme ne constitue pas un oracle independant.

Pour AP, distinguer protocole valide et comportement du fork. Le serveur factice
doit fournir RoomInfo/Connected, datapackage, slot_data, scouts et demandes de
groupes/hints selon le vrai client utilise. Ne pas connecter les tests a un
serveur public ni generer une seed pour chaque test. Un message ReceivedItems
recu ne prouve pas que Light Arrows a ete effectivement livre a l'inventaire,
ni que les prix des scrubs/shops respectent les reglages : cas jeu distincts.

## Editeur accessible : constat et contrat

Le dossier local `SoH_Map_Ressource_Maker` a ete examine en lecture seulement.
Il contient un modele Qt/Python et `pack_io.py`, pas une integration formelle de
calibration dans ce checkout. Les cartes utilisent `maps.json`, `areas/*.json`,
images, map IDs, `soh_id`, `map_locations` et liens.

Les champs inconnus sont importes dans `extra` puis exportes seulement si
`preserve_unknown` est active. Ainsi `player_position` peut etre conserve en
mode preservation, mais perdu en mode nettoyage. Ne pas supposer que ce checkout
correspond a la derniere release publiee.

Premiere approche : petites fixtures de contrat partagees/versionnees et un job
optionnel qui execute le vrai exporteur sur une revision fixee, puis le vrai
consommateur C++. Pas de copie du code de l'editeur dans Shipwright. Ne pas rendre
l'editeur/Qt necessaire pour tous les tests rapides.

## Obstacles E2E et securite des donnees

- Le jeu necessite ses archives locales `oot.o2r`/`oot-mq.o2r`, issues des assets
  officiels. Les tests de maths/parsing/protocole/packaging doivent s'en passer.
- Les configurations et saves personnelles contiennent progression, identites
  et potentiellement mots de passe. Ne jamais executer une suite destructive
  dans `x64/Debug` avec les fichiers personnels.
- Sur Windows portable, le repertoire courant sert de repertoire de donnees.
  `SHIP_HOME` est gere sous Linux/macOS, pas comme override general Windows.
  Le lookup peut retomber sur le dossier de l'executable : l'isolation des mods
  et assets doit donc etre verifiee, pas seulement le cwd change.
- Les commandes actuelles de debug offrent des actions utiles, mais pas une CLI
  de scenario deterministe, readiness/result JSON ou commande batch stable.
- La console de debug, les dialogues de crash/assertion, les fenetres ImGui,
  le focus, la resolution, le backend et les bindings sont des obstacles a une
  automatisation fondee sur des clics absolus ou des sleeps fixes.
- Preparer un repertoire de test prive, des saves fabriquees pour la validation
  et des ressources officielles referencees localement sans export/publication.
  Ajouter, si necessaire, un override de repertoire cible et restreint au mode
  validation plutot que modifier le systeme de chemins complet.
- Ne pas deposer les images officielles exportees, ROMs, captures sensibles ou
  credentials dans des artefacts CI. Les images de fixtures seront synthetiques.

## Strategie par jalon

### Jalon 1 - Le plus petit socle utile

1. Ajouter un projet de tests CMake leger, CTest et wrappers `check-fast`,
   `check-integration`, `check-e2e`, `check-all`, plus `check-env`.
   Detection de Python/CMake/MSVC/dependances; pas de bootstrap reseau silencieux.
   Garder des runners non interactifs et des .bat avec pause uniquement en facade.
2. Brancher les deux tests versionnes; remplacer les asserts de test par des
   checks actifs en Debug et Release. Ne pas recompter leurs assertions comme
   autant de tests independants.
3. Ajouter les contrats de packaging avec donnees synthetiques et les regressions
   de shutdown Win32/proxy ImGui. Le helper Win32 se teste deja sans moteur.
   Exposer le calcul de viewport par une petite frontiere reelle, sans copier
   son corps par regex dans les tests.
4. Ajouter les premiers cas de mapping/visibilite/cache selon le cout du vrai
   bootstrap StaticData/Rando. Si necessaire, extraire une fonction de decision
   ou un parseur pur, pas tous les modules du tracker. Publier les limites si
   l'integration du graphe complet reste au jalon 2.
5. Corriger la fraicheur de `soh.o2r` dans les packages locaux, harmoniser les
   chemins des wrappers/tasks et rendre le nombre de jobs configurable.

Objectif de temps : `check-fast` sous une minute apres preparation/compilation
incremente, a mesurer. La premiere compilation peut etre plus longue. Ce jalon
n'est pas une certification du tracker en jeu ou de toute la progression AP.

### Jalon 2 - Integrations a haute valeur

1. Tester le vrai parseur de pack et le vrai etat tracker : visibilite provisoire
   puis scouts, accessibilite et totaux, plusieurs seeds/inventaires explicites.
2. Ajouter une frontiere ciblee pour traiter les evenements AP avec les vrais
   modeles et effets attendus; le mock ne remplace que transport/effets plateforme,
   jamais l'algorithme a verifier. Puis un serveur localhost pour tester le vrai
   apclientpp et ses callbacks de bout en bout.
3. Tester Config/SaveManager sur fichiers isoles et des fixtures sans progression
   personnelle. Couvrir index des objets, reconnexion et attente des ecritures.
4. Construire un mode/scenario de validation opt-in : commandes stables sur la
   vraie boucle de jeu, etats observables, delais bornes, resultat JSON et sortie
   normale. Il ne doit pas court-circuiter la pause ou les controles qu'il teste.
5. Limiter les E2E initiaux a quelques parcours : charger une save de test,
   visualiser un etat AP attendu, pause vanilla/tracker, popup/navigation,
   fermeture 2D/3D avec popup et worker actif, relance et persistance.

La reception/livraison d'un objet important ou un achat shop necessite un cas
jeu dedie. La generation de seeds AP reelles reste optionnelle : petite matrice
fixee avec generateur/APWorld/config explicites, jamais aleatoire a chaque PR.

### Jalon 3 - CI, rapports et routine autonome

- Ajouter un job qualite rapide Windows/Linux; assertions Debug et Release.
  Les contrats Python de packaging couvrent les trois formats natifs avec
  fixtures; la verification des vrais artefacts suit les jobs de build existants.
- CTest pour le C++, unittest pour Python, orchestration fine en PowerShell.
  Produire JUnit/JSON normalises, stdout/stderr et manifestes de versions.
- Un cas absent = SKIP explicite. Un `check-e2e` demande sans prerequis critiques
  n'est pas vert. `check-all` expose Passed/Failed/Skipped/NotRun et un resultat
  PARTIAL si le jeu n'a pas ete valide; gate release exige les suites requises.
- Une selection de suite qui decouvre zero test est une erreur, pas un succes.
- Timeouts par test et groupe, port localhost dynamique, attente sur conditions
  observees, reset par fixture/processus. Pas de retry pour cacher une regression.
- Rapports : commit/dirty state, toolchain, plateforme/backend, suite, scenario,
  seed/fixture/pack hashes, resultat et reproduction. Filtrer les secrets.
- Captures seulement pour quelques parcours fixes; resolution, scaling/font,
  scene et backend specifies. Pas de comparaison pixel parfaite des trois GPU.
- Garder une checklist humaine courte pour manette reelle, lisibilite/projection
  3D et animations/audio. Un runner prive interactif est une option ulterieure,
  pas une condition pour commencer les tests en CI publique.
- Apres merge upstream : fast + integration obligatoires, builds 3 OS, E2E local
  avant release. Ecrire une routine Codex dans un futur `AGENTS.md` qui execute
  ces commandes et distingue automatique, manuel et non verifie.

## Refactorings acceptables et arbitrages

Petits changements recommandes : cible de tests, fixtures, helpers de geometrie
et policy d'etat, horloge injectable pour repeats/animations, compteurs readiness
et traitement semantique des evenements AP. Chaque extraction doit etre utilisee
par le jeu et les tests, garder son comportement et n'avoir qu'une responsabilite.

Ne pas lancer maintenant : separation generale de `soh` en bibliotheques, nouvelle
architecture de rendu, refonte de libultraship, duplication du graphe randomizer,
automation GUI massive, tests de tous les croisements de toutes les options.

Arbitrages utiles avant jalon 2 : accepter un mode validation opt-in restreint
a la build de test; choix des saves/scenarios prives; acces futur a des runners
graphiques Windows/macOS/Linux. Proposition initiale : pas de runner prive requis,
pas de ROM sur GitHub, fixtures/protocole publics et E2E Windows local.

Definition de termine : le mainteneur peut lancer les commandes sans IA, un
vrai echec donne un code non nul et une reproduction, les suites ne recopient pas
la logique metier, et les prerequis manquants ne sont pas des succes implicites.
