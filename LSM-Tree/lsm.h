#ifndef LSM_H
#define LSM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Nombre maximum d'entrées dans la MemTable (flush automatique à cette limite)
#define MAX_ENTRIES 10000

// Taille maximale d'une clé (caractères + '\0')
#define KEY_SIZE 500

// Taille maximale d'une valeur (caractères + '\0')
#define VALUE_SIZE 200

// Une paire clé-valeur
typedef struct
{
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    bool is_deleted; // tombstone : true = entrée supprimée
} Entry;

// MemTable : tableau fixe en mémoire (environ 7 Mo)
typedef struct
{
    Entry entries[MAX_ENTRIES];
    int size; // nombre d'entrées utilisées
} MemTable;

// ---------- Fonctions de la MemTable (définies dans lsm.c) ----------
void memtable_init(MemTable *table);
bool memtable_put(MemTable *table, const char *key, const char *value);
bool memtable_get(MemTable *table, const char *key, char *result);
bool memtable_delete(MemTable *table, const char *key);
void memtable_display(const MemTable *table);
bool memtable_flush(MemTable *table, const char *filename);
bool memtable_flush_segment(MemTable *table, const char *path);
bool memtable_load(MemTable *table, const char *filename);

// ---------- Menu (défini dans Lsm_db.c) ----------
void lsm_menu(MemTable *table);

#endif