#include "btree.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

_Static_assert(CHAR_BIT == 8, "Le format IBTR exige des octets de 8 bits.");
_Static_assert(BTREE_VALUE_CAPACITY <= UINT64_C(65536),
               "La longueur de la valeur doit tenir sur 16 bits.");

/*
 * IBTR v1, entiers en petit-boutiste.
 * En-tête : signature (4 octets), version (u16), nombre de lignes (u64).
 * Ligne : clé (i32 en complément à deux), longueur (u16), valeur brute.
 * Le caractère nul final de la valeur est reconstruit à la lecture.
 */
static const unsigned char file_magic[] = { 0x49, 0x42, 0x54, 0x52 };
enum { FORMAT_VERSION = 1 };

typedef struct {
    FILE *file;
    size_t remaining;
    int previous_key;
    bool have_previous;
    bool failed;
} SaveContext;

/* Retourne zéro pour un arbre inutilisable ou un chemin invalide. */
static size_t storage_path_length(const BTree *tree)
{
    const char *end;

    if (tree == NULL || tree->root == NULL) {
        return 0;
    }

    end = memchr(tree->path, '\0', sizeof tree->path);
    return end == NULL ? 0 : (size_t)(end - tree->path);
}

static bool write_uint_le(FILE *file, uint64_t value, size_t width)
{
    unsigned char bytes[8];

    for (size_t i = 0; i < width; ++i) {
        bytes[i] = (unsigned char)(value & UINT64_C(0xff));
        value >>= 8;
    }

    return fwrite(bytes, 1, width, file) == width;
}

static bool read_uint_le(FILE *file, size_t width, uint64_t *value)
{
    unsigned char bytes[8];

    if (fread(bytes, 1, width, file) != width) {
        return false;
    }

    *value = 0;
    for (size_t i = 0; i < width; ++i) {
        *value |= (uint64_t)bytes[i] << (8 * i);
    }

    return true;
}

static bool write_record(FILE *file, const DbRecord *record)
{
    const char *end = memchr(record->value, '\0', sizeof record->value);
    intmax_t key = record->key;
    size_t length;

    if (end == NULL || key < INT32_MIN || key > INT32_MAX) {
        return false;
    }

    length = (size_t)(end - record->value);
    return write_uint_le(file, (uint32_t)key, 4) &&
           write_uint_le(file, length, 2) &&
           fwrite(record->value, 1, length, file) == length;
}

static void save_record(const DbRecord *record, void *context)
{
    SaveContext *save = context;

    if (save->failed) {
        return;
    }

    if (save->remaining == 0 ||
        (save->have_previous && record->key <= save->previous_key) ||
        !write_record(save->file, record)) {
        save->failed = true;
        return;
    }

    --save->remaining;
    save->previous_key = record->key;
    save->have_previous = true;
}

static FILE *open_temporary(const char *path)
{
#ifdef _WIN32
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    int fd;
    FILE *file;

    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    /* Le descripteur, puis le flux, prennent possession du handle. */
    fd = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_WRONLY);
    if (fd == -1) {
        CloseHandle(handle);
        (void)remove(path);
        return NULL;
    }

    file = _fdopen(fd, "wb");
    if (file == NULL) {
        _close(fd);
        (void)remove(path);
    }
    return file;
#else
    return fopen(path, "wbx");
#endif
}

static bool replace_file(const char *temporary, const char *path)
{
#ifdef _WIN32
    return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

int btree_save(const BTree *tree)
{
    char temporary[BTREE_PATH_CAPACITY + sizeof ".tmp" - 1];
    size_t path_length = storage_path_length(tree);
    FILE *file;
    SaveContext save;
    bool ok;

    if (path_length == 0 || (uintmax_t)tree->count > UINT64_MAX) {
        return -1;
    }

    memcpy(temporary, tree->path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof ".tmp");
    file = open_temporary(temporary);
    if (file == NULL) {
        return -1;
    }

    save = (SaveContext){ .file = file, .remaining = tree->count };
    ok = fwrite(file_magic, 1, sizeof file_magic, file) == sizeof file_magic &&
         write_uint_le(file, FORMAT_VERSION, 2) &&
         write_uint_le(file, (uint64_t)tree->count, 8);

    if (ok) {
        btree_traverse(tree, save_record, &save);
        ok = !save.failed && save.remaining == 0;
    }

    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok || !replace_file(temporary, tree->path)) {
        /* Le fichier définitif n'est jamais supprimé pour forcer le remplacement. */
        (void)remove(temporary);
        return -1;
    }

    return 0;
}

static bool read_header(FILE *file, uint64_t *count)
{
    unsigned char magic[sizeof file_magic];
    uint64_t version;

    return fread(magic, 1, sizeof magic, file) == sizeof magic &&
           memcmp(magic, file_magic, sizeof magic) == 0 &&
           read_uint_le(file, 2, &version) &&
           version == FORMAT_VERSION &&
           read_uint_le(file, 8, count) &&
           *count <= SIZE_MAX;
}

static bool read_record(FILE *file, DbRecord *record)
{
    uint64_t raw_key;
    uint64_t raw_length;
    int64_t key;
    size_t length;

    if (!read_uint_le(file, 4, &raw_key) ||
        !read_uint_le(file, 2, &raw_length) ||
        raw_length >= sizeof record->value) {
        return false;
    }

    /* Décode la clé sans conversion directe d'un entier non signé hors plage. */
    key = (int64_t)raw_key;
    if (key > INT32_MAX) {
        key -= INT64_C(4294967296);
    }
    if (key < INT_MIN || key > INT_MAX) {
        return false;
    }

    length = (size_t)raw_length;
    if (fread(record->value, 1, length, file) != length ||
        memchr(record->value, '\0', length) != NULL) {
        return false;
    }

    record->key = (int)key;
    record->value[length] = '\0';
    return true;
}

static bool load_records(FILE *file, BTree *tree, uint64_t count)
{
    int previous_key = 0;
    bool have_previous = false;

    while (count != 0) {
        DbRecord record = { 0 };

        if (!read_record(file, &record) ||
            (have_previous && record.key <= previous_key) ||
            btree_put(tree, &record) != 0) {
            return false;
        }

        previous_key = record.key;
        have_previous = true;
        --count;
    }

    return fgetc(file) == EOF && !ferror(file);
}

int btree_load(BTree *tree)
{
    BTree loaded = { 0 };
    uint64_t count;
    FILE *file;
    bool ok;

    if (storage_path_length(tree) == 0) {
        return -1;
    }

    file = fopen(tree->path, "rb");
    if (file == NULL) {
        return -1;
    }

    ok = read_header(file, &count) &&
         btree_init(&loaded, tree->path) == 0 &&
         load_records(file, &loaded, count);

    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        btree_destroy(&loaded);
        return -1;
    }

    btree_reset_stats(&loaded);
    btree_destroy(tree);
    *tree = loaded;
    return 0;
}