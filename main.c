#include <stdio.h>
#include <stdlib.h>

void btree_menu();
void lsm_menu();

int main()
{

    int choice;

    do
    {
        printf("\n================================\n");
        printf("       DATABASES                  \n");
        printf("==================================\n");

        printf("1. B-Tree Database\n");
        printf("2. LSM-Tree Database\n");
        printf("0. Exit\n");

        printf("\nChoose your database: ");
        scanf("%d", &choice);

        switch (choice)
        {
        case 1:
            btree_menu();
            break;

        case 2:
            lsm_menu();
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