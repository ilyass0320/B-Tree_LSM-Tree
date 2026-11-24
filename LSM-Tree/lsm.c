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

// Écrit la MemTable dans un nouveau segment de la table 'name'
static bool flush_segment(MemTable *table, const char *name)
{
    char path[PATH_SIZE];
    int n = prochain_segment(name);
    chemin_segment(path, sizeof(path), name, n);

    if (memtable_flush_segment(table, path))
    {
        printf("Segment ecrit : %s\n", path);
        return true;
    }
    return false;
}

// Demande un nom à l'utilisateur jusqu'à ce qu'il soit valide
static void demander_nom(char *name)
{
    do
    {
        printf("Nom du Table : ");
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

        printf("[%s] : %d entree(s) en memoire%s\n", courante, table->size, sur_disque ? "" : "  (pas encore sur le disque)");
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

            // Extrait le nom de la table (avant "_seg1.dat")
            memcpy(nom, entry->d_name, len_nom);
            nom[len_nom] = '\0';

            count++;
            printf("%d. %s  (%d segment(s))%s\n", count, nom, prochain_segment(nom) - 1, (nomme && strcmp(nom, courante) == 0) ? "  <- table courante (en memoire)" : "");
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
        printf("5. Flush MemTable (new segment)\n");
        printf("6. Load segments (from disk)\n");
        printf("7. Rename database\n");
        printf("8. List tables\n");
        printf("9. Select table\n");
        printf("0. Back to main menu\n");

        printf("\nChoose operation: ");
        if (scanf("%d", &choice) != 1)
        {
            // Saisie non numérique : on vide le buffer et on force "invalide"
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

            // Verifie les tables en memoire et sur le disque (et les affiche)
            int nb_disque = lister_tables(table, db_name, nomme);

            if (!nomme && nb_disque == 0)
            {
                // Aucune table nulle part : creation directe
                printf("Aucune table existante. Creation de la premiere table.\n");
                demander_nom(db_name);
                nomme = true;
            }
            else
            {
                // Question posee a chaque insertion
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

                // Limite atteinte : flush automatique vers un nouveau segment
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

        case 2: // Recherche (en mémoire uniquement)
            printf("Key a chercher : ");
            scanf("%499s", Key);
            if (memtable_get(table, Key, result))
                printf("Key %s trouvee, valeur : %s\n", Key, result);
            else
                printf("Key %s n'est pas trouvee en memoire\n", Key);
            break;

        case 3: // Suppression (tombstone, en mémoire uniquement)
            printf("Key a supprimer : ");
            scanf("%499s", Key);
            if (memtable_delete(table, Key))
                printf("Key %s est supprimee\n", Key);
            else
                printf("Key %s n'existe pas en memoire\n", Key);
            break;

        case 4: // Affichage de la MemTable
            memtable_display(table);
            break;

        case 5: // Flush manuel vers un nouveau segment
            if (table->size == 0)
                printf("MemTable vide, rien a ecrire.\n");
            else if (!flush_segment(table, db_name))
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
                printf("Une base nommee '%s' existe deja.\n", new_name);
            else
            {
                // Renomme tous les segments existants
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
                printf("Base renommee en '%s'.\n", db_name);
            }
            break;
        }

        case 8: // Lister les tables (memoire + disque)
            lister_tables(table, db_name, nomme);
            break;

        case 9: // Choisir une table pour y ajouter des données
        {
            char choisi[NAME_SIZE];
            char path[PATH_SIZE];

            if (lister_tables(table, db_name, nomme) == 0)
            {
                printf("Aucune table. Insere des donnees puis fais un flush (option 5).\n");
                break;
            }

            printf("Nom de la table a utiliser : ");
            scanf("%49s", choisi);

            // La table doit exister (au moins le segment 1)
            chemin_segment(path, sizeof(path), choisi, 1);
            if (!nom_valide(choisi) || !fichier_existe(path))
            {
                printf("Table '%s' introuvable.\n", choisi);
                break;
            }

            // Les données en mémoire appartiennent à l'ancienne table :
            // on les sauvegarde avant de changer, pour ne pas les mélanger
            if (table->size > 0)
            {
                if (nomme)
                {
                    printf("Sauvegarde des donnees de [%s]...\n", db_name);
                    flush_segment(table, db_name);
                }
                else
                {
                    printf("Donnees non sauvegardees abandonnees.\n");
                    table->size = 0;
                }
            }

            strcpy(db_name, choisi);
            nomme = true;
            printf("Table courante : [%s]. Utilise l'option 1 pour ajouter des donnees.\n", db_name);
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