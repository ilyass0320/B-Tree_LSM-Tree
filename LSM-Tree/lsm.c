#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h> // lister les fichiers du dossier data
#include <direct.h> // _mkdir (Windows / MinGW)
#include "lsm.h"

#define NAME_SIZE 50  // taille maximale du nom d'une table
#define PATH_SIZE 128 // taille maximale d'un chemin de fichier

// Vérifie le nom d'une table : lettres, chiffres, '_' et '-' uniquement
static bool nom_valide(const char *name)
{
    if (name[0] == '\0' || strlen(name) >= NAME_SIZE)
        return false;
    for (int i = 0; name[i] != '\0'; i++)
    {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-')
            return false;
    }
    return true;
}

// Construit le chemin d'un segment : data/<nom>_seg<n>.dat
static void chemin_segment(char *path, size_t size, const char *name, int n)
{
    snprintf(path, size, "data/%s_seg%d.dat", name, n);
}

// Teste si un fichier existe sur le disque
static bool fichier_existe(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

// Retourne le numéro du prochain segment libre pour une table
static int prochain_segment(const char *name)
{
    char path[PATH_SIZE];
    int n = 1;
    for (;;)
    {
        chemin_segment(path, sizeof(path), name, n);
        if (!fichier_existe(path))
            return n;
        n++;
    }
}

// Compte le nombre d'entrées (lignes) d'un segment
static int compter_lignes(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;

    int lignes = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
    {
        if (c == '\n')
            lignes++;
    }
    fclose(f);
    return lignes;
}

// Écrit la MemTable dans les segments de la table 'name' :
// 1) remplit d'abord le dernier segment existant (jusqu'à MAX_ENTRIES lignes)
// 2) s'il reste des données, crée un nouveau segment
static bool flush_segment(MemTable *table, const char *name)
{
    char path[PATH_SIZE];
    int envoye = 0;

    if (table->size == 0)
        return false;

    int dernier = prochain_segment(name) - 1; // 0 si aucun segment

    // 1) Remplir le dernier segment s'il n'est pas plein
    if (dernier > 0)
    {
        chemin_segment(path, sizeof(path), name, dernier);
        int place = MAX_ENTRIES - compter_lignes(path);

        if (place > 0)
        {
            int n = (table->size < place) ? table->size : place;
            if (!memtable_append_range(table, path, 0, n))
                return false;

            envoye = n;
            printf("%d entree(s) ajoutee(s) au segment : %s\n", n, path);
        }
    }

    // 2) Le reste va dans un nouveau segment
    if (envoye < table->size)
    {
        memmove(table->entries, table->entries + envoye,
                (size_t)(table->size - envoye) * sizeof(Entry));
        table->size -= envoye;

        chemin_segment(path, sizeof(path), name, prochain_segment(name));
        if (!memtable_flush_segment(table, path))
            return false;

        printf("Nouveau segment cree : %s\n", path);
    }
    else
        table->size = 0; // tout est écrit

    return true;
}

// Résultat : 1 = trouvé, 0 = absent, -1 = supprimé (tombstone)
static int chercher_memoire(const MemTable *table, const char *key, char *result)
{
    for (int i = 0; i < table->size; i++)
    {
        if (strcmp(table->entries[i].key, key) == 0)
        {
            if (table->entries[i].is_deleted)
                return -1;
            strcpy(result, table->entries[i].value);
            return 1;
        }
    }
    return 0;
}

// Cherche dans un segment : la DERNIÈRE occurrence de la clé gagne
static int chercher_dans_segment(const char *path, const char *key, char *result)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;

    char k[KEY_SIZE];
    char v[VALUE_SIZE];
    int supprime;
    int etat = 0;

    while (fscanf(f, "%499s %199s %d", k, v, &supprime) == 3)
    {
        if (strcmp(k, key) == 0)
        {
            if (supprime)
                etat = -1;
            else
            {
                strcpy(result, v);
                etat = 1;
            }
        }
    }
    fclose(f);
    return etat;
}

// Parcourt les segments du plus récent au plus ancien.
// Retourne le numéro du segment où la clé est trouvée, 0 sinon.
static int chercher_disque(const char *name, const char *key, char *result)
{
    char path[PATH_SIZE];

    for (int n = prochain_segment(name) - 1; n >= 1; n--)
    {
        chemin_segment(path, sizeof(path), name, n);
        printf("  Recherche dans le segment %d...\n", n);

        int etat = chercher_dans_segment(path, key, result);
        if (etat == 1)
            return n;
        if (etat == -1)
            return 0; // supprimée : le segment plus récent l'emporte
    }
    return 0;
}
// Affiche le contenu d'un segment (une ligne par entrée)
static int afficher_segment(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;

    char k[KEY_SIZE];
    char v[VALUE_SIZE];
    int supprime;
    int count = 0;

    while (fscanf(f, "%499s %199s %d", k, v, &supprime) == 3)
    {
        if (supprime)
            printf("  Key: %s | Value: [DELETED]\n", k);
        else
            printf("  Key: %s | Value: %s\n", k, v);
        count++;
    }
    fclose(f);
    return count;
}

// Affiche une table : memoire (si table courante) puis segments du 1er au dernier
static void afficher_table(const MemTable *table, const char *cible, bool courante)
{
    char path[PATH_SIZE];

    printf("\n===== Table [%s] =====\n", cible);

    if (courante)
    {
        printf("--- Memoire (%d entree(s)) ---\n", table->size);
        for (int i = 0; i < table->size; i++)
        {
            if (table->entries[i].is_deleted)
                printf("  Key: %s | Value: [DELETED]\n", table->entries[i].key);
            else
                printf("  Key: %s | Value: %s\n", table->entries[i].key, table->entries[i].value);
        }
    }

    int nb = prochain_segment(cible) - 1;
    if (nb == 0)
        printf("(aucun segment sur le disque)\n");

    for (int n = 1; n <= nb; n++)
    {
        chemin_segment(path, sizeof(path), cible, n);
        printf("--- Segment %d ---\n", n);
        if (afficher_segment(path) == 0)
            printf("  (vide)\n");
    }
    printf("======================\n");
}
// Écrit un tombstone directement dans les segments d'une table (sur le disque).
// Utilise le dernier segment s'il a de la place, sinon crée un nouveau segment.
static bool ecrire_tombstone_disque(const char *name, const char *key)
{
    char path[PATH_SIZE];
    int dernier = prochain_segment(name) - 1;

    if (dernier > 0)
    {
        chemin_segment(path, sizeof(path), name, dernier);
        if (compter_lignes(path) >= MAX_ENTRIES)
            dernier = 0; // segment plein : on passe à un nouveau
    }

    if (dernier == 0)
        chemin_segment(path, sizeof(path), name, prochain_segment(name));

    FILE *f = fopen(path, "a");
    if (f == NULL)
        return false;

    // Format d'une ligne : clé valeur is_deleted (la valeur "-" est un simple remplissage)
    fprintf(f, "%s - 1\n", key);
    return fclose(f) == 0;
}
// Une ligne lue dans un segment (utilisée pour le compactage)
typedef struct
{
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    int deleted;
    int ordre; // ordre de lecture : plus grand = plus récent
} Ligne;

// Tri par clé, puis par ordre de lecture
static int comparer_lignes(const void *a, const void *b)
{
    const Ligne *x = (const Ligne *)a;
    const Ligne *y = (const Ligne *)b;
    int c = strcmp(x->key, y->key);
    if (c != 0)
        return c;
    return x->ordre - y->ordre;
}

// Compacte tous les segments d'UNE table : ne garde que la dernière version
// de chaque clé et supprime définitivement les clés supprimées (tombstones).
// Retourne false en cas d'erreur (les anciens segments restent alors intacts).
static bool compacter_table(const char *name)
{
    char path[PATH_SIZE];
    int nb_seg = prochain_segment(name) - 1;

    if (nb_seg < 1)
    {
        printf("Aucun segment a compacter pour [%s].\n", name);
        return false;
    }

    // 1) Lire toutes les lignes, du segment le plus ancien au plus récent
    Ligne *lignes = NULL;
    size_t n = 0, capacite = 0;

    for (int s = 1; s <= nb_seg; s++)
    {
        chemin_segment(path, sizeof(path), name, s);
        FILE *f = fopen(path, "r");
        if (f == NULL)
            continue;

        char k[KEY_SIZE];
        char v[VALUE_SIZE];
        int d;
        while (fscanf(f, "%499s %199s %d", k, v, &d) == 3)
        {
            if (n == capacite)
            {
                size_t nouvelle = (capacite == 0) ? 1024 : capacite * 2;
                Ligne *tmp = realloc(lignes, nouvelle * sizeof(Ligne));
                if (tmp == NULL)
                {
                    printf("Memoire insuffisante.\n");
                    fclose(f);
                    free(lignes);
                    return false;
                }
                lignes = tmp;
                capacite = nouvelle;
            }
            strcpy(lignes[n].key, k);
            strcpy(lignes[n].value, v);
            lignes[n].deleted = d;
            lignes[n].ordre = (int)n;
            n++;
        }
        fclose(f);
    }

    if (n == 0)
    {
        printf("Segments vides, rien a compacter.\n");
        free(lignes);
        return false;
    }

    // 2) Trier : les versions d'une même clé deviennent consécutives
    qsort(lignes, n, sizeof(Ligne), comparer_lignes);

    // 3) Écrire les clés vivantes dans des fichiers temporaires
    char tmp_path[PATH_SIZE];
    FILE *out = NULL;
    int nb_tmp = 0, ecrits = 0;
    size_t vivantes = 0;
    bool erreur = false;

    for (size_t i = 0; i < n; i++)
    {
        // Ce n'est pas la dernière version de la clé : on l'ignore
        if (i + 1 < n && strcmp(lignes[i].key, lignes[i + 1].key) == 0)
            continue;

        // Dernière version supprimée : la clé disparaît
        if (lignes[i].deleted)
            continue;

        // Nouveau fichier temporaire si besoin (premier ou segment plein)
        if (out == NULL || ecrits >= MAX_ENTRIES)
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

    // En cas d'erreur : on supprime les temporaires, les anciens segments restent
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

    // 4) Supprimer les anciens segments de cette table
    for (int s = 1; s <= nb_seg; s++)
    {
        chemin_segment(path, sizeof(path), name, s);
        remove(path);
    }

    // 5) Renommer les temporaires en segments 1, 2, 3...
    for (int t = 1; t <= nb_tmp; t++)
    {
        snprintf(tmp_path, sizeof(tmp_path), "data/%s_cmp%d.tmp", name, t);
        chemin_segment(path, sizeof(path), name, t);
        if (rename(tmp_path, path) != 0)
            printf("Echec du renommage de %s\n", tmp_path);
    }

    printf("Compactage termine pour [%s] : %d segment(s) -> %d segment(s), "
           "%d ligne(s) -> %d entree(s) vivante(s).\n",
           name, nb_seg, nb_tmp, (int)n, (int)vivantes);

    if (nb_tmp == 0)
        printf("Toutes les cles etaient supprimees : la table n'a plus de segment.\n");

    return true;
}
// Affiche la vue compactée d'une table (sans modifier le disque) :
// dernière version de chaque clé, clés supprimées exclues.
// La mémoire (table courante) est la plus récente, donc elle est lue en dernier.
static void afficher_vue_compactee(const MemTable *table, const char *cible, bool courante)
{
    char path[PATH_SIZE];
    Ligne *lignes = NULL;
    size_t n = 0, capacite = 0;
    int nb_seg = prochain_segment(cible) - 1;

    // 1) Segments, du plus ancien au plus récent
    for (int s = 1; s <= nb_seg; s++)
    {
        chemin_segment(path, sizeof(path), cible, s);
        FILE *f = fopen(path, "r");
        if (f == NULL)
            continue;

        char k[KEY_SIZE];
        char v[VALUE_SIZE];
        int d;
        while (fscanf(f, "%499s %199s %d", k, v, &d) == 3)
        {
            if (n == capacite)
            {
                size_t nouvelle = (capacite == 0) ? 1024 : capacite * 2;
                Ligne *tmp = realloc(lignes, nouvelle * sizeof(Ligne));
                if (tmp == NULL)
                {
                    printf("Memoire insuffisante.\n");
                    fclose(f);
                    free(lignes);
                    return;
                }
                lignes = tmp;
                capacite = nouvelle;
            }
            strcpy(lignes[n].key, k);
            strcpy(lignes[n].value, v);
            lignes[n].deleted = d;
            lignes[n].ordre = (int)n;
            n++;
        }
        fclose(f);
    }

    // 2) Mémoire (table courante) : version la plus récente
    if (courante)
    {
        for (int i = 0; i < table->size; i++)
        {
            if (n == capacite)
            {
                size_t nouvelle = (capacite == 0) ? 1024 : capacite * 2;
                Ligne *tmp = realloc(lignes, nouvelle * sizeof(Ligne));
                if (tmp == NULL)
                {
                    printf("Memoire insuffisante.\n");
                    free(lignes);
                    return;
                }
                lignes = tmp;
                capacite = nouvelle;
            }
            strcpy(lignes[n].key, table->entries[i].key);
            strcpy(lignes[n].value, table->entries[i].value);
            lignes[n].deleted = table->entries[i].is_deleted;
            lignes[n].ordre = (int)n;
            n++;
        }
    }

    printf("\n===== Vue compactee de [%s] =====\n", cible);

    if (n == 0)
    {
        printf("  (aucune donnee)\n");
        printf("=================================\n");
        return;
    }

    // 3) Tri par clé puis par ordre : la dernière version est en fin de groupe
    qsort(lignes, n, sizeof(Ligne), comparer_lignes);

    int vivantes = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (i + 1 < n && strcmp(lignes[i].key, lignes[i + 1].key) == 0)
            continue; // ancienne version
        if (lignes[i].deleted)
            continue; // clé supprimée
        printf("  Key: %s | Value: %s\n", lignes[i].key, lignes[i].value);
        vivantes++;
    }

    if (vivantes == 0)
        printf("  (toutes les cles sont supprimees)\n");
    printf("---------------------------------\n");
    printf("%d cle(s) valide(s) sur %d ligne(s) lue(s)\n", vivantes, (int)n);
    printf("=================================\n");

    free(lignes);
}
// Demande un nom à l'utilisateur jusqu'à ce qu'il soit valide
static void demander_nom(char *name)
{
    do
    {
        printf("Nom de la table : ");
        if (scanf("%49s", name) != 1)
            name[0] = '\0';
        if (!nom_valide(name))
            printf("Nom invalide, reessayez.\n");
    } while (!nom_valide(name));
}

// Affiche la table en mémoire (table courante) puis les tables sur le disque.
// Retourne le nombre de tables trouvées sur le disque.
static int lister_tables(const MemTable *table, const char *courante, bool nomme)
{
    char path[PATH_SIZE];
    bool sur_disque = false;

    // ---------- 1. Table en mémoire ----------
    printf("\n--- Table en memoire ---\n");
    if (nomme)
    {
        chemin_segment(path, sizeof(path), courante, 1);
        sur_disque = fichier_existe(path);

        printf("[%s] : %d entree(s) en memoire%s\n", courante, table->size,
               sur_disque ? "" : "  (pas encore sur le disque)");
    }
    else
        printf("(aucune table nommee)\n");

    // ---------- 2. Tables sur le disque ----------
    printf("\n--- Tables sur le disque ---\n");

    DIR *dir = opendir("data");
    if (dir == NULL)
    {
        printf("(dossier data introuvable)\n");
        printf("----------------------------\n");
        return 0;
    }

    const char *suffixe = "_seg1.dat";
    size_t len_suffixe = strlen(suffixe);
    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        size_t len = strlen(entry->d_name);

        // Garde uniquement les fichiers qui se terminent par "_seg1.dat"
        if (len > len_suffixe &&
            strcmp(entry->d_name + len - len_suffixe, suffixe) == 0)
        {
            char nom[NAME_SIZE];
            size_t len_nom = len - len_suffixe;
            if (len_nom >= NAME_SIZE)
                continue;

            memcpy(nom, entry->d_name, len_nom);
            nom[len_nom] = '\0';

            count++;
            printf("%d. %s  (%d segment(s))%s\n", count, nom,
                   prochain_segment(nom) - 1,
                   (nomme && strcmp(nom, courante) == 0) ? "  <- table courante (en memoire)" : "");
        }
    }
    closedir(dir);

    if (count == 0)
        printf("(aucune table)\n");
    printf("----------------------------\n");
    return count;
}

// Change de table courante. Les données en mémoire appartiennent à l'ancienne
// table : on les sauvegarde dans un segment avant de changer.
// Retourne false si la sauvegarde échoue (on reste alors sur l'ancienne table).
static bool basculer_table(MemTable *table, char *db_name, bool *nomme, const char *nouveau)
{
    // Déjà sur cette table : rien à faire
    if (*nomme && strcmp(db_name, nouveau) == 0)
        return true;

    if (table->size > 0)
    {
        if (*nomme)
        {
            printf("Sauvegarde des donnees de [%s]...\n", db_name);
            if (!flush_segment(table, db_name))
            {
                printf("Echec de la sauvegarde, changement annule.\n");
                return false;
            }
        }
        else
            table->size = 0;
    }

    strcpy(db_name, nouveau);
    *nomme = true;
    return true;
}

// Menu principal de la base LSM-Tree
void lsm_menu(MemTable *table)
{
    int choice;
    char Key[KEY_SIZE];
    char value[VALUE_SIZE];
    char result[VALUE_SIZE];

    char db_name[NAME_SIZE] = "memtable"; // nom de la table courante
    bool nomme = false;                   // le nom a-t-il été choisi ?

    _mkdir("data"); // crée le dossier data s'il n'existe pas

    do
    {
        printf("\n========== LSM-TREE DATABASE [%s] ==========\n", db_name);
        printf("1. Insert data\n");
        printf("2. Search data\n");
        printf("3. Delete data\n");
        printf("4. Display database\n");
        printf("5. Flush MemTable of [%s] (save to disk)\n", db_name);
        printf("6. Load segments (from disk)\n");
        printf("7. Rename database\n");
        printf("8. List tables\n");
        printf("9. Select table\n");
        printf("10. Compact table\n");
        printf("0. Back to main menu\n");

        printf("\nChoose operation: ");
        if (scanf("%d", &choice) != 1)
        {
            while (getchar() != '\n')
                ;
            choice = -1;
        }

        switch (choice)
        {
        case 1: // Insertion : verification memoire + disque a chaque fois
        {
            bool ok = true;
            int dest = 0;

            int nb_disque = lister_tables(table, db_name, nomme);

            if (!nomme && nb_disque == 0)
            {
                printf("Aucune table existante. Creation de la premiere table.\n");
                demander_nom(db_name);
                nomme = true;
            }
            else
            {
                printf("\nOu voulez-vous inserer cette donnee ?\n");
                if (nomme)
                    printf("1. Dans la table courante [%s]\n", db_name);
                if (nb_disque > 0)
                    printf("2. Dans une autre table existante\n");
                printf("3. Dans une nouvelle table\n");
                printf("Votre choix : ");
                if (scanf("%d", &dest) != 1)
                {
                    while (getchar() != '\n')
                        ;
                    dest = 0;
                }

                if (dest == 1 && nomme)
                {
                    // on reste sur la table courante
                }
                else if (dest == 2 && nb_disque > 0)
                {
                    char choisi[NAME_SIZE];
                    char path[PATH_SIZE];

                    printf("Nom de la table existante : ");
                    scanf("%49s", choisi);

                    chemin_segment(path, sizeof(path), choisi, 1);
                    if (!nom_valide(choisi) || !fichier_existe(path))
                    {
                        printf("Table '%s' introuvable.\n", choisi);
                        ok = false;
                    }
                    else if (!basculer_table(table, db_name, &nomme, choisi))
                        ok = false;
                    else
                        printf("Table [%s] selectionnee.\n", db_name);
                }
                else if (dest == 3)
                {
                    char nouveau[NAME_SIZE];
                    char path[PATH_SIZE];

                    demander_nom(nouveau);
                    chemin_segment(path, sizeof(path), nouveau, 1);

                    if (fichier_existe(path))
                    {
                        printf("La table '%s' existe deja. Choisissez l'option 2.\n", nouveau);
                        ok = false;
                    }
                    else if (!basculer_table(table, db_name, &nomme, nouveau))
                        ok = false;
                    else
                        printf("Nouvelle table [%s] creee.\n", db_name);
                }
                else
                {
                    printf("Choix invalide, insertion annulee.\n");
                    ok = false;
                }
            }

            if (!ok)
                break;

            printf("Key : ");
            scanf("%499s", Key);
            printf("Valeur : ");
            scanf("%199s", value);

            if (memtable_put(table, Key, value))
            {
                printf("Insertion reussie dans [%s] (%d/%d)\n", db_name, table->size, MAX_ENTRIES);

                // Limite atteinte : flush automatique
                if (table->size >= MAX_ENTRIES)
                {
                    printf("MemTable pleine : flush automatique...\n");
                    if (!flush_segment(table, db_name))
                        printf("Echec du flush automatique\n");
                }
            }
            else
                printf("Insertion echouee\n");
            break;
        }

        case 2: // Recherche : memoire puis segments (du dernier au premier)
        {
            char cible[NAME_SIZE];
            int dest = 1;
            int nb_disque = lister_tables(table, db_name, nomme);

            if (!nomme && nb_disque == 0)
            {
                printf("Aucune table a explorer.\n");
                break;
            }

            printf("\nOu chercher ?\n");
            if (nomme)
                printf("1. Dans la table courante [%s]\n", db_name);
            if (nb_disque > 0)
                printf("2. Dans une autre table (sur le disque)\n");
            printf("Votre choix : ");
            if (scanf("%d", &dest) != 1)
            {
                while (getchar() != '\n')
                    ;
                dest = 0;
            }

            if (dest == 1 && nomme)
                strcpy(cible, db_name);
            else if (dest == 2 && nb_disque > 0)
            {
                char path[PATH_SIZE];
                printf("Nom de la table : ");
                scanf("%49s", cible);
                chemin_segment(path, sizeof(path), cible, 1);
                if (!nom_valide(cible) || !fichier_existe(path))
                {
                    printf("Table '%s' introuvable.\n", cible);
                    break;
                }
            }
            else
            {
                printf("Choix invalide.\n");
                break;
            }

            printf("Key a chercher : ");
            scanf("%499s", Key);

            // 1) Memoire : uniquement pour la table courante
            if (nomme && strcmp(cible, db_name) == 0)
            {
                int etat = chercher_memoire(table, Key, result);
                if (etat == 1)
                {
                    printf("Key %s trouvee en MEMOIRE [%s], valeur : %s\n", Key, cible, result);
                    break;
                }
                if (etat == -1)
                {
                    printf("Key %s a ete supprimee (tombstone en memoire).\n", Key);
                    break;
                }
                printf("Non trouvee en memoire, recherche dans les segments...\n");
            }

            // 2) Disque : du dernier segment au premier
            int seg = chercher_disque(cible, Key, result);
            if (seg > 0)
                printf("Key %s trouvee dans le segment %d de [%s], valeur : %s\n", Key, seg, cible, result);
            else
                printf("Key %s n'est pas trouvee dans [%s].\n", Key, cible);
            break;
        }

        case 3: // Suppression : choix de la table, memoire puis segments
        {
            char cible[NAME_SIZE];
            int dest = 0;
            int nb_disque = lister_tables(table, db_name, nomme);

            if (!nomme && nb_disque == 0)
            {
                printf("Aucune table a explorer.\n");
                break;
            }

            // Question : table courante ou autre table
            printf("\nDans quelle table supprimer ?\n");
            if (nomme)
                printf("1. Dans la table courante [%s]\n", db_name);
            if (nb_disque > 0)
                printf("2. Dans une autre table (sur le disque)\n");
            printf("Votre choix : ");
            if (scanf("%d", &dest) != 1)
            {
                while (getchar() != '\n')
                    ;
                dest = 0;
            }

            if (dest == 1 && nomme)
                strcpy(cible, db_name);
            else if (dest == 2 && nb_disque > 0)
            {
                char path[PATH_SIZE];
                printf("Nom de la table : ");
                scanf("%49s", cible);
                chemin_segment(path, sizeof(path), cible, 1);
                if (!nom_valide(cible) || !fichier_existe(path))
                {
                    printf("Table '%s' introuvable.\n", cible);
                    break;
                }
            }
            else
            {
                printf("Choix invalide.\n");
                break;
            }

            printf("Key a supprimer : ");
            scanf("%499s", Key);

            bool courante = (nomme && strcmp(cible, db_name) == 0);

            // 1) Memoire (table courante uniquement)
            if (courante)
            {
                int etat = chercher_memoire(table, Key, result);
                if (etat == 1)
                {
                    memtable_delete(table, Key);
                    printf("Key %s supprimee en MEMOIRE [%s].\n", Key, cible);
                    break;
                }
                if (etat == -1)
                {
                    printf("Key %s est deja supprimee.\n", Key);
                    break;
                }
                printf("Non trouvee en memoire, recherche dans les segments...\n");
            }

            // 2) Disque : du dernier segment au premier
            int seg = chercher_disque(cible, Key, result);
            if (seg == 0)
            {
                printf("Key %s n'existe pas dans [%s].\n", Key, cible);
                break;
            }

            // La cle existe dans un segment : on ecrit un tombstone
            if (courante)
            {
                // Tombstone en memoire (ecrit dans un segment au prochain flush)
                if (memtable_put(table, Key, "-") && memtable_delete(table, Key))
                    printf("Key %s (segment %d) supprimee : tombstone cree en memoire.\n", Key, seg);
                else
                    printf("Echec de la suppression.\n");
            }
            else
            {
                // Autre table : tombstone ecrit directement sur le disque
                if (ecrire_tombstone_disque(cible, Key))
                    printf("Key %s (segment %d) supprimee : tombstone ecrit sur le disque dans [%s].\n", Key, seg, cible);
                else
                    printf("Echec de l'ecriture du tombstone.\n");
            }
            break;
        }
        case 4: // Affichage : choix de la table, memoire + segments
        {
            char cible[NAME_SIZE];
            int dest = 0;
            int nb_disque = lister_tables(table, db_name, nomme);

            if (!nomme && nb_disque == 0)
            {
                printf("Aucune table a afficher.\n");
                break;
            }

            printf("\nQuelle table afficher ?\n");
            if (nomme)
                printf("1. La table courante [%s]\n", db_name);
            if (nb_disque > 0)
                printf("2. Une autre table (sur le disque)\n");
            printf("Votre choix : ");
            if (scanf("%d", &dest) != 1)
            {
                while (getchar() != '\n')
                    ;
                dest = 0;
            }

            if (dest == 1 && nomme)
                strcpy(cible, db_name);
            else if (dest == 2 && nb_disque > 0)
            {
                char path[PATH_SIZE];
                printf("Nom de la table : ");
                scanf("%49s", cible);
                chemin_segment(path, sizeof(path), cible, 1);
                if (!nom_valide(cible) || !fichier_existe(path))
                {
                    printf("Table '%s' introuvable.\n", cible);
                    break;
                }
            }
            else
            {
                printf("Choix invalide.\n");
                break;
            }

            {
                bool courante = (nomme && strcmp(cible, db_name) == 0);
                afficher_table(table, cible, courante);         // contenu brut : memoire + segments
                afficher_vue_compactee(table, cible, courante); // etat final de la table
            }
            break;
        }

        case 5: // Flush manuel
            if (!nomme)
                printf("Aucune table nommee. Inserez d'abord des donnees.\n");
            else if (table->size == 0)
                printf("MemTable vide, rien a ecrire.\n");
            else if (flush_segment(table, db_name))
                printf("Flush termine pour la table [%s].\n", db_name);
            else
                printf("Echec du flush\n");
            break;

        case 6: // Chargement de tous les segments de la table courante
        {
            char path[PATH_SIZE];
            int n = 1, charges = 0;

            // Du plus ancien au plus récent : le récent écrase l'ancien
            for (;;)
            {
                chemin_segment(path, sizeof(path), db_name, n);
                if (!fichier_existe(path))
                    break;
                if (memtable_load(table, path))
                    charges++;
                n++;
            }
            printf("%d segment(s) charge(s).\n", charges);
            break;
        }

        case 7: // Renommer la table (et tous ses segments)
        {
            char new_name[NAME_SIZE];
            char old_path[PATH_SIZE], new_path[PATH_SIZE];

            demander_nom(new_name);
            chemin_segment(new_path, sizeof(new_path), new_name, 1);

            if (strcmp(new_name, db_name) == 0)
                printf("C'est deja le nom actuel.\n");
            else if (fichier_existe(new_path))
                printf("Une table nommee '%s' existe deja.\n", new_name);
            else
            {
                for (int n = 1;; n++)
                {
                    chemin_segment(old_path, sizeof(old_path), db_name, n);
                    if (!fichier_existe(old_path))
                        break;
                    chemin_segment(new_path, sizeof(new_path), new_name, n);
                    if (rename(old_path, new_path) != 0)
                        printf("Echec du renommage de %s\n", old_path);
                }
                strcpy(db_name, new_name);
                nomme = true;
                printf("Table renommee en '%s'.\n", db_name);
            }
            break;
        }

        case 8: // Lister les tables (memoire + disque)
            lister_tables(table, db_name, nomme);
            break;

        case 9: // Choisir une table
        {
            char choisi[NAME_SIZE];
            char path[PATH_SIZE];

            if (lister_tables(table, db_name, nomme) == 0)
            {
                printf("Aucune table. Inserez des donnees puis faites un flush (option 5).\n");
                break;
            }

            printf("Nom de la table a utiliser : ");
            scanf("%49s", choisi);

            chemin_segment(path, sizeof(path), choisi, 1);
            if (!nom_valide(choisi) || !fichier_existe(path))
            {
                printf("Table '%s' introuvable.\n", choisi);
                break;
            }

            if (basculer_table(table, db_name, &nomme, choisi))
                printf("Table courante : [%s].\n", db_name);
            break;
        }
        case 10: // Compactage : choix de la table
        {
            char cible[NAME_SIZE];
            int dest = 0;
            int nb_disque = lister_tables(table, db_name, nomme);

            if (nb_disque == 0)
            {
                printf("Aucune table sur le disque a compacter.\n");
                break;
            }

            printf("\nQuelle table compacter ?\n");
            if (nomme)
                printf("1. La table courante [%s]\n", db_name);
            printf("2. Une autre table (sur le disque)\n");
            printf("Votre choix : ");
            if (scanf("%d", &dest) != 1)
            {
                while (getchar() != '\n')
                    ;
                dest = 0;
            }

            if (dest == 1 && nomme)
                strcpy(cible, db_name);
            else if (dest == 2)
            {
                char path[PATH_SIZE];
                printf("Nom de la table : ");
                scanf("%49s", cible);
                chemin_segment(path, sizeof(path), cible, 1);
                if (!nom_valide(cible) || !fichier_existe(path))
                {
                    printf("Table '%s' introuvable.\n", cible);
                    break;
                }
            }
            else
            {
                printf("Choix invalide.\n");
                break;
            }

            // Les données non sauvegardées de la table courante doivent être
            // écrites d'abord, sinon leurs tombstones ne seraient pas pris en compte
            if (nomme && strcmp(cible, db_name) == 0 && table->size > 0)
            {
                printf("Flush de la MemTable avant compactage...\n");
                if (!flush_segment(table, db_name))
                {
                    printf("Echec du flush, compactage annule.\n");
                    break;
                }
            }

            compacter_table(cible);
            break;
        }

        case 0:
            printf("Returning to main menu...\n");
            break;

        default:
            printf("Invalid choice!\n");
        }

    } while (choice != 0);
}