/*
 * Tests des algorithmes du B-tree : insertion, recherche, suppression,
 * split, merge (emprunt/fusion entre freres) et parcours.
 *
 *   gcc -std=c11 -Isrc tests/test_btree.c src/btree.c src/btree_operations.c \
 *       src/storage.c -o btree_tests && ./btree_tests
 */

#include "btree.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independant de NDEBUG : un echec arrete le test avec le numero de ligne. */
#define CHECK(condition)                                                           \
    do {                                                                           \
        if (!(condition)) {                                                        \
            fprintf(stderr, "ECHEC %s:%d : %s\n", __FILE__, __LINE__, #condition);  \
            fflush(stderr);                                                        \
            exit(1);                                                               \
        }                                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* Utilitaires                                                         */
/* ------------------------------------------------------------------ */

static DbRecord make_record(int key, const char *value) {
    DbRecord record;
    record_init(&record, key, value);
    return record;
}

static void put_value(BTree *tree, int key) {
    char value[32];
    DbRecord record;
    snprintf(value, sizeof(value), "value-%d", key);
    record = make_record(key, value);
    CHECK(btree_put(tree, &record) >= 0);
}

static const DbRecord *subtree_min(const BTreeNode *node) {
    while (node != NULL && !node->leaf) {
        node = node->children[0];
    }
    if (node == NULL || node->key_count == 0) {
        return NULL;
    }
    return &node->keys[0];
}

static const DbRecord *subtree_max(const BTreeNode *node) {
    while (node != NULL && !node->leaf) {
        node = node->children[node->key_count];
    }
    if (node == NULL || node->key_count == 0) {
        return NULL;
    }
    return &node->keys[node->key_count - 1];
}

/*
 * Verifie les invariants du B-tree :
 *  - cles triees dans chaque noeud et bornes respectees entre sous-arbres ;
 *  - nombre de cles entre BTREE_MIN_DEGREE - 1 et BTREE_MAX_KEYS (sauf racine) ;
 *  - toutes les feuilles a la meme profondeur ;
 *  - nombre total de cles egal a tree->count.
 */
static bool check_node(const BTreeNode *node, int depth, int *leaf_depth, bool is_root,
                       size_t *total) {
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (node->key_count > BTREE_MAX_KEYS) {
        return false;
    }
    if (!is_root && node->key_count < BTREE_MIN_DEGREE - 1) {
        return false;
    }
    for (i = 1; i < node->key_count; ++i) {
        if (node->keys[i - 1].key >= node->keys[i].key) {
            return false;
        }
    }
    if (node->leaf) {
        if (node->key_count > 0 || is_root) {
            if (*leaf_depth < 0) {
                *leaf_depth = depth;
            } else if (*leaf_depth != depth) {
                return false;
            }
        }
    } else {
        for (i = 0; i <= node->key_count; ++i) {
            const DbRecord *first;
            const DbRecord *last;
            if (node->children[i] == NULL) {
                return false;
            }
            if (!check_node(node->children[i], depth + 1, leaf_depth, false, total)) {
                return false;
            }
            first = subtree_min(node->children[i]);
            last = subtree_max(node->children[i]);
            if (first == NULL || last == NULL) {
                return false;
            }
            /* Sous-arbre i : cles > keys[i - 1] et cles < keys[i]. */
            if (i > 0 && first->key <= node->keys[i - 1].key) {
                return false;
            }
            if (i < node->key_count && last->key >= node->keys[i].key) {
                return false;
            }
        }
    }
    *total += node->key_count;
    return true;
}

static void assert_btree_valid(const BTree *tree) {
    int leaf_depth = -1;
    size_t total = 0;
    CHECK(check_node(btree_root(tree), 0, &leaf_depth, true, &total));
    CHECK(total == tree->count);
}

typedef struct {
    DbRecord *out;
    size_t capacity;
    size_t count;
} Collector;

static void collect_visitor(const DbRecord *record, void *user_data) {
    Collector *collector = (Collector *)user_data;
    if (collector->count < collector->capacity) {
        collector->out[collector->count] = *record;
    }
    collector->count++;
}

/* Remplit out avec les enregistrements en ordre croissant, retourne le nombre. */
static size_t in_order(const BTree *tree, DbRecord *out, size_t capacity) {
    Collector collector;
    collector.out = out;
    collector.capacity = capacity;
    collector.count = 0;
    btree_traverse(tree, collect_visitor, &collector);
    return collector.count;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_insertion_et_recherche(void) {
    BTree tree;
    DbRecord result;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 1; i <= 30; ++i) {
        put_value(&tree, i);
        assert_btree_valid(&tree);
    }
    CHECK(tree.count == 30);
    for (i = 1; i <= 30; ++i) {
        char expected[32];
        snprintf(expected, sizeof(expected), "value-%d", i);
        CHECK(btree_get(&tree, i, &result) == 1);
        CHECK(strcmp(result.value, expected) == 0);
    }
    CHECK(btree_get(&tree, 0, &result) == 0);
    CHECK(btree_get(&tree, 31, &result) == 0);
    CHECK(btree_get(&tree, 15, &result) == 1);
    CHECK(result.key == 15);
    CHECK(tree.count == 30);
    CHECK(tree.stats.splits >= 1);
    btree_destroy(&tree);
    printf("  ok  insertion + recherche (30 cles, racine splittee)\n");
}

static void test_mise_a_jour(void) {
    BTree tree;
    DbRecord result;
    DbRecord updated;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 1; i <= 50; ++i) {
        put_value(&tree, i);
    }
    updated = make_record(17, "updated-17");
    CHECK(btree_put(&tree, &updated) == 1);
    CHECK(tree.count == 50);
    CHECK(tree.stats.updates == 1);
    CHECK(btree_get(&tree, 17, &result) == 1);
    CHECK(strcmp(result.value, "updated-17") == 0);
    assert_btree_valid(&tree);
    btree_destroy(&tree);
    printf("  ok  mise a jour sans duplication\n");
}

static void test_parcours_trie(void) {
    BTree tree;
    DbRecord records[100];
    size_t count;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 0; i < 60; ++i) {
        put_value(&tree, (i * 37) % 60); /* ordre d'insertion melange */
    }
    count = in_order(&tree, records, 100);
    CHECK(count == 60);
    for (i = 0; i < 60; ++i) {
        CHECK(records[i].key == i);
    }
    btree_destroy(&tree);
    printf("  ok  parcours in-order trie (60 cles)\n");
}

static void test_suppression_feuille(void) {
    BTree tree;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 1; i <= 40; ++i) {
        put_value(&tree, i);
    }
    for (i = 1; i <= 40; i += 2) {
        CHECK(btree_delete(&tree, i) == 1);
        CHECK(btree_get(&tree, i, NULL) == 0);
        assert_btree_valid(&tree);
    }
    CHECK(tree.count == 20);
    for (i = 2; i <= 40; i += 2) {
        CHECK(btree_get(&tree, i, NULL) == 1);
    }
    CHECK(btree_delete(&tree, 999) == 0);
    btree_destroy(&tree);
    printf("  ok  suppression feuilles + cle absente\n");
}

static void test_suppression_noeuds_internes(void) {
    BTree tree;
    DbRecord records[300];
    size_t count;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 1; i <= 200; ++i) {
        put_value(&tree, i);
    }
    /* Supprime une cle sur trois : force des fusions et des emprunts. */
    for (i = 3; i <= 200; i += 3) {
        CHECK(btree_delete(&tree, i) == 1);
        assert_btree_valid(&tree);
    }
    CHECK(tree.count == 200 - 66);
    count = in_order(&tree, records, 300);
    CHECK(count == tree.count);
    for (i = 1; i <= 200; ++i) {
        CHECK(btree_get(&tree, i, NULL) == (i % 3 == 0 ? 0 : 1));
    }
    for (i = 1; i < (int)count; ++i) {
        CHECK(records[i - 1].key < records[i].key);
    }
    btree_destroy(&tree);
    printf("  ok  suppression en cascade (merge/borrow, 200 -> %d cles)\n", 200 - 66);
}

static void test_suppression_totale(void) {
    BTree tree;
    int i;

    btree_init(&tree, "test_btree_ops.idx");
    for (i = 1; i <= 80; ++i) {
        put_value(&tree, i);
    }
    for (i = 1; i <= 80; ++i) {
        CHECK(btree_delete(&tree, i) == 1);
        assert_btree_valid(&tree);
    }
    CHECK(tree.count == 0);
    CHECK(btree_root(&tree) != NULL);
    CHECK(btree_root(&tree)->key_count == 0);
    CHECK(btree_root(&tree)->leaf == true);
    CHECK(btree_delete(&tree, 1) == 0);
    put_value(&tree, 42);
    CHECK(tree.count == 1);
    CHECK(btree_get(&tree, 42, NULL) == 1);
    assert_btree_valid(&tree);
    btree_destroy(&tree);
    printf("  ok  suppression totale puis reutilisation\n");
}

static void test_aleatoire(void) {
    enum { KEY_SPACE = 4000, OPERATIONS = 12000 };
    static bool present[KEY_SPACE];
    static DbRecord records[KEY_SPACE];
    BTree tree;
    size_t expected = 0;
    unsigned int seed = 12345u;
    int step;
    int k;

    btree_init(&tree, "test_btree_ops.idx");
    memset(present, 0, sizeof(present));

    for (step = 0; step < OPERATIONS; ++step) {
        int key;
        seed = seed * 1103515245u + 12345u;
        key = (int)((seed >> 8) % KEY_SPACE);
        if ((step % 3) == 0) {
            int removed = btree_delete(&tree, key);
            CHECK(removed == (present[key] ? 1 : 0));
            if (present[key]) {
                present[key] = false;
                expected--;
            }
        } else {
            put_value(&tree, key);
            if (!present[key]) {
                present[key] = true;
                expected++;
            }
        }
        if ((step % 500) == 0) {
            assert_btree_valid(&tree);
            CHECK(tree.count == expected);
        }
    }
    assert_btree_valid(&tree);
    CHECK(tree.count == expected);

    {
        size_t count = in_order(&tree, records, KEY_SPACE);
        size_t position = 0;
        CHECK(count == expected);
        for (k = 0; k < KEY_SPACE; ++k) {
            CHECK(btree_get(&tree, k, NULL) == (present[k] ? 1 : 0));
            if (present[k]) {
                CHECK(records[position].key == k);
                position++;
            }
        }
        CHECK(position == expected);
    }
    btree_destroy(&tree);
    printf("  ok  operations aleatoires (12000 operations, %d cles finales)\n", (int)expected);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Tests des algorithmes du B-tree\n");
    test_insertion_et_recherche();
    test_mise_a_jour();
    test_parcours_trie();
    test_suppression_feuille();
    test_suppression_noeuds_internes();
    test_suppression_totale();
    test_aleatoire();
    printf("Tous les tests B-tree sont passes.\n");
    return 0;
}