# Chan Model — Reference Notes

**Provenance** : copié depuis `~/.claude/plans/Model Chan Explication.txt` fourni par Michael le 2026-04-29.
**Statut** : référence externe — utilisée comme oracle de validation industriel (LTspice) pour le mécanisme Lm(B) de TWISTERION.
**Voir aussi** : `docs/RESEARCH_LM_DYNAMIC.md` §10 (lien Chan ↔ approche TWISTERION).

---

Le modèle de **Chan** dans LTspice est un modèle compact d'hystérésis de noyau magnétique qui transforme des paramètres matériau et géométriques en une inductance non linéaire dépendante de l'histoire du flux. [forums.melaudia](https://forums.melaudia.net/showthread.php?tid=11987&pid=197634)

Pour un transformateur audio, il faut le comprendre comme un modèle de **branche magnétisante non linéaire** plutôt qu'un transformateur complet prêt à l'emploi. [youtube](https://www.youtube.com/watch?v=3wUn3euwCJ4)

## Vue d'ensemble

LTwiki indique que le modèle implémenté dans LTspice vient du travail de John Chan et al. de 1991, enrichi par des méthodes ultérieures, et qu'il a été choisi parce qu'il est robuste, compact et rapide par rapport à d'anciens modèles de noyau. [forums.melaudia](https://forums.melaudia.net/showthread.php?tid=11987&pid=197634)

Son idée centrale est simple : au lieu de supposer une inductance constante, il reconstruit une boucle B-H avec hystérésis à partir de peu de paramètres, puis en déduit le comportement électrique vu par le circuit. [allaboutcircuits](https://www.allaboutcircuits.com/technical-articles/simulating-non-linear-transformers-in-ltspice/)

## Ce qu'il modélise

Le Chan model modélise la relation non linéaire entre champ magnétique H et densité de flux B, avec mémoire, donc il reproduit à la fois la saturation et la rémanence.

Les trois paramètres matériau principaux sont **Hc** la coercivité, **Br** la rémanence et **Bs** la densité de flux à saturation ; LTwiki précise qu'ils suffisent à décrire "la plupart" des boucles d'hystérésis usuelles.

## Paramètres LTspice

Une fois le matériau défini, LTspice demande aussi des paramètres géométriques pour relier le noyau au composant circuit : **A** pour la section magnétique, **Lm** pour la longueur moyenne du chemin magnétique, **Lg** pour l'entrefer, puis **N** pour le nombre de spires.

Le modèle sépare bien deux couches : d'abord la physique du matériau (Hc, Br, Bs), puis l'échelle du composant réel (A, Lm, Lg, N).

## Comment il produit l'inductance

Dans ce cadre, l'inductance n'est pas entrée comme une constante fixe : elle émerge du point de fonctionnement magnétique, de la géométrie et des spires.

Quand le flux augmente vers la saturation, la pente locale de la courbe B(H) diminue, ce qui veut dire que la perméabilité incrémentale chute et que l'inductance incrémentale vue électriquement baisse aussi.

C'est exactement pour cela qu'un noyau Chan absorbe plus de courant de magnétisation à fort niveau et qu'il peut créer un comportement BF dépendant du signal.

## Mémoire et hystérésis

Le point fondamental qui distingue Chan d'une simple loi de saturation sans hystérésis est la **mémoire** : pour une même valeur instantanée de champ H, la valeur de B dépend du trajet précédent dans la boucle.

Cela permet de reproduire la rémanence, la coercivité et des cycles mineurs, donc un comportement différent selon que le noyau "entre" ou "sort" de saturation.

LTwiki note d'ailleurs que cette construction par segments hystérétiques est compacte et rapide, mais qu'elle peut poser un problème numérique lorsque le rapport **Br/Bs devient trop élevé** (~> 2/3), en particulier sous forte excitation asymétrique.

## Traduction électromagnétique

Dans un inductor Chan, le simulateur :

1. relie la tension électrique au flux via l'intégrale de la tension,
2. relie le flux à B,
3. relie B à H au moyen de la loi hystérétique du modèle,
4. le courant résulte alors du champ nécessaire pour soutenir cet état magnétique dans le noyau.

Vu autrement, **la tension impose l'évolution du flux, tandis que le courant est la "réaction" magnétique du matériau**, ce qui est très cohérent avec la physique d'un composant à noyau.

C'est pour cela que Chan est plus physique qu'une simple self dont la valeur dépend directement du courant sans variable d'état magnétique explicite.

## Pourquoi il est utile

Le modèle est particulièrement utile quand on veut voir des effets que les selfs linéaires ne montrent pas :
- écrasement de l'inductance à fort flux,
- hystérésis,
- saturation douce ou plus marquée selon le matériau,
- pertes apparentes liées à la boucle B-H.

## Ce qu'il ne fait pas tout seul

LTwiki est très clair sur la plus grosse limite pratique : **l'implémentation LTspice du Chan model ne supporte pas directement la mutual inductance**.

Donc, pour un vrai transformateur à plusieurs enroulements, on ne met pas simplement deux inductances Chan avec un couplage K ; il faut construire les enroulements avec un montage auxiliaire ou un transformateur idéal, puis raccorder la branche Chan comme noyau magnétisant commun.

## Architecture d'un transfo Chan

La page Transformers de LTwiki donne la méthode recommandée : créer les enroulements séparément, faire la transformation de tension/courant par une structure de "winding" idéale, puis connecter un unique inductor Chan sur le nœud de cœur magnétique commun.

Dans cette architecture, les enroulements imposent au cœur la somme des ampère-tours, et le cœur renvoie vers chaque bobinage la tension par tour correspondante ; c'est une représentation très propre de la causalité magnétique du transfo.

Cette séparation est essentielle, parce qu'elle évite de confondre fuite, résistances cuivre et magnétisation du noyau.

## Sens des sous-circuits

Le sous-circuit `Winding` donné par LTwiki utilise :
- une **source de tension contrôlée** pour imposer au bobinage une tension égale à `volts par tour du cœur × nombre de tours`
- une **source de courant contrôlée** pour injecter dans le cœur le courant `ampères du bobinage × nombre de tours`

Autrement dit, le bobinage convertit les variables électriques classiques du port externe en variables magnétiques internes au nœud de cœur, puis les reconvertit pour les autres enroulements.

C'est exactement ce qui permet de greffer ensuite un cœur linéaire ou Chan sans changer la structure des bobinages.

## Rôle de chaque paramètre

| Paramètre | Rôle physique | Effet pratique |
|---|---|---|
| `Hc` | Champ coercitif | Plus il monte, plus il faut de champ pour inverser l'aimantation, donc la boucle est plus "large" |
| `Br` | Flux rémanent | Il fixe la mémoire résiduelle quand le champ retombe à zéro |
| `Bs` | Flux à saturation | Il borne la densité de flux et détermine l'entrée en saturation |
| `A` | Section du noyau | Relie flux total et densité de flux ; plus A est grand, plus un même flux donne un B faible |
| `Lm` | Longueur moyenne du chemin magnétique | Plus elle est grande, plus il faut d'ampère-tours pour obtenir un même champ H |
| `Lg` | Entrefer | Il réduit l'inductance effective et linéarise partiellement le comportement |
| `N` ou `n` | Nombre de spires | Il règle le lien entre volts, flux et ampère-tours dans le composant |

## ⚠️ Ce que signifie "Lm" ici

**Attention à une ambiguïté de notation importante pour ton travail audio** : dans la littérature transfo, **Lm désigne souvent l'inductance de magnétisation**, alors que dans les exemples LTspice Chan de LTwiki, `Lm` désigne la **longueur moyenne du chemin magnétique**.

Donc dans LTspice, `Lm` n'est **pas** directement "l'inductance magnétisante" que tu veux faire varier dans ton modèle DSP ; c'est un paramètre géométrique du noyau.

L'inductance magnétisante électrique résulte ensuite du matériau, de la géométrie et du nombre de spires, et varie avec le point de fonctionnement.

## Différence avec un modèle "saturating only"

LTwiki précise que LTspice permet aussi de créer une inductance arbitraire basée sur le flux ou le courant pour simuler une saturation sans hystérésis.

La différence est majeure : un modèle "saturation only" peut faire baisser l'inductance avec le niveau, mais il n'a pas la mémoire magnétique, donc il ne reproduit ni rémanence ni vraie boucle B-H.

Chan est donc préférable si tu veux étudier non seulement l'écrasement de Lm, mais aussi le retard/mémoire magnétique et les asymétries de cycle.

## Limites numériques

LTwiki signale une faiblesse spécifique : lorsque **Br/Bs dépasse environ 2/3**, surtout avec une excitation très asymétrique et une saturation profonde, les branches "entrée en saturation" et "sortie de saturation" ne s'alignent pas bien numériquement, ce qui peut bloquer le solveur.

En pratique, cela veut dire que le modèle est plus confortable pour des matériaux à boucle douce, et moins idéal pour certains noyaux à boucle carrée comme des tape-wound cores fortement rémanents.

Pour ces cas, LTwiki suggère qu'une **approximation de type Jiles-Atherton en sous-circuit peut mieux convenir**, au prix d'une simulation beaucoup plus lente.

## Ce qu'on peut en tirer pour l'audio

Pour un transformateur audio, le modèle Chan est très bon pour explorer :
- la branche magnétisante non linéaire,
- la montée du courant de magnétisation à basse fréquence,
- l'écrasement de l'inductance incrémentale avec le niveau,
- l'impact de l'hystérésis sur la forme d'onde.

Il ne suffit pas à lui seul pour capturer toute la signature sonore d'un "gros transfo", parce qu'il faut aussi modéliser résistances cuivre, fuite, capacités inter-enroulements et charge réfléchie.

Mais c'est une **base très crédible** si l'objectif est de tester scientifiquement l'idée que le grave et la phase changent avec le niveau à cause de la magnétisation non linéaire.

## Lecture opérationnelle

En une phrase d'ingé : **Chan prend la tension appliquée, en déduit le flux, passe ce flux dans une loi B-H hystérétique paramétrée par Hc, Br et Bs, puis renvoie le courant magnétisant correspondant au circuit en tenant compte de la géométrie et des spires.**

C'est pour cela qu'il "fabrique" une inductance apparente qui change avec le niveau et avec l'histoire du signal, au lieu d'utiliser une simple self fixe.

---

## Application pour TWISTERION (notes de rapprochement)

1. **Notre J-A = Chan raffiné** : 5+ paramètres au lieu de 3, gestion meilleure des minor loops, extension naturelle avec `H_DC` offset (cf. mécanisme #1 mastering). Pour mastering nickel (boucles douces, Br/Bs ≈ 0.3-0.5), les deux modèles fonctionnent ; J-A préféré pour la finesse.
2. **Architecture winding/core de LTwiki** = pattern à reprendre pour la cascade input + output (mécanisme #4). Sépare proprement bobinage idéal et cœur magnétique commun.
3. **Validation point gratuit** : avant prêt d'unité réelle pour M1, simuler en LTspice Chan avec paramètres équivalents → oracle indépendant pour comparer notre J-A en termes d'écrasement Lm(B).
4. **Piège notation** : dans notre code, `Lm` = inductance magnétisante (variable). Dans Chan LTspice, `Lm` = longueur géométrique. Ne pas confondre quand on lit du SPICE en parallèle.
