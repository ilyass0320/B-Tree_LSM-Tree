#ifndef INDEXLAB_BTREE_H
#define INDEXLAB_BTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BTREE_MIN_DEGREE   3
#define BTREE_MAX_KEYS     (2 * BTREE_MIN_DEGREE - 1)
#define BTREE_MAX_CHILDREN (2 * BTREE_MIN_DEGREE)

/* Capacités en octets, caractère nul final compris. */
#define BTREE_VALUE_CAPACITY 256
#define BTREE_PATH_CAPACITY  1024

typedef struct {
    int key;
    char value[BTREE_VALUE_CAPACITY];
} DbRecord;

typedef struct {
    uint64_t reads;
    uint64_t comparisons;
    uint64_t inserts;
    uint64_t updates;
    uint64_t deletes;
    uint64_t splits;
} BTreeStats;

typedef struct BTreeNode {
    bool leaf;
    size_t key_count;
    DbRecord keys[BTREE_MAX_KEYS];
    struct BTreeNode *children[BTREE_MAX_CHILDREN];
} BTreeNode;

typedef struct {
    BTreeNode *root; /* Racine et descendants possédés par l'arbre. */
    size_t count;    /* Nombre total d'enregistrements. */
    BTreeStats stats;
    char path[BTREE_PATH_CAPACITY];
} BTree;

/* record est emprunté pendant l'appel. context est transmis tel quel. */
typedef void (*BTreeVisitor)(const DbRecord *record, void *context);

/*
 * Initialise dst avec key et une copie de value.
 * value == NULL représente une chaîne vide. Une chaîne trop longue est
 * limitée à BTREE_VALUE_CAPACITY - 1 octets, suivis de '\0'.
 * dst == NULL est ignoré.
 */
void record_init(DbRecord *dst, int key, const char *value);

/*
 * Initialise tree, qui doit être neuf ou avoir été détruit.
 * Copie path, qui doit être une chaîne non vide tenant dans tree->path.
 * Retourne 0 si la racine feuille vide est créée, -1 en cas d'erreur.
 * Après un échec, un tree non NULL peut être passé à btree_destroy.
 */
int btree_init(BTree *tree, const char *path);

/* Libère les nœuds et remet *tree à zéro. tree == NULL est ignoré. */
void btree_destroy(BTree *tree);

/*
 * Retourne la racine empruntée, ou NULL si tree ou sa racine est NULL.
 * Une modification de l'arbre peut invalider le pointeur.
 */
const BTreeNode *btree_root(const BTree *tree);

/* Remet les compteurs de tree à zéro. tree == NULL est ignoré. */
void btree_reset_stats(BTree *tree);

/*
 * Sauvegarde les enregistrements de tree dans le fichier tree->path.
 * tree doit avoir été initialisé. Retourne 0 en cas de succès, -1 sinon.
 */
int btree_save(const BTree *tree);

/*
 * Charge les enregistrements depuis tree->path et remplace le contenu
 * de tree, qui doit avoir été initialisé. Retourne 0 en cas de succès.
 * Retourne -1 en cas d'erreur et conserve le contenu de l'arbre.
 */
int btree_load(BTree *tree);

/*
 * Copie record dans tree. Retourne 0 pour une insertion, 1 pour
 * une mise à jour et -1 lorsqu'une erreur est détectée.
 */
int btree_put(BTree *tree, const DbRecord *record);

/*
 * Cherche key dans tree et, si elle existe, copie l'enregistrement dans
 * out lorsque out est non NULL. Retourne 1 si la clé est trouvée,
 * 0 si elle est absente et -1 si tree est NULL. Met à jour les statistiques.
 */
int btree_get(BTree *tree, int key, DbRecord *out);

/*
 * Supprime key de tree. Retourne 1 si la clé est supprimée, 0 si elle
 * est absente et -1 si tree ou sa racine est NULL.
 */
int btree_delete(BTree *tree, int key);

/*
 * Appelle visit pour chaque enregistrement de tree, par clé croissante.
 * Ne pas modifier ni détruire tree dans visit. Copier l'enregistrement
 * pour le conserver après l'appel. tree ou visit NULL est ignoré.
 */
void btree_traverse(const BTree *tree, BTreeVisitor visit, void *context);

#endif /* INDEXLAB_BTREE_H */