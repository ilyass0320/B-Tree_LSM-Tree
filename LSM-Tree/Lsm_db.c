#include <stdio.h>
#include "lsm.h"
#define FILENAME "memtable.dat"
// la table est passee en parametre
void lsm_menu(MemTable *table)
{

    int choice;
    char Key[100];
    char value[100];
    char result[100];
    do
    {
        printf("\n========== LSM-TREE DATABASE ==========\n");

        printf("1. Insert data\n");
        printf("2. Search data\n");
        printf("3. Delete data\n");
        printf("4. Display database\n");
        printf("5. Flush MemTable\n");
        printf("6. Compact SSTables\n");
        printf("0. Back to main menu\n");

        printf("\nChoose operation: ");
        scanf("%d", &choice);

        switch (choice)
        {

        case 1:
            printf("Insert operation\n");
            printf("Key en entier :");
            scanf("%99s", Key);
            printf("Valeur :");
            scanf("%99s", value);
            if (memtable_put(table, Key, value))
                printf("Insertion reussie\n");
            else
                printf("Insertion Echoue\n");
            break;

        case 2:
            printf("Search operation\n");
            printf("Key a chercher : ");
            scanf("%99s", Key);
            {
                if (memtable_get(table, Key, result))
                    printf("Le Key %s = %s est Trouve", Key, result);
                else
                    printf("Le Key %s n'est pas Trouve", Key);
            }
            break;

        case 3:
            printf("Delete operation\n");
            printf("key a Supprimer \n");
            scanf("%99s", Key);
            // souvent implemente avec un "tombstone"

            if (memtable_delete(table, Key))
                printf("Key %s est supprimer \n", Key);
            else
                printf("Key %s n'est pas supprimer \n", Key);
            break;

        case 4:
            printf("Display operation\n");
            // pas de saisie, juste la table
            memtable_display(table);
            break;

        case 5:
            printf("Flush MemTable\n");
            // ecrit la memtable dans un SSTable sur le disk
            if (memtable_flush(table, FILENAME))
                printf("MemTable chargee depuis %s\n", FILENAME);
            else
                printf("Echec du chargement (fichier absent ?)\n");
            break;

        case 6:
            printf("Compact SSTables\n");
            // fusionne les SSTables(pas toujours besoin de la table)
            memtable_load(table, FILENAME);
            break;

        case 0:
            printf("Returning to main menu...\n");
            break;

        default:
            printf("Invalid choice!\n");
        }

    } while (choice != 0);
}