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

    if (strlen(key) >= KEY_SIZE || strlen(value) >= VALUE_SIZE)
        return false;

    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            strcpy(table->entries[i].value, value);
            table->entries[i].is_deleted = false;
            return true;
        }
    }

    if (table->size >= MAX_ENTRIES)
        return false;

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
            if (table->entries[i].is_deleted)
                return false;

            strcpy(result, table->entries[i].value);
            return true;
        }
    }

    return false;
}

// Supprime une clé en posant un tombstone
bool memtable_delete(MemTable *table, const char *key)
{
    if (table == NULL || key == NULL)
        return false;

    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            if (table->entries[i].is_deleted)
                return false;

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

// Comparaison pour trier les entrées par clé (qsort)
static int comparer_entrees(const void *a, const void *b)
{
    return strcmp(((const Entry *)a)->key, ((const Entry *)b)->key);
}

// Écrit la MemTable triée dans un NOUVEAU fichier segment, puis la vide
bool memtable_flush_segment(MemTable *table, const char *path)
{
    if (table == NULL || path == NULL || table->size == 0)
        return false;

    qsort(table->entries, table->size, sizeof(Entry), comparer_entrees);

    FILE *file = fopen(path, "w");
    if (file == NULL)
        return false;

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
    return true;
}

// Écrit la MemTable à la fin d'un fichier (mode ajout), puis la vide
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
// Ne vide pas la MemTable (utilisée par flush_segment dans Lsm_db.c).
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

    while (fscanf(file, "%499s %199s %d", key, value, &deleted) == 3)
    {
        int index = -1;

        for (int i = 0; i < table->size; i++)
        {
            if (strcmp(table->entries[i].key, key) == 0)
            {
                index = i;
                break;
            }
        }

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

// ====================================================================
// COMPACTAGE = fusion de tous les segments d'UNE table
// ====================================================================

#define COMPACT_PATH_SIZE 128

typedef struct
{
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    int deleted;
    int ordre; // plus grand = plus récent
} LigneCompact;

static void compact_chemin(char *path, size_t size, const char *name, int n)
{
    snprintf(path, size, "data/%s_seg%d.dat", name, n);
}

// Nombre de segments consécutifs existants (1, 2, 3...)
static int compact_nb_segments(const char *name)
{
    char path[COMPACT_PATH_SIZE];
    int n = 0;

    for (;;)
    {
        compact_chemin(path, sizeof(path), name, n + 1);
        FILE *f = fopen(path, "r");
        if (f == NULL)
            return n;
        fclose(f);
        n++;
    }
}

// Tri par clé, puis par ordre de lecture (la plus récente en dernier)
static int comparer_lignes_compact(const void *a, const void *b)
{
    const LigneCompact *x = (const LigneCompact *)a;
    const LigneCompact *y = (const LigneCompact *)b;
    int c = strcmp(x->key, y->key);
    if (c != 0)
        return c;
    return x->ordre - y->ordre;
}

// Lit toutes les lignes des segments 1..nb_seg (du plus ancien au plus récent)
static bool lire_tous_segments(const char *name, int nb_seg,
                               LigneCompact **lignes, size_t *n)
{
    char path[COMPACT_PATH_SIZE];
    size_t capacite = 0;
    *lignes = NULL;
    *n = 0;

    for (int s = 1; s <= nb_seg; s++)
    {
        compact_chemin(path, sizeof(path), name, s);
        FILE *f = fopen(path, "r");
        if (f == NULL)
            continue;

        char k[KEY_SIZE];
        char v[VALUE_SIZE];
        int d;
        while (fscanf(f, "%499s %199s %d", k, v, &d) == 3)
        {
            if (*n == capacite)
            {
                size_t nouvelle = (capacite == 0) ? 1024 : capacite * 2;
                LigneCompact *tmp = realloc(*lignes, nouvelle * sizeof(LigneCompact));
                if (tmp == NULL)
                {
                    fclose(f);
                    free(*lignes);
                    *lignes = NULL;
                    return false;
                }
                *lignes = tmp;
                capacite = nouvelle;
            }
            strcpy((*lignes)[*n].key, k);
            strcpy((*lignes)[*n].value, v);
            (*lignes)[*n].deleted = d;
            (*lignes)[*n].ordre = (int)*n;
            (*n)++;
        }
        fclose(f);
    }
    return true;
}

// Compacte la table 'name' : fusionne tous ses segments, garde la dernière
// version de chaque clé, supprime définitivement les clés supprimées.
// Résultat : segments de SEGMENT_SIZE lignes. Erreur => segments inchangés.
bool lsm_compact_table(const char *name)
{
    if (name == NULL)
        return false;

    char path[COMPACT_PATH_SIZE];
    char tmp_path[COMPACT_PATH_SIZE];
    int nb_seg = compact_nb_segments(name);

    if (nb_seg < 1)
    {
        printf("Aucun segment a compacter pour [%s].\n", name);
        return false;
    }
    if (nb_seg == 1)
        printf("Un seul segment : seuls les doublons et tombstones seront nettoyes.\n");

    // 1) Lecture de tous les segments (fusion)
    LigneCompact *lignes = NULL;
    size_t n = 0;
    if (!lire_tous_segments(name, nb_seg, &lignes, &n))
    {
        printf("Memoire insuffisante.\n");
        return false;
    }
    if (n == 0)
    {
        printf("Segments vides, rien a compacter.\n");
        free(lignes);
        return false;
    }

    // 2) Tri : les versions d'une même clé deviennent consécutives
    qsort(lignes, n, sizeof(LigneCompact), comparer_lignes_compact);

    // 3) Écriture des clés vivantes dans des fichiers temporaires
    FILE *out = NULL;
    int nb_tmp = 0, ecrits = 0;
    size_t vivantes = 0;
    bool erreur = false;

    for (size_t i = 0; i < n; i++)
    {
        if (i + 1 < n && strcmp(lignes[i].key, lignes[i + 1].key) == 0)
            continue; // ancienne version
        if (lignes[i].deleted)
            continue; // clé supprimée

        if (out == NULL || ecrits >= SEGMENT_SIZE)
        {
            if (out != NULL)
                fclose(out);
            nb_tmp++;
            snprintf(tmp_path, sizeof(tmp_path), "data/%s_cmp%d.tmp", name, nb_tmp);
            out = fopen(tmp_path, "w");
            if (out == NULL)
            {
                erreur = true;
                break;
            }
            ecrits = 0;
        }

        fprintf(out, "%s %s 0\n", lignes[i].key, lignes[i].value);
        ecrits++;
        vivantes++;
    }
    if (out != NULL)
        fclose(out);
    free(lignes);

    if (erreur)
    {
        for (int t = 1; t <= nb_tmp; t++)
        {
            snprintf(tmp_path, sizeof(tmp_path), "data/%s_cmp%d.tmp", name, t);
            remove(tmp_path);
        }
        printf("Echec du compactage, segments inchanges.\n");
        return false;
    }

    // 4) Suppression des anciens segments de cette table
    for (int s = 1; s <= nb_seg; s++)
    {
        compact_chemin(path, sizeof(path), name, s);
        remove(path);
    }

    // 5) Renommage des temporaires en segments 1, 2, 3...
    for (int t = 1; t <= nb_tmp; t++)
    {
        snprintf(tmp_path, sizeof(tmp_path), "data/%s_cmp%d.tmp", name, t);
        compact_chemin(path, sizeof(path), name, t);
        if (rename(tmp_path, path) != 0)
            printf("Echec du renommage de %s\n", tmp_path);
    }

    printf("Compactage termine pour [%s] : %d segment(s) fusionne(s) -> %d segment(s), "
           "%d ligne(s) -> %d entree(s) vivante(s).\n",
           name, nb_seg, nb_tmp, (int)n, (int)vivantes);

    if (nb_tmp == 0)
        printf("Toutes les cles etaient supprimees : la table n'a plus de segment.\n");

    return true;
}