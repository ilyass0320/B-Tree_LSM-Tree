#include "lsm.h"

// Initialize MemTable
void memtable_init(MemTable *table) {
    table->size = 0;
}

// Insert or update a key-value pair
bool memtable_put(MemTable *table, const char *key, const char *value) {

    if (table == NULL || key == NULL || value == NULL)
        return false;

    if (strlen(key) >= KEY_SIZE || strlen(value) >= VALUE_SIZE)
        return false;

    // Check if key already exists
    for (int i = 0; i < table->size; i++) {

        if (strcmp(table->entries[i].key, key) == 0) {

            strcpy(table->entries[i].value, value);
            table->entries[i].is_deleted = false;

            return true;
        }
    }

    // Check if MemTable is full
    if (table->size >= MAX_ENTRIES)
        return false;

    // Insert new entry
    strcpy(table->entries[table->size].key, key);
    strcpy(table->entries[table->size].value, value);

    table->entries[table->size].is_deleted = false;

    table->size++;

    return true;
}

// Search for a value using its key
bool memtable_get(MemTable *table, const char *key, char *result) {

    if (table == NULL || key == NULL || result == NULL)
        return false;

    for (int i = 0; i < table->size; i++) {

        if (strcmp(table->entries[i].key, key) == 0) {

            if (table->entries[i].is_deleted)
                return false;

            strcpy(result, table->entries[i].value);

            return true;
        }
    }

    return false;
}

// Delete an entry using a tombstone
bool memtable_delete(MemTable *table, const char *key) {

    if (table == NULL || key == NULL)
        return false;

    for (int i = 0; i < table->size; i++) {

        if (strcmp(table->entries[i].key, key) == 0) {

            if (table->entries[i].is_deleted)
                return false;

            table->entries[i].is_deleted = true;

            return true;
        }
    }

    return false;
}

// Display all entries
void memtable_display(const MemTable *table) {

    if (table == NULL)
        return;

    printf("\n--- MemTable ---\n");

    for (int i = 0; i < table->size; i++) {

        printf("Key: %s | ", table->entries[i].key);

        if (table->entries[i].is_deleted)
            printf("Value: [DELETED]\n");
        else
            printf("Value: %s\n", table->entries[i].value);
    }

    printf("----------------\n");
}
   
// Save MemTable entries to disk
bool memtable_flush(MemTable *table, const char *filename) {

    if (table == NULL || filename == NULL)
        return false;

    // Open file in append mode
    FILE *file = fopen(filename, "a");

    if (file == NULL) {
        printf("Error: Cannot open SSTable file!\n");
        return false;
    }

    // Write all entries to disk
    for (int i = 0; i < table->size; i++) {

        fprintf(file, "%s %s %d\n",
                table->entries[i].key,
                table->entries[i].value,
                table->entries[i].is_deleted);
    }

    // Close file
    if (fclose(file) != 0)
        return false;

    // Clear MemTable after successful flush
    table->size = 0;

    printf("MemTable flushed successfully!\n");

    return true;
}    

// Load data from SSTable file into MemTable
bool memtable_load(MemTable *table, const char *filename) {

    if (table == NULL || filename == NULL)
        return false;

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        printf("Error: Cannot open SSTable file!\n");
        return false;
    }

    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    int deleted;

    while (fscanf(file, "%49s %199s %d",
                  key, value, &deleted) == 3) {

        int found = -1;

        // Check if key already exists
        for (int i = 0; i < table->size; i++) {
            if (strcmp(table->entries[i].key, key) == 0) {
                found = i;
                break;
            }
        }

        int index;

        if (found != -1) {
            index = found;
        } else {
            if (table->size >= MAX_ENTRIES) {
                fclose(file);
                return false;
            }

            index = table->size;
            table->size++;
        }

        strcpy(table->entries[index].key, key);
        strcpy(table->entries[index].value, value);
        table->entries[index].is_deleted = (deleted != 0);
    }

    if (ferror(file)) {
        fclose(file);
        return false;
    }

    fclose(file);

    printf("Data loaded successfully!\n");

    return true;
}

