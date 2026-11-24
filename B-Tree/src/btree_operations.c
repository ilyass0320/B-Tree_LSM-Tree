#include "btree_internal.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Outils internes                                                     */
/* ================================================================== */

/* Premier indice i tel que node->keys[i].key >= key (0..key_count). */
static size_t lower_bound_index(const BTreeNode *node, int key) {
    size_t i = 0;
    while (i < node->key_count && node->keys[i].key < key) {
        ++i;
    }
    return i;
}

/* Retire la cle a la position demandee dans un noeud feuille. */
static void key_remove_at(BTreeNode *node, size_t index) {
    size_t i;
    for (i = index; i + 1 < node->key_count; ++i) {
        node->keys[i] = node->keys[i + 1];
    }
    node->key_count--;
    memset(&node->keys[node->key_count], 0, sizeof(DbRecord));
}

/* ================================================================== */
/* Recherche                                                           */
/* ================================================================== */

BTreeNode *btree_find_node(BTree *tree, BTreeNode *node, int key, size_t *index_out) {
    size_t i = 0;
    if (node == NULL) {
        return NULL;
    }
    tree->stats.reads++;
    while (i < node->key_count && key > node->keys[i].key) {
        tree->stats.comparisons++;
        ++i;
    }
    if (i < node->key_count) {
        tree->stats.comparisons++;
    }
    if (i < node->key_count && key == node->keys[i].key) {
        if (index_out != NULL) {
            *index_out = i;
        }
        return node;
    }
    if (node->leaf) {
        return NULL;
    }
    return btree_find_node(tree, node->children[i], key, index_out);
}

int btree_get(BTree *tree, int key, DbRecord *out) {
    size_t index = 0;
    BTreeNode *node;
    if (tree == NULL) {
        return -1;
    }
    node = btree_find_node(tree, tree->root, key, &index);
    if (node == NULL) {
        return 0;
    }
    if (out != NULL) {
        *out = node->keys[index];
    }
    return 1;
}

/* ================================================================== */
/* Split et insertion                                                  */
/* ================================================================== */

/* Coupe un enfant plein en deux et remonte la cle mediane dans le parent. */
void btree_node_split_child(BTree *tree, BTreeNode *parent, size_t index) {
    BTreeNode *full = parent->children[index];
    BTreeNode *right = btree_node_create(full->leaf);
    size_t j;
    if (right == NULL) {
        return;
    }

    right->key_count = BTREE_MIN_DEGREE - 1;
    for (j = 0; j < BTREE_MIN_DEGREE - 1; ++j) {
        right->keys[j] = full->keys[j + BTREE_MIN_DEGREE];
    }
    if (!full->leaf) {
        for (j = 0; j < BTREE_MIN_DEGREE; ++j) {
            right->children[j] = full->children[j + BTREE_MIN_DEGREE];
            full->children[j + BTREE_MIN_DEGREE] = NULL;
        }
    }
    full->key_count = BTREE_MIN_DEGREE - 1;

    for (j = parent->key_count + 1; j > index + 1; --j) {
        parent->children[j] = parent->children[j - 1];
    }
    parent->children[index + 1] = right;
    for (j = parent->key_count; j > index; --j) {
        parent->keys[j] = parent->keys[j - 1];
    }
    parent->keys[index] = full->keys[BTREE_MIN_DEGREE - 1];
    parent->key_count++;
    tree->stats.splits++;
}

/* Insertion recursive dans un noeud qui n'est pas plein. */
int btree_node_put_nonfull(BTree *tree, BTreeNode *node, const DbRecord *record) {
    size_t i = node->key_count;
    if (node->leaf) {
        while (i > 0 && record->key < node->keys[i - 1].key) {
            node->keys[i] = node->keys[i - 1];
            --i;
        }
        node->keys[i] = *record;
        node->key_count++;
        return 0;
    }

    while (i > 0 && record->key < node->keys[i - 1].key) {
        --i;
    }
    if (node->children[i]->key_count == BTREE_MAX_KEYS) {
        btree_node_split_child(tree, node, i);
        if (record->key > node->keys[i].key) {
            ++i;
        } else if (record->key == node->keys[i].key) {
            node->keys[i] = *record;
            return 1;
        }
    }
    return btree_node_put_nonfull(tree, node->children[i], record);
}

/* Insertion (retour 0) ou mise a jour (retour 1) d'un enregistrement. */
int btree_put(BTree *tree, const DbRecord *record) {
    BTreeNode *root;
    size_t index = 0;
    if (tree == NULL || record == NULL || tree->root == NULL) {
        return -1;
    }
    {
        BTreeNode *found = btree_find_node(tree, tree->root, record->key, &index);
        if (found != NULL) {
            found->keys[index] = *record;
            tree->stats.updates++;
            return 1;
        }
    }
    root = tree->root;
    if (root->key_count == BTREE_MAX_KEYS) {
        BTreeNode *new_root = btree_node_create(false);
        if (new_root == NULL) {
            return -1;
        }
        new_root->children[0] = root;
        tree->root = new_root;
        btree_node_split_child(tree, new_root, 0);
        btree_node_put_nonfull(tree, new_root, record);
    } else {
        btree_node_put_nonfull(tree, root, record);
    }
    tree->count++;
    tree->stats.inserts++;
    return 0;
}

/* ================================================================== */
/* Parcours                                                            */
/* ================================================================== */

static size_t collect_node(const BTreeNode *node, DbRecord *out, size_t capacity, size_t count) {
    size_t i;
    if (node == NULL) {
        return count;
    }
    for (i = 0; i < node->key_count; ++i) {
        if (!node->leaf) {
            count = collect_node(node->children[i], out, capacity, count);
        }
        if (count < capacity) {
            out[count++] = node->keys[i];
        }
    }
    if (!node->leaf) {
        count = collect_node(node->children[node->key_count], out, capacity, count);
    }
    return count;
}

/* Parcours in-order : copie les enregistrements tries dans out. */
size_t btree_collect_records(const BTreeNode *node, DbRecord *out, size_t capacity, size_t count) {
    return collect_node(node, out, capacity, count);
}

static void traverse_node(const BTreeNode *node, BTreeVisitor visit, void *context) {
    size_t i;
    if (node == NULL) {
        return;
    }
    for (i = 0; i < node->key_count; ++i) {
        if (!node->leaf) {
            traverse_node(node->children[i], visit, context);
        }
        visit(&node->keys[i], context);
    }
    if (!node->leaf) {
        traverse_node(node->children[node->key_count], visit, context);
    }
}

/* Parcours in-order avec callback (utilise par les tests et l'export). */
void btree_traverse(const BTree *tree, BTreeVisitor visit, void *context) {
    if (tree == NULL || visit == NULL) {
        return;
    }
    traverse_node(tree->root, visit, context);
}

/* ================================================================== */
/* Suppression : emprunt a un frere (borrow) et fusion (merge)         */
/* ================================================================== */

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

/* Fusionne enfants[index], la cle mediane et enfants[index + 1]. */
static void merge_children(BTree *tree, BTreeNode *parent, size_t index) {
    BTreeNode *left = parent->children[index];
    BTreeNode *right = parent->children[index + 1];
    size_t i;
    size_t left_count;

    (void)tree;
    left_count = left->key_count;

    left->keys[left_count] = parent->keys[index];
    for (i = 0; i < right->key_count; ++i) {
        left->keys[left_count + 1 + i] = right->keys[i];
    }
    if (!left->leaf) {
        for (i = 0; i <= right->key_count; ++i) {
            left->children[left_count + 1 + i] = right->children[i];
        }
    }
    left->key_count = left_count + 1 + right->key_count;

    for (i = index; i + 1 < parent->key_count; ++i) {
        parent->keys[i] = parent->keys[i + 1];
    }
    for (i = index + 1; i < parent->key_count; ++i) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->key_count--;
    /* Le noeud fusionne occupe children[index] : seul le slot situe
       apres le dernier enfant valide doit etre vide. */
    parent->children[parent->key_count + 1] = NULL;
    free(right);
}

/* Emprunte une cle au frere gauche via la cle du parent. */
static void borrow_from_prev(BTreeNode *parent, size_t index) {
    BTreeNode *child = parent->children[index];
    BTreeNode *sibling = parent->children[index - 1];
    size_t i;

    for (i = child->key_count; i > 0; --i) {
        child->keys[i] = child->keys[i - 1];
    }
    if (!child->leaf) {
        for (i = child->key_count + 1; i > 0; --i) {
            child->children[i] = child->children[i - 1];
        }
    }
    child->keys[0] = parent->keys[index - 1];
    if (!child->leaf) {
        child->children[0] = sibling->children[sibling->key_count];
    }
    parent->keys[index - 1] = sibling->keys[sibling->key_count - 1];
    child->key_count++;
    sibling->key_count--;
    if (!sibling->leaf) {
        sibling->children[sibling->key_count + 1] = NULL;
    }
}

/* Emprunte une cle au frere droit via la cle du parent. */
static void borrow_from_next(BTreeNode *parent, size_t index) {
    BTreeNode *child = parent->children[index];
    BTreeNode *sibling = parent->children[index + 1];
    size_t i;

    child->keys[child->key_count] = parent->keys[index];
    if (!child->leaf) {
        child->children[child->key_count + 1] = sibling->children[0];
    }
    parent->keys[index] = sibling->keys[0];
    for (i = 0; i + 1 < sibling->key_count; ++i) {
        sibling->keys[i] = sibling->keys[i + 1];
    }
    if (!sibling->leaf) {
        for (i = 0; i < sibling->key_count; ++i) {
            sibling->children[i] = sibling->children[i + 1];
        }
        sibling->children[sibling->key_count] = NULL;
    }
    sibling->key_count--;
    child->key_count++;
}

static int delete_from_node(BTree *tree, BTreeNode *node, int key);

/* Garantit que enfants[*index] a au moins BTREE_MIN_DEGREE cles avant
   de descendre dedans : emprunte a un frere, sinon fusionne. */
static void ensure_child_capacity(BTree *tree, BTreeNode *node, size_t *index) {
    if (node->children[*index]->key_count >= BTREE_MIN_DEGREE) {
        return;
    }
    if (*index > 0 && node->children[*index - 1]->key_count >= BTREE_MIN_DEGREE) {
        borrow_from_prev(node, *index);
        return;
    }
    if (*index + 1 <= node->key_count &&
        node->children[*index + 1]->key_count >= BTREE_MIN_DEGREE) {
        borrow_from_next(node, *index);
        return;
    }
    if (*index < node->key_count) {
        merge_children(tree, node, *index);
    } else {
        /* Dernier enfant : fusion avec le precedent. */
        merge_children(tree, node, *index - 1);
        (*index)--;
    }
}

/* Supprime une cle situee dans un noeud interne. */
static int delete_from_internal(BTree *tree, BTreeNode *node, size_t index) {
    int key = node->keys[index].key;
    DbRecord replacement;

    if (node->children[index]->key_count >= BTREE_MIN_DEGREE) {
        replacement = *subtree_max(node->children[index]);
        node->keys[index] = replacement;
        return delete_from_node(tree, node->children[index], replacement.key);
    }
    if (node->children[index + 1]->key_count >= BTREE_MIN_DEGREE) {
        replacement = *subtree_min(node->children[index + 1]);
        node->keys[index] = replacement;
        return delete_from_node(tree, node->children[index + 1], replacement.key);
    }
    merge_children(tree, node, index);
    return delete_from_node(tree, node->children[index], key);
}

/* Suppression recursive descendante. */
static int delete_from_node(BTree *tree, BTreeNode *node, int key) {
    size_t index;
    if (node == NULL) {
        return 0;
    }
    index = lower_bound_index(node, key);

    if (index < node->key_count && node->keys[index].key == key) {
        if (node->leaf) {
            key_remove_at(node, index);
            return 1;
        }
        return delete_from_internal(tree, node, index);
    }
    if (node->leaf) {
        return 0;
    }
    ensure_child_capacity(tree, node, &index);
    return delete_from_node(tree, node->children[index], key);
}

int btree_delete(BTree *tree, int key) {
    int removed;
    if (tree == NULL || tree->root == NULL) {
        return -1;
    }
    removed = delete_from_node(tree, tree->root, key);
    if (removed != 1) {
        return 0;
    }
    if (tree->count > 0) {
        tree->count--;
    }
    tree->stats.deletes++;
    if (tree->root->key_count == 0 && !tree->root->leaf) {
        BTreeNode *old_root = tree->root;
        tree->root = old_root->children[0];
        old_root->children[0] = NULL;
        btree_node_destroy(old_root);
    }
    return 1;
}