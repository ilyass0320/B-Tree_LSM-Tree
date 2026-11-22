#include <stdio.h>

void lsm_menu()
{

    int choice;

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
            // lsm_insert();
            break;

        case 2:
            printf("Search operation\n");
            // lsm_search();
            break;

        case 3:
            printf("Delete operation\n");
            // lsm_delete();
            break;

        case 4:
            printf("Display operation\n");
            // lsm_display();
            break;

        case 5:
            printf("Flush MemTable\n");
            // lsm_flush();
            break;

        case 6:
            printf("Compact SSTables\n");
            // lsm_compact();
            break;

        case 0:
            printf("Returning to main menu...\n");
            break;

        default:
            printf("Invalid choice!\n");
        }

    } while (choice != 0);
}