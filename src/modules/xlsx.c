/* xlsx module — Office Open XML workbooks (docs/xlsx_design.md).
 *
 * Included into src/eval.c as a single translation unit, following
 * modules/xml.c and modules/rowmodel.c, so it can use the runtime's value
 * constructors and error reporting directly.
 *
 * STAGE 1 (this file, so far): the ZIP container, the PART TREE, and a
 * read-only workbook handle. Cells and the formula engine come next.
 *
 * WHY A PART TREE AND NOT A CELL DUMP (§13.A). The destination is round-trip:
 * read a workbook, change some cells, write it back without destroying the
 * charts, pivot tables, conditional formatting and macros we do not model. That
 * is only possible if the reader keeps EVERYTHING — so every part is retained
 * verbatim as raw decompressed bytes, whether or not we understand it, and the
 * modelled view is built alongside rather than instead. Discarding a part here
 * would be discovered much later, when write silently produced a lesser file.
 *
 * This is also why libxlsxwriter was rejected: it generates new workbooks and
 * cannot edit an existing one, so it can only ever emit what it knows about.
 *
 * DEPENDENCIES. zlib for inflate; the ZIP container itself is implemented here
 * (§13.B). libxml2 for the XML parts, via the same dependency the xml module
 * already uses. Both are optional at build time and the module degrades to a
 * clean runtime error, per the project convention.
 */

#if HAVE_ZLIB
#include <zlib.h>
#endif

/* ---------------------------------------------------------------- the parts
 *
 * A part is one entry in the ZIP: its name, and its DECOMPRESSED bytes. Bytes
 * are held rather than re-read on demand because a workbook is small relative
 * to memory (a 100 MB xlsx is a monster) and because write has to re-emit
 * untouched parts exactly, which is simplest when they never left. */
typedef struct {
    char *name;
    unsigned char *data;
    size_t length;
    int modelled;        /* 1 once something has interpreted it; diagnostic only */
} XlsxPart;

typedef struct {
    char *name;          /* the user-visible sheet name */
    char *part;          /* the part path backing it, e.g. xl/worksheets/sheet1.xml */
    char *rel_id;        /* r:id linking workbook.xml to the rels */
} XlsxSheet;

struct XlsxWorkbook {
    size_t ref_count;
    char *path;
    XlsxPart *parts;
    size_t part_count;
    XlsxSheet *sheets;
    size_t sheet_count;
    char **shared;       /* the shared-string table, resolved */
    size_t shared_count;
};

typedef struct XlsxWorkbook XlsxWorkbook;

static Value xlsx_raise(const char *message) {
    runtime_error_raise(message, 7001, "xlsx");
    return value_null();
}

#if HAVE_ZLIB && HAVE_LIBXML2

/* ------------------------------------------------------------ ZIP container
 *
 * Only what an .xlsx actually needs: the End of Central Directory record, the
 * central directory it points at, and stored/deflated entries. Encryption,
 * spanning and ZIP64 beyond the 4 GB boundary are not supported and say so
 * rather than misreading.
 *
 * Reading the CENTRAL DIRECTORY rather than walking local headers is
 * deliberate: local headers may carry zeroed sizes with the real values in a
 * trailing data descriptor, and the central directory is the authoritative
 * index in every case. */

static unsigned xlsx_u16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long xlsx_u32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Inflate `in` (raw deflate, no zlib header — which is what ZIP stores) into a
 * buffer of exactly `out_len` bytes. Returns NULL on failure. */
static unsigned char *xlsx_inflate(const unsigned char *in, size_t in_len, size_t out_len) {
    unsigned char *out = malloc(out_len ? out_len : 1);
    if (!out) {
        abort();
    }
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    /* -MAX_WBITS selects RAW deflate: ZIP entries have no zlib wrapper. */
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        free(out);
        return NULL;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = out;
    zs.avail_out = (uInt)out_len;
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || zs.total_out != out_len) {
        free(out);
        return NULL;
    }
    return out;
}

/* Locate the End of Central Directory record by scanning backwards for its
 * signature. The comment field is variable-length, so there is no fixed offset;
 * the scan is bounded by the maximum comment size (64 KB) plus the record. */
static long xlsx_find_eocd(const unsigned char *buf, size_t len) {
    if (len < 22) {
        return -1;
    }
    size_t max_back = 22 + 65535;
    if (max_back > len) {
        max_back = len;
    }
    for (size_t back = 22; back <= max_back; back++) {
        const unsigned char *p = buf + (len - back);
        if (p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x05 && p[3] == 0x06) {
            return (long)(len - back);
        }
    }
    return -1;
}

static void xlsx_parts_free(XlsxPart *parts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(parts[i].name);
        free(parts[i].data);
    }
    free(parts);
}

/* Read every entry of the ZIP at `buf` into a part array. Returns 1 on success;
 * on failure sets *err to a static message and leaves nothing allocated. */
static int xlsx_read_container(const unsigned char *buf, size_t len,
                               XlsxPart **out_parts, size_t *out_count,
                               const char **err) {
    *err = NULL;
    long eocd = xlsx_find_eocd(buf, len);
    if (eocd < 0) {
        *err = "xlsx: not a ZIP container (no end-of-central-directory record)";
        return 0;
    }
    const unsigned char *e = buf + eocd;
    unsigned entries = xlsx_u16(e + 10);
    unsigned long cd_size = xlsx_u32(e + 12);
    unsigned long cd_off = xlsx_u32(e + 16);

    if (cd_off == 0xFFFFFFFFUL || cd_size == 0xFFFFFFFFUL || entries == 0xFFFFU) {
        *err = "xlsx: ZIP64 containers are not supported";
        return 0;
    }
    if ((size_t)cd_off + (size_t)cd_size > len) {
        *err = "xlsx: central directory runs past the end of the file";
        return 0;
    }

    XlsxPart *parts = calloc(entries ? entries : 1, sizeof(XlsxPart));
    if (!parts) {
        abort();
    }
    size_t count = 0;
    const unsigned char *p = buf + cd_off;
    const unsigned char *cd_end = p + cd_size;

    for (unsigned i = 0; i < entries; i++) {
        if (p + 46 > cd_end) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: truncated central directory";
            return 0;
        }
        if (!(p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x01 && p[3] == 0x02)) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: bad central directory signature";
            return 0;
        }
        unsigned flags = xlsx_u16(p + 8);
        unsigned method = xlsx_u16(p + 10);
        unsigned long comp_size = xlsx_u32(p + 20);
        unsigned long uncomp_size = xlsx_u32(p + 24);
        unsigned name_len = xlsx_u16(p + 28);
        unsigned extra_len = xlsx_u16(p + 30);
        unsigned comment_len = xlsx_u16(p + 32);
        unsigned long local_off = xlsx_u32(p + 42);
        const unsigned char *name = p + 46;

        if (flags & 0x0001) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: encrypted workbooks are not supported";
            return 0;
        }
        if (method != 0 && method != 8) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: unsupported ZIP compression method (only store and deflate)";
            return 0;
        }

        /* The local header's name and extra fields may differ in LENGTH from
         * the central directory's, so the data offset must be computed from the
         * local header itself rather than assumed. */
        if ((size_t)local_off + 30 > len) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: local header offset past end of file";
            return 0;
        }
        const unsigned char *lh = buf + local_off;
        if (!(lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04)) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: bad local header signature";
            return 0;
        }
        size_t data_off = (size_t)local_off + 30 + xlsx_u16(lh + 26) + xlsx_u16(lh + 28);
        if (data_off + comp_size > len) {
            xlsx_parts_free(parts, count);
            *err = "xlsx: entry data runs past the end of the file";
            return 0;
        }

        unsigned char *data = NULL;
        if (method == 0) {
            if (comp_size != uncomp_size) {
                xlsx_parts_free(parts, count);
                *err = "xlsx: stored entry with mismatched sizes";
                return 0;
            }
            data = malloc(uncomp_size ? uncomp_size : 1);
            if (!data) {
                abort();
            }
            memcpy(data, buf + data_off, uncomp_size);
        } else {
            data = xlsx_inflate(buf + data_off, comp_size, uncomp_size);
            if (!data) {
                xlsx_parts_free(parts, count);
                *err = "xlsx: a compressed entry could not be inflated";
                return 0;
            }
        }

        parts[count].name = malloc(name_len + 1);
        if (!parts[count].name) {
            abort();
        }
        memcpy(parts[count].name, name, name_len);
        parts[count].name[name_len] = '\0';
        parts[count].data = data;
        parts[count].length = (size_t)uncomp_size;
        parts[count].modelled = 0;
        count++;

        p += 46 + name_len + extra_len + comment_len;
    }

    *out_parts = parts;
    *out_count = count;
    return 1;
}

static XlsxPart *xlsx_find_part(XlsxWorkbook *wb, const char *name) {
    for (size_t i = 0; i < wb->part_count; i++) {
        if (strcmp(wb->parts[i].name, name) == 0) {
            return &wb->parts[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- XML helpers */

/* Parse one retained part's bytes as XML. The part stays owned by the
 * workbook; the document is the caller's to free. */
static xmlDocPtr xlsx_parse_part(XlsxPart *part) {
    if (!part) {
        return NULL;
    }
    part->modelled = 1;
    return xmlReadMemory((const char *)part->data, (int)part->length,
                         "part.xml", NULL,
                         XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
}

static char *xlsx_prop(xmlNodePtr node, const char *name) {
    xmlChar *v = xmlGetProp(node, (const xmlChar *)name);
    if (!v) {
        return NULL;
    }
    char *out = copy_string((const char *)v);
    xmlFree(v);
    return out;
}

static int xlsx_is(xmlNodePtr n, const char *name) {
    return n && n->type == XML_ELEMENT_NODE && n->name &&
           strcmp((const char *)n->name, name) == 0;
}

/* Concatenated text of an element's descendants, honouring <t> runs. Used for
 * shared strings, where a single entry may be split across formatting runs. */
static void xlsx_collect_text(xmlNodePtr node, char **buf, size_t *len, size_t *cap) {
    for (xmlNodePtr c = node ? node->children : NULL; c; c = c->next) {
        if (c->type == XML_TEXT_NODE || c->type == XML_CDATA_SECTION_NODE) {
            if (!c->content) {
                continue;
            }
            size_t add = strlen((const char *)c->content);
            if (*len + add + 1 > *cap) {
                *cap = (*cap ? *cap * 2 : 64);
                while (*len + add + 1 > *cap) {
                    *cap *= 2;
                }
                char *g = realloc(*buf, *cap);
                if (!g) {
                    abort();
                }
                *buf = g;
            }
            memcpy(*buf + *len, c->content, add);
            *len += add;
            (*buf)[*len] = '\0';
        }
        xlsx_collect_text(c, buf, len, cap);
    }
}

static char *xlsx_text_of(xmlNodePtr node) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    xlsx_collect_text(node, &buf, &len, &cap);
    if (!buf) {
        buf = copy_string("");
    }
    return buf;
}

/* --------------------------------------------------------- workbook parsing
 *
 * Sheet order and names come from xl/workbook.xml; the part backing each sheet
 * comes from the relationship id, resolved through xl/_rels/workbook.xml.rels.
 * Sheet NAME cannot be used to guess the part path — Excel writes sheet1.xml,
 * sheet2.xml in creation order, which need not match tab order, and a renamed
 * or deleted sheet breaks the correspondence entirely. */
static void xlsx_load_sheets(XlsxWorkbook *wb) {
    XlsxPart *wbp = xlsx_find_part(wb, "xl/workbook.xml");
    XlsxPart *relp = xlsx_find_part(wb, "xl/_rels/workbook.xml.rels");
    if (!wbp) {
        return;
    }
    xmlDocPtr wdoc = xlsx_parse_part(wbp);
    if (!wdoc) {
        return;
    }

    /* rel id -> target, for the workbook's own relationships. */
    char **rel_ids = NULL;
    char **rel_targets = NULL;
    size_t rel_count = 0;
    if (relp) {
        xmlDocPtr rdoc = xlsx_parse_part(relp);
        if (rdoc) {
            xmlNodePtr root = xmlDocGetRootElement(rdoc);
            for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
                if (!xlsx_is(n, "Relationship")) {
                    continue;
                }
                char *id = xlsx_prop(n, "Id");
                char *tgt = xlsx_prop(n, "Target");
                if (id && tgt) {
                    rel_ids = realloc(rel_ids, (rel_count + 1) * sizeof(char *));
                    rel_targets = realloc(rel_targets, (rel_count + 1) * sizeof(char *));
                    if (!rel_ids || !rel_targets) {
                        abort();
                    }
                    rel_ids[rel_count] = id;
                    rel_targets[rel_count] = tgt;
                    rel_count++;
                } else {
                    free(id);
                    free(tgt);
                }
            }
            xmlFreeDoc(rdoc);
        }
    }

    xmlNodePtr root = xmlDocGetRootElement(wdoc);
    for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
        if (!xlsx_is(n, "sheets")) {
            continue;
        }
        for (xmlNodePtr sh = n->children; sh; sh = sh->next) {
            if (!xlsx_is(sh, "sheet")) {
                continue;
            }
            char *name = xlsx_prop(sh, "name");
            char *rid = xlsx_prop(sh, "id");   /* r:id, namespace-stripped by libxml2 prop lookup */
            if (!rid) {
                rid = xlsx_prop(sh, "r:id");
            }
            char *target = NULL;
            for (size_t i = 0; i < rel_count; i++) {
                if (rid && strcmp(rel_ids[i], rid) == 0) {
                    target = copy_string(rel_targets[i]);
                    break;
                }
            }
            /* Relationship targets in xl/_rels are relative to xl/. */
            char *part_path = NULL;
            if (target) {
                size_t need = strlen("xl/") + strlen(target) + 1;
                part_path = malloc(need);
                if (!part_path) {
                    abort();
                }
                snprintf(part_path, need, "xl/%s", target);
                free(target);
            }
            wb->sheets = realloc(wb->sheets, (wb->sheet_count + 1) * sizeof(XlsxSheet));
            if (!wb->sheets) {
                abort();
            }
            wb->sheets[wb->sheet_count].name = name ? name : copy_string("");
            wb->sheets[wb->sheet_count].part = part_path;
            wb->sheets[wb->sheet_count].rel_id = rid;
            wb->sheet_count++;
        }
    }

    for (size_t i = 0; i < rel_count; i++) {
        free(rel_ids[i]);
        free(rel_targets[i]);
    }
    free(rel_ids);
    free(rel_targets);
    xmlFreeDoc(wdoc);
}

/* The shared-string table. Most text in a sheet is a numeric index into this,
 * so it has to be resolved before any cell can be read. */
static void xlsx_load_shared(XlsxWorkbook *wb) {
    XlsxPart *p = xlsx_find_part(wb, "xl/sharedStrings.xml");
    if (!p) {
        return;
    }
    xmlDocPtr doc = xlsx_parse_part(p);
    if (!doc) {
        return;
    }
    xmlNodePtr root = xmlDocGetRootElement(doc);
    for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
        if (!xlsx_is(n, "si")) {
            continue;
        }
        char *text = xlsx_text_of(n);
        wb->shared = realloc(wb->shared, (wb->shared_count + 1) * sizeof(char *));
        if (!wb->shared) {
            abort();
        }
        wb->shared[wb->shared_count++] = text;
    }
    xmlFreeDoc(doc);
}

/* ------------------------------------------------------------------- cells
 *
 * A sheet is SPARSE: <sheetData> lists only rows that have content, and each
 * row only the cells that do. That is modelled faithfully rather than expanded
 * into a dense grid — a sheet whose used range is A1:Z100000 but which holds
 * two hundred values should cost two hundred cells, not two million.
 *
 * A cell carries FOUR things and the reader keeps all of them (§13.A): the
 * cached value, the formula text if any, the value's type, and the style index.
 * Dropping the formula would make recalculation impossible later; dropping the
 * style index would lose the number format, which is the only thing
 * distinguishing a date from the number 45000. */

/* "AB12" -> column 27 (0-based), row 12 (1-based). Returns 0 if malformed. */
static int xlsx_parse_ref(const char *ref, long *col, long *row) {
    if (!ref || !*ref) {
        return 0;
    }
    long c = 0;
    const char *p = ref;
    while (*p >= 'A' && *p <= 'Z') {
        c = c * 26 + (*p - 'A' + 1);
        p++;
    }
    if (c == 0 || *p < '0' || *p > '9') {
        return 0;
    }
    long r = 0;
    while (*p >= '0' && *p <= '9') {
        r = r * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') {
        return 0;
    }
    *col = c - 1;
    *row = r;
    return 1;
}

/* Build the gBASIC record for one <c> element. Returns a record value. */
static Value xlsx_cell_record(XlsxWorkbook *wb, xmlNodePtr c) {
    char *ref = xlsx_prop(c, "r");
    char *type = xlsx_prop(c, "t");
    char *style = xlsx_prop(c, "s");

    char *formula = NULL;
    char *vtext = NULL;
    char *inline_text = NULL;
    for (xmlNodePtr k = c->children; k; k = k->next) {
        if (xlsx_is(k, "f")) {
            formula = xlsx_text_of(k);
        } else if (xlsx_is(k, "v")) {
            vtext = xlsx_text_of(k);
        } else if (xlsx_is(k, "is")) {
            inline_text = xlsx_text_of(k);
        }
    }

    /* The `t` attribute names the CACHED VALUE's type, not the cell's format.
     * Absent means number, which is also what a date is stored as — the style's
     * number format is the only thing that distinguishes them, so the style
     * index is preserved for the styles pass rather than guessed at here. */
    const char *kind = "number";
    Value val = value_unknown();
    if (type && strcmp(type, "s") == 0) {
        kind = "text";
        long idx = vtext ? strtol(vtext, NULL, 10) : -1;
        if (idx >= 0 && (size_t)idx < wb->shared_count) {
            val = value_string(wb->shared[idx]);
        } else {
            val = value_unknown();
            kind = "text";
        }
    } else if (type && strcmp(type, "inlineStr") == 0) {
        kind = "text";
        val = value_string(inline_text ? inline_text : "");
    } else if (type && strcmp(type, "str") == 0) {
        kind = "text";
        val = value_string(vtext ? vtext : "");
    } else if (type && strcmp(type, "b") == 0) {
        kind = "boolean";
        val = value_bool(vtext && strcmp(vtext, "0") != 0);
    } else if (type && strcmp(type, "e") == 0) {
        /* An Excel error (#DIV/0!, #N/A). Kept as its literal text under its own
         * kind: it is neither a number nor a string, and flattening it to either
         * would let a spreadsheet error silently become data. */
        kind = "error";
        val = value_string(vtext ? vtext : "#ERROR");
    } else {
        kind = "number";
        if (vtext) {
            val = value_number(strtod(vtext, NULL));
        }
    }

    RecordField *fields = calloc(5, sizeof(RecordField));
    if (!fields) {
        abort();
    }
    fields[0].name = copy_string("ref");
    fields[0].value = cell_alloc();
    *fields[0].value = value_string(ref ? ref : "");
    fields[1].name = copy_string("value");
    fields[1].value = cell_alloc();
    *fields[1].value = val;
    fields[2].name = copy_string("kind");
    fields[2].value = cell_alloc();
    *fields[2].value = value_string(kind);
    fields[3].name = copy_string("formula");
    fields[3].value = cell_alloc();
    *fields[3].value = formula ? value_string(formula) : value_unknown();
    fields[4].name = copy_string("style");
    fields[4].value = cell_alloc();
    *fields[4].value = style ? value_number(strtod(style, NULL)) : value_number(0);

    free(ref);
    free(type);
    free(style);
    free(formula);
    free(vtext);
    free(inline_text);
    return value_record(fields, 5);
}

static XlsxSheet *xlsx_find_sheet(XlsxWorkbook *wb, const char *name) {
    for (size_t i = 0; i < wb->sheet_count; i++) {
        if (strcmp(wb->sheets[i].name, name) == 0) {
            return &wb->sheets[i];
        }
    }
    return NULL;
}

#endif /* HAVE_ZLIB && HAVE_LIBXML2 */

/* ----------------------------------------------------------- the value kind */

static Value value_workbook(XlsxWorkbook *wb) {
    Value value = {0};
    value.kind = VALUE_WORKBOOK;
    value.as.workbook = wb;
    return value;
}

static void xlsx_workbook_retain(XlsxWorkbook *wb) {
    if (wb) {
        wb->ref_count++;
    }
}

static void xlsx_workbook_release(XlsxWorkbook *wb) {
    if (!wb) {
        return;
    }
    if (--wb->ref_count > 0) {
        return;
    }
#if HAVE_ZLIB && HAVE_LIBXML2
    xlsx_parts_free(wb->parts, wb->part_count);
#endif
    for (size_t i = 0; i < wb->sheet_count; i++) {
        free(wb->sheets[i].name);
        free(wb->sheets[i].part);
        free(wb->sheets[i].rel_id);
    }
    free(wb->sheets);
    for (size_t i = 0; i < wb->shared_count; i++) {
        free(wb->shared[i]);
    }
    free(wb->shared);
    free(wb->path);
    free(wb);
}

/* ------------------------------------------------------------ the gBASIC API */

static Value xlsx_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;

#if !HAVE_ZLIB || !HAVE_LIBXML2
    (void)name;
    return xlsx_raise("xlsx support is not available in this build; "
                      "install zlib and libxml2 development files and rebuild");
#else

    if (strcmp(name, "open") == 0) {
        if (expr->as.call.args.count != 1) {
            return xlsx_raise("xlsx.open expects one argument (a path)");
        }
        Value pathv = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(pathv);
            return value_null();
        }
        if (pathv.kind != VALUE_STRING && pathv.kind != VALUE_FILE) {
            value_free(pathv);
            return xlsx_raise("xlsx.open expects a path string");
        }
        const char *path = pathv.kind == VALUE_FILE ? pathv.as.file_path : pathv.as.string;

        FILE *f = fopen(path, "rb");
        if (!f) {
            char message[512];
            snprintf(message, sizeof message, "xlsx.open: cannot read %s", path);
            value_free(pathv);
            return xlsx_raise(message);
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            value_free(pathv);
            return xlsx_raise("xlsx.open: file is not seekable");
        }
        long size = ftell(f);
        rewind(f);
        if (size < 0) {
            fclose(f);
            value_free(pathv);
            return xlsx_raise("xlsx.open: cannot determine file size");
        }
        unsigned char *buf = malloc((size_t)size ? (size_t)size : 1);
        if (!buf) {
            abort();
        }
        size_t got = fread(buf, 1, (size_t)size, f);
        fclose(f);
        if (got != (size_t)size) {
            free(buf);
            value_free(pathv);
            return xlsx_raise("xlsx.open: short read");
        }

        XlsxPart *parts = NULL;
        size_t part_count = 0;
        const char *err = NULL;
        int ok = xlsx_read_container(buf, (size_t)size, &parts, &part_count, &err);
        free(buf);
        if (!ok) {
            value_free(pathv);
            return xlsx_raise(err ? err : "xlsx.open: unreadable container");
        }

        XlsxWorkbook *wb = calloc(1, sizeof *wb);
        if (!wb) {
            abort();
        }
        wb->ref_count = 1;
        wb->path = copy_string(path);
        wb->parts = parts;
        wb->part_count = part_count;
        value_free(pathv);

        xlsx_load_shared(wb);
        xlsx_load_sheets(wb);
        return value_workbook(wb);
    }

    if (strcmp(name, "sheets") == 0) {
        if (expr->as.call.args.count != 1) {
            return xlsx_raise("xlsx.sheets expects one argument (a workbook)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        if (wbv.kind != VALUE_WORKBOOK) {
            value_free(wbv);
            return xlsx_raise("xlsx.sheets expects a workbook");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        Value *items = wb->sheet_count ? calloc(wb->sheet_count, sizeof(Value)) : NULL;
        if (wb->sheet_count && !items) {
            abort();
        }
        for (size_t i = 0; i < wb->sheet_count; i++) {
            items[i] = value_string(wb->sheets[i].name);
        }
        Value out = value_array(items, wb->sheet_count);
        value_free(wbv);
        return out;
    }

    /* Every retained part, in container order, with its byte length and whether
     * anything has interpreted it. Exists so that "the reader discards nothing"
     * is a testable claim rather than a promise (§13.A). */
    if (strcmp(name, "parts") == 0) {
        if (expr->as.call.args.count != 1) {
            return xlsx_raise("xlsx.parts expects one argument (a workbook)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        if (wbv.kind != VALUE_WORKBOOK) {
            value_free(wbv);
            return xlsx_raise("xlsx.parts expects a workbook");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        Value *items = wb->part_count ? calloc(wb->part_count, sizeof(Value)) : NULL;
        if (wb->part_count && !items) {
            abort();
        }
        for (size_t i = 0; i < wb->part_count; i++) {
            RecordField *fields = calloc(3, sizeof(RecordField));
            if (!fields) {
                abort();
            }
            fields[0].name = copy_string("name");
            fields[0].value = cell_alloc();
            *fields[0].value = value_string(wb->parts[i].name);
            fields[1].name = copy_string("bytes");
            fields[1].value = cell_alloc();
            *fields[1].value = value_number((double)wb->parts[i].length);
            fields[2].name = copy_string("modelled");
            fields[2].value = cell_alloc();
            *fields[2].value = value_bool(wb->parts[i].modelled);
            items[i] = value_record(fields, 3);
        }
        Value out = value_array(items, wb->part_count);
        value_free(wbv);
        return out;
    }

    /* The raw decompressed bytes of one part, as a binary-safe string. The
     * escape hatch for anything the model does not cover, and the mechanism a
     * round-trip test uses to prove a part survived unchanged. */
    if (strcmp(name, "part") == 0) {
        if (expr->as.call.args.count != 2) {
            return xlsx_raise("xlsx.part expects two arguments (a workbook and a part name)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value namev = eval_expr(expr->as.call.args.items[1]);
        if (wbv.kind != VALUE_WORKBOOK || namev.kind != VALUE_STRING) {
            value_free(wbv);
            value_free(namev);
            return xlsx_raise("xlsx.part expects a workbook and a part name");
        }
        XlsxPart *p = xlsx_find_part(wbv.as.workbook, namev.as.string);
        Value out;
        if (!p) {
            out = value_unknown();
        } else {
            out = value_string_n((const char *)p->data, p->length);
        }
        value_free(wbv);
        value_free(namev);
        return out;
    }

    /* Every populated cell of a sheet, in document order. Sparse by
     * construction: empty cells are absent, not unknown-valued. */
    if (strcmp(name, "cells") == 0 || strcmp(name, "cell") == 0) {
        int one = strcmp(name, "cell") == 0;
        size_t want_args = one ? 3 : 2;
        if (expr->as.call.args.count != want_args) {
            return xlsx_raise(one
                ? "xlsx.cell expects three arguments (workbook, sheet, ref)"
                : "xlsx.cells expects two arguments (workbook, sheet)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        Value refv = value_null();
        if (one) {
            refv = eval_expr(expr->as.call.args.items[2]);
        }
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING ||
            (one && refv.kind != VALUE_STRING)) {
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return xlsx_raise("xlsx: expects a workbook, a sheet name, and (for cell) a ref");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        XlsxSheet *sheet = xlsx_find_sheet(wb, shv.as.string);
        if (!sheet || !sheet->part) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return xlsx_raise(message);
        }
        XlsxPart *sp = xlsx_find_part(wb, sheet->part);
        if (!sp) {
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return xlsx_raise("xlsx: sheet part missing from the container");
        }
        xmlDocPtr doc = xlsx_parse_part(sp);
        if (!doc) {
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return xlsx_raise("xlsx: sheet part is not well-formed XML");
        }

        Value *items = NULL;
        size_t count = 0, cap = 0;
        Value found = value_unknown();
        xmlNodePtr root = xmlDocGetRootElement(doc);
        for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
            if (!xlsx_is(n, "sheetData")) {
                continue;
            }
            for (xmlNodePtr r = n->children; r; r = r->next) {
                if (!xlsx_is(r, "row")) {
                    continue;
                }
                for (xmlNodePtr c = r->children; c; c = c->next) {
                    if (!xlsx_is(c, "c")) {
                        continue;
                    }
                    if (one) {
                        char *cref = xlsx_prop(c, "r");
                        int hit = cref && strcmp(cref, refv.as.string) == 0;
                        free(cref);
                        if (!hit) {
                            continue;
                        }
                        value_free(found);
                        found = xlsx_cell_record(wb, c);
                        continue;
                    }
                    if (count == cap) {
                        cap = cap ? cap * 2 : 32;
                        Value *g = realloc(items, cap * sizeof(Value));
                        if (!g) {
                            abort();
                        }
                        items = g;
                    }
                    items[count++] = xlsx_cell_record(wb, c);
                }
            }
        }
        xmlFreeDoc(doc);
        value_free(wbv);
        value_free(shv);
        value_free(refv);
        if (one) {
            free(items);
            return found;
        }
        value_free(found);
        return value_array(items, count);
    }

    /* The USED range, computed from the cells actually present rather than read
     * from <dimension>: that attribute is a hint Excel writes and other
     * producers get wrong or omit, so trusting it would make the answer depend
     * on who wrote the file. */
    if (strcmp(name, "dims") == 0) {
        if (expr->as.call.args.count != 2) {
            return xlsx_raise("xlsx.dims expects two arguments (workbook, sheet)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING) {
            value_free(wbv);
            value_free(shv);
            return xlsx_raise("xlsx.dims expects a workbook and a sheet name");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        XlsxSheet *sheet = xlsx_find_sheet(wb, shv.as.string);
        XlsxPart *sp = sheet && sheet->part ? xlsx_find_part(wb, sheet->part) : NULL;
        if (!sp) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv);
            value_free(shv);
            return xlsx_raise(message);
        }
        xmlDocPtr doc = xlsx_parse_part(sp);
        long min_r = 0, max_r = 0, min_c = 0, max_c = 0, seen = 0;
        xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
        for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
            if (!xlsx_is(n, "sheetData")) {
                continue;
            }
            for (xmlNodePtr r = n->children; r; r = r->next) {
                if (!xlsx_is(r, "row")) {
                    continue;
                }
                for (xmlNodePtr c = r->children; c; c = c->next) {
                    if (!xlsx_is(c, "c")) {
                        continue;
                    }
                    char *ref = xlsx_prop(c, "r");
                    long col = 0, row = 0;
                    if (ref && xlsx_parse_ref(ref, &col, &row)) {
                        if (!seen) {
                            min_r = max_r = row;
                            min_c = max_c = col;
                            seen = 1;
                        } else {
                            if (row < min_r) min_r = row;
                            if (row > max_r) max_r = row;
                            if (col < min_c) min_c = col;
                            if (col > max_c) max_c = col;
                        }
                    }
                    free(ref);
                }
            }
        }
        if (doc) {
            xmlFreeDoc(doc);
        }
        RecordField *fields = calloc(5, sizeof(RecordField));
        if (!fields) {
            abort();
        }
        fields[0].name = copy_string("cells");
        fields[0].value = cell_alloc();
        *fields[0].value = value_bool(seen != 0);
        fields[1].name = copy_string("first_row");
        fields[1].value = cell_alloc();
        *fields[1].value = value_number((double)min_r);
        fields[2].name = copy_string("last_row");
        fields[2].value = cell_alloc();
        *fields[2].value = value_number((double)max_r);
        fields[3].name = copy_string("first_col");
        fields[3].value = cell_alloc();
        *fields[3].value = value_number((double)min_c);
        fields[4].name = copy_string("last_col");
        fields[4].value = cell_alloc();
        *fields[4].value = value_number((double)max_c);
        value_free(wbv);
        value_free(shv);
        return value_record(fields, 5);
    }

    char message[160];
    snprintf(message, sizeof message, "unknown xlsx function: %s", name);
    return xlsx_raise(message);
#endif
}
