#ifndef LSM_H
#define LSM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Maximum number of entries in MemTable
#define MAX_ENTRIES 100

// Maximum size of a key
#define KEY_SIZE 50

// Maximum size of a value
#define VALUE_SIZE 200

// Structure representing one key-value pair
typedef struct {
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    bool is_deleted;
} Entry;

// Structure representing MemTable
typedef struct {
    Entry entries[MAX_ENTRIES];
    int size;
} MemTable;

// Function declarations
void memtable_init(MemTable *table);

bool memtable_put(MemTable *table, const char *key, const char *value);

bool memtable_get(MemTable *table, const char *key, char *result);

bool memtable_delete(MemTable *table, const char *key);

void memtable_display(const MemTable *table);

bool memtable_flush(MemTable *table, const char *filename);

bool memtable_load(MemTable *table, const char *filename);

#endif