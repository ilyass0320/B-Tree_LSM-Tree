#include <stdio.h>
#include <stdlib.h>
#include "lsm.h" // pour le type MemTable, memtable_init et lsm_menu

void btree_menu();

int main()
{
    int choice;

    // La MemTable pese ~7 Mo : static pour ne pas saturer la pile
    static MemTable table;
    memtable_init(&table);

    do
    {
        printf("\n================================\n");
        printf("       DATABASES                  \n");
        printf("==================================\n");

        printf("1. B-Tree Database\n");
        printf("2. LSM-Tree Database\n");
        printf("0. Exit\n");

        printf("\nChoose your database: ");
        if (scanf("%d", &choice) != 1)
        {
            while (getchar() != '\n')
                ;
            choice = -1;
        }

        switch (choice)
        {
        case 1:
            btree_menu();
            break;

        case 2:
            lsm_menu(&table); // on passe la table au menu
            break;

        case 0:
            printf("Goodbye!\n");
            break;

        default:
            printf("Invalid choice!\n");
        }

    } while (choice != 0);

    return 0;
}