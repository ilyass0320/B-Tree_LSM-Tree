#include "lsm.h"

// Initialise la MemTable (aucune entrée au départ)
void memtable_init(MemTable *table)
{
    if (table != NULL)
        table->size = 0;
}

// Insère une paire clé-valeur, ou met à jour la valeur si la clé existe déjà
bool memtable_put(MemTable *table, const char *key, const char *value)
{
    if (table == NULL || key == NULL || value == NULL)
        return false;

    // Refuse les clés et valeurs trop longues
    if (strlen(key) >= KEY_SIZE || strlen(value) >= VALUE_SIZE)
        return false;

    // Mise à jour si la clé existe déjà
    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            strcpy(table->entries[i].value, value);
            table->entries[i].is_deleted = false; // réactive une clé supprimée
            return true;
        }
    }

    // La table a atteint sa limite ?
    if (table->size >= MAX_ENTRIES)
        return false;

    // Insertion d'une nouvelle entrée à la fin
    strcpy(table->entries[table->size].key, key);
    strcpy(table->entries[table->size].value, value);
    table->entries[table->size].is_deleted = false;
    table->size++;

    return true;
}

// Cherche la valeur associée à une clé (copiée dans result)
bool memtable_get(MemTable *table, const char *key, char *result)
{
    if (table == NULL || key == NULL || result == NULL)
        return false;

    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            // Une entrée supprimée (tombstone) est considérée comme absente
            if (table->entries[i].is_deleted)
                return false;

            strcpy(result, table->entries[i].value);
            return true;
        }
    }

    return false;
}

// Supprime une clé en posant un tombstone (l'entrée reste mais est marquée)
bool memtable_delete(MemTable *table, const char *key)
{
    if (table == NULL || key == NULL)
        return false;

    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            if (table->entries[i].is_deleted)
                return false; // déjà supprimée

            table->entries[i].is_deleted = true;
            return true;
        }
    }

    return false;
}

// Affiche toutes les entrées de la MemTable
void memtable_display(const MemTable *table)
{
    if (table == NULL)
        return;

    printf("\n--- MemTable ---\n");

    for (int i = 0; i < table->size; i++)
    {
        printf("Key: %s | ", table->entries[i].key);

        if (table->entries[i].is_deleted)
            printf("Value: [DELETED]\n");
        else
            printf("Value: %s\n", table->entries[i].value);
    }

    printf("----------------\n");
}

// Fonction de comparaison pour trier les entrées par clé (utilisée par qsort)
static int comparer_entrees(const void *a, const void *b)
{
    return strcmp(((const Entry *)a)->key, ((const Entry *)b)->key);
}

// Écrit la MemTable triée dans un NOUVEAU fichier segment, puis la vide
bool memtable_flush_segment(MemTable *table, const char *path)
{
    if (table == NULL || path == NULL || table->size == 0)
        return false;

    // Un segment (SSTable) est trié par clé
    qsort(table->entries, table->size, sizeof(Entry), comparer_entrees);

    FILE *file = fopen(path, "w"); // "w" : un segment = un fichier neuf
    if (file == NULL)
        return false;

    // Format de chaque ligne : clé valeur is_deleted
    for (int i = 0; i < table->size; i++)
    {
        fprintf(file, "%s %s %d\n",
                table->entries[i].key,
                table->entries[i].value,
                table->entries[i].is_deleted);
    }

    if (fclose(file) != 0)
        return false;

    table->size = 0; // MemTable vidée après un flush réussi
    return true;
}

// Écrit la MemTable à la fin d'un fichier existant (mode ajout), puis la vide
bool memtable_flush(MemTable *table, const char *filename)
{
    if (table == NULL || filename == NULL)
        return false;

    FILE *file = fopen(filename, "a");

    if (file == NULL)
    {
        printf("Erreur : impossible d'ouvrir le fichier !\n");
        return false;
    }

    for (int i = 0; i < table->size; i++)
    {
        fprintf(file, "%s %s %d\n",
                table->entries[i].key,
                table->entries[i].value,
                table->entries[i].is_deleted);
    }

    if (fclose(file) != 0)
        return false;

    table->size = 0;

    printf("MemTable ecrite sur le disque avec succes !\n");
    return true;
}

// Ajoute à la fin d'un fichier 'nombre' entrées de la MemTable, à partir de 'debut'.
// Ne vide pas la MemTable.
// Les données vont d'abord dans le dernier segment de la table (même nom), jusqu'à MAX_ENTRIES lignes.
// S'il reste des données et que le segment est plein, un nouveau segment est créé.
bool memtable_append_range(const MemTable *table, const char *path, int debut, int nombre)
{
    if (table == NULL || path == NULL || debut < 0 || nombre <= 0 ||
        debut + nombre > table->size)
        return false;

    FILE *file = fopen(path, "a");
    if (file == NULL)
        return false;

    for (int i = debut; i < debut + nombre; i++)
    {
        fprintf(file, "%s %s %d\n",
                table->entries[i].key,
                table->entries[i].value,
                table->entries[i].is_deleted);
    }

    return fclose(file) == 0;
}
// Charge un fichier segment dans la MemTable (la valeur lue écrase l'ancienne)
bool memtable_load(MemTable *table, const char *filename)
{
    if (table == NULL || filename == NULL)
        return false;

    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        printf("Erreur : impossible d'ouvrir le fichier !\n");
        return false;
    }

    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    int deleted;

    // Lit chaque ligne : clé valeur is_deleted
    while (fscanf(file, "%499s %199s %d", key, value, &deleted) == 3)
    {
        int index = -1;

        // La clé existe déjà dans la MemTable ?
        for (int i = 0; i < table->size; i++)
        {
            if (strcmp(table->entries[i].key, key) == 0)
            {
                index = i;
                break;
            }
        }

        // Nouvelle clé : on ajoute une entrée (si la table n'est pas pleine)
        if (index == -1)
        {
            if (table->size >= MAX_ENTRIES)
            {
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

    if (ferror(file))
    {
        fclose(file);
        return false;
    }

    fclose(file);

    printf("Donnees chargees avec succes !\n");
    return true;
}