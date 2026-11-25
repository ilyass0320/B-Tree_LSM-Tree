#include "btree_internal.h"

#include <stdlib.h>
#include <string.h>

/* Retourne la longueur lue, avec limit si aucun '\0' n'a été rencontré. */
static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }

    while (length < limit && text[length] != '\0') {
        ++length;
    }

    return length;
}

void record_init(DbRecord *dst, int key, const char *value)
{
    DbRecord record = { .key = key };
    size_t length;

    if (dst == NULL) {
        return;
    }

    length = bounded_length(value, sizeof record.value - 1);
    if (length > 0) {
        memcpy(record.value, value, length);
    }

    /* Le tableau est déjà terminé par '\0', même après troncature. */
    *dst = record;
}

BTreeNode *btree_node_create(bool leaf)
{
    BTreeNode *node = malloc(sizeof *node);

    if (node != NULL) {
        *node = (BTreeNode){ .leaf = leaf };
    }

    return node;
}

void btree_node_destroy(BTreeNode *node)
{
    if (node == NULL) {
        return;
    }

    if (!node->leaf) {
        /* Un nœud interne possède key_count + 1 emplacements actifs. */
        for (size_t i = 0; i <= node->key_count; ++i) {
            btree_node_destroy(node->children[i]);
        }
    }

    free(node);
}

int btree_init(BTree *tree, const char *path)
{
    BTree initial = { 0 };
    size_t length;

    if (tree == NULL) {
        return -1;
    }

    length = bounded_length(path, sizeof initial.path);
    if (length > 0 && length < sizeof initial.path) {
        initial.root = btree_node_create(true);
        if (initial.root != NULL) {
            memcpy(initial.path, path, length + 1);
        }
    }

    /* En cas d'échec, initial reste vide et peut être détruit. */
    *tree = initial;
    return initial.root != NULL ? 0 : -1;
}

void btree_destroy(BTree *tree)
{
    if (tree == NULL) {
        return;
    }

    btree_node_destroy(tree->root);
    *tree = (BTree){ 0 };
}

const BTreeNode *btree_root(const BTree *tree)
{
    return tree == NULL ? NULL : tree->root;
}

void btree_reset_stats(BTree *tree)
{
    if (tree != NULL) {
        tree->stats = (BTreeStats){ 0 };
    }
}