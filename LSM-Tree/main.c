#include <stdio.h>
#include "lsm.h"

int main() {

    MemTable table;

    // Initialize the database
    memtable_init(&table);


    if (memtable_load(&table, "data/sstable.dat")) {
        printf("Previous data restored successfully!\n");
    } else {
        printf("Starting with an empty database.\n");
    }

    int choice;

    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    char result[VALUE_SIZE];

    printf("=================================\n");
    printf("       LSM TREE DATABASE\n");
    printf("=================================\n");

    while (1) {

        printf("\n========== MENU ==========\n");
        printf("1. PUT    - Insert a value\n");
        printf("2. GET    - Search for a value\n");
        printf("3. DELETE - Delete a value\n");
        printf("4. DISPLAY - Show all data\n");
        printf("5. FLUSH  - Save data to disk\n");
        printf("6. EXIT\n");
        printf("==========================\n");

        printf("Choose an operation: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input!\n");
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF);
            continue;
        }

        switch (choice) {

            case 1:

                printf("Enter key: ");
                scanf("%49s", key);

                printf("Enter value: ");
                scanf("%199s", value);

                if (memtable_put(&table, key, value))
                    printf("Value inserted successfully!\n");
                else
                    printf("Error: Could not insert value.\n");

                break;

            case 2:

                printf("Enter key to search: ");
                scanf("%49s", key);

                if (memtable_get(&table, key, result))
                    printf("Value found: %s\n", result);
                else
                    printf("Key not found!\n");

                break;

            case 3:

                printf("Enter key to delete: ");
                scanf("%49s", key);

                if (memtable_delete(&table, key))
                    printf("Key deleted successfully!\n");
                else
                    printf("Key not found or already deleted!\n");

                break;

            case 4:

                memtable_display(&table);

                break;

            case 5:

                if (memtable_flush(&table, "data/sstable.dat"))
                    printf("Data saved to disk!\n");
                else
                    printf("Flush failed!\n");

                break;

            case 6:

                printf("Exiting LSM Tree Database...\n");
                return 0;

                
                

            default:

                printf("Invalid choice! Try again.\n");
        }
    }

    return 0;
}