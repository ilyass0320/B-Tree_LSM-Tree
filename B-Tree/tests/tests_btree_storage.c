#include "btree.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char *scenario = "préparation";
static char directory[64];
static char path[BTREE_PATH_CAPACITY];
static char temporary[BTREE_PATH_CAPACITY + sizeof ".tmp" - 1];
static bool directory_created;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "ÉCHEC [%s] %s:%d — %s\n", scenario,            \
                    __FILE__, __LINE__, #condition);                       \
            exit(EXIT_FAILURE);                                            \
        }                                                                  \
    } while (0)

static void join_path(char *out, size_t capacity,
                      const char *base, const char *suffix)
{
    int length = snprintf(out, capacity, "%s%s", base, suffix);
    CHECK(length >= 0 && (size_t)length < capacity);
}

/* Seul un répertoire créé par cette exécution devient notre espace de test. */
static void create_sandbox(void)
{
    for (unsigned i = 0; i < 1000; ++i) {
        int length = snprintf(directory, sizeof directory,
                              "btree-storage-test-%u", i);
        int result;

        CHECK(length >= 0 && (size_t)length < sizeof directory);
        join_path(path, sizeof path, directory, "/tree.ibtr");
        join_path(temporary, sizeof temporary, path, ".tmp");
#ifdef _WIN32
        result = _mkdir(directory);
#else
        result = mkdir(directory, 0700);
#endif
        if (result == 0) {
            directory_created = true;
            return;
        }
        CHECK(errno == EEXIST);
    }
    CHECK(directory_created);
}

static bool remove_if_present(const char *file)
{
    if (remove(file) == 0 || errno == ENOENT) {
        return true;
    }
    perror(file);
    return false;
}

static bool clean_files(void)
{
    bool removed_path = remove_if_present(path);
    bool removed_temp = remove_if_present(temporary);
    return removed_path && removed_temp;
}

static bool cleanup_sandbox(void)
{
    bool files_clean;
    int result;

    if (!directory_created) {
        return true;
    }

    files_clean = clean_files();
#ifdef _WIN32
    result = _rmdir(directory);
#else
    result = rmdir(directory);
#endif
    if (result != 0) {
        perror(directory);
        return false;
    }
    directory_created = false;
    return files_clean;
}

static void cleanup_on_exit(void)
{
    (void)cleanup_sandbox();
}

static bool same_stats(BTreeStats a, BTreeStats b)
{
    return a.reads == b.reads && a.comparisons == b.comparisons &&
           a.inserts == b.inserts && a.updates == b.updates &&
           a.deletes == b.deletes && a.splits == b.splits;
}

static void check_empty(const BTree *tree)
{
    const BTreeNode *root = btree_root(tree);
    CHECK(root != NULL && root->leaf && root->key_count == 0);
    CHECK(tree->count == 0);
    CHECK(same_stats(tree->stats, (BTreeStats){ 0 }));
    CHECK(strcmp(tree->path, path) == 0);
}

static void put(BTree *tree, int key, const char *value, int expected)
{
    DbRecord record;
    record_init(&record, key, value);
    CHECK(btree_put(tree, &record) == expected);
}

static void check_value(const DbRecord *record, int key, const char *value)
{
    CHECK(record->key == key);
    CHECK(memchr(record->value, '\0', sizeof record->value) != NULL);
    CHECK(strcmp(record->value, value) == 0);
}

static void expect_record(BTree *tree, int key, const char *value)
{
    DbRecord record;
    CHECK(btree_get(tree, key, &record) == 1);
    check_value(&record, key, value);
}

static void reopen_tree(BTree *tree)
{
    btree_destroy(tree);
    CHECK(btree_init(tree, path) == 0);
    CHECK(btree_load(tree) == 0);
}

static void test_initialisation(void)
{
    BTree tree;
    CHECK(btree_init(&tree, path) == 0);
    check_empty(&tree);
    btree_destroy(&tree);
}

static void test_record_init(void)
{
    DbRecord record;
    char long_value[BTREE_VALUE_CAPACITY + 16];

    record_init(&record, 17, "valeur");
    check_value(&record, 17, "valeur");
    record_init(&record, 18, "");
    check_value(&record, 18, "");
    record_init(&record, 19, NULL);
    check_value(&record, 19, "");

    memset(long_value, 'x', sizeof long_value - 1);
    long_value[sizeof long_value - 1] = '\0';
    record_init(&record, 20, long_value);
    CHECK(record.key == 20);
    CHECK(record.value[BTREE_VALUE_CAPACITY - 1] == '\0');
    CHECK(memcmp(record.value, long_value, BTREE_VALUE_CAPACITY - 1) == 0);
}

static void test_save_load(void)
{
    static const struct {
        int key;
        const char *value;
    } records[] = {
        { 40, "quarante" }, { 10, "dix" }, { 70, "soixante-dix" },
        { 20, "vingt" }, { 60, "soixante" }, { 30, "trente" },
        { 50, "cinquante" }
    };
    const size_t count = sizeof records / sizeof records[0];
    BTree tree;

    CHECK(btree_init(&tree, path) == 0);
    for (size_t i = 0; i < count; ++i) {
        put(&tree, records[i].key, records[i].value, 0);
    }
    CHECK(btree_save(&tree) == 0);
    reopen_tree(&tree);
    CHECK(tree.count == count);
    for (size_t i = 0; i < count; ++i) {
        expect_record(&tree, records[i].key, records[i].value);
    }
    btree_destroy(&tree);
}

static void test_empty_tree(void)
{
    BTree tree;
    CHECK(btree_init(&tree, path) == 0);
    CHECK(btree_save(&tree) == 0);
    put(&tree, 11, "en mémoire", 0);
    CHECK(btree_load(&tree) == 0);
    check_empty(&tree);
    CHECK(btree_get(&tree, 11, NULL) == 0);
    btree_destroy(&tree);
}

static void test_modified_tree(void)
{
    BTree tree;
    CHECK(btree_init(&tree, path) == 0);
    put(&tree, 1, "un", 0);
    put(&tree, 2, "deux", 0);
    put(&tree, 3, "trois", 0);
    put(&tree, 4, "quatre", 0);
    put(&tree, 1, "un modifié", 1);
    put(&tree, 3, "trois modifié", 1);
    CHECK(btree_delete(&tree, 2) == 1);
    CHECK(btree_delete(&tree, 4) == 1);
    CHECK(btree_save(&tree) == 0);
    reopen_tree(&tree);
    CHECK(tree.count == 2);
    expect_record(&tree, 1, "un modifié");
    expect_record(&tree, 3, "trois modifié");
    CHECK(btree_get(&tree, 2, NULL) == 0);
    CHECK(btree_get(&tree, 4, NULL) == 0);
    btree_destroy(&tree);
}

static void test_replace_save(void)
{
    BTree tree;
    CHECK(btree_init(&tree, path) == 0);
    put(&tree, 5, "ancienne version", 0);
    put(&tree, 6, "à supprimer", 0);
    CHECK(btree_save(&tree) == 0);
    put(&tree, 5, "nouvelle version", 1);
    CHECK(btree_delete(&tree, 6) == 1);
    CHECK(btree_save(&tree) == 0);
    reopen_tree(&tree);
    CHECK(tree.count == 1);
    expect_record(&tree, 5, "nouvelle version");
    CHECK(btree_get(&tree, 6, NULL) == 0);
    btree_destroy(&tree);
}

/* Comparer les compteurs avant les recherches, qui les font évoluer. */
static void expect_unchanged_after_load_error(BTree *tree)
{
    const BTreeNode *root = btree_root(tree);
    size_t count = tree->count;
    BTreeStats stats = tree->stats;

    CHECK(btree_load(tree) == -1);
    CHECK(btree_root(tree) == root && tree->count == count);
    CHECK(same_stats(tree->stats, stats));
    CHECK(strcmp(tree->path, path) == 0);
    expect_record(tree, -10, "témoin");
    expect_record(tree, 42, "autre témoin");
}

static void test_missing_file(void)
{
    BTree tree;
    CHECK(btree_init(&tree, path) == 0);
    put(&tree, -10, "témoin", 0);
    put(&tree, 42, "autre témoin", 0);
    expect_unchanged_after_load_error(&tree);
    btree_destroy(&tree);
}

static void write_fixture(const char *file_path, const void *bytes, size_t size)
{
    FILE *file = fopen(file_path, "wb");
    CHECK(file != NULL);
    size_t written = fwrite(bytes, 1, size, file);
    int closed = fclose(file);
    CHECK(written == size && closed == 0);
}

static void test_invalid_files(void)
{
    enum { HEADER = 14, ROW = 7, VALID_SIZE = HEADER + 2 * ROW };
    /* IBTR v1, deux clés (1 et 2), une valeur d'un octet par clé. */
    static const unsigned char valid[VALID_SIZE + 1] = {
        0x49, 0x42, 0x54, 0x52, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 1, 0, 0x41,
        2, 0, 0, 0, 1, 0, 0x42,
        0xff /* Utilisé uniquement par le cas des octets supplémentaires. */
    };
    static const struct {
        const char *name;
        size_t size;
        int offset;
        unsigned char byte;
    } cases[] = {
        { "IBTR / signature",       VALID_SIZE,     0,          0 },
        { "IBTR / version",         VALID_SIZE,     4,          2 },
        { "IBTR / en-tête tronqué", HEADER - 1,     -1,          0 },
        { "IBTR / ligne tronquée",  VALID_SIZE - 1, -1,          0 },
        /* L'octet de poids fort porte la longueur à 257 octets. */
        { "IBTR / longueur",        VALID_SIZE,     HEADER + 5,  1 },
        { "IBTR / clé dupliquée",    VALID_SIZE,     HEADER + ROW, 1 },
        { "IBTR / ordre des clés",   VALID_SIZE,     HEADER,      3 },
        { "IBTR / octet en trop",    sizeof valid,   -1,          0 }
    };
    const char *parent = scenario;
    BTree tree;

    /* Confirme que l'image de référence est acceptée avant les mutations. */
    write_fixture(path, valid, VALID_SIZE);
    CHECK(btree_init(&tree, path) == 0);
    CHECK(btree_load(&tree) == 0 && tree.count == 2);
    expect_record(&tree, 1, "A");
    expect_record(&tree, 2, "B");
    btree_destroy(&tree);

    CHECK(btree_init(&tree, path) == 0);
    put(&tree, -10, "témoin", 0);
    put(&tree, 42, "autre témoin", 0);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned char bytes[sizeof valid];
        scenario = cases[i].name;
        memcpy(bytes, valid, sizeof bytes);
        if (cases[i].offset >= 0) {
            bytes[cases[i].offset] = cases[i].byte;
        }
        write_fixture(path, bytes, cases[i].size);
        expect_unchanged_after_load_error(&tree);
    }
    scenario = parent;
    btree_destroy(&tree);
}

static void test_limits(void)
{
    char longest[BTREE_VALUE_CAPACITY];
    BTree tree;

    memset(longest, 'm', sizeof longest - 1);
    longest[sizeof longest - 1] = '\0';
    CHECK(btree_init(&tree, path) == 0);
    put(&tree, INT_MAX, "maximum", 0);
    put(&tree, -7, "négatif", 0);
    put(&tree, 0, "", 0);
    put(&tree, INT_MIN, longest, 0);
    CHECK(btree_save(&tree) == 0);
    reopen_tree(&tree);
    CHECK(tree.count == 4);
    expect_record(&tree, INT_MAX, "maximum");
    expect_record(&tree, -7, "négatif");
    expect_record(&tree, 0, "");
    expect_record(&tree, INT_MIN, longest);
    btree_destroy(&tree);
}

static void test_stats(void)
{
    const int inserted = 2 * BTREE_MAX_KEYS + 1;
    const BTreeNode *root;
    BTreeStats before_save;
    BTree tree;

    CHECK(btree_init(&tree, path) == 0);
    for (int key = 1; key <= inserted; ++key) {
        put(&tree, key, "initial", 0);
    }
    put(&tree, 1, "modifié", 1);
    CHECK(btree_delete(&tree, inserted) == 1);
    before_save = tree.stats;
    expect_record(&tree, 1, "modifié");
    CHECK(tree.stats.reads > before_save.reads);
    CHECK(tree.stats.comparisons > before_save.comparisons);
    CHECK(tree.stats.inserts == (uint64_t)inserted && tree.stats.updates == 1);
    CHECK(tree.stats.deletes == 1 && tree.stats.splits > 0);

    before_save = tree.stats;
    CHECK(btree_save(&tree) == 0);
    CHECK(same_stats(tree.stats, before_save));

    root = btree_root(&tree);
    btree_reset_stats(&tree);
    CHECK(same_stats(tree.stats, (BTreeStats){ 0 }));
    CHECK(btree_root(&tree) == root && tree.count == (size_t)(inserted - 1));
    CHECK(strcmp(tree.path, path) == 0);
    expect_record(&tree, 1, "modifié");
    CHECK(tree.stats.reads > 0);

    CHECK(btree_load(&tree) == 0);
    CHECK(same_stats(tree.stats, (BTreeStats){ 0 }));
    CHECK(tree.count == (size_t)(inserted - 1) && strcmp(tree.path, path) == 0);
    expect_record(&tree, 1, "modifié");
    CHECK(btree_get(&tree, inserted, NULL) == 0);
    btree_destroy(&tree);
}

static void count_visit(const DbRecord *record, void *context)
{
    size_t *count = context;
    (void)record;
    ++*count;
}

static void test_errors(void)
{
    static const char marker[] = "bloqueur";
    char actual[sizeof marker];
    char too_long[BTREE_PATH_CAPACITY + 1];
    char inaccessible[BTREE_PATH_CAPACITY];
    DbRecord record;
    BTree tree;
    BTree blocked;
    size_t visits = 0;

    record_init(NULL, 1, "ignoré");
    record_init(&record, 1, "valeur");
    CHECK(btree_init(NULL, path) == -1);
    CHECK(btree_root(NULL) == NULL);
    CHECK(btree_get(NULL, 1, NULL) == -1);
    CHECK(btree_put(NULL, &record) == -1);
    CHECK(btree_delete(NULL, 1) == -1);
    CHECK(btree_save(NULL) == -1 && btree_load(NULL) == -1);
    btree_destroy(NULL);
    btree_reset_stats(NULL);
    btree_traverse(NULL, count_visit, &visits);
    CHECK(visits == 0);

    memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    CHECK(btree_init(&tree, NULL) == -1);
    btree_destroy(&tree);
    CHECK(btree_init(&tree, "") == -1);
    btree_destroy(&tree);
    CHECK(btree_init(&tree, too_long) == -1);
    btree_destroy(&tree);

    CHECK(btree_init(&tree, path) == 0);
    CHECK(btree_put(&tree, NULL) == -1);
    put(&tree, 1, "sauvegarde précédente", 0);
    btree_traverse(&tree, NULL, NULL);
    CHECK(btree_get(&tree, 1, NULL) == 1);
    CHECK(btree_save(&tree) == 0);

    /* Un fichier ordinaire ne peut pas servir de répertoire parent. */
    join_path(inaccessible, sizeof inaccessible, path, "/child.ibtr");
    CHECK(btree_init(&blocked, inaccessible) == 0);
    CHECK(btree_save(&blocked) == -1);
    btree_destroy(&blocked);

    put(&tree, 1, "changement en mémoire", 1);
    write_fixture(temporary, marker, sizeof marker);
    CHECK(btree_save(&tree) == -1);

    FILE *file = fopen(temporary, "rb");
    CHECK(file != NULL);
    size_t length = fread(actual, 1, sizeof actual, file);
    int end = fgetc(file);
    bool read_ok = !ferror(file);
    int closed = fclose(file);
    CHECK(length == sizeof marker && end == EOF && read_ok && closed == 0);
    CHECK(memcmp(actual, marker, sizeof marker) == 0);

    reopen_tree(&tree);
    CHECK(tree.count == 1);
    expect_record(&tree, 1, "sauvegarde précédente");
    btree_destroy(&tree);
}

static void test_lifecycle(void)
{
    BTree tree;

    for (size_t cycle = 0; cycle < 32; ++cycle) {
        CHECK(btree_init(&tree, path) == 0);
        for (int key = 0; key <= 3 * BTREE_MAX_KEYS; ++key) {
            put(&tree, key, "donnée", 0);
        }
        btree_destroy(&tree);
        btree_destroy(&tree);
        CHECK(btree_root(&tree) == NULL && tree.count == 0);
        CHECK(tree.path[0] == '\0' && same_stats(tree.stats, (BTreeStats){ 0 }));
    }

    CHECK(btree_init(&tree, "") == -1);
    btree_destroy(&tree);
    btree_destroy(&tree);
    CHECK(btree_root(&tree) == NULL);
}

int main(void)
{
    static const struct {
        const char *name;
        void (*run)(void);
    } tests[] = {
        { "Initialisation de l’arbre", test_initialisation },
        { "Initialisation des enregistrements", test_record_init },
        { "Sauvegarde et rechargement", test_save_load },
        { "Persistance d’un arbre vide", test_empty_tree },
        { "Persistance après modification", test_modified_tree },
        { "Remplacement d’une sauvegarde", test_replace_save },
        { "Fichier inexistant", test_missing_file },
        { "Fichiers IBTR invalides", test_invalid_files },
        { "Valeurs limites", test_limits },
        { "Statistiques", test_stats },
        { "Gestion des erreurs", test_errors },
        { "Cycles de vie et destruction", test_lifecycle }
    };
    const size_t count = sizeof tests / sizeof tests[0];

    CHECK(atexit(cleanup_on_exit) == 0);
    create_sandbox();
    puts("Tests de persistance du B-tree");
    for (size_t i = 0; i < count; ++i) {
        scenario = tests[i].name;
        tests[i].run();
        CHECK(clean_files());
        printf("  OK  %s\n", scenario);
    }
    scenario = "nettoyage";
    CHECK(cleanup_sandbox());
    printf("%zu tests réussis.\n", count);
    return EXIT_SUCCESS;
}