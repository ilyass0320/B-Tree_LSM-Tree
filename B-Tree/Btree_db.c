#include <stdio.h>

void btree_menu() {

    int choice;

    do {
        printf("\n========== B-TREE DATABASE ==========\n");

        printf("1. Insert data\n");
        printf("2. Search data\n");
        printf("3. Delete data\n");
        printf("4. Display database\n");
        printf("0. Back to main menu\n");

        printf("\nChoose operation: ");
        scanf("%d", &choice);

        switch(choice) {

            case 1:
                printf("Insert operation\n");
                // btree_insert();
                break;

            case 2:
                printf("Search operation\n");
                // btree_search();
                break;

            case 3:
                printf("Delete operation\n");
                // btree_delete();
                break;

            case 4:
                printf("Display operation\n");
                // btree_display();
                break;

            case 0:
                printf("Returning to main menu...\n");
                break;

            default:
                printf("Invalid choice!\n");
        }

    } while(choice != 0);
}