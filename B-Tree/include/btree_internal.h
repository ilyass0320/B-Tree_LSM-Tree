#ifndef INDEXLAB_BTREE_INTERNAL_H
#define INDEXLAB_BTREE_INTERNAL_H

#include "btree.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Fonctions partagees entre les deux parties du projet :
 *   btree.c ... btree_storage.c : structure, noeuds, memoire, persistance
 *   btree_operations.c          : insertion, recherche, suppression, split, merge, parcours
 */

BTreeNode *btree_node_create(bool leaf);
void btree_node_destroy(BTreeNode *node);
void btree_node_split_child(BTree *tree, BTreeNode *parent, size_t index);
int btree_node_put_nonfull(BTree *tree, BTreeNode *node, const DbRecord *record);
BTreeNode *btree_find_node(BTree *tree, BTreeNode *node, int key, size_t *index_out);
size_t btree_collect_records(const BTreeNode *node, DbRecord *out, size_t capacity, size_t count);

#endif