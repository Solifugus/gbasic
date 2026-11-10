/* The `zip` module: listing, reading and extracting the members of a ZIP file
 * (gap G11 from the gBASIC books: partners send several files in one ZIP, and
 * gBASIC read ZIP only inside its spreadsheet engine).
 *
 * Included into eval.c like the other modules. It needs zlib alone.
 *
 * THE CENTRAL DIRECTORY IS THE INDEX, as in xlsx.c's container reader and for
 * the same reason: a local header may carry zeroed sizes with the real ones in
 * a trailing data descriptor. Unlike that reader this one does not load the
 * file: it reads the directory, and then only the member asked for, so a
 * listing of a large archive costs its directory.
 *
 * What it refuses rather than misreads: ZIP64 (an archive or member past
 * 4 GB), spanned archives, encrypted members, and any method but store (0) and
 * deflate (8), each when that member is READ; `members` lists them all and
 * says which. A member whose bytes do not match its recorded CRC-32 or size is
 * refused. */

#define ZIP_ERROR_CODE 5201

static Value zip_raise(const char *message) {
    runtime_error_raise(message, ZIP_ERROR_CODE, "zip");
    return value_null();
}

#if HAVE_ZLIB

typedef struct {
    char *name;            /* UTF-8 */
    unsigned flags;
    unsigned method;
    unsigned long crc;
    unsigned long packed;
    unsigned long size;
    unsigned dos_time, dos_date;
    unsigned long external;
    unsigned made_by;      /* the high byte: 3 is Unix */
    unsigned long local_off;
} ZipEntry;

typedef struct {
    FILE *f;
    const char *path;
    ZipEntry *e;
    size_t n;
} ZipArchive;

static unsigned zip_u16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long zip_u32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static void zip_close(ZipArchive *z) {
    for (size_t i = 0; i < z->n; i++) {
        free(z->e[i].name);
    }
    free(z->e);
    if (z->f) {
        fclose(z->f);
    }
    memset(z, 0, sizeof *z);
}

/* Code page 437, the bytes 0x80-0xFF: what a name means when the archive does
 * not set the UTF-8 flag (bit 11), which is how the format defines it. */
static const unsigned short zip_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC,
    0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2,
    0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310,
    0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C,
    0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF,
    0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2,
    0x25A0, 0x00A0
};

static char *zip_name(const unsigned char *p, size_t n, int utf8) {
    char *out = malloc(n * 3 + 1);
    if (!out) {
        abort();
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = p[i];
        if (utf8 || c < 0x80) {
            out[o++] = (char)c;
            continue;
        }
        unsigned u = zip_cp437[c - 0x80];
        if (u < 0x800) {
            out[o++] = (char)(0xC0 | (u >> 6));
            out[o++] = (char)(0x80 | (u & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (u >> 12));
            out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (u & 0x3F));
        }
    }
    out[o] = '\0';
    return out;
}

/* Open and index the archive. Answers 1, or 0 having raised. */
static int zip_open(const char *verb, const char *path, ZipArchive *z) {
    char m[700];
    memset(z, 0, sizeof *z);
    z->path = path;
    z->f = fopen(path, "rb");
    if (!z->f) {
        snprintf(m, sizeof m, "could not read file: %s (%s)", path, strerror(path_open_errno(path, errno)));
        runtime_error_raise(m, 1004, "file operation");
        return 0;
    }
    if (stream_is_directory(z->f)) {
        fclose(z->f);
        z->f = NULL;
        snprintf(m, sizeof m, "could not read file: %s (%s)", path, strerror(EISDIR));
        runtime_error_raise(m, 1004, "file operation");
        return 0;
    }
    if (gb_fseek(z->f, 0, SEEK_END) != 0) {
        snprintf(m, sizeof m, "%s: could not read %s (%s)", verb, path, strerror(errno));
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    gb_off_t len = gb_ftell(z->f);
    /* the End of Central Directory record: 22 bytes and a comment of up to
       64 KB, so it is found by scanning back over at most that much */
    size_t tail = len < 22 + 65535 ? (size_t)len : 22 + 65535;
    unsigned char *buf = malloc(tail ? tail : 1);
    if (!buf) {
        abort();
    }
    if (gb_fseek(z->f, len - (gb_off_t)tail, SEEK_SET) != 0 || fread(buf, 1, tail, z->f) != tail) {
        free(buf);
        snprintf(m, sizeof m, "%s: could not read %s", verb, path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    long eocd = -1;
    for (size_t back = 22; back <= tail; back++) {
        const unsigned char *p = buf + (tail - back);
        if (p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x05 && p[3] == 0x06) {
            eocd = (long)(tail - back);
            break;
        }
    }
    if (eocd < 0) {
        free(buf);
        snprintf(m, sizeof m, "%s: %s is not a ZIP file (it has no end-of-central-directory record)", verb, path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    const unsigned char *e = buf + eocd;
    if ((size_t)eocd + 22 + zip_u16(e + 20) > tail) {
        free(buf);
        snprintf(m, sizeof m, "%s: %s is damaged: it ends inside its own archive comment (was it cut short?)", verb,
                 path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    unsigned disk = zip_u16(e + 4), cd_disk = zip_u16(e + 6);
    unsigned entries = zip_u16(e + 10);
    unsigned long cd_size = zip_u32(e + 12), cd_off = zip_u32(e + 16);
    free(buf);
    if (disk != 0 || cd_disk != 0) {
        snprintf(m, sizeof m, "%s: %s is one part of a spanned archive, which is not supported", verb, path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    if (entries == 0xFFFFU || cd_size == 0xFFFFFFFFUL || cd_off == 0xFFFFFFFFUL) {
        snprintf(m, sizeof m, "%s: %s is a ZIP64 archive (past 4 GB or 65535 members), which is not supported", verb,
                 path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    if ((gb_off_t)cd_off + (gb_off_t)cd_size > len) {
        snprintf(m, sizeof m, "%s: %s is damaged: its central directory runs past the end of the file", verb, path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    unsigned char *cd = malloc(cd_size ? cd_size : 1);
    if (!cd) {
        abort();
    }
    if (gb_fseek(z->f, (gb_off_t)cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, z->f) != cd_size) {
        free(cd);
        snprintf(m, sizeof m, "%s: could not read the central directory of %s", verb, path);
        zip_close(z);
        zip_raise(m);
        return 0;
    }
    z->e = calloc(entries ? entries : 1, sizeof *z->e);
    if (!z->e) {
        abort();
    }
    const unsigned char *p = cd, *end = cd + cd_size;
    for (unsigned i = 0; i < entries; i++) {
        if (p + 46 > end || !(p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x01 && p[3] == 0x02)) {
            free(cd);
            snprintf(m, sizeof m, "%s: %s is damaged: member %u of %u has no central directory entry", verb, path,
                     i + 1, entries);
            zip_close(z);
            zip_raise(m);
            return 0;
        }
        unsigned name_len = zip_u16(p + 28), extra_len = zip_u16(p + 30), comment_len = zip_u16(p + 32);
        if (p + 46 + name_len + extra_len + comment_len > end) {
            free(cd);
            snprintf(m, sizeof m, "%s: %s is damaged: member %u's entry runs past the central directory", verb,
                     path, i + 1);
            zip_close(z);
            zip_raise(m);
            return 0;
        }
        ZipEntry *x = &z->e[z->n++];
        x->made_by = zip_u16(p + 4) >> 8;
        x->flags = zip_u16(p + 8);
        x->method = zip_u16(p + 10);
        x->dos_time = zip_u16(p + 12);
        x->dos_date = zip_u16(p + 14);
        x->crc = zip_u32(p + 16);
        x->packed = zip_u32(p + 20);
        x->size = zip_u32(p + 24);
        x->external = zip_u32(p + 38);
        x->local_off = zip_u32(p + 42);
        x->name = zip_name(p + 46, name_len, (x->flags & 0x0800) != 0);
        p += 46 + name_len + extra_len + comment_len;
    }
    free(cd);
    return 1;
}

static int zip_is_dir(const ZipEntry *x) {
    size_t n = strlen(x->name);
    return n > 0 && x->name[n - 1] == '/';
}

/* A Unix symbolic link: made on Unix, with S_IFLNK in the mode's type bits. */
static int zip_is_link(const ZipEntry *x) {
    return x->made_by == 3 && ((x->external >> 16) & 0170000) == 0120000;
}

static const char *zip_method_name(unsigned method) {
    switch (method) {
    case 0: return "store";
    case 8: return "deflate";
    case 9: return "deflate64";
    case 12: return "bzip2";
    case 14: return "lzma";
    case 93: return "zstd";
    case 95: return "xz";
    default: return "other";
    }
}

static Value zip_modified(const ZipEntry *x) {
    DateTime dt = {0};
    dt.year = 1980 + (int)(x->dos_date >> 9);
    dt.month = (int)((x->dos_date >> 5) & 0x0F);
    dt.day = (int)(x->dos_date & 0x1F);
    dt.hour = (int)(x->dos_time >> 11);
    dt.minute = (int)((x->dos_time >> 5) & 0x3F);
    dt.second = (int)((x->dos_time & 0x1F) * 2);
    if (dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > 31 || dt.hour > 23 || dt.minute > 59 ||
        dt.second > 59) {
        return value_unknown();
    }
    dt.precision = PREC_SECOND;
    return value_datetime(dt);
}

/* One member's bytes, checked against its recorded size and CRC-32. Answers
 * a string, or null having raised. */
static Value zip_member_bytes(const char *verb, ZipArchive *z, const ZipEntry *x) {
    char m[900];
    if (zip_is_dir(x)) {
        snprintf(m, sizeof m, "%s: \"%s\" in %s is a folder, which has no bytes", verb, x->name, z->path);
        return zip_raise(m);
    }
    if (x->flags & 0x0001) {
        snprintf(m, sizeof m, "%s: \"%s\" in %s is encrypted, which is not supported", verb, x->name, z->path);
        return zip_raise(m);
    }
    if (x->method != 0 && x->method != 8) {
        snprintf(m, sizeof m, "%s: \"%s\" in %s is compressed with %s (method %u); only store and deflate are "
                 "supported", verb, x->name, z->path, zip_method_name(x->method), x->method);
        return zip_raise(m);
    }
    if (x->size == 0xFFFFFFFFUL || x->packed == 0xFFFFFFFFUL || x->local_off == 0xFFFFFFFFUL) {
        snprintf(m, sizeof m, "%s: \"%s\" in %s is a ZIP64 member (past 4 GB), which is not supported", verb,
                 x->name, z->path);
        return zip_raise(m);
    }
    unsigned char lh[30];
    if (gb_fseek(z->f, (gb_off_t)x->local_off, SEEK_SET) != 0 || fread(lh, 1, 30, z->f) != 30 ||
        !(lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04)) {
        snprintf(m, sizeof m, "%s: %s is damaged: \"%s\" has no local header where its entry says", verb, z->path,
                 x->name);
        return zip_raise(m);
    }
    /* the local name and extra fields may differ in length from the central
       directory's, so the data starts where THIS header says */
    gb_off_t data = (gb_off_t)x->local_off + 30 + zip_u16(lh + 26) + zip_u16(lh + 28);
    unsigned char *packed = malloc(x->packed ? x->packed : 1);
    if (!packed) {
        abort();
    }
    if (gb_fseek(z->f, data, SEEK_SET) != 0 || fread(packed, 1, x->packed, z->f) != x->packed) {
        free(packed);
        snprintf(m, sizeof m, "%s: %s is damaged: \"%s\" runs past the end of the file", verb, z->path, x->name);
        return zip_raise(m);
    }
    unsigned char *out;
    if (x->method == 0) {
        if (x->packed != x->size) {
            free(packed);
            snprintf(m, sizeof m, "%s: %s is damaged: \"%s\" is stored, and its two sizes differ", verb, z->path,
                     x->name);
            return zip_raise(m);
        }
        out = packed;
    } else {
        out = malloc(x->size ? x->size : 1);
        if (!out) {
            abort();
        }
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        int ok = inflateInit2(&zs, -MAX_WBITS) == Z_OK;   /* raw deflate: ZIP has no zlib wrapper */
        if (ok) {
            zs.next_in = packed;
            zs.avail_in = (uInt)x->packed;
            zs.next_out = out;
            zs.avail_out = (uInt)x->size;
            int rc = inflate(&zs, Z_FINISH);
            ok = rc == Z_STREAM_END && zs.total_out == x->size;
            inflateEnd(&zs);
        }
        free(packed);
        if (!ok) {
            free(out);
            snprintf(m, sizeof m, "%s: %s is damaged: \"%s\" does not inflate to its recorded %lu bytes", verb,
                     z->path, x->name, x->size);
            return zip_raise(m);
        }
    }
    unsigned long crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out, (uInt)x->size);
    if (crc != x->crc) {
        free(out);
        snprintf(m, sizeof m, "%s: %s is damaged: \"%s\" does not match its CRC-32", verb, z->path, x->name);
        return zip_raise(m);
    }
    Value v = value_string_n((const char *)out, x->size);
    free(out);
    return v;
}

/* Where a member may be written: a relative path of ordinary components.
 * Answers NULL, or why not. */
static const char *zip_unsafe_name(const char *name) {
    if (name[0] == '\0') {
        return "it has no name";
    }
    if (name[0] == '/') {
        return "it is an absolute path";
    }
    if (strchr(name, '\\')) {
        return "it contains a backslash";
    }
    if (isalpha((unsigned char)name[0]) && name[1] == ':') {
        return "it names a drive";
    }
    const char *p = name;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            return "it climbs out with ..";
        }
        if (n == 0 && slash) {
            return "it has an empty component";
        }
        if (!slash) {
            break;
        }
        p = slash + 1;
    }
    return NULL;
}

static int zip_path_arg(const char *verb, Value v, const char *what) {
    char m[300];
    if (v.kind != VALUE_FILE && v.kind != VALUE_STRING) {
        snprintf(m, sizeof m, "%s expects %s, such as {file}\"partner.zip\"", verb, what);
        zip_raise(m);
        return 0;
    }
    return 1;
}

static Value zip_members(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        return zip_raise("zip.members expects one argument, the ZIP file");
    }
    Value f = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(f);
        return value_null();
    }
    if (!zip_path_arg("zip.members", f, "a file")) {
        value_free(f);
        return value_null();
    }
    ZipArchive z;
    if (!zip_open("zip.members", file_target_path(f), &z)) {
        value_free(f);
        return value_null();
    }
    Value out = value_array(NULL, 0);
    for (size_t i = 0; i < z.n; i++) {
        const ZipEntry *x = &z.e[i];
        Value r = value_record(NULL, 0);
        record_set(&r, "name", value_string(x->name));
        record_set(&r, "size", value_number_exact((long long)x->size));
        record_set(&r, "packed", value_number_exact((long long)x->packed));
        record_set(&r, "method", value_string(zip_method_name(x->method)));
        record_set(&r, "modified", zip_modified(x));
        record_set(&r, "folder", value_bool(zip_is_dir(x)));
        record_set(&r, "link", value_bool(zip_is_link(x)));
        record_set(&r, "encrypted", value_bool((x->flags & 0x0001) != 0));
        record_set(&r, "crc", value_number_exact((long long)x->crc));
        Value ignored = append_to_array_ref(&out, r, 0);
        value_free(ignored);
    }
    zip_close(&z);
    value_free(f);
    return out;
}

/* The one entry named `name`; two of that name is a refusal, not a pick. */
static const ZipEntry *zip_find(const char *verb, ZipArchive *z, const char *name) {
    char m[900];
    const ZipEntry *hit = NULL;
    for (size_t i = 0; i < z->n; i++) {
        if (strcmp(z->e[i].name, name) == 0) {
            if (hit) {
                snprintf(m, sizeof m, "%s: %s holds two members named \"%s\", and which one is meant cannot be "
                         "told", verb, z->path, name);
                zip_raise(m);
                return NULL;
            }
            hit = &z->e[i];
        }
    }
    if (!hit) {
        snprintf(m, sizeof m, "%s: %s has no member named \"%s\" (zip.members lists them)", verb, z->path, name);
        zip_raise(m);
    }
    return hit;
}

static Value zip_read(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        return zip_raise("zip.read expects the ZIP file and a member's name");
    }
    Value f = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(f);
        return value_null();
    }
    Value name = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(f);
        value_free(name);
        return value_null();
    }
    if (!zip_path_arg("zip.read", f, "a file")) {
        value_free(f);
        value_free(name);
        return value_null();
    }
    if (name.kind != VALUE_STRING) {
        value_free(f);
        value_free(name);
        return zip_raise("zip.read expects the member's name as text, as zip.members gives it");
    }
    ZipArchive z;
    if (!zip_open("zip.read", file_target_path(f), &z)) {
        value_free(f);
        value_free(name);
        return value_null();
    }
    Value out = value_null();
    const ZipEntry *x = zip_find("zip.read", &z, name.as.string);
    if (x) {
        out = zip_member_bytes("zip.read", &z, x);
    }
    zip_close(&z);
    value_free(f);
    value_free(name);
    return out;
}

/* zip.extract(f, folder[, options]): every member written under `folder`,
 * which must exist. Every name is checked before anything is written, so an
 * archive with one unsafe name writes nothing. A file already there is
 * refused unless `replace: true`. A link is refused: an archive cannot be
 * allowed to make one. Answers the paths written, in archive order. */
static Value zip_extract(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        return zip_raise("zip.extract expects the ZIP file, a folder, and an optional options record");
    }
    Value f = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(f);
        return value_null();
    }
    Value dir = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(f);
        value_free(dir);
        return value_null();
    }
    Value opts = value_null();
    if (argc == 3) {
        opts = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(f);
            value_free(dir);
            value_free(opts);
            return value_null();
        }
    }
    Value out = value_null();
    char m[1200];
    int replace = 0;
    ZipArchive z = {0};
    if (!zip_path_arg("zip.extract", f, "a file")) {
        goto done;
    }
    if (dir.kind != VALUE_DIR && dir.kind != VALUE_STRING) {
        zip_raise("zip.extract expects a folder to write into, such as {dir}\"incoming\"");
        goto done;
    }
    if (opts.kind != VALUE_NULL) {
        if (opts.kind != VALUE_RECORD) {
            zip_raise("zip.extract: the options must be a record, such as { replace: true }");
            goto done;
        }
        for (size_t i = 0; i < opts.as.record.count; i++) {
            RecordField *rf = &opts.as.record.fields[i];
            if (strcmp(rf->name, "replace") != 0) {
                snprintf(m, sizeof m, "zip.extract: no option named %s (the one option is replace)", rf->name);
                zip_raise(m);
                goto done;
            }
            if (rf->value->kind != VALUE_BOOL) {
                zip_raise("zip.extract: replace must be true or false");
                goto done;
            }
            replace = rf->value->as.boolean;
        }
    }
    const char *root = directory_path(dir);
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(m, sizeof m, "zip.extract: the folder %s does not exist (make_dir makes one)", root);
        zip_raise(m);
        goto done;
    }
    if (!zip_open("zip.extract", file_target_path(f), &z)) {
        goto done;
    }
    for (size_t i = 0; i < z.n; i++) {
        const char *why = zip_unsafe_name(z.e[i].name);
        if (!why && zip_is_link(&z.e[i])) {
            why = "it is a symbolic link";
        }
        if (why) {
            snprintf(m, sizeof m, "zip.extract: %s holds \"%s\", which cannot be written: %s; nothing was "
                     "extracted", z.path, z.e[i].name, why);
            zip_raise(m);
            goto done;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(z.e[i].name, z.e[j].name) == 0) {
                snprintf(m, sizeof m, "zip.extract: %s holds two members named \"%s\"; nothing was extracted",
                         z.path, z.e[i].name);
                zip_raise(m);
                goto done;
            }
        }
    }
    out = value_array(NULL, 0);
    for (size_t i = 0; i < z.n; i++) {
        const ZipEntry *x = &z.e[i];
        size_t plen = strlen(root) + 1 + strlen(x->name) + 1;
        char *target = malloc(plen);
        if (!target) {
            abort();
        }
        snprintf(target, plen, "%s/%s", root, x->name);
        /* the folders on the way: made, and never followed out of the root
           (no member can be a link, and no name climbs) */
        char *slash = strrchr(target, '/');
        if (zip_is_dir(x)) {
            target[strlen(target) - 1] = '\0';
            slash = target + strlen(target);
        }
        char why[600];
        if (slash && slash > target + strlen(root)) {
            char keep = *slash;
            *slash = '\0';
            if (make_dir_parents(target, why, sizeof why) != 0) {
                snprintf(m, sizeof m, "zip.extract: %s", why);
                *slash = keep;
                free(target);
                zip_raise(m);
                value_free(out);
                out = value_null();
                goto done;
            }
            *slash = keep;
        }
        if (zip_is_dir(x)) {
            free(target);
            continue;
        }
        Value bytes = zip_member_bytes("zip.extract", &z, x);
        if (error_action_pending()) {
            free(target);
            value_free(bytes);
            value_free(out);
            out = value_null();
            goto done;
        }
        FILE *w = fopen(target, replace ? "wb" : "wbx");
        if (!w) {
            if (errno == EEXIST) {
                snprintf(m, sizeof m, "zip.extract: %s is already there (replace: true writes over it)", target);
            } else {
                snprintf(m, sizeof m, "zip.extract: could not write %s (%s)", target, strerror(errno));
            }
            free(target);
            value_free(bytes);
            zip_raise(m);
            value_free(out);
            out = value_null();
            goto done;
        }
        size_t n = string_length(bytes.as.string);
        int bad = fwrite(bytes.as.string, 1, n, w) != n;
        bad |= fclose(w) != 0;
        value_free(bytes);
        if (bad) {
            snprintf(m, sizeof m, "zip.extract: could not write %s (%s)", target, strerror(errno));
            free(target);
            zip_raise(m);
            value_free(out);
            out = value_null();
            goto done;
        }
        Value ignored = append_to_array_ref(&out, value_file(target), 0);
        value_free(ignored);
        free(target);
    }
done:
    zip_close(&z);
    value_free(f);
    value_free(dir);
    value_free(opts);
    return out;
}

/* ------------------------------------------------------------- writing (G55)
 *
 * zip.write(f, members): a ZIP made of `members`, a list of { name, data },
 * in that order (gap G55 from the gBASIC books: a workbook's part could be
 * read out of its ZIP and not put back). Each member is deflated, or stored
 * when deflating would not shrink it.
 *
 * THE SAME MEMBERS GIVE THE SAME BYTES: every member is dated 1980-01-01
 * 00:00, the format's earliest date, and carries no extra fields, so an
 * archive written twice compares equal -- the book had Info-ZIP's zip run
 * over files touched to that date for it.
 *
 * Written only as this module reads: a name zip.extract would refuse, two
 * members of one name, and anything needing ZIP64 (a member or the archive
 * past 4 GB, or more than 65535 members) are refused before a byte is
 * written. A name past ASCII is marked UTF-8 (flag bit 11). A name ending in
 * "/" is a folder, and holds no data. */

typedef struct {
    unsigned char *d;
    size_t n, cap;
} ZipBuf;

static void zipb_put(ZipBuf *b, const void *p, size_t n) {
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n) cap *= 2;
        unsigned char *d = realloc(b->d, cap);
        if (!d) abort();
        b->d = d;
        b->cap = cap;
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

static void zipb_16(ZipBuf *b, unsigned v) {
    unsigned char t[2] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff) };
    zipb_put(b, t, 2);
}

static void zipb_32(ZipBuf *b, unsigned long v) {
    unsigned char t[4] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
                           (unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff) };
    zipb_put(b, t, 4);
}

/* raw deflate of `in`, or NULL when it would not shrink it */
static unsigned char *zip_deflate(const unsigned char *in, size_t n, size_t *out_n) {
    if (n == 0) return NULL;
    uLong bound = compressBound((uLong)n) + 64;
    unsigned char *out = malloc(bound);
    if (!out) abort();
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(out);
        return NULL;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)n;
    zs.next_out = out;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    size_t made = zs.total_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END || made >= n) {
        free(out);
        return NULL;
    }
    *out_n = made;
    return out;
}

static Value zip_write(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        return zip_raise("zip.write expects the ZIP file to write and its members, a list of { name, data }");
    }
    Value f = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(f);
        return value_null();
    }
    Value list = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(f);
        value_free(list);
        return value_null();
    }
    if (!zip_path_arg("zip.write", f, "the file to write")) {
        value_free(f);
        value_free(list);
        return value_null();
    }
    char m[900] = "";
    size_t count = list.kind == VALUE_ARRAY ? list.as.array.store->count : 0;
    if (list.kind != VALUE_ARRAY) {
        snprintf(m, sizeof m, "zip.write: the members are a list of { name, data }, such as "
                 "[{ name: \"a.txt\", data: \"hello\" }]");
    } else if (count > 65535) {
        snprintf(m, sizeof m, "zip.write: %zu members is more than the 65535 a ZIP holds without ZIP64, which "
                 "zip does not write", count);
    }
    /* every member checked before anything is written */
    for (size_t i = 0; !m[0] && i < count; i++) {
        Value *r = &list.as.array.store->items[i];
        RecordField *nf = r->kind == VALUE_RECORD ? record_find(r, "name") : NULL;
        RecordField *df = r->kind == VALUE_RECORD ? record_find(r, "data") : NULL;
        if (r->kind != VALUE_RECORD || !nf) {
            snprintf(m, sizeof m, "zip.write: member %zu is not a record { name, data }", i + 1);
            break;
        }
        for (size_t k = 0; k < r->as.record.count; k++) {
            const char *fname = r->as.record.fields[k].name;
            if (strcmp(fname, "name") != 0 && strcmp(fname, "data") != 0) {
                snprintf(m, sizeof m, "zip.write: member %zu has the field '%s'; a member is { name, data }", i + 1,
                         fname);
                break;
            }
        }
        if (m[0]) break;
        if (nf->value->kind != VALUE_STRING) {
            snprintf(m, sizeof m, "zip.write: member %zu's name is not text", i + 1);
            break;
        }
        const char *name = nf->value->as.string;
        size_t nn = string_length(name);
        const char *why = nn != strlen(name) ? "it holds a NUL" : zip_unsafe_name(name);
        if (!why && nn > 65535) why = "it is longer than the 65535 bytes a ZIP name holds";
        if (!why && utf8_first_invalid((const unsigned char *)name, nn) != nn) why = "it is not UTF-8";
        if (why) {
            snprintf(m, sizeof m, "zip.write: member %zu's name \"%.300s\" cannot be written: %s", i + 1, name, why);
            break;
        }
        int folder = nn > 0 && name[nn - 1] == '/';
        if (df && df->value->kind != VALUE_STRING) {
            snprintf(m, sizeof m, "zip.write: member \"%.300s\"'s data is not text (its bytes)", name);
            break;
        }
        size_t dn = df ? string_length(df->value->as.string) : 0;
        if (!df && !folder) {
            snprintf(m, sizeof m, "zip.write: member \"%.300s\" has no data (only a folder, a name ending in /, "
                     "has none)", name);
            break;
        }
        if (folder && dn) {
            snprintf(m, sizeof m, "zip.write: member \"%.300s\" is a folder (its name ends in /) and cannot hold "
                     "data", name);
            break;
        }
        if (dn > 0xFFFFFFFEul) {
            snprintf(m, sizeof m, "zip.write: member \"%.300s\" is past 4 GB, which needs ZIP64, which zip does "
                     "not write", name);
            break;
        }
        for (size_t j = 0; j < i; j++) {
            Value *o = &list.as.array.store->items[j];
            const char *on = record_find(o, "name")->value->as.string;
            if (string_length(on) == nn && memcmp(on, name, nn) == 0) {
                snprintf(m, sizeof m, "zip.write: two members are named \"%.300s\" (%zu and %zu), and a reader "
                         "could not tell which is meant", name, j + 1, i + 1);
                break;
            }
        }
    }
    if (m[0]) {
        value_free(f);
        value_free(list);
        return zip_raise(m);
    }
    ZipBuf body = {0}, dir = {0};
    for (size_t i = 0; i < count && !m[0]; i++) {
        Value *r = &list.as.array.store->items[i];
        const char *name = record_find(r, "name")->value->as.string;
        RecordField *df = record_find(r, "data");
        const unsigned char *data = df ? (const unsigned char *)df->value->as.string : (const unsigned char *)"";
        size_t n = df ? string_length(df->value->as.string) : 0, nn = strlen(name);
        size_t packed = 0;
        unsigned char *z = zip_deflate(data, n, &packed);
        unsigned method = z ? 8 : 0;
        if (!z) packed = n;
        unsigned long crc = crc32(0L, Z_NULL, 0);
        for (size_t at = 0; at < n;) {
            uInt step = n - at > 0x40000000u ? 0x40000000u : (uInt)(n - at);
            crc = crc32(crc, data + at, step);
            at += step;
        }
        int utf8 = 0;
        for (size_t k = 0; k < nn; k++) utf8 |= (unsigned char)name[k] >= 0x80;
        unsigned flags = utf8 ? 0x0800 : 0;
        size_t local = body.n;
        if (local > 0xFFFFFFFEul || local + 30 + nn + packed > 0xFFFFFFFEul) {
            snprintf(m, sizeof m, "zip.write: the archive passes 4 GB at member \"%.300s\", which needs ZIP64, "
                     "which zip does not write", name);
            free(z);
            break;
        }
        int folder = nn > 0 && name[nn - 1] == '/';
        zipb_32(&body, 0x04034b50);
        zipb_16(&body, 20);
        zipb_16(&body, flags);
        zipb_16(&body, method);
        zipb_16(&body, 0);        /* 00:00 */
        zipb_16(&body, 0x21);     /* 1980-01-01 */
        zipb_32(&body, crc);
        zipb_32(&body, (unsigned long)packed);
        zipb_32(&body, (unsigned long)n);
        zipb_16(&body, (unsigned)nn);
        zipb_16(&body, 0);
        zipb_put(&body, name, nn);
        zipb_put(&body, z ? z : data, packed);
        zipb_32(&dir, 0x02014b50);
        zipb_16(&dir, 0x0314);    /* made by Unix, so the mode below is read */
        zipb_16(&dir, 20);
        zipb_16(&dir, flags);
        zipb_16(&dir, method);
        zipb_16(&dir, 0);
        zipb_16(&dir, 0x21);
        zipb_32(&dir, crc);
        zipb_32(&dir, (unsigned long)packed);
        zipb_32(&dir, (unsigned long)n);
        zipb_16(&dir, (unsigned)nn);
        zipb_16(&dir, 0);
        zipb_16(&dir, 0);
        zipb_16(&dir, 0);
        zipb_16(&dir, 0);
        /* rw-r--r-- for a file, rwxr-xr-x for a folder, and MS-DOS's folder bit */
        zipb_32(&dir, folder ? (0040755ul << 16) | 0x10 : 0100644ul << 16);
        zipb_32(&dir, (unsigned long)local);
        zipb_put(&dir, name, nn);
        free(z);
    }
    if (!m[0] && body.n + dir.n > 0xFFFFFFFEul) {
        snprintf(m, sizeof m, "zip.write: the archive passes 4 GB, which needs ZIP64, which zip does not write");
    }
    if (m[0]) {
        free(body.d);
        free(dir.d);
        value_free(f);
        value_free(list);
        return zip_raise(m);
    }
    size_t cd = body.n;
    zipb_put(&body, dir.d ? dir.d : (unsigned char *)"", dir.n);
    zipb_32(&body, 0x06054b50);
    zipb_16(&body, 0);
    zipb_16(&body, 0);
    zipb_16(&body, (unsigned)count);
    zipb_16(&body, (unsigned)count);
    zipb_32(&body, (unsigned long)dir.n);
    zipb_32(&body, (unsigned long)cd);
    zipb_16(&body, 0);
    free(dir.d);
    const char *path = file_target_path(f);
    FILE *out = fopen(path, "wb");
    int failed = !out, saved = errno;
    if (out) {
        failed = fwrite(body.d, 1, body.n, out) != body.n || ferror(out);
        saved = errno;
        if (fclose(out) != 0 && !failed) {
            failed = 1;
            saved = errno;
        }
    }
    free(body.d);
    Value result = value_bool(1);
    if (failed) {
        snprintf(m, sizeof m, "could not write file: %s (%s)", path, strerror(out ? saved : path_open_errno(path, saved)));
        runtime_error_raise(m, 1004, "file operation");
        result = value_null();
    }
    value_free(f);
    value_free(list);
    return result;
}

#endif

static Value zip_eval_call(AstExpr *expr) {
    const char *verb = expr->as.call.name;
#if !HAVE_ZLIB
    (void)verb;
    return zip_raise("zip support is not available in this build; install zlib development files and rebuild");
#else
    if (strcmp(verb, "members") == 0) {
        return zip_members(expr);
    }
    if (strcmp(verb, "read") == 0) {
        return zip_read(expr);
    }
    if (strcmp(verb, "extract") == 0) {
        return zip_extract(expr);
    }
    if (strcmp(verb, "write") == 0) {
        return zip_write(expr);
    }
    char message[200];
    module_unknown_verb(message, sizeof message, "zip", verb);
    return zip_raise(message);
#endif
}
