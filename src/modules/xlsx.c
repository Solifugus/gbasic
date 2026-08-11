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
/* Defined with the rest of the shared-formula machinery, further down; used
 * here by the cell-record path, which is earlier in the file. */
static char *xlsx_translate_formula(const char *src, long drow, long dcol);

/* The shared-formula master table for one sheet: si -> text and anchor cell.
 * Collected in a pre-pass because the reading paths need it before they reach
 * the continuations, and a master may in principle follow one. Full rationale
 * on xlsx_translate_formula. */
typedef struct { long si; char *text; long row, col; } XlsxSharedMaster;
typedef struct { XlsxSharedMaster *items; size_t count; } XlsxShared;

static void xlsx_shared_collect(xmlNodePtr root, XlsxShared *sh) {
    sh->items = NULL; sh->count = 0;
    size_t cap = 0;
    for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
        if (!xlsx_is(n, "sheetData")) continue;
        for (xmlNodePtr r = n->children; r; r = r->next) {
            if (!xlsx_is(r, "row")) continue;
            for (xmlNodePtr c = r->children; c; c = c->next) {
                if (!xlsx_is(c, "c")) continue;
                for (xmlNodePtr k = c->children; k; k = k->next) {
                    if (!xlsx_is(k, "f")) continue;
                    char *ft = xlsx_prop(k, "t");
                    if (!ft || strcmp(ft, "shared") != 0) { free(ft); continue; }
                    free(ft);
                    char *txt = xlsx_text_of(k);
                    char *si = xlsx_prop(k, "si");
                    char *cref = xlsx_prop(c, "r");
                    long col = 0, row = 0;
                    if (txt && *txt && si && cref && xlsx_parse_ref(cref, &col, &row)) {
                        if (sh->count == cap) {
                            cap = cap ? cap * 2 : 32;
                            XlsxSharedMaster *g = realloc(sh->items, cap * sizeof *g);
                            if (!g) abort();
                            sh->items = g;
                        }
                        sh->items[sh->count].si = strtol(si, NULL, 10);
                        sh->items[sh->count].text = txt;
                        sh->items[sh->count].row = row;
                        sh->items[sh->count].col = col;
                        sh->count++;
                        txt = NULL;               /* ownership moved */
                    }
                    free(txt); free(si); free(cref);
                }
            }
        }
    }
}

static void xlsx_shared_free(XlsxShared *sh) {
    for (size_t i = 0; i < sh->count; i++) free(sh->items[i].text);
    free(sh->items);
    sh->items = NULL; sh->count = 0;
}

static const XlsxSharedMaster *xlsx_shared_find(const XlsxShared *sh, long si) {
    for (size_t i = 0; i < sh->count; i++) if (sh->items[i].si == si) return &sh->items[i];
    return NULL;
}

static Value xlsx_cell_record(XlsxWorkbook *wb, xmlNodePtr c, const XlsxShared *shared) {
    char *ref = xlsx_prop(c, "r");
    char *type = xlsx_prop(c, "t");
    char *style = xlsx_prop(c, "s");

    char *formula = NULL;
    char *vtext = NULL;
    char *inline_text = NULL;
    long shared_si = -1;
    for (xmlNodePtr k = c->children; k; k = k->next) {
        if (xlsx_is(k, "f")) {
            formula = xlsx_text_of(k);
            char *ft = xlsx_prop(k, "t");
            if (ft && strcmp(ft, "shared") == 0) {
                char *si = xlsx_prop(k, "si");
                if (si) shared_si = strtol(si, NULL, 10);
                free(si);
            }
            free(ft);
        } else if (xlsx_is(k, "v")) {
            vtext = xlsx_text_of(k);
        } else if (xlsx_is(k, "is")) {
            inline_text = xlsx_text_of(k);
        }
    }
    /* A shared-formula CONTINUATION carries an empty <f/>. Report the formula
     * it actually stands for, translated to this cell, so that what a caller
     * reads back matches what xlsx.evaluate computes -- the two disagreeing
     * would be worse than either being wrong alone. */
    if (formula && !*formula) { free(formula); formula = NULL; }
    if (!formula && shared_si >= 0 && shared && ref) {
        const XlsxSharedMaster *m = xlsx_shared_find(shared, shared_si);
        long col = 0, row = 0;
        if (m && xlsx_parse_ref(ref, &col, &row)) {
            formula = xlsx_translate_formula(m->text, row - m->row, col - m->col);
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

/* ------------------------------------------------------------- ZIP writing
 *
 * The counterpart to the reader, and the reason the part tree exists. An
 * UNTOUCHED part is written from the bytes we read, never regenerated — so
 * charts, pivot tables, conditional formatting, VBA and anything else we do not
 * model survive a round trip exactly. Only parts marked dirty are rebuilt.
 *
 * The output is a fresh ZIP rather than an in-place edit: entry offsets shift
 * when any part changes size, so rewriting the container is both simpler and
 * safer than patching it. Compression level and ordering may therefore differ
 * from the input — what must not differ is any part's CONTENT. */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} XlsxBuf;

static void xlsx_buf_put(XlsxBuf *b, const void *bytes, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + n) {
            cap *= 2;
        }
        unsigned char *g = realloc(b->data, cap);
        if (!g) {
            abort();
        }
        b->data = g;
        b->cap = cap;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
}

static void xlsx_put16(XlsxBuf *b, unsigned v) {
    unsigned char t[2] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff) };
    xlsx_buf_put(b, t, 2);
}

static void xlsx_put32(XlsxBuf *b, unsigned long v) {
    unsigned char t[4] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
                           (unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff) };
    xlsx_buf_put(b, t, 4);
}

/* Raw deflate, matching what the reader expects (no zlib wrapper). Falls back
 * to STORED when compression would not shrink the part, which is both smaller
 * and what real producers do for tiny entries. */
static unsigned char *xlsx_deflate(const unsigned char *in, size_t in_len,
                                   size_t *out_len, int *stored) {
    *stored = 0;
    uLongf bound = compressBound((uLong)in_len) + 64;
    unsigned char *out = malloc(bound ? bound : 1);
    if (!out) {
        abort();
    }
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        free(out);
        return NULL;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = out;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    size_t produced = zs.total_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        free(out);
        return NULL;
    }
    if (produced >= in_len) {
        free(out);
        *stored = 1;
        *out_len = in_len;
        unsigned char *copy = malloc(in_len ? in_len : 1);
        if (!copy) {
            abort();
        }
        memcpy(copy, in, in_len);
        return copy;
    }
    *out_len = produced;
    return out;
}

/* Serialize the whole part tree as a ZIP. Returns a malloc'd buffer. */
static unsigned char *xlsx_write_container(XlsxWorkbook *wb, size_t *out_len) {
    XlsxBuf body = {0};
    XlsxBuf dir = {0};
    size_t written = 0;

    for (size_t i = 0; i < wb->part_count; i++) {
        XlsxPart *p = &wb->parts[i];
        size_t comp_len = 0;
        int stored = 0;
        unsigned char *comp = xlsx_deflate(p->data, p->length, &comp_len, &stored);
        if (!comp) {
            free(body.data);
            free(dir.data);
            return NULL;
        }
        unsigned long crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, (const Bytef *)p->data, (uInt)p->length);
        unsigned method = stored ? 0 : 8;
        size_t name_len = strlen(p->name);
        size_t local_off = written;

        /* Local file header. Timestamps are fixed rather than taken from the
         * clock: a workbook written twice from identical input must produce
         * identical bytes, or the round-trip test measures the clock. */
        xlsx_put32(&body, 0x04034b50);
        xlsx_put16(&body, 20);              /* version needed */
        xlsx_put16(&body, 0);               /* flags */
        xlsx_put16(&body, method);
        xlsx_put16(&body, 0);               /* mod time  (fixed) */
        xlsx_put16(&body, 0x21);            /* mod date  (fixed: 1980-01-01) */
        xlsx_put32(&body, crc);
        xlsx_put32(&body, (unsigned long)comp_len);
        xlsx_put32(&body, (unsigned long)p->length);
        xlsx_put16(&body, (unsigned)name_len);
        xlsx_put16(&body, 0);               /* extra len */
        xlsx_buf_put(&body, p->name, name_len);
        xlsx_buf_put(&body, comp, comp_len);
        written = body.len;

        /* Central directory entry. */
        xlsx_put32(&dir, 0x02014b50);
        xlsx_put16(&dir, 20);               /* version made by */
        xlsx_put16(&dir, 20);               /* version needed */
        xlsx_put16(&dir, 0);
        xlsx_put16(&dir, method);
        xlsx_put16(&dir, 0);
        xlsx_put16(&dir, 0x21);
        xlsx_put32(&dir, crc);
        xlsx_put32(&dir, (unsigned long)comp_len);
        xlsx_put32(&dir, (unsigned long)p->length);
        xlsx_put16(&dir, (unsigned)name_len);
        xlsx_put16(&dir, 0);                /* extra */
        xlsx_put16(&dir, 0);                /* comment */
        xlsx_put16(&dir, 0);                /* disk */
        xlsx_put16(&dir, 0);                /* internal attrs */
        xlsx_put32(&dir, 0);                /* external attrs */
        xlsx_put32(&dir, (unsigned long)local_off);
        xlsx_buf_put(&dir, p->name, name_len);

        free(comp);
    }

    size_t cd_off = body.len;
    xlsx_buf_put(&body, dir.data, dir.len);
    xlsx_put32(&body, 0x06054b50);
    xlsx_put16(&body, 0);
    xlsx_put16(&body, 0);
    xlsx_put16(&body, (unsigned)wb->part_count);
    xlsx_put16(&body, (unsigned)wb->part_count);
    xlsx_put32(&body, (unsigned long)dir.len);
    xlsx_put32(&body, (unsigned long)cd_off);
    xlsx_put16(&body, 0);                   /* comment length */

    free(dir.data);
    *out_len = body.len;
    return body.data;
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

/* ==================================================================== FORMULA
 *
 * Phase A+B of docs/xlsx_design.md §13.G: the expression evaluator (operators,
 * references, ranges) plus the durable-core functions.
 *
 * WHY THE EXPRESSION EVALUATOR IS THE FIRST MILESTONE. Measured on the Enron
 * corpus, four of the nine most-used "functions" are arithmetic operators
 * (+ - * /), so precedence, references and ranges carry the largest single
 * share of real usage before any function library exists.
 *
 * THE ORACLE, AND ITS LIMIT. An xlsx stores both the formula and Excel's own
 * cached result for every formula cell, so `xlsx.check` can evaluate each
 * formula and compare. Crucially this needs NO DEPENDENCY GRAPH: every input
 * cell already carries a cached value, so each formula can be checked in
 * isolation. The graph is only required once something is CHANGED.
 *
 * The limit is worth stating plainly: on a SYNTHETIC fixture the cached values
 * were written by hand, so `check` measures self-consistency, not conformance
 * to Excel. It becomes a real oracle only when pointed at a workbook Excel
 * actually wrote.
 *
 * VOLATILE FUNCTIONS CANNOT PARTICIPATE. NOW/TODAY/RAND are in the measured top
 * nine, and their cached value dates from whenever the workbook last
 * calculated, so comparing against it is meaningless. They are reported
 * separately rather than counted as agreements or failures. */

typedef enum { XV_NUM, XV_STR, XV_BOOL, XV_ERR, XV_EMPTY } XlsxValKind;

typedef struct {
    XlsxValKind kind;
    double num;
    char *str;              /* owned: STR text, or the ERR code */
} XlsxVal;

static XlsxVal xv_num(double d) { XlsxVal v = {XV_NUM, d, NULL}; return v; }
static XlsxVal xv_bool(int b) { XlsxVal v = {XV_BOOL, b ? 1 : 0, NULL}; return v; }
static XlsxVal xv_empty(void) { XlsxVal v = {XV_EMPTY, 0, NULL}; return v; }
static XlsxVal xv_str(const char *s) { XlsxVal v = {XV_STR, 0, copy_string(s)}; return v; }
static XlsxVal xv_err(const char *e) { XlsxVal v = {XV_ERR, 0, copy_string(e)}; return v; }
static void xv_free(XlsxVal v) { free(v.str); }

/* One cell of a parsed sheet snapshot. The snapshot exists so evaluation does
 * not re-parse the sheet XML per reference — checking a sheet of N formulas
 * would otherwise be O(N) full XML parses. */
typedef struct {
    long row, col;
    XlsxValKind kind;
    double num;
    char *str;
    char *formula;          /* NULL when the cell is a literal */
    int hidden;             /* the row's hidden="1" -- SUBTOTAL 101-111 needs it */
} XlsxSnapCell;

/* A sheet snapshot, plus an INDEX from (row,col) to position.
 *
 * The index is not a micro-optimisation. Without it both lookups here are
 * linear scans of every cell, and they run inside per-formula loops, so the
 * cost is the PRODUCT: evaluating a sheet is O(formulas x refs x cells).
 * Measured on the Enron corpus, one real workbook
 * (john_griffith__15586__Crude.xlsx) has 182,752 cells and 50,343 formulas on
 * a single sheet -- billions of comparisons, and xlsx.check on it did not
 * finish inside 300 seconds. Reading the same file takes 0.44s, so the cost
 * was entirely in the scans, not the parsing.
 *
 * Open addressing, linear probing, power-of-two capacity at 2x count. Built
 * once when the snapshot is complete, and valid for its whole life because
 * recalculation mutates cell VALUES in place and never adds or removes a
 * cell. */
typedef struct {
    XlsxSnapCell *cells;
    size_t count;
    size_t *slots;      /* position + 1, or 0 for empty */
    size_t mask;        /* capacity - 1; capacity is a power of two */
} XlsxSnap;

static size_t xlsx_snap_hash(long row, long col) {
    /* 64-bit mix of the two coordinates; splitmix64's finaliser. */
    unsigned long long h = (unsigned long long)row * 0x9E3779B97F4A7C15ULL
                         ^ (unsigned long long)col * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return (size_t)h;
}

static void xlsx_snap_build_index(XlsxSnap *s) {
    free(s->slots);
    s->slots = NULL;
    s->mask = 0;
    if (!s->count) return;
    size_t cap = 16;
    while (cap < s->count * 2) cap *= 2;
    s->slots = calloc(cap, sizeof(size_t));
    if (!s->slots) abort();
    s->mask = cap - 1;
    for (size_t i = 0; i < s->count; i++) {
        size_t p = xlsx_snap_hash(s->cells[i].row, s->cells[i].col) & s->mask;
        /* A duplicate ref keeps the FIRST occurrence, matching the previous
         * linear scan, which returned the first match. */
        while (s->slots[p]) {
            const XlsxSnapCell *e = &s->cells[s->slots[p] - 1];
            if (e->row == s->cells[i].row && e->col == s->cells[i].col) break;
            p = (p + 1) & s->mask;
        }
        if (!s->slots[p]) s->slots[p] = i + 1;
    }
}

/* Position of a cell, or (size_t)-1. */
static size_t xlsx_snap_pos(const XlsxSnap *s, long row, long col) {
    if (!s->slots) return (size_t)-1;
    size_t p = xlsx_snap_hash(row, col) & s->mask;
    while (s->slots[p]) {
        const XlsxSnapCell *e = &s->cells[s->slots[p] - 1];
        if (e->row == row && e->col == col) return s->slots[p] - 1;
        p = (p + 1) & s->mask;
    }
    return (size_t)-1;
}

static void xlsx_snap_free(XlsxSnap *s) {
    free(s->slots);
    s->slots = NULL;
    s->mask = 0;
    for (size_t i = 0; i < s->count; i++) {
        free(s->cells[i].str);
        free(s->cells[i].formula);
    }
    free(s->cells);
    s->cells = NULL;
    s->count = 0;
}

/* A SHEET WITH NO WORKSHEET PART.
 *
 * Excel writes VBA module and macro sheets into workbook.xml with an empty
 * relationship id -- <sheet name="Module1" state="veryHidden" r:id=""/> -- so
 * the sheet is genuinely part of the workbook while having no worksheet part
 * behind it. Treating that as "no such sheet" is simply false, and it breaks
 * the one loop every caller writes:
 *
 *     for each s in xlsx.sheets(wb) / for each c in xlsx.cells(wb, s)
 *
 * because xlsx.sheets lists the sheet that xlsx.cells then rejects.
 *
 * Measured, not guessed: scanning the 15,871-workbook Enron corpus, 400 files
 * (2.5%) carry such a sheet, and every single scan failure was this one case --
 * no ZIP, XML, or cell-parsing failure occurred in the whole corpus.
 *
 * So: reads treat a partless sheet as an EMPTY sheet, which is what it is; a
 * macro sheet has no cells. Writes still refuse, but say why. A name that is
 * genuinely not in the workbook still raises "no such sheet".
 */

/* ------------------------------------------------------------ SHARED FORMULAS
 *
 * Excel does not repeat a formula that was filled down a column. It writes the
 * text ONCE on the first cell --
 *
 *     <c r="C2"><f t="shared" ref="C2:C500" si="0">A2*B2</f><v>12</v></c>
 *
 * -- and every other cell in the run carries only a back-reference with NO
 * TEXT AT ALL:
 *
 *     <c r="C3"><f t="shared" si="0"/><v>15</v></c>
 *
 * Read naively, C3 has "a formula whose text is the empty string", which
 * evaluates to #VALUE!. That is not a cosmetic misreading: `xlsx.recalc`
 * WRITES evaluated values back, so it replaced every such cell with #VALUE! --
 * silent corruption of a real workbook, measured at 171 cells on the first
 * corpus file tried.
 *
 * This is not a rare shape. Across the Enron corpus, 61.0% of formula-bearing
 * workbooks use shared formulas, and 13.2 MILLION of the 20.7M formula cells
 * are continuations -- so nearly two thirds of every formula cell in the
 * corpus was being read as empty (docs/xlsx_design.md §13.J).
 *
 * Resolving one means TRANSLATING the master's text by the offset between the
 * two cells: relative references shift, absolute ones ($) do not. That is what
 * xlsx_translate_formula does, and getting it wrong is worse than not doing it
 * at all, because the result is a plausible number computed from the wrong
 * cells. */

/* Is s[i..] a cell reference token, and where does it end? Sets the parsed
 * column/row and which halves were absolute. Deliberately strict: the token
 * must be exactly [$]LETTERS[$]DIGITS, so `B2` is a reference while `B2B`,
 * `LOG10` and a bare defined name like `DateToday` are not. */
static int xlsx_ref_token(const char *s, size_t *len,
                          long *col, long *row, int *abs_col, int *abs_row) {
    size_t i = 0;
    *abs_col = 0; *abs_row = 0;
    if (s[i] == '$') { *abs_col = 1; i++; }
    size_t l0 = i;
    long c = 0;
    while (isalpha((unsigned char)s[i])) {
        c = c * 26 + (toupper((unsigned char)s[i]) - 'A' + 1);
        i++;
        if (i - l0 > 3) return 0;          /* no column beyond XFD */
    }
    if (i == l0) return 0;
    if (s[i] == '$') { *abs_row = 1; i++; }
    size_t d0 = i;
    long r = 0;
    while (isdigit((unsigned char)s[i])) {
        r = r * 10 + (s[i] - '0');
        i++;
        if (i - d0 > 7) return 0;
    }
    if (i == d0) return 0;
    /* A trailing letter, digit-continuation or '(' means this was part of a
     * longer name, not a reference. */
    if (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '(') return 0;
    *len = i; *col = c; *row = r;
    return 1;
}

/* Rewrite `src` as it would read if the formula were moved by (drow, dcol).
 * Returns a malloc'd string. Text inside string literals is copied verbatim,
 * and a reference shifted off the sheet becomes #REF!, as Excel does. */
static char *xlsx_translate_formula(const char *src, long drow, long dcol) {
    size_t cap = strlen(src) * 2 + 32, len = 0;
    char *out = malloc(cap);
    if (!out) abort();
    for (size_t i = 0; src[i];) {
        if (len + 32 > cap) {
            cap = cap * 2 + 32;
            char *g = realloc(out, cap);
            if (!g) abort();
            out = g;
        }
        /* A quoted string: copy through, honouring Excel's "" escape. A
         * reference-looking substring inside one is text, not a reference. */
        if (src[i] == '"') {
            out[len++] = src[i++];
            while (src[i]) {
                if (len + 4 > cap) {
                    cap = cap * 2 + 32;
                    char *g = realloc(out, cap);
                    if (!g) abort();
                    out = g;
                }
                if (src[i] == '"' && src[i + 1] == '"') {
                    out[len++] = src[i++]; out[len++] = src[i++];
                    continue;
                }
                if (src[i] == '"') { out[len++] = src[i++]; break; }
                out[len++] = src[i++];
            }
            continue;
        }
        /* A quoted sheet name ('My Sheet'!A1): copy the quoted part verbatim,
         * then let the reference after it translate normally. */
        if (src[i] == '\'') {
            out[len++] = src[i++];
            while (src[i]) {
                if (len + 4 > cap) {
                    cap = cap * 2 + 32;
                    char *g = realloc(out, cap);
                    if (!g) abort();
                    out = g;
                }
                if (src[i] == '\'' && src[i + 1] == '\'') {
                    out[len++] = src[i++]; out[len++] = src[i++];
                    continue;
                }
                if (src[i] == '\'') { out[len++] = src[i++]; break; }
                out[len++] = src[i++];
            }
            continue;
        }
        /* Only try a reference at a token BOUNDARY, so the "A1" tail of
         * `NAMEDA1` is never mistaken for one. */
        int boundary = (i == 0) || !(isalnum((unsigned char)src[i - 1]) ||
                                     src[i - 1] == '_' || src[i - 1] == '.' ||
                                     src[i - 1] == '$' || src[i - 1] == '!');
        size_t tl; long c, r; int ac, ar;
        if (boundary && (src[i] == '$' || isalpha((unsigned char)src[i])) &&
            xlsx_ref_token(src + i, &tl, &c, &r, &ac, &ar)) {
            long nc = ac ? c : c + dcol;
            long nr = ar ? r : r + drow;
            if (nc < 1 || nr < 1 || nc > 16384 || nr > 1048576) {
                memcpy(out + len, "#REF!", 5); len += 5;
            } else {
                char colname[8];
                size_t cl = 0;
                long t = nc;
                while (t > 0 && cl < sizeof colname - 1) {
                    long rem = (t - 1) % 26;
                    colname[cl++] = (char)('A' + rem);
                    t = (t - 1) / 26;
                }
                if (ac) out[len++] = '$';
                while (cl) out[len++] = colname[--cl];
                if (ar) out[len++] = '$';
                len += (size_t)snprintf(out + len, cap - len, "%ld", nr);
            }
            i += tl;
            continue;
        }
        out[len++] = src[i++];
    }
    out[len] = '\0';
    return out;
}

/* Parse one sheet into a snapshot. Sparse: only populated cells appear.
 * Returns 0 only when the sheet cannot be read at all; a sheet that exists but
 * has no worksheet part succeeds with an empty snapshot (see above). */
static int xlsx_snapshot(XlsxWorkbook *wb, const char *sheet_name, XlsxSnap *out) {
    out->cells = NULL;
    out->count = 0;
    out->slots = NULL;
    out->mask = 0;
    XlsxSheet *sheet = xlsx_find_sheet(wb, sheet_name);
    if (!sheet) {
        return 0;
    }
    if (!sheet->part) {
        return 1;   /* a macro/module sheet: real, and empty */
    }
    XlsxPart *sp = xlsx_find_part(wb, sheet->part);
    if (!sp) {
        return 0;
    }
    xmlDocPtr doc = xlsx_parse_part(sp);
    if (!doc) {
        return 0;
    }
    size_t cap = 0;
    /* Shared-formula bookkeeping: masters carry the text, continuations point
     * at one by si and are resolved after the sweep. */
    typedef struct { long si; const char *text; long row, col; } XlsxMaster;
    typedef struct { size_t cell; long si; } XlsxPending;
    XlsxMaster *mast = NULL; size_t mast_n = 0, mast_cap = 0;
    XlsxPending *pend = NULL; size_t pend_n = 0, pend_cap = 0;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
        if (!xlsx_is(n, "sheetData")) continue;
        for (xmlNodePtr r = n->children; r; r = r->next) {
            if (!xlsx_is(r, "row")) continue;
            /* Hidden is a ROW attribute, so it is read here and copied onto
             * each of the row's cells -- SUBTOTAL's 101-111 forms are defined
             * in terms of it, and by then the row element is long out of
             * scope. */
            char *hid = xlsx_prop(r, "hidden");
            int row_hidden = hid && (strcmp(hid, "1") == 0 || strcmp(hid, "true") == 0);
            free(hid);
            for (xmlNodePtr c = r->children; c; c = c->next) {
                if (!xlsx_is(c, "c")) continue;
                char *ref = xlsx_prop(c, "r");
                long col = 0, row = 0;
                if (!ref || !xlsx_parse_ref(ref, &col, &row)) { free(ref); continue; }
                free(ref);
                char *type = xlsx_prop(c, "t");
                char *formula = NULL, *vtext = NULL, *inl = NULL;
                long shared_si = -1;
                int shared_master = 0;
                for (xmlNodePtr k = c->children; k; k = k->next) {
                    if (xlsx_is(k, "f")) {
                        formula = xlsx_text_of(k);
                        char *ft = xlsx_prop(k, "t");
                        if (ft && strcmp(ft, "shared") == 0) {
                            char *si = xlsx_prop(k, "si");
                            if (si) shared_si = strtol(si, NULL, 10);
                            free(si);
                            /* The master is the one carrying the text; every
                             * other cell in the run has an empty <f/>. */
                            shared_master = formula && *formula;
                        }
                        free(ft);
                    }
                    else if (xlsx_is(k, "v")) vtext = xlsx_text_of(k);
                    else if (xlsx_is(k, "is")) inl = xlsx_text_of(k);
                }
                /* An empty <f/> is NOT a formula whose text is "" -- it is a
                 * continuation to be resolved once every master is known.
                 * Masters can in principle follow their continuations, so this
                 * cannot be done in one pass. */
                if (formula && !*formula) { free(formula); formula = NULL; }
                if (shared_si >= 0 && !shared_master) {
                    if (pend_n == pend_cap) {
                        pend_cap = pend_cap ? pend_cap * 2 : 64;
                        XlsxPending *g = realloc(pend, pend_cap * sizeof *g);
                        if (!g) abort();
                        pend = g;
                    }
                    pend[pend_n].cell = out->count;   /* filled in just below */
                    pend[pend_n].si = shared_si;
                    pend_n++;
                } else if (shared_master) {
                    if (mast_n == mast_cap) {
                        mast_cap = mast_cap ? mast_cap * 2 : 64;
                        XlsxMaster *g = realloc(mast, mast_cap * sizeof *g);
                        if (!g) abort();
                        mast = g;
                    }
                    mast[mast_n].si = shared_si;
                    mast[mast_n].text = formula;      /* borrowed, not owned */
                    mast[mast_n].row = row;
                    mast[mast_n].col = col;
                    mast_n++;
                }
                if (out->count == cap) {
                    cap = cap ? cap * 2 : 64;
                    XlsxSnapCell *g = realloc(out->cells, cap * sizeof(XlsxSnapCell));
                    if (!g) abort();
                    out->cells = g;
                }
                XlsxSnapCell *sc = &out->cells[out->count++];
                sc->row = row; sc->col = col; sc->formula = formula;
                sc->str = NULL; sc->num = 0; sc->kind = XV_EMPTY;
                sc->hidden = row_hidden;
                if (type && strcmp(type, "s") == 0) {
                    long idx = vtext ? strtol(vtext, NULL, 10) : -1;
                    sc->kind = XV_STR;
                    sc->str = copy_string(idx >= 0 && (size_t)idx < wb->shared_count
                                          ? wb->shared[idx] : "");
                } else if (type && (strcmp(type, "inlineStr") == 0 || strcmp(type, "str") == 0)) {
                    sc->kind = XV_STR;
                    sc->str = copy_string(inl ? inl : (vtext ? vtext : ""));
                } else if (type && strcmp(type, "b") == 0) {
                    sc->kind = XV_BOOL;
                    sc->num = (vtext && strcmp(vtext, "0") != 0) ? 1 : 0;
                } else if (type && strcmp(type, "e") == 0) {
                    sc->kind = XV_ERR;
                    sc->str = copy_string(vtext ? vtext : "#ERROR");
                } else if (vtext) {
                    sc->kind = XV_NUM;
                    sc->num = strtod(vtext, NULL);
                }
                free(type); free(vtext); free(inl);
            }
        }
    }
    /* Resolve shared-formula continuations now that every master is known.
     * Each gets the master's text translated by the offset between the two
     * cells, which is what makes A2*B2 on row 2 become A3*B3 on row 3. A
     * continuation whose master is missing (a damaged file) is left with no
     * formula rather than given a wrong one -- it keeps its cached value and
     * is simply not recalculated. */
    for (size_t i = 0; i < pend_n; i++) {
        const XlsxMaster *m = NULL;
        for (size_t k = 0; k < mast_n; k++) {
            if (mast[k].si == pend[i].si) { m = &mast[k]; break; }
        }
        if (!m || !m->text) continue;
        XlsxSnapCell *sc = &out->cells[pend[i].cell];
        sc->formula = xlsx_translate_formula(m->text, sc->row - m->row, sc->col - m->col);
    }
    free(mast);
    free(pend);
    xmlFreeDoc(doc);
    xlsx_snap_build_index(out);
    return 1;
}

static const XlsxSnapCell *xlsx_snap_at(const XlsxSnap *s, long row, long col) {
    size_t p = xlsx_snap_pos(s, row, col);
    return p == (size_t)-1 ? NULL : &s->cells[p];
}

/* ------------------------------------------------------------ formula lexer */

typedef enum {
    XT_END, XT_NUM, XT_STR, XT_REF, XT_NAME, XT_OP, XT_LPAREN, XT_RPAREN,
    XT_COMMA, XT_COLON, XT_ERRLIT
} XlsxTokKind;

typedef struct {
    XlsxTokKind kind;
    double num;
    char text[128];
    /* A reference may be QUALIFIED BY A SHEET: Data!A1, 'Nymex hist.'!A:B.
     * Measured on the corpus, cross-sheet references are the single largest
     * cause of disagreement -- 3.09M cells, 54% of all of them -- so the
     * qualifier travels with the token rather than being handled by callers.
     * Empty means "the sheet being evaluated".
     *
     * `external` marks the [n] form, '[4]CurveFetch'!$D$8, which names a
     * DIFFERENT WORKBOOK. That file is not in front of us, so it is reported
     * rather than guessed at -- 28% of the cross-sheet population. */
    char sheet[128];
    int external;
} XlsxTok;

typedef struct {
    const char *p;
    XlsxTok cur;
    int bad;
} XlsxLex;

/* Scan the reference that follows a sheet qualifier's `!`. The sheet name is
 * already in lx->cur.sheet.
 *
 * This also accepts the WHOLE-COLUMN and WHOLE-ROW forms -- 'Nymex hist.'!A:B,
 * Data!$3:$7 -- which appear constantly in real lookup tables because the
 * author does not know how far the data will grow. A bare column letter is not
 * a cell reference, so it is emitted as a REF with row 0 meaning "unbounded",
 * and the range logic fills in the sheet's actual extent later. */
static void xlsx_lex_sheet_qualified(XlsxLex *lx) {
    size_t n = 0;
    while (*lx->p && (isalnum((unsigned char)*lx->p) || *lx->p == '$' || *lx->p == '_' ||
                      *lx->p == '.')) {
        if (*lx->p != '$' && n + 1 < sizeof lx->cur.text) lx->cur.text[n++] = *lx->p;
        lx->p++;
    }
    lx->cur.text[n] = '\0';
    long col, row;
    if (xlsx_parse_ref(lx->cur.text, &col, &row)) {
        lx->cur.kind = XT_REF;
        return;
    }
    /* A bare column ("A") or row ("3"): only valid as one end of a range, and
     * xlsx_parse_ref rejects it. Pass it through as a REF and let the range
     * builder interpret it. */
    if (*lx->cur.text) {
        lx->cur.kind = XT_REF;
        return;
    }
    lx->bad = 1;
    lx->cur.kind = XT_END;
}

static void xlsx_lex_next(XlsxLex *lx) {
    while (*lx->p == ' ' || *lx->p == '\t' || *lx->p == '\n' || *lx->p == '\r') lx->p++;
    lx->cur.text[0] = '\0';
    lx->cur.sheet[0] = '\0';
    lx->cur.external = 0;
    char c = *lx->p;
    if (!c) { lx->cur.kind = XT_END; return; }

    if (c == '(') { lx->p++; lx->cur.kind = XT_LPAREN; return; }
    if (c == ')') { lx->p++; lx->cur.kind = XT_RPAREN; return; }
    if (c == ',') { lx->p++; lx->cur.kind = XT_COMMA; return; }
    if (c == ':') { lx->p++; lx->cur.kind = XT_COLON; return; }

    if (c == '"') {
        /* "" is an escaped quote inside an Excel string literal. */
        lx->p++;
        size_t n = 0;
        while (*lx->p) {
            if (*lx->p == '"') {
                if (lx->p[1] == '"') { if (n + 1 < sizeof lx->cur.text) lx->cur.text[n++] = '"'; lx->p += 2; continue; }
                lx->p++; break;
            }
            if (n + 1 < sizeof lx->cur.text) lx->cur.text[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.text[n] = '\0';
        lx->cur.kind = XT_STR;
        return;
    }

    /* A QUOTED SHEET NAME: 'Nymex hist.'!A:B. Quoting is required whenever the
     * name holds a space or punctuation, which is why 42% of the corpus's
     * cross-sheet references take this form -- more than the plain kind. '' is
     * an escaped apostrophe. */
    if (c == '\'') {
        lx->p++;
        size_t n = 0;
        while (*lx->p) {
            if (*lx->p == '\'') {
                if (lx->p[1] == '\'') {
                    if (n + 1 < sizeof lx->cur.sheet) lx->cur.sheet[n++] = '\'';
                    lx->p += 2;
                    continue;
                }
                lx->p++;
                break;
            }
            if (n + 1 < sizeof lx->cur.sheet) lx->cur.sheet[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.sheet[n] = '\0';
        if (*lx->p != '!') { lx->bad = 1; lx->cur.kind = XT_END; return; }
        lx->p++;
        xlsx_lex_sheet_qualified(lx);
        return;
    }

    if (c == '#') {                       /* an error literal: #DIV/0! #N/A ... */
        size_t n = 0;
        while (*lx->p && (isalnum((unsigned char)*lx->p) || strchr("#/!?_", *lx->p))) {
            if (n + 1 < sizeof lx->cur.text) lx->cur.text[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.text[n] = '\0';
        lx->cur.kind = XT_ERRLIT;
        return;
    }

    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)lx->p[1]))) {
        char *end = NULL;
        lx->cur.num = strtod(lx->p, &end);
        lx->p = end;
        lx->cur.kind = XT_NUM;
        return;
    }

    /* A reference or a name. `$` marks absolute addressing, which matters only
     * when a formula is COPIED; evaluating one in place, $A$1 and A1 name the
     * same cell, so the marker is stripped. */
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        size_t n = 0;
        const char *start = lx->p;
        while (*lx->p && (isalnum((unsigned char)*lx->p) || *lx->p == '_' || *lx->p == '$' || *lx->p == '.')) {
            if (*lx->p != '$' && n + 1 < sizeof lx->cur.text) lx->cur.text[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.text[n] = '\0';
        (void)start;
        /* An unquoted sheet qualifier: Data!A1. What was just scanned is the
         * SHEET NAME, not a reference. */
        if (*lx->p == '!') {
            snprintf(lx->cur.sheet, sizeof lx->cur.sheet, "%s", lx->cur.text);
            lx->p++;
            xlsx_lex_sheet_qualified(lx);
            return;
        }
        /* TRUE/FALSE are literals, not names. */
        long col, row;
        if (*lx->p == '(') {
            lx->cur.kind = XT_NAME;
        } else if (xlsx_parse_ref(lx->cur.text, &col, &row)) {
            lx->cur.kind = XT_REF;
        } else {
            lx->cur.kind = XT_NAME;
        }
        return;
    }

    /* An EXTERNAL workbook reference begins with the bracketed index of an
     * entry in xl/externalLinks: [4]CurveFetch!$D$8. Consumed here so the
     * evaluator can report it as unavailable rather than the lexer failing on
     * a character it does not know. */
    if (c == '[') {
        size_t n = 0;
        lx->cur.sheet[n++] = '[';
        lx->p++;
        while (*lx->p && *lx->p != '!' ) {
            if (n + 1 < sizeof lx->cur.sheet) lx->cur.sheet[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.sheet[n] = '\0';
        lx->cur.external = 1;
        if (*lx->p == '!') lx->p++;
        xlsx_lex_sheet_qualified(lx);
        return;
    }

    if (strchr("+-*/^&%=<>", c)) {
        size_t n = 0;
        lx->cur.text[n++] = c;
        lx->p++;
        if ((c == '<' && (*lx->p == '=' || *lx->p == '>')) || (c == '>' && *lx->p == '=')) {
            lx->cur.text[n++] = *lx->p;
            lx->p++;
        }
        lx->cur.text[n] = '\0';
        lx->cur.kind = XT_OP;
        return;
    }

    lx->bad = 1;
    lx->cur.kind = XT_END;
}

/* --------------------------------------------------- recursive-descent parse
 *
 * Evaluated directly rather than built into an AST first: Phase A only needs
 * one-shot evaluation, and the tree would be built and discarded per cell. When
 * the dependency graph arrives it will want a retained AST, and this becomes
 * the front half of it. */

/* Snapshots of OTHER sheets, taken on demand.
 *
 * A cross-sheet reference needs the referenced sheet parsed, and parsing it per
 * reference would be ruinous -- a sheet of N formulas each pointing at a lookup
 * table would re-parse that table N times. So each sheet is snapshotted at most
 * once per evaluation and kept until the evaluation ends.
 *
 * Values are read from the referenced sheet's CACHED values, exactly as a
 * same-sheet reference reads the snapshot rather than recomputing. That keeps
 * evaluation non-recursive across sheets, and it is why xlsx.recalc remains a
 * per-sheet operation -- see the note on xlsx_recalc. */
typedef struct {
    char *name;
    /* HEAP-ALLOCATED, not stored inline. The pool grows by realloc, and callers
     * hold the XlsxSnap* they were handed -- a range descriptor keeps one for
     * the length of a call. Storing snapshots inline meant the second sheet a
     * formula touched could move the first one out from under a live pointer,
     * which crashed on a real corpus workbook rather than merely reading
     * stale data. */
    XlsxSnap *snap;
    int ok;
} XlsxSheetCache;

/* Owned by the CALLER of a whole evaluation pass, not by one formula.
 * xlsx.check and xlsx.recalc walk thousands of formulas that all point at the
 * same lookup table; a per-formula cache would re-parse that sheet once per
 * formula, which is the same shape of blowup the (row,col) index removed. */
typedef struct {
    XlsxSheetCache *items;
    size_t count;
} XlsxSheetPool;

static void xlsx_pool_free(XlsxSheetPool *p) {
    for (size_t i = 0; i < p->count; i++) {
        free(p->items[i].name);
        if (p->items[i].snap) {
            if (p->items[i].ok) xlsx_snap_free(p->items[i].snap);
            free(p->items[i].snap);
        }
    }
    free(p->items);
    p->items = NULL; p->count = 0;
}

typedef struct {
    XlsxLex lx;
    XlsxWorkbook *wb;
    const XlsxSnap *snap;         /* the sheet being evaluated */
    const char *sheet_name;       /* its name, so Self!A1 resolves to itself */
    XlsxSheetPool *pool;          /* borrowed; lives for the whole pass */
    int depth;
    int unsupported;        /* set when a function is not implemented */
    char unsupported_name[64];
} XlsxEval;

/* The snapshot for `sheet`, or NULL if there is no such sheet. An empty name
 * means the sheet under evaluation. */
static const XlsxSnap *xlsx_eval_sheet(XlsxEval *ev, const char *sheet) {
    if (!sheet || !*sheet) return ev->snap;
    if (ev->sheet_name && strcmp(sheet, ev->sheet_name) == 0) return ev->snap;
    if (!ev->pool) return NULL;
    for (size_t i = 0; i < ev->pool->count; i++) {
        if (strcmp(ev->pool->items[i].name, sheet) == 0) {
            return ev->pool->items[i].ok ? ev->pool->items[i].snap : NULL;
        }
    }
    XlsxSheetCache *g = realloc(ev->pool->items, (ev->pool->count + 1) * sizeof *g);
    if (!g) abort();
    ev->pool->items = g;
    XlsxSheetCache *slot = &ev->pool->items[ev->pool->count++];
    slot->name = copy_string(sheet);
    slot->snap = calloc(1, sizeof *slot->snap);
    if (!slot->snap) abort();
    slot->ok = xlsx_snapshot(ev->wb, sheet, slot->snap);
    return slot->ok ? slot->snap : NULL;
}

static XlsxVal xlsx_expr(XlsxEval *ev);

static XlsxVal xlsx_cell_value_in(XlsxEval *ev, const char *sheet, int external,
                                  const char *ref) {
    long col, row;
    if (!xlsx_parse_ref(ref, &col, &row)) return xv_err("#REF!");
    if (external) {
        /* A different workbook, which is not open. Excel caches the last-known
         * value in xl/externalLinks, but reading a stale copy of a file we do
         * not have and presenting it as current would be exactly the confident
         * wrong number this module refuses. Reported by name instead. */
        ev->unsupported = 1;
        snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                 "external workbook reference %s", sheet);
        return xv_err("#REF!");
    }
    const XlsxSnap *snap = xlsx_eval_sheet(ev, sheet);
    if (!snap) return xv_err("#REF!");
    const XlsxSnapCell *c = xlsx_snap_at(snap, row, col);
    if (!c) return xv_empty();
    switch (c->kind) {
    case XV_NUM:  return xv_num(c->num);
    case XV_BOOL: return xv_bool((int)c->num);
    case XV_STR:  return xv_str(c->str ? c->str : "");
    case XV_ERR:  return xv_err(c->str ? c->str : "#ERROR");
    default:      return xv_empty();
    }
}

/* Coerce for arithmetic. Empty is zero; a non-numeric string is #VALUE!,
 * matching Excel rather than silently reading as zero. */
static int xlsx_as_num(XlsxVal v, double *out) {
    switch (v.kind) {
    case XV_NUM:  *out = v.num; return 1;
    case XV_BOOL: *out = v.num; return 1;
    case XV_EMPTY: *out = 0; return 1;
    case XV_STR: {
        if (!v.str || !*v.str) { *out = 0; return 1; }
        char *end = NULL;
        double d = strtod(v.str, &end);
        while (end && *end == ' ') end++;
        if (end && *end == '\0') { *out = d; return 1; }
        return 0;
    }
    default: return 0;
    }
}

static void xlsx_to_text(XlsxVal v, char *buf, size_t n) {
    switch (v.kind) {
    case XV_STR:  snprintf(buf, n, "%s", v.str ? v.str : ""); break;
    case XV_NUM:  snprintf(buf, n, "%.15g", v.num); break;
    case XV_BOOL: snprintf(buf, n, "%s", v.num ? "TRUE" : "FALSE"); break;
    case XV_ERR:  snprintf(buf, n, "%s", v.str ? v.str : "#ERROR"); break;
    default:      snprintf(buf, n, "%s", ""); break;
    }
}

/* A range left UNMATERIALISED: where it is, not what is in it.
 *
 * VLOOKUP over a big lookup table is the dominant shape in real workbooks, and
 * expanding the table into a value list per call is ruinous -- one corpus
 * workbook calls VLOOKUP twice per formula over 'CP Trade Data'!$D$2:$P$6949,
 * a 90,324-cell rectangle, across thousands of formulas. Searching one column
 * of it costs 6,948 indexed lookups instead of 90,324 value copies.
 *
 * So the four functions that address a range BY POSITION take the descriptor
 * and read the cells they actually need. Everything else still gets the flat
 * list, because an aggregate genuinely does have to visit every cell. */
typedef struct {
    const XlsxSnap *snap;
    long r1, c1, r2, c2;
    int is_range;
} XlsxRangeDesc;

/* One cell of an unmaterialised range, addressed from its top-left. */
static XlsxVal xlsx_range_cell(const XlsxRangeDesc *d, long r, long c) {
    if (!d->is_range || !d->snap) return xv_empty();
    long row = d->r1 + r, col = d->c1 + c;
    if (row < d->r1 || row > d->r2 || col < d->c1 || col > d->c2) return xv_empty();
    const XlsxSnapCell *sc = xlsx_snap_at(d->snap, row, col);
    if (!sc) return xv_empty();
    switch (sc->kind) {
    case XV_NUM:  return xv_num(sc->num);
    case XV_BOOL: return xv_bool((int)sc->num);
    case XV_STR:  return xv_str(sc->str ? sc->str : "");
    case XV_ERR:  return xv_err(sc->str ? sc->str : "#ERROR");
    default:      return xv_empty();
    }
}

/* Could this token be one END of a range? A REF always can. A bare column
 * ("B") or row ("7") lexes as a NAME because it names no single cell, but it is
 * exactly what the whole-column form A:B is made of, so it qualifies too --
 * and only when it is purely letters or purely digits, which keeps a defined
 * name from being mistaken for one. */
static int xlsx_range_endpoint(const XlsxTok *t) {
    if (t->kind == XT_REF) return 1;
    if (t->kind != XT_NAME || !t->text[0]) return 0;
    int letters = 1, digits = 1;
    for (const char *p = t->text; *p; p++) {
        if (!isalpha((unsigned char)*p)) letters = 0;
        if (!isdigit((unsigned char)*p)) digits = 0;
    }
    return (letters && strlen(t->text) <= 3) || digits;
}

/* The extent of a sheet's populated cells. Used to give a WHOLE-COLUMN range a
 * finite size. */
static void xlsx_snap_extent(const XlsxSnap *s, long *max_row, long *max_col) {
    *max_row = 0; *max_col = 0;
    for (size_t i = 0; i < s->count; i++) {
        if (s->cells[i].row > *max_row) *max_row = s->cells[i].row;
        if (s->cells[i].col > *max_col) *max_col = s->cells[i].col;
    }
}

/* Resolve the two ends of a range, including the WHOLE-COLUMN and WHOLE-ROW
 * forms (A:B, 3:7) that a lookup table is usually written with because the
 * author does not know how far the data will grow.
 *
 * A:B nominally means 1,048,576 rows. Materialising that per formula is not an
 * option -- the corpus has sheets with tens of thousands of such formulas -- so
 * an open end is clamped to the sheet's ACTUAL extent, which contains every
 * value the range could have held anyway. */
static int xlsx_range_bounds(const XlsxSnap *snap, const char *a, const char *b,
                             long *c1, long *r1, long *c2, long *r2) {
    long mr = 0, mc = 0;
    int a_full_col = 1, b_full_col = 1, a_full_row = 1, b_full_row = 1;
    for (const char *p = a; *p; p++) { if (isdigit((unsigned char)*p)) a_full_col = 0; else a_full_row = 0; }
    for (const char *p = b; *p; p++) { if (isdigit((unsigned char)*p)) b_full_col = 0; else b_full_row = 0; }
    if (!*a || !*b) return 0;

    if (a_full_col && b_full_col) {          /* A:B -- whole columns */
        xlsx_snap_extent(snap, &mr, &mc);
        long ca = 0, cb = 0;
        for (const char *p = a; *p; p++) ca = ca * 26 + (toupper((unsigned char)*p) - 'A' + 1);
        for (const char *p = b; *p; p++) cb = cb * 26 + (toupper((unsigned char)*p) - 'A' + 1);
        /* xlsx_parse_ref stores columns ZERO-based (A -> 0) while rows stay
         * 1-based, so the letter value must be decremented to match. Getting
         * this wrong reads the neighbouring column, which is invisible when the
         * neighbour happens to hold a plausible value: COUNTA(A:A) returned the
         * right answer for the wrong column purely because column B also had
         * four entries. */
        *c1 = ca - 1; *c2 = cb - 1; *r1 = 1; *r2 = mr ? mr : 1;
        return 1;
    }
    if (a_full_row && b_full_row) {          /* 3:7 -- whole rows */
        xlsx_snap_extent(snap, &mr, &mc);
        *r1 = strtol(a, NULL, 10); *r2 = strtol(b, NULL, 10);
        *c1 = 1; *c2 = mc ? mc : 1;
        return 1;
    }
    return xlsx_parse_ref(a, c1, r1) && xlsx_parse_ref(b, c2, r2);
}

/* Collect a function argument. A bare range (B2:B3) expands to its cells; every
 * other argument yields one value.
 *
 * `srcs`, when non-NULL, receives the SOURCE CELL behind each value, or NULL
 * for anything computed rather than read. Only SUBTOTAL needs it, and it needs
 * it for two things it cannot do from the values alone: skip hidden rows, and
 * skip cells that are themselves SUBTOTALs. Both are properties of where a
 * value came from, which flattening to a value list would otherwise discard. */
static void xlsx_arg_values(XlsxEval *ev, XlsxVal **vals, const XlsxSnapCell ***srcs,
                            size_t *n, size_t *cap, long *out_rows, long *out_cols,
                            XlsxRangeDesc *out_range, int lazy) {
    /* A range is ENDPOINT COLON ENDPOINT and can only appear as a whole
     * argument. */
    if (xlsx_range_endpoint(&ev->lx.cur)) {
        char first[128], sheet[128];
        int ext = ev->lx.cur.external;
        snprintf(first, sizeof first, "%s", ev->lx.cur.text);
        snprintf(sheet, sizeof sheet, "%s", ev->lx.cur.sheet);
        XlsxLex save = ev->lx;
        xlsx_lex_next(&ev->lx);
        if (ev->lx.cur.kind == XT_COLON) {
            xlsx_lex_next(&ev->lx);
            if (xlsx_range_endpoint(&ev->lx.cur)) {
                long c1, r1, c2, r2;
                const XlsxSnap *rs = ext ? NULL : xlsx_eval_sheet(ev, sheet);
                if (ext) {
                    ev->unsupported = 1;
                    snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                             "external workbook reference %s", sheet);
                }
                if (rs && xlsx_range_bounds(rs, first, ev->lx.cur.text, &c1, &r1, &c2, &r2)) {
                    if (c1 > c2) { long t = c1; c1 = c2; c2 = t; }
                    if (r1 > r2) { long t = r1; r1 = r2; r2 = t; }
                    if (out_range) {
                        out_range->snap = rs; out_range->is_range = 1;
                        out_range->r1 = r1; out_range->c1 = c1;
                        out_range->r2 = r2; out_range->c2 = c2;
                    }
                    if (out_rows) *out_rows = r2 - r1 + 1;
                    if (out_cols) *out_cols = c2 - c1 + 1;
                    if (lazy) {           /* the caller will read cells itself */
                        xlsx_lex_next(&ev->lx);
                        return;
                    }
                    for (long r = r1; r <= r2; r++) {
                        for (long c = c1; c <= c2; c++) {
                            const XlsxSnapCell *sc = xlsx_snap_at(rs, r, c);
                            XlsxVal v = xv_empty();
                            if (sc) {
                                switch (sc->kind) {
                                case XV_NUM: v = xv_num(sc->num); break;
                                case XV_BOOL: v = xv_bool((int)sc->num); break;
                                case XV_STR: v = xv_str(sc->str ? sc->str : ""); break;
                                case XV_ERR: v = xv_err(sc->str ? sc->str : "#ERROR"); break;
                                default: break;
                                }
                            }
                            if (*n == *cap) {
                                *cap = *cap ? *cap * 2 : 8;
                                XlsxVal *g = realloc(*vals, *cap * sizeof(XlsxVal));
                                if (!g) abort();
                                *vals = g;
                                if (srcs) {
                                    const XlsxSnapCell **gs =
                                        realloc(*srcs, *cap * sizeof(*gs));
                                    if (!gs) abort();
                                    *srcs = gs;
                                }
                            }
                            if (srcs) (*srcs)[*n] = sc;
                            (*vals)[(*n)++] = v;
                        }
                    }
                    if (out_rows) *out_rows = r2 - r1 + 1;
                    if (out_cols) *out_cols = c2 - c1 + 1;
                    xlsx_lex_next(&ev->lx);
                    return;
                }
            }
        }
        ev->lx = save;   /* not a range after all; re-parse as an expression */
    }
    /* A single REF is still a cell read, so it keeps its provenance: a grand
     * total written SUBTOTAL(9,B2,B5,B9) over individual subtotal cells must
     * exclude them exactly as the range form does. */
    const XlsxSnapCell *one = NULL;
    if (srcs && ev->lx.cur.kind == XT_REF && !ev->lx.cur.external) {
        long c0, r0;
        const XlsxSnap *os = xlsx_eval_sheet(ev, ev->lx.cur.sheet);
        if (os && xlsx_parse_ref(ev->lx.cur.text, &c0, &r0)) one = xlsx_snap_at(os, r0, c0);
    }
    XlsxVal v = xlsx_expr(ev);
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        XlsxVal *g = realloc(*vals, *cap * sizeof(XlsxVal));
        if (!g) abort();
        *vals = g;
        if (srcs) {
            const XlsxSnapCell **gs = realloc(*srcs, *cap * sizeof(*gs));
            if (!gs) abort();
            *srcs = gs;
        }
    }
    if (srcs) (*srcs)[*n] = one;
    (*vals)[(*n)++] = v;
    if (out_rows) *out_rows = 1;
    if (out_cols) *out_cols = 1;
}

static int xlsx_is_volatile(const char *name) {
    return strcmp(name, "NOW") == 0 || strcmp(name, "TODAY") == 0 ||
           strcmp(name, "RAND") == 0 || strcmp(name, "RANDBETWEEN") == 0;
}

/* ------------------------------------------------------- Excel date serials
 *
 * A date in Excel is a NUMBER: days since the epoch, with the time of day as
 * the fraction. Only the cell's number format distinguishes 45000 from a date,
 * which is why the reader preserves style indices (§13.B).
 *
 * THE EPOCH IS 1899-12-30, NOT 1900-01-01. Lotus 1-2-3 treated 1900 as a leap
 * year; Excel reproduced the bug deliberately for file compatibility and is
 * stuck with it. So serial 60 is the day that never existed, 1900-02-29, and
 * every serial from 61 on is one greater than a correct day count would give.
 * Shifting the epoch back two days makes all dates from 1900-03-01 onward come
 * out right, which is the whole range anyone has data in. Dates before that are
 * REFUSED rather than returned off by one -- see xlsx_serial_from_civil.
 *
 * 25569 is the resulting serial for 1970-01-01, the well-known constant. */
#define XLSX_EPOCH_1970 25569
#define XLSX_SERIAL_1900_03_01 61

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
 * days_from_civil). Exact for the whole range we care about, and it does not
 * call mktime, whose DST normalisation would shift a bare date by an hour. */
static long xlsx_days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

/* The Excel serial for a civil date, or -1 if it predates the epoch bug's
 * safe range. Refusing is deliberate: 1900-01-01..1900-02-28 would each need a
 * different correction, and returning a silently-wrong day is the failure mode
 * this module exists to avoid. */
static double xlsx_serial_from_civil(long y, unsigned m, unsigned d) {
    double s = (double)xlsx_days_from_civil(y, m, d) + XLSX_EPOCH_1970;
    if (s < XLSX_SERIAL_1900_03_01) return -1;
    return s;
}

/* A date ARGUMENT: already a serial, or text Excel would coerce to one.
 * Kept separate from xlsx_as_num rather than folded into it, so that widening
 * what counts as a number cannot quietly change the meaning of ordinary
 * arithmetic elsewhere in the evaluator. */
static int xlsx_as_serial(XlsxVal v, double *out);

static long xlsx_days_in_month(long y, unsigned m) {
    static const int len[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 30;
    if (m == 2) {
        int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return len[m];
}

/* The inverse of xlsx_serial_from_civil (Hinnant's civil_from_days). Returns 0
 * for a serial inside the range the 1900 leap-year bug makes ambiguous, so a
 * date function refuses rather than answering one day out. */
static int xlsx_civil_from_serial(double serial, long *y, long *m, long *d) {
    if (serial < XLSX_SERIAL_1900_03_01) return 0;
    long z = (long)serial - XLSX_EPOCH_1970 + 719468L;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long doe = (unsigned long)(z - era * 146097L);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned long mm = mp + (mp < 10 ? 3 : -9);
    *y = yy + (mm <= 2);
    *m = (long)mm;
    *d = (long)dd;
    return 1;
}

static int xlsx_as_serial(XlsxVal v, double *out) {
    if (v.kind == XV_NUM || v.kind == XV_BOOL || v.kind == XV_EMPTY) {
        return xlsx_as_num(v, out);
    }
    if (v.kind != XV_STR || !v.str) return 0;
    long y = 0, m = 0, d = 0;
    if (sscanf(v.str, "%ld-%ld-%ld", &y, &m, &d) == 3 ||
        sscanf(v.str, "%ld/%ld/%ld", &m, &d, &y) == 3) {
        if (m < 1 || m > 12 || d < 1 || d > xlsx_days_in_month(y, (unsigned)m)) return 0;
        double s = xlsx_serial_from_civil(y, (unsigned)m, (unsigned)d);
        if (s < 0) return 0;
        *out = s;
        return 1;
    }
    return xlsx_as_num(v, out);
}

/* The current instant as an Excel serial, in LOCAL time -- Excel's NOW() is
 * local, and a UTC answer would be a day out for half the world near midnight.
 *
 * GBASIC_XLSX_NOW is a TEST SEAM, and the only reason it exists: the epoch
 * above is exactly the kind of arithmetic that is wrong by one and stays wrong,
 * and a test that can only assert "NOW is a plausible number" would never catch
 * that. Set it to an integer number of seconds since the Unix epoch (UTC) to
 * pin the clock. Unset -- always, outside the test suite -- the real clock is
 * used. It is read fresh on each call rather than cached, so a test can step
 * it between recalculations. */
static double xlsx_now_serial(void) {
    time_t t;
    const char *pin = getenv("GBASIC_XLSX_NOW");
    if (pin && *pin) {
        char *end = NULL;
        long long v = strtoll(pin, &end, 10);
        if (end && *end == '\0') {
            t = (time_t)v;
        } else {
            t = time(NULL);
        }
    } else {
        t = time(NULL);
    }
    struct tm lt;
    if (!localtime_r(&t, &lt)) return 0;
    double day = xlsx_serial_from_civil((long)lt.tm_year + 1900,
                                        (unsigned)lt.tm_mon + 1,
                                        (unsigned)lt.tm_mday);
    if (day < 0) return 0;
    double frac = ((double)lt.tm_hour * 3600.0 + (double)lt.tm_min * 60.0 +
                   (double)lt.tm_sec) / 86400.0;
    return day + frac;
}

/* ------------------------------------------------- post-2001 function naming
 *
 * A function newer than the original ECMA-376 function list is not stored under
 * its own name. Excel writes it with a FUTURE-FUNCTION PREFIX so that an older
 * reader cannot mistake it for something it knows:
 *
 *     XLOOKUP(...)      is stored as  _xlfn.XLOOKUP(...)
 *     SORT(...)         is stored as  _xlfn._xlws.SORT(...)     worksheet-only
 *     LET(x, A1, x*2)   is stored as  _xlfn.LET(_xlpm.x, A1, _xlpm.x*2)
 *
 * The split is by SCHEMA VERSION, not by release year, which is why the
 * 2007-era additions -- IFERROR, SUMIFS, COUNTIFS, AVERAGEIFS, EOMONTH,
 * YEARFRAC, NETWORKDAYS -- carry no prefix at all while STDEV.S and IFNA do.
 * Verified by generating each one and reading the bytes back, not assumed.
 *
 * Stripping these is a precondition for supporting ANY modern function: without
 * it every one of them is an unknown name however well it is implemented.
 *
 * What must NOT be stripped: `_xll.` (a third-party add-in, e.g. Enron's
 * _xll.HPVAL, 9,240 uses in the corpus) and `_xludf.` (a VBA user-defined
 * function). Those genuinely cannot be evaluated, and reporting them by their
 * real name is the point -- they are the 3.0% ceiling in §13.I. */
static const char *xlsx_strip_future_prefix(const char *name) {
    for (;;) {
        if (strncasecmp(name, "_xlfn.", 6) == 0) { name += 6; continue; }
        if (strncasecmp(name, "_xlws.", 6) == 0) { name += 6; continue; }
        break;
    }
    return name;
}

/* ------------------------------------------------------ criteria matching
 *
 * Shared by the whole IF-criteria family (SUMIF, SUMIFS, and kin). A criterion is a VALUE with an optional
 * leading comparison operator, all inside one string: ">4", "<>x", "apple",
 * "<=2026-01-01". A bare value means equality.
 *
 * Two rules that are easy to get wrong and silently produce plausible totals:
 *   * a NUMERIC criterion compares numerically, so ">4" matches 12 -- string
 *     comparison would put "12" before "4" and quietly drop it;
 *   * text comparison is CASE-INSENSITIVE in Excel, so "apple" matches "Apple".
 *
 * Wildcards (* and ?) are supported for text equality, since a criterion like
 * "Loan*" is ordinary in exactly the report-shaped work this is for. */
static int xlsx_wild_match(const char *pat, const char *s) {
    /* Iterative backtracking: no recursion, so a pathological pattern cannot
     * blow the stack the way the JSON parser once did. */
    const char *star = NULL, *ss = s;
    while (*s) {
        if (*pat == '~' && (pat[1] == '*' || pat[1] == '?')) {
            /* ~ escapes a literal * or ? in Excel criteria. */
            if (tolower((unsigned char)pat[1]) != tolower((unsigned char)*s)) {
                if (!star) return 0;
                pat = star + 1; s = ++ss; continue;
            }
            pat += 2; s++; continue;
        }
        if (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*s)) {
            pat++; s++; continue;
        }
        if (*pat == '*') { star = pat++; ss = s; continue; }
        if (star) { pat = star + 1; s = ++ss; continue; }
        return 0;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

static int xlsx_criteria_match(XlsxVal v, const char *crit) {
    if (!crit) return 0;
    /* Split off a leading comparison operator. */
    int op = '='; size_t skip = 0;
    if (crit[0] == '>' && crit[1] == '=') { op = 'G'; skip = 2; }
    else if (crit[0] == '<' && crit[1] == '=') { op = 'L'; skip = 2; }
    else if (crit[0] == '<' && crit[1] == '>') { op = 'N'; skip = 2; }
    else if (crit[0] == '>') { op = '>'; skip = 1; }
    else if (crit[0] == '<') { op = '<'; skip = 1; }
    else if (crit[0] == '=') { op = '='; skip = 1; }
    const char *rhs = crit + skip;

    /* Numeric comparison when the criterion's value is a number AND the cell
     * holds one. An empty cell participates in no comparison but equality
     * against an empty criterion. */
    char *end = NULL;
    double rnum = strtod(rhs, &end);
    int rhs_numeric = end && end != rhs && *end == '\0';

    if (rhs_numeric && (v.kind == XV_NUM || v.kind == XV_BOOL)) {
        double x = v.num;
        switch (op) {
        case '>': return x > rnum;
        case '<': return x < rnum;
        case 'G': return x >= rnum;
        case 'L': return x <= rnum;
        case 'N': return x != rnum;
        default:  return x == rnum;
        }
    }

    /* Otherwise compare as text, case-insensitively. */
    char buf[256];
    xlsx_to_text(v, buf, sizeof buf);
    if (v.kind == XV_EMPTY) buf[0] = '\0';
    if (op == '=' || op == 'N') {
        int eq;
        if (strpbrk(rhs, "*?")) eq = xlsx_wild_match(rhs, buf);
        else eq = strcasecmp(buf, rhs) == 0;
        /* An empty cell does not satisfy "not equal to X" in Excel. */
        if (op == 'N') return v.kind == XV_EMPTY ? 0 : !eq;
        return eq;
    }
    if (v.kind == XV_EMPTY) return 0;
    int c = strcasecmp(buf, rhs);
    switch (op) {
    case '>': return c > 0;
    case '<': return c < 0;
    case 'G': return c >= 0;
    case 'L': return c <= 0;
    default:  return 0;
    }
}

/* Does this cell hold a SUBTOTAL formula? SUBTOTAL ignores nested SUBTOTALs in
 * its range, which is the whole reason the function exists rather than SUM: a
 * grand total can span a column that already contains per-group subtotals
 * without double-counting them. Without this, the classic report layout
 * silently returns twice the right answer. */
static int xlsx_cell_is_subtotal(const XlsxSnapCell *c) {
    if (!c || !c->formula) return 0;
    const char *p = c->formula;
    while (*p == ' ' || *p == '=' || *p == '+') p++;
    return strncasecmp(p, "SUBTOTAL", 8) == 0;
}

static XlsxVal xlsx_call(XlsxEval *ev, const char *raw_name) {
    /* Resolve the on-disk spelling to the function's real name before anything
     * else looks at it, so every dispatch below -- and the unsupported report
     * -- sees XLOOKUP rather than _xlfn.XLOOKUP. */
    const char *name = xlsx_strip_future_prefix(raw_name);
    XlsxVal *args = NULL;
    const XlsxSnapCell **srcs = NULL;
    size_t n = 0, cap = 0;
    int want_srcs = strcmp(name, "SUBTOTAL") == 0;
    /* On entry the current token is the NAME and the input sits at '('. Two
     * advances are needed: one to land ON the paren, one to step PAST it. With
     * only one, the first argument is parsed as a parenthesised expression —
     * which made SUM(B2:B3) silently return B2 alone, a wrong number that looks
     * entirely plausible. */
    xlsx_lex_next(&ev->lx);              /* cur == '(' */
    xlsx_lex_next(&ev->lx);              /* cur == first argument, or ')' */
    /* ARGUMENT BOUNDARIES. A range argument expands to many values, so the flat
     * list alone cannot say which argument a value came from -- and the
     * IF-criteria family is defined by walking two or more ranges IN PARALLEL.
     * argoff[k] is where argument k starts; argoff[argc] is n. */
    size_t *argoff = NULL, argc = 0, argcap = 0;
    /* Per-argument SHAPE. VLOOKUP and friends must index a range by row and
     * column -- "the 15th column of the matched row" -- which a flat list
     * cannot express. Recorded here because only the collector knows it. */
    long *argrows = NULL, *argcols = NULL;
    XlsxRangeDesc *argrange = NULL;
    /* The four functions that address a range BY POSITION read cells directly
     * from the descriptor instead of having the whole rectangle materialised. */
    int lazy = strcmp(name, "VLOOKUP") == 0 || strcmp(name, "HLOOKUP") == 0 ||
               strcmp(name, "INDEX") == 0 || strcmp(name, "MATCH") == 0;
    if (ev->lx.cur.kind != XT_RPAREN) {
        for (;;) {
            if (argc == argcap) {
                argcap = argcap ? argcap * 2 : 8;
                size_t *g = realloc(argoff, (argcap + 1) * sizeof *g);
                if (!g) abort();
                argoff = g;
                long *gr = realloc(argrows, argcap * sizeof *gr);
                long *gc = realloc(argcols, argcap * sizeof *gc);
                XlsxRangeDesc *gd = realloc(argrange, argcap * sizeof *gd);
                if (!gr || !gc || !gd) abort();
                argrows = gr; argcols = gc; argrange = gd;
            }
            argoff[argc] = n;
            argrows[argc] = 0; argcols[argc] = 0;
            memset(&argrange[argc], 0, sizeof argrange[argc]);
            argc++;
            xlsx_arg_values(ev, &args, want_srcs ? &srcs : NULL, &n, &cap,
                            &argrows[argc - 1], &argcols[argc - 1],
                            &argrange[argc - 1], lazy);
            if (ev->lx.cur.kind != XT_COMMA) break;
            xlsx_lex_next(&ev->lx);
        }
    }
    if (argoff) argoff[argc] = n;
    /* Length of argument k, and its first value -- the two accessors every
     * criteria function below is written in terms of. */
    #define ARG_LEN(k)  ((k) < argc ? argoff[(k) + 1] - argoff[(k)] : 0)
    #define ARG_AT(k,i) (args[argoff[(k)] + (i)])
    /* Row-major within the argument's rectangle. */
    #define ARG_RC(k,r,c) (args[argoff[(k)] + (size_t)((r) * argcols[(k)] + (c))])
    /* One cell of a descriptor-held range, by offset from its top-left. */
    #define RNG_AT(k,r,c) xlsx_range_cell(&argrange[(k)], (r), (c))
    if (ev->lx.cur.kind == XT_RPAREN) xlsx_lex_next(&ev->lx);

    XlsxVal out = xv_err("#NAME?");

    /* Any error among the arguments propagates, which is Excel's rule and the
     * reason an error is its own kind rather than a string. */
    /* The error-CATCHING functions must see their arguments, errors and all;
     * everything else propagates the first error it was handed, which is
     * Excel's rule and the reason an error is its own value kind. */
    int catches_errors = strcmp(name, "IFERROR") == 0 || strcmp(name, "IFNA") == 0 ||
                         strcmp(name, "ISERROR") == 0 || strcmp(name, "ISERR") == 0 ||
                         strcmp(name, "ISNA") == 0 || strcmp(name, "ISBLANK") == 0 ||
                         strcmp(name, "ISNUMBER") == 0 || strcmp(name, "ISTEXT") == 0 ||
                         strcmp(name, "ISNONTEXT") == 0 || strcmp(name, "ISLOGICAL") == 0 ||
                         strcmp(name, "AGGREGATE") == 0;
    for (size_t i = 0; i < n && !catches_errors; i++) {
        if (args[i].kind == XV_ERR) {
            out = xv_err(args[i].str);
            goto done;
        }
    }

    if (strcmp(name, "SUM") == 0 || strcmp(name, "AVERAGE") == 0) {
        double total = 0; long cnt = 0;
        for (size_t i = 0; i < n; i++) {
            /* Text and empties are SKIPPED by SUM over a range, not coerced —
             * Excel counts only numbers, and coercing would invent data. */
            if (args[i].kind == XV_NUM || args[i].kind == XV_BOOL) { total += args[i].num; cnt++; }
        }
        if (strcmp(name, "AVERAGE") == 0) {
            out = cnt ? xv_num(total / (double)cnt) : xv_err("#DIV/0!");
        } else {
            out = xv_num(total);
        }
    } else if (strcmp(name, "COUNT") == 0) {
        long cnt = 0;
        for (size_t i = 0; i < n; i++) if (args[i].kind == XV_NUM) cnt++;
        out = xv_num((double)cnt);
    } else if (strcmp(name, "COUNTA") == 0) {
        long cnt = 0;
        for (size_t i = 0; i < n; i++) if (args[i].kind != XV_EMPTY) cnt++;
        out = xv_num((double)cnt);
    } else if (strcmp(name, "MIN") == 0 || strcmp(name, "MAX") == 0) {
        int is_min = strcmp(name, "MIN") == 0;
        double best = 0; int seen = 0;
        for (size_t i = 0; i < n; i++) {
            if (args[i].kind != XV_NUM && args[i].kind != XV_BOOL) continue;
            if (!seen || (is_min ? args[i].num < best : args[i].num > best)) { best = args[i].num; seen = 1; }
        }
        out = xv_num(seen ? best : 0);
    } else if (strcmp(name, "ROUND") == 0 || strcmp(name, "ROUNDUP") == 0 ||
               strcmp(name, "ROUNDDOWN") == 0) {
        double x = 0, digits = 0;
        if (n >= 1 && xlsx_as_num(args[0], &x) && (n < 2 || xlsx_as_num(args[1], &digits))) {
            double scale = pow(10.0, digits);
            double y = x * scale;
            if (strcmp(name, "ROUND") == 0) y = (y < 0) ? -floor(-y + 0.5) : floor(y + 0.5);
            else if (strcmp(name, "ROUNDUP") == 0) y = (y < 0) ? floor(y) : ceil(y);
            else y = (y < 0) ? ceil(y) : floor(y);
            out = xv_num(y / scale);
        } else {
            out = xv_err("#VALUE!");
        }
    } else if (strcmp(name, "ABS") == 0) {
        double x = 0;
        out = (n >= 1 && xlsx_as_num(args[0], &x)) ? xv_num(fabs(x)) : xv_err("#VALUE!");
    } else if (strcmp(name, "IF") == 0) {
        double c = 0;
        int truthy = n >= 1 && xlsx_as_num(args[0], &c) && c != 0;
        if (n >= 3) out = args[truthy ? 1 : 2], args[truthy ? 1 : 2] = xv_empty();
        else if (n == 2) out = truthy ? (args[1]) : xv_bool(0), args[1] = truthy ? xv_empty() : args[1];
        else out = xv_err("#VALUE!");
    } else if (strcmp(name, "IFERROR") == 0) {
        if (n >= 2) {
            if (args[0].kind == XV_ERR) { out = args[1]; args[1] = xv_empty(); }
            else { out = args[0]; args[0] = xv_empty(); }
        } else {
            out = xv_err("#VALUE!");
        }
    } else if (strcmp(name, "TRUE") == 0) {
        out = xv_bool(1);
    } else if (strcmp(name, "FALSE") == 0) {
        out = xv_bool(0);
    } else if (strcmp(name, "SUMIF") == 0 || strcmp(name, "AVERAGEIF") == 0 ||
               strcmp(name, "COUNTIF") == 0) {
        /* The single-criterion forms, whose argument order is the ODD one:
         *     SUMIF(range, criteria, [sum_range])
         * with the aggregated range LAST and optional, where the *IFS forms
         * below put it first and require it. Mixing the two up yields a
         * confident wrong total, so they are implemented separately rather
         * than folded together. */
        char crit[256];
        if (argc < 2) { out = xv_err("#VALUE!"); goto done; }
        xlsx_to_text(ARG_AT(1, 0), crit, sizeof crit);
        size_t rn = ARG_LEN(0);
        int have_agg = argc >= 3;
        double total = 0; long cnt = 0;
        for (size_t i = 0; i < rn; i++) {
            if (!xlsx_criteria_match(ARG_AT(0, i), crit)) continue;
            if (strcmp(name, "COUNTIF") == 0) { cnt++; continue; }
            /* The aggregated cell is the one at the SAME OFFSET in the other
             * range; a shorter sum_range simply has no cell there. */
            XlsxVal a = have_agg ? (i < ARG_LEN(2) ? ARG_AT(2, i) : xv_empty())
                                 : ARG_AT(0, i);
            if (a.kind == XV_NUM || a.kind == XV_BOOL) { total += a.num; cnt++; }
        }
        if (strcmp(name, "COUNTIF") == 0) out = xv_num((double)cnt);
        else if (strcmp(name, "AVERAGEIF") == 0)
            out = cnt ? xv_num(total / (double)cnt) : xv_err("#DIV/0!");
        else out = xv_num(total);
    } else if (strcmp(name, "SUMIFS") == 0 || strcmp(name, "AVERAGEIFS") == 0 ||
               strcmp(name, "COUNTIFS") == 0 || strcmp(name, "MAXIFS") == 0 ||
               strcmp(name, "MINIFS") == 0) {
        /* The multi-criteria forms. COUNTIFS is pairs only; the rest lead with
         * the range being aggregated:
         *     SUMIFS(sum_range, crit_range1, crit1, crit_range2, crit2, ...)
         *     COUNTIFS(crit_range1, crit1, ...)
         * A row counts only when EVERY criterion matches at that offset. */
        int counting = strcmp(name, "COUNTIFS") == 0;
        size_t first_pair = counting ? 0 : 1;
        if (argc < first_pair + 2 || ((argc - first_pair) % 2) != 0) {
            out = xv_err("#VALUE!"); goto done;
        }
        size_t rows = counting ? ARG_LEN(0) : ARG_LEN(1);
        double total = 0, best = 0; long cnt = 0; int seen = 0;
        int is_max = strcmp(name, "MAXIFS") == 0, is_min = strcmp(name, "MINIFS") == 0;
        for (size_t i = 0; i < rows; i++) {
            int all = 1;
            for (size_t p = first_pair; p + 1 < argc && all; p += 2) {
                char crit[256];
                xlsx_to_text(ARG_AT(p + 1, 0), crit, sizeof crit);
                if (i >= ARG_LEN(p) || !xlsx_criteria_match(ARG_AT(p, i), crit)) all = 0;
            }
            if (!all) continue;
            if (counting) { cnt++; continue; }
            XlsxVal a = i < ARG_LEN(0) ? ARG_AT(0, i) : xv_empty();
            if (a.kind != XV_NUM && a.kind != XV_BOOL) continue;
            total += a.num; cnt++;
            if (!seen || (is_min ? a.num < best : a.num > best)) { best = a.num; seen = 1; }
        }
        if (counting) out = xv_num((double)cnt);
        else if (is_max || is_min) out = xv_num(seen ? best : 0);
        else if (strcmp(name, "AVERAGEIFS") == 0)
            out = cnt ? xv_num(total / (double)cnt) : xv_err("#DIV/0!");
        else out = xv_num(total);
    } else if (strcmp(name, "IFNA") == 0) {
        /* Narrower than IFERROR on purpose: only #N/A is caught, so a genuine
         * #VALUE! still surfaces instead of being swallowed by a lookup guard. */
        if (n >= 2) {
            int is_na = args[0].kind == XV_ERR && args[0].str &&
                        strcmp(args[0].str, "#N/A") == 0;
            size_t take = is_na ? 1 : 0;
            out = args[take];
            args[take] = xv_empty();
        } else out = xv_err("#VALUE!");
    } else if (strcmp(name, "NA") == 0) {
        out = xv_err("#N/A");
    } else if (strcmp(name, "ISNA") == 0 || strcmp(name, "ISERROR") == 0 ||
               strcmp(name, "ISERR") == 0 || strcmp(name, "ISNUMBER") == 0 ||
               strcmp(name, "ISTEXT") == 0 || strcmp(name, "ISNONTEXT") == 0 ||
               strcmp(name, "ISBLANK") == 0 || strcmp(name, "ISLOGICAL") == 0) {
        /* The type predicates. ISNA/ISERROR/ISERR are in the error-catching
         * list above, so they receive an error argument instead of propagating
         * it -- which is the whole point of them. The three differ: ISERROR is
         * any error, ISERR is any error EXCEPT #N/A, and ISNA is only #N/A.
         * A failed lookup wrapped in the wrong one silently changes which
         * failures are hidden. */
        XlsxVal a = n >= 1 ? args[0] : xv_empty();
        int is_err = a.kind == XV_ERR;
        int is_na = is_err && a.str && strcmp(a.str, "#N/A") == 0;
        int r = 0;
        if (strcmp(name, "ISNA") == 0)            r = is_na;
        else if (strcmp(name, "ISERROR") == 0)    r = is_err;
        else if (strcmp(name, "ISERR") == 0)      r = is_err && !is_na;
        else if (strcmp(name, "ISNUMBER") == 0)   r = a.kind == XV_NUM;
        else if (strcmp(name, "ISTEXT") == 0)     r = a.kind == XV_STR;
        else if (strcmp(name, "ISNONTEXT") == 0)  r = a.kind != XV_STR;
        else if (strcmp(name, "ISBLANK") == 0)    r = a.kind == XV_EMPTY;
        else                                      r = a.kind == XV_BOOL;
        out = xv_bool(r);
    } else if (strcmp(name, "XOR") == 0) {
        long t = 0;
        for (size_t i = 0; i < n; i++) {
            double x = 0;
            if (xlsx_as_num(args[i], &x) && x != 0) t++;
        }
        out = xv_bool((int)(t & 1));
    } else if (strcmp(name, "IFS") == 0) {
        /* Condition/value pairs, first true wins. No else branch: Excel returns
         * #N/A when nothing matches, which authors emulate with a final TRUE. */
        out = xv_err("#N/A");
        for (size_t i = 0; i + 1 < n; i += 2) {
            double c = 0;
            if (xlsx_as_num(args[i], &c) && c != 0) {
                out = args[i + 1]; args[i + 1] = xv_empty();
                break;
            }
        }
    } else if (strcmp(name, "SWITCH") == 0) {
        /* SWITCH(expr, v1, r1, v2, r2, ..., [default]) -- a trailing odd
         * argument is the default. */
        out = xv_err("#N/A");
        char want[256];
        xlsx_to_text(n ? args[0] : xv_empty(), want, sizeof want);
        size_t i = 1;
        int matched = 0;
        for (; i + 1 < n; i += 2) {
            char have[256];
            xlsx_to_text(args[i], have, sizeof have);
            if (strcasecmp(want, have) == 0) {
                out = args[i + 1]; args[i + 1] = xv_empty();
                matched = 1;
                break;
            }
        }
        if (!matched && n >= 2 && ((n - 1) % 2) == 1) {
            out = args[n - 1]; args[n - 1] = xv_empty();
        }
    } else if (strcmp(name, "CONCAT") == 0 || strcmp(name, "CONCATENATE") == 0) {
        /* CONCAT takes ranges; CONCATENATE (the older name) takes single
         * values. Both flatten to the same list here, so one body serves. */
        size_t len = 0;
        char *buf = malloc(1); buf[0] = '\0';
        for (size_t i = 0; i < n; i++) {
            char piece[256];
            xlsx_to_text(args[i], piece, sizeof piece);
            size_t pl = strlen(piece);
            char *g = realloc(buf, len + pl + 1);
            if (!g) abort();
            buf = g;
            memcpy(buf + len, piece, pl + 1);
            len += pl;
        }
        out = xv_str(buf);
        free(buf);
    } else if (strcmp(name, "TEXTJOIN") == 0) {
        /* TEXTJOIN(delimiter, ignore_empty, text...) */
        if (argc < 3) { out = xv_err("#VALUE!"); goto done; }
        char delim[128];
        xlsx_to_text(ARG_AT(0, 0), delim, sizeof delim);
        double ig = 0;
        xlsx_as_num(ARG_AT(1, 0), &ig);
        size_t len = 0;
        char *buf = malloc(1); buf[0] = '\0';
        int first = 1;
        for (size_t k = 2; k < argc; k++) {
            for (size_t i = 0; i < ARG_LEN(k); i++) {
                XlsxVal v = ARG_AT(k, i);
                if (ig != 0 && v.kind == XV_EMPTY) continue;
                char piece[256];
                xlsx_to_text(v, piece, sizeof piece);
                if (ig != 0 && !*piece) continue;
                size_t dl = first ? 0 : strlen(delim), pl = strlen(piece);
                char *g = realloc(buf, len + dl + pl + 1);
                if (!g) abort();
                buf = g;
                if (dl) { memcpy(buf + len, delim, dl); len += dl; }
                memcpy(buf + len, piece, pl + 1);
                len += pl;
                first = 0;
            }
        }
        out = xv_str(buf);
        free(buf);
    } else if (strcmp(name, "VLOOKUP") == 0 || strcmp(name, "HLOOKUP") == 0) {
        /* VLOOKUP(value, table, index, [range_lookup]) -- the workhorse, and
         * the dominant consumer of cross-sheet ranges in the corpus.
         *
         * THE DEFAULT IS THE DANGEROUS PART. Omitting the 4th argument means
         * APPROXIMATE match, not exact: the table is assumed sorted ascending
         * and the largest value not greater than the lookup wins. Treating a
         * missing argument as exact would turn a legitimate approximate lookup
         * into #N/A, and treating an exact request as approximate would return
         * a confidently wrong neighbouring row. Both directions are silent, so
         * both are implemented rather than one being assumed. */
        int vert = strcmp(name, "VLOOKUP") == 0;
        double idx = 0, approx = 1;
        if (argc < 3 || !xlsx_as_num(ARG_AT(2, 0), &idx)) { out = xv_err("#VALUE!"); goto done; }
        if (argc >= 4) {
            double a = 0;
            if (xlsx_as_num(ARG_AT(3, 0), &a)) approx = (a != 0);
        }
        if (!argrange[1].is_range) { out = xv_err("#REF!"); goto done; }
        long rows = argrows[1], cols = argcols[1];
        long span = vert ? rows : cols;
        if (idx < 1 || (vert ? idx > cols : idx > rows)) { out = xv_err("#REF!"); goto done; }
        char want[256];
        xlsx_to_text(ARG_AT(0, 0), want, sizeof want);
        double wnum = 0;
        int want_num = ARG_AT(0, 0).kind == XV_NUM || ARG_AT(0, 0).kind == XV_BOOL;
        if (want_num) wnum = ARG_AT(0, 0).num;
        long hit = -1;
        for (long i = 0; i < span; i++) {
            XlsxVal key = vert ? RNG_AT(1, i, 0) : RNG_AT(1, 0, i);
            int num_key = key.kind == XV_NUM || key.kind == XV_BOOL;
            if (want_num && num_key) {
                if (approx) { if (key.num <= wnum) hit = i; else { xv_free(key); break; } }
                else if (key.num == wnum) { hit = i; xv_free(key); break; }
            } else if (!want_num && key.kind != XV_EMPTY) {
                char have[256];
                xlsx_to_text(key, have, sizeof have);
                int cmp = strcasecmp(have, want);
                if (approx) { if (cmp <= 0) hit = i; else { xv_free(key); break; } }
                else if (cmp == 0) { hit = i; xv_free(key); break; }
            }
            xv_free(key);
        }
        if (hit < 0) { out = xv_err("#N/A"); goto done; }
        out = vert ? RNG_AT(1, hit, (long)idx - 1) : RNG_AT(1, (long)idx - 1, hit);
    } else if (strcmp(name, "INDEX") == 0) {
        /* INDEX(range, row, [col]). A one-dimensional range takes a single
         * ordinal, which is how INDEX/MATCH pairs are usually written. */
        long rows = argrows[0], cols = argcols[0];
        double rr = 0, cc = 0;
        if (argc < 2 || !argrange[0].is_range) { out = xv_err("#REF!"); goto done; }
        xlsx_as_num(ARG_AT(1, 0), &rr);
        if (argc >= 3) xlsx_as_num(ARG_AT(2, 0), &cc);
        long r0, c0;
        if (argc >= 3) { r0 = (long)rr - 1; c0 = (long)cc - 1; }
        else if (rows == 1) { r0 = 0; c0 = (long)rr - 1; }
        else { r0 = (long)rr - 1; c0 = 0; }
        if (r0 < 0 || c0 < 0 || r0 >= rows || c0 >= cols) { out = xv_err("#REF!"); goto done; }
        out = RNG_AT(0, r0, c0);
    } else if (strcmp(name, "MATCH") == 0) {
        /* MATCH(value, range, [type]). Type 1 (the DEFAULT) is "largest value
         * <= lookup, assuming ascending order"; 0 is exact; -1 is descending.
         * Same trap as VLOOKUP: the default is not exact. */
        double type = 1;
        if (argc < 2 || !argrange[1].is_range) { out = xv_err("#N/A"); goto done; }
        if (argc >= 3) xlsx_as_num(ARG_AT(2, 0), &type);
        char want[256];
        xlsx_to_text(ARG_AT(0, 0), want, sizeof want);
        double wnum = 0;
        int want_num = ARG_AT(0, 0).kind == XV_NUM || ARG_AT(0, 0).kind == XV_BOOL;
        if (want_num) wnum = ARG_AT(0, 0).num;
        long span = argrows[1] * argcols[1];
        long hit = -1;
        for (long i = 0; i < span; i++) {
            long rr2 = argcols[1] == 1 ? i : 0, cc2 = argcols[1] == 1 ? 0 : i;
            if (argrows[1] > 1 && argcols[1] > 1) { rr2 = i / argcols[1]; cc2 = i % argcols[1]; }
            XlsxVal key = RNG_AT(1, rr2, cc2);
            int num_key = key.kind == XV_NUM || key.kind == XV_BOOL;
            if (type == 0) {
                if (want_num && num_key) { if (key.num == wnum) { hit = i; xv_free(key); break; } }
                else if (!want_num && key.kind != XV_EMPTY) {
                    char have[256];
                    xlsx_to_text(key, have, sizeof have);
                    if (strcasecmp(have, want) == 0) { hit = i; xv_free(key); break; }
                }
            } else if (type > 0) {
                if (want_num && num_key) { if (key.num <= wnum) hit = i; else { xv_free(key); break; } }
            } else {
                if (want_num && num_key) { if (key.num >= wnum) hit = i; else { xv_free(key); break; } }
            }
            xv_free(key);
        }
        out = hit < 0 ? xv_err("#N/A") : xv_num((double)hit + 1);
    } else if (strcmp(name, "XLOOKUP") == 0 || strcmp(name, "XMATCH") == 0) {
        /* XLOOKUP(lookup, lookup_array, return_array, [if_not_found])
         * XMATCH(lookup, lookup_array)
         *
         * Only EXACT match is implemented -- which is XLOOKUP's default, unlike
         * VLOOKUP's. The approximate and binary-search modes take a match_mode
         * argument; rather than ignore it and return a plausible neighbour,
         * anything other than exact is refused by name below. */
        int is_match = strcmp(name, "XMATCH") == 0;
        if (argc < 2) { out = xv_err("#VALUE!"); goto done; }
        double mode = 0;
        size_t mode_arg = is_match ? 2 : 3;
        int has_mode = 0;
        if (!is_match && argc >= 5) { xlsx_as_num(ARG_AT(4, 0), &mode); has_mode = 1; }
        if (is_match && argc >= 3) { xlsx_as_num(ARG_AT(mode_arg, 0), &mode); has_mode = 1; }
        if (has_mode && mode != 0) {
            ev->unsupported = 1;
            snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                     "%s (match_mode %g)", name, mode);
            out = xv_err("#NAME?");
            goto done;
        }
        char want[256];
        xlsx_to_text(ARG_AT(0, 0), want, sizeof want);
        size_t ln = ARG_LEN(1);
        long hit = -1;
        for (size_t i = 0; i < ln; i++) {
            char have[256];
            xlsx_to_text(ARG_AT(1, i), have, sizeof have);
            if (strcasecmp(want, have) == 0) { hit = (long)i; break; }
        }
        if (hit < 0) {
            /* XLOOKUP's 4th argument is the not-found value; without one both
             * functions give #N/A. */
            if (!is_match && argc >= 4) { out = ARG_AT(3, 0); ARG_AT(3, 0) = xv_empty(); }
            else out = xv_err("#N/A");
        } else if (is_match) {
            out = xv_num((double)hit + 1);        /* 1-based, like MATCH */
        } else if (argc >= 3 && (size_t)hit < ARG_LEN(2)) {
            out = ARG_AT(2, (size_t)hit); ARG_AT(2, (size_t)hit) = xv_empty();
        } else {
            out = xv_err("#REF!");
        }
    } else if (strcmp(name, "SUBTOTAL") == 0) {
        /* SUBTOTAL(function_num, ref1, ...). Rank 2 in the corpus build order
         * (+316 fully-recalculable workbooks, docs/xlsx_design.md §13.I).
         *
         * Measured in the corpus before implementing: 399 workbooks use it,
         * and the function_num histogram is 9 (SUM) 34,571 uses, 3 (COUNTA)
         * 3,651, 1 (AVERAGE) 402, 5 (MIN) 200, 4 (MAX) 100 -- and NOTHING in
         * the 101-111 range, which postdates this 2001 corpus. Both families
         * are implemented anyway; 101-111 costs one comparison once the row's
         * hidden flag is carried on the cell.
         *
         * THE ONE GENUINE AMBIGUITY, stated rather than papered over: 1-11
         * include manually hidden rows but exclude rows hidden by an active
         * FILTER, and the file format records both as hidden="1". We include
         * them, which is right for manual hiding and wrong under a live
         * filter. At most 47 corpus workbooks have both an autoFilter and a
         * hidden row anywhere, so the exposure is small and measurable -- and
         * because these workbooks carry Excel's own cached values, xlsx.check
         * over the corpus reports how often it actually bites, rather than
         * leaving it a matter of opinion. */
        double fn = 0;
        if (n < 1 || !xlsx_as_num(args[0], &fn)) {
            out = xv_err("#VALUE!");
        } else {
            int code = (int)fn;
            int skip_hidden = code > 100;
            int op = skip_hidden ? code - 100 : code;
            if (op < 1 || op > 11) {
                out = xv_err("#VALUE!");
            } else {
                double total = 0, best = 0, prod = 1;
                long cnt = 0, cnt_all = 0;
                int seen = 0;
                /* Two accumulators for the variance/stdev family, which needs
                 * the mean before it can sum squared deviations; a second pass
                 * over the kept values is cheaper than storing them. */
                double sum_for_mean = 0; long n_for_mean = 0;
                for (size_t i = 1; i < n; i++) {
                    const XlsxSnapCell *sc = srcs ? srcs[i] : NULL;
                    if (skip_hidden && sc && sc->hidden) continue;
                    if (xlsx_cell_is_subtotal(sc)) continue;
                    if (args[i].kind != XV_EMPTY) cnt_all++;      /* COUNTA */
                    if (args[i].kind != XV_NUM && args[i].kind != XV_BOOL) continue;
                    double x = args[i].num;
                    total += x; cnt++;
                    prod *= x;
                    sum_for_mean += x; n_for_mean++;
                    if (!seen || (op == 5 ? x < best : x > best)) { best = x; seen = 1; }
                }
                switch (op) {
                case 1:  out = cnt ? xv_num(total / (double)cnt) : xv_err("#DIV/0!"); break;
                case 2:  out = xv_num((double)cnt); break;          /* COUNT   */
                case 3:  out = xv_num((double)cnt_all); break;      /* COUNTA  */
                case 4:  out = xv_num(seen ? best : 0); break;      /* MAX     */
                case 5:  out = xv_num(seen ? best : 0); break;      /* MIN     */
                case 6:  out = xv_num(cnt ? prod : 0); break;       /* PRODUCT */
                case 9:  out = xv_num(total); break;                /* SUM     */
                case 7: case 8: case 10: case 11: {
                    /* STDEV(7)/STDEVP(8)/VAR(10)/VARP(11). Sample forms divide
                     * by n-1 and need at least two values. */
                    int sample = (op == 7 || op == 10);
                    if (n_for_mean < (sample ? 2 : 1)) { out = xv_err("#DIV/0!"); break; }
                    double mean = sum_for_mean / (double)n_for_mean;
                    double ss = 0;
                    for (size_t i = 1; i < n; i++) {
                        const XlsxSnapCell *sc = srcs ? srcs[i] : NULL;
                        if (skip_hidden && sc && sc->hidden) continue;
                        if (xlsx_cell_is_subtotal(sc)) continue;
                        if (args[i].kind != XV_NUM && args[i].kind != XV_BOOL) continue;
                        double d = args[i].num - mean;
                        ss += d * d;
                    }
                    double denom = sample ? (double)(n_for_mean - 1) : (double)n_for_mean;
                    double var = ss / denom;
                    out = (op == 7 || op == 8) ? xv_num(sqrt(var)) : xv_num(var);
                    break;
                }
                default: out = xv_err("#VALUE!"); break;
                }
            }
        }
    } else if (strcmp(name, "MEDIAN") == 0 || strcmp(name, "PERCENTILE") == 0 ||
               strcmp(name, "PERCENTILE.INC") == 0 || strcmp(name, "QUARTILE") == 0 ||
               strcmp(name, "QUARTILE.INC") == 0 || strcmp(name, "SMALL") == 0 ||
               strcmp(name, "LARGE") == 0) {
        /* Order statistics. The 2010 renames (PERCENTILE -> PERCENTILE.INC and
         * kin) are the SAME function under a new name -- Excel split each into
         * .INC/.EXC and kept the old spelling as the inclusive one -- so they
         * share a body rather than being reimplemented. */
        size_t rn = ARG_LEN(0);
        double *v = malloc((rn ? rn : 1) * sizeof *v);
        if (!v) abort();
        size_t m = 0;
        for (size_t i = 0; i < rn; i++)
            if (ARG_AT(0, i).kind == XV_NUM || ARG_AT(0, i).kind == XV_BOOL)
                v[m++] = ARG_AT(0, i).num;
        if (!m) { free(v); out = xv_err("#NUM!"); goto done; }
        for (size_t i = 1; i < m; i++) {           /* insertion sort: m is small */
            double x = v[i]; size_t j = i;
            while (j && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
            v[j] = x;
        }
        double p = 0.5;
        int ok = 1;
        if (strcmp(name, "MEDIAN") == 0) p = 0.5;
        else if (strncmp(name, "QUARTILE", 8) == 0) {
            double q = 0;
            if (argc < 2 || !xlsx_as_num(ARG_AT(1, 0), &q) || q < 0 || q > 4) ok = 0;
            p = q / 4.0;
        } else if (strcmp(name, "SMALL") == 0 || strcmp(name, "LARGE") == 0) {
            double k = 0;
            if (argc < 2 || !xlsx_as_num(ARG_AT(1, 0), &k) || k < 1 || k > (double)m) ok = 0;
            else {
                size_t idx = strcmp(name, "SMALL") == 0 ? (size_t)k - 1 : m - (size_t)k;
                out = xv_num(v[idx]);
                free(v);
                goto done;
            }
        } else {
            if (argc < 2 || !xlsx_as_num(ARG_AT(1, 0), &p) || p < 0 || p > 1) ok = 0;
        }
        if (!ok) { free(v); out = xv_err("#NUM!"); goto done; }
        /* Inclusive percentile: linear interpolation between order statistics. */
        double pos = p * (double)(m - 1);
        size_t lo = (size_t)floor(pos);
        double frac = pos - (double)lo;
        double res = (lo + 1 < m) ? v[lo] + frac * (v[lo + 1] - v[lo]) : v[m - 1];
        free(v);
        out = xv_num(res);
    } else if (strcmp(name, "STDEV") == 0 || strcmp(name, "STDEV.S") == 0 ||
               strcmp(name, "STDEVP") == 0 || strcmp(name, "STDEV.P") == 0 ||
               strcmp(name, "VAR") == 0 || strcmp(name, "VAR.S") == 0 ||
               strcmp(name, "VARP") == 0 || strcmp(name, "VAR.P") == 0) {
        /* Sample forms divide by n-1, population forms by n. STDEV.S/STDEV.P
         * are the 2010 renames of STDEV/STDEVP. */
        int pop = strcmp(name, "STDEVP") == 0 || strcmp(name, "STDEV.P") == 0 ||
                  strcmp(name, "VARP") == 0 || strcmp(name, "VAR.P") == 0;
        int want_sd = strncmp(name, "STDEV", 5) == 0;
        double sum = 0; long cnt = 0;
        for (size_t i = 0; i < n; i++)
            if (args[i].kind == XV_NUM || args[i].kind == XV_BOOL) { sum += args[i].num; cnt++; }
        if (cnt < (pop ? 1 : 2)) { out = xv_err("#DIV/0!"); goto done; }
        double mean = sum / (double)cnt, ss = 0;
        for (size_t i = 0; i < n; i++)
            if (args[i].kind == XV_NUM || args[i].kind == XV_BOOL) {
                double d0 = args[i].num - mean; ss += d0 * d0;
            }
        double var = ss / (double)(pop ? cnt : cnt - 1);
        out = xv_num(want_sd ? sqrt(var) : var);
    } else if (strcmp(name, "RANK") == 0 || strcmp(name, "RANK.EQ") == 0) {
        /* RANK.EQ is the 2010 rename of RANK. Order 0 (default) ranks
         * descending, anything else ascending; ties share the better rank. */
        double x = 0, order = 0;
        if (argc < 2 || !xlsx_as_num(ARG_AT(0, 0), &x)) { out = xv_err("#VALUE!"); goto done; }
        if (argc >= 3) xlsx_as_num(ARG_AT(2, 0), &order);
        long better = 0, found = 0;
        for (size_t i = 0; i < ARG_LEN(1); i++) {
            XlsxVal c = ARG_AT(1, i);
            if (c.kind != XV_NUM && c.kind != XV_BOOL) continue;
            if (c.num == x) found = 1;
            if (order == 0 ? c.num > x : c.num < x) better++;
        }
        out = found ? xv_num((double)better + 1) : xv_err("#N/A");
    } else if (strcmp(name, "AGGREGATE") == 0) {
        /* AGGREGATE(fn, options, range...) -- SUBTOTAL's successor, adding
         * ERROR SKIPPING via the options argument, which is why it is in the
         * error-catching list above. Options 2/3/6/7 ignore errors; the
         * reference forms (fn 14-19) take an extra k argument and are refused
         * by name rather than approximated. */
        double fn = 0, opt = 0;
        if (argc < 3 || !xlsx_as_num(ARG_AT(0, 0), &fn) || !xlsx_as_num(ARG_AT(1, 0), &opt)) {
            out = xv_err("#VALUE!"); goto done;
        }
        int op = (int)fn;
        if (op > 13) {
            ev->unsupported = 1;
            snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                     "AGGREGATE (function %d)", op);
            out = xv_err("#NAME?");
            goto done;
        }
        int o = (int)opt;
        int skip_err = (o == 2 || o == 3 || o == 6 || o == 7);
        double total = 0, best = 0, prod = 1;
        long cnt = 0, cnt_all = 0; int seen = 0;
        for (size_t k = 2; k < argc; k++) {
            for (size_t i = 0; i < ARG_LEN(k); i++) {
                XlsxVal a = ARG_AT(k, i);
                if (a.kind == XV_ERR) {
                    if (skip_err) continue;
                    out = xv_err(a.str); goto done;
                }
                if (a.kind != XV_EMPTY) cnt_all++;
                if (a.kind != XV_NUM && a.kind != XV_BOOL) continue;
                total += a.num; cnt++; prod *= a.num;
                if (!seen || (op == 5 ? a.num < best : a.num > best)) { best = a.num; seen = 1; }
            }
        }
        switch (op) {
        case 1:  out = cnt ? xv_num(total / (double)cnt) : xv_err("#DIV/0!"); break;
        case 2:  out = xv_num((double)cnt); break;
        case 3:  out = xv_num((double)cnt_all); break;
        case 4:  case 5: out = xv_num(seen ? best : 0); break;
        case 6:  out = xv_num(cnt ? prod : 0); break;
        case 9:  out = xv_num(total); break;
        default:
            ev->unsupported = 1;
            snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                     "AGGREGATE (function %d)", op);
            out = xv_err("#NAME?");
            break;
        }
    } else if (strcmp(name, "TEXTBEFORE") == 0 || strcmp(name, "TEXTAFTER") == 0) {
        char hay[512], nee[256];
        if (argc < 2) { out = xv_err("#VALUE!"); goto done; }
        xlsx_to_text(ARG_AT(0, 0), hay, sizeof hay);
        xlsx_to_text(ARG_AT(1, 0), nee, sizeof nee);
        const char *at = *nee ? strstr(hay, nee) : NULL;
        if (!at) { out = xv_err("#N/A"); goto done; }
        if (strcmp(name, "TEXTBEFORE") == 0) {
            size_t len = (size_t)(at - hay);
            char *b = malloc(len + 1);
            memcpy(b, hay, len); b[len] = '\0';
            out = xv_str(b); free(b);
        } else {
            out = xv_str(at + strlen(nee));
        }
    } else if (strcmp(name, "ISOWEEKNUM") == 0) {
        /* ISO 8601: week 1 is the one containing the first Thursday, and weeks
         * start on Monday. Computed by shifting to that week's Thursday, which
         * avoids the year-boundary special cases entirely. */
        double s = 0;
        if (n < 1 || !xlsx_as_serial(args[0], &s)) { out = xv_err("#VALUE!"); goto done; }
        long ser = (long)floor(s);
        long dow_mon = ((ser - 1) % 7 + 7) % 7;      /* 0=Sun */
        long iso_dow = dow_mon == 0 ? 7 : dow_mon;   /* 1=Mon .. 7=Sun */
        long thursday = ser - iso_dow + 4;
        long y, m, d;
        if (!xlsx_civil_from_serial((double)thursday, &y, &m, &d)) { out = xv_err("#NUM!"); goto done; }
        double jan1 = xlsx_serial_from_civil(y, 1, 1);
        out = xv_num(floor(((double)thursday - jan1) / 7.0) + 1);
    } else if (strcmp(name, "NETWORKDAYS") == 0) {
        /* Whole weekdays between two dates, inclusive, less any listed
         * holidays that fall on a weekday inside the range. */
        double a = 0, b = 0;
        if (argc < 2 || !xlsx_as_serial(ARG_AT(0, 0), &a) || !xlsx_as_serial(ARG_AT(1, 0), &b)) {
            out = xv_err("#VALUE!"); goto done;
        }
        long s0 = (long)floor(a), s1 = (long)floor(b), sign = 1;
        if (s0 > s1) { long t = s0; s0 = s1; s1 = t; sign = -1; }
        long days = 0;
        for (long k = s0; k <= s1; k++) {
            long dow = ((k - 1) % 7 + 7) % 7;        /* 0=Sun, 6=Sat */
            if (dow == 0 || dow == 6) continue;
            int holiday = 0;
            for (size_t h = 2; h < argc && !holiday; h++)
                for (size_t i = 0; i < ARG_LEN(h); i++) {
                    double hv = 0;
                    if (xlsx_as_serial(ARG_AT(h, i), &hv) && (long)floor(hv) == k) { holiday = 1; break; }
                }
            if (!holiday) days++;
        }
        out = xv_num((double)(days * sign));
    } else if (strcmp(name, "YEARFRAC") == 0) {
        /* Day-count conventions. Basis 0 (US 30/360) is the default and the one
         * amortization schedules use; 2 and 3 are simple actual/360 and
         * actual/365; 4 is the European 30/360.
         *
         * Basis 1 (actual/actual) is REFUSED rather than approximated: its
         * denominator rule is intricate and getting it subtly wrong produces a
         * plausible interest figure, which is precisely the failure this module
         * refuses to make. */
        double a = 0, b = 0, basis = 0;
        if (argc < 2 || !xlsx_as_serial(ARG_AT(0, 0), &a) || !xlsx_as_serial(ARG_AT(1, 0), &b)) {
            out = xv_err("#VALUE!"); goto done;
        }
        if (argc >= 3) xlsx_as_num(ARG_AT(2, 0), &basis);
        long s0 = (long)floor(a), s1 = (long)floor(b);
        if (s0 > s1) { long t = s0; s0 = s1; s1 = t; }
        long y1, m1, d1, y2, m2, d2;
        if (!xlsx_civil_from_serial((double)s0, &y1, &m1, &d1) ||
            !xlsx_civil_from_serial((double)s1, &y2, &m2, &d2)) { out = xv_err("#NUM!"); goto done; }
        int ib = (int)basis;
        if (ib == 0 || ib == 4) {
            if (ib == 0) {
                if (d1 == 31) d1 = 30;
                if (d2 == 31 && d1 >= 30) d2 = 30;
            } else {
                if (d1 == 31) d1 = 30;
                if (d2 == 31) d2 = 30;
            }
            double days = (double)((y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1));
            out = xv_num(days / 360.0);
        } else if (ib == 2 || ib == 3) {
            out = xv_num((double)(s1 - s0) / (ib == 2 ? 360.0 : 365.0));
        } else {
            ev->unsupported = 1;
            snprintf(ev->unsupported_name, sizeof ev->unsupported_name,
                     "YEARFRAC (basis %d)", ib);
            out = xv_err("#NAME?");
        }
    } else if (strcmp(name, "EOMONTH") == 0 || strcmp(name, "EDATE") == 0) {
        /* EOMONTH(start, months) / EDATE(start, months). Day-count work is what
         * amortization schedules are made of, so these matter more for CECL
         * than their corpus frequency suggests (§13.G).
         *
         * Both are month arithmetic on a SERIAL, which means converting back to
         * a civil date, shifting, and converting forward -- adding 30*months
         * would drift, and drift in a payment schedule is silent. */
        double s = 0, mo = 0;
        if (argc < 2 || !xlsx_as_serial(ARG_AT(0, 0), &s) || !xlsx_as_num(ARG_AT(1, 0), &mo)) {
            out = xv_err("#VALUE!"); goto done;
        }
        long y, m, d;
        if (!xlsx_civil_from_serial(floor(s), &y, &m, &d)) { out = xv_err("#NUM!"); goto done; }
        long total = (y * 12 + (m - 1)) + (long)mo;
        long ny = total / 12, nm = total % 12;
        if (nm < 0) { nm += 12; ny -= 1; }
        nm += 1;
        long last = xlsx_days_in_month(ny, (unsigned)nm);
        long nd = strcmp(name, "EOMONTH") == 0 ? last : (d < last ? d : last);
        double ser = xlsx_serial_from_civil(ny, (unsigned)nm, (unsigned)nd);
        out = ser < 0 ? xv_err("#NUM!") : xv_num(ser);
    } else if (strcmp(name, "DAYS") == 0) {
        double a = 0, b = 0;
        if (argc >= 2 && xlsx_as_serial(ARG_AT(0, 0), &a) && xlsx_as_serial(ARG_AT(1, 0), &b))
            out = xv_num(floor(a) - floor(b));
        else out = xv_err("#VALUE!");
    } else if (strcmp(name, "YEAR") == 0 || strcmp(name, "MONTH") == 0 ||
               strcmp(name, "DAY") == 0) {
        double s = 0;
        long y, m, d;
        if (n >= 1 && xlsx_as_serial(args[0], &s) && xlsx_civil_from_serial(floor(s), &y, &m, &d)) {
            out = xv_num(strcmp(name, "YEAR") == 0 ? (double)y
                       : strcmp(name, "MONTH") == 0 ? (double)m : (double)d);
        } else out = xv_err("#VALUE!");
    } else if (strcmp(name, "DATE") == 0) {
        double y = 0, m = 0, d = 0;
        if (n >= 3 && xlsx_as_num(args[0], &y) && xlsx_as_num(args[1], &m) &&
            xlsx_as_num(args[2], &d)) {
            /* Excel normalises out-of-range months and days, which is how
             * DATE(y, m+1, 0) is used to mean "last day of month m". */
            long total = ((long)y * 12) + ((long)m - 1);
            long ny = total / 12, nm = total % 12;
            if (nm < 0) { nm += 12; ny -= 1; }
            double ser = xlsx_serial_from_civil(ny, (unsigned)nm + 1, 1);
            out = ser < 0 ? xv_err("#NUM!") : xv_num(ser + ((long)d - 1));
        } else out = xv_err("#VALUE!");
    } else if (strcmp(name, "WEEKDAY") == 0) {
        /* Serial 1 (1900-01-01) was a Sunday, so type 1 (Sun=1) is
         * ((serial - 1) mod 7) + 1 -- and the 1900 leap-year bug does not
         * disturb it, because the phantom day shifts every later serial by
         * exactly one whole day. */
        double s = 0, type = 1;
        if (n < 1 || !xlsx_as_serial(args[0], &s)) { out = xv_err("#VALUE!"); goto done; }
        if (n >= 2) xlsx_as_num(args[1], &type);
        long ser = (long)floor(s);
        long dow = ((ser - 1) % 7 + 7) % 7;         /* 0 = Sunday */
        switch ((int)type) {
        case 1:  out = xv_num((double)(dow + 1)); break;            /* Sun=1 */
        case 2:  out = xv_num((double)((dow + 6) % 7 + 1)); break;  /* Mon=1 */
        case 3:  out = xv_num((double)((dow + 6) % 7)); break;      /* Mon=0 */
        default: out = xv_err("#NUM!"); break;
        }
    } else if (strcmp(name, "NOW") == 0) {
        /* VOLATILE. Measured on the Enron corpus this is the single largest
         * coverage win available -- present in 16.3% of formula-bearing
         * workbooks, and 1,099 of them become fully recalculable the moment it
         * exists (docs/xlsx_design.md §13.I). It is also nearly free.
         *
         * The catch, which is real and permanent: implementing it makes those
         * workbooks recalculable while making them UNVERIFIABLE against their
         * cached values, since the cache dates from whenever Excel last
         * calculated. xlsx.check must keep skipping it -- xlsx_is_volatile
         * above is what enforces that, and it already listed NOW before NOW
         * could be evaluated at all. */
        out = xv_num(xlsx_now_serial());
    } else if (strcmp(name, "TODAY") == 0) {
        /* The date with the time fraction discarded. floor, not truncation:
         * they differ for pre-epoch serials, and floor is what Excel's INT
         * does. */
        out = xv_num(floor(xlsx_now_serial()));
    } else {
        /* REFUSE LOUDLY. In a financial model a plausible wrong number is worse
         * than a failure, so an unimplemented function is reported by name
         * rather than defaulted to zero (§13.G). */
        ev->unsupported = 1;
        snprintf(ev->unsupported_name, sizeof ev->unsupported_name, "%s", name);
        out = xv_err("#NAME?");
    }

done:
    for (size_t i = 0; i < n; i++) xv_free(args[i]);
    free(args);
    free(srcs);   /* borrowed pointers into the snapshot; only the array is ours */
    free(argoff);
    free(argrows);
    free(argcols);
    free(argrange);
    #undef RNG_AT
    #undef ARG_RC
    #undef ARG_LEN
    #undef ARG_AT
    return out;
}

static XlsxVal xlsx_primary(XlsxEval *ev) {
    if (ev->depth++ > 64) return xv_err("#VALUE!");
    XlsxVal v = xv_err("#VALUE!");
    switch (ev->lx.cur.kind) {
    case XT_NUM: v = xv_num(ev->lx.cur.num); xlsx_lex_next(&ev->lx); break;
    case XT_STR: v = xv_str(ev->lx.cur.text); xlsx_lex_next(&ev->lx); break;
    case XT_ERRLIT: v = xv_err(ev->lx.cur.text); xlsx_lex_next(&ev->lx); break;
    case XT_REF: {
        char ref[128], sheet[128];
        int ext = ev->lx.cur.external;
        snprintf(ref, sizeof ref, "%s", ev->lx.cur.text);
        snprintf(sheet, sizeof sheet, "%s", ev->lx.cur.sheet);
        v = xlsx_cell_value_in(ev, sheet, ext, ref);
        xlsx_lex_next(&ev->lx);
        break;
    }
    case XT_NAME: {
        char nm[128];
        size_t i = 0;
        for (; ev->lx.cur.text[i] && i < sizeof nm - 1; i++) nm[i] = (char)toupper((unsigned char)ev->lx.cur.text[i]);
        nm[i] = '\0';
        if (strcmp(nm, "TRUE") == 0) { v = xv_bool(1); xlsx_lex_next(&ev->lx); break; }
        if (strcmp(nm, "FALSE") == 0) { v = xv_bool(0); xlsx_lex_next(&ev->lx); break; }
        v = xlsx_call(ev, nm);
        break;
    }
    case XT_LPAREN:
        xlsx_lex_next(&ev->lx);
        v = xlsx_expr(ev);
        if (ev->lx.cur.kind == XT_RPAREN) xlsx_lex_next(&ev->lx);
        break;
    case XT_OP:
        if (strcmp(ev->lx.cur.text, "-") == 0) {
            xlsx_lex_next(&ev->lx);
            XlsxVal inner = xlsx_primary(ev);
            double d = 0;
            v = xlsx_as_num(inner, &d) ? xv_num(-d) : xv_err("#VALUE!");
            xv_free(inner);
        } else if (strcmp(ev->lx.cur.text, "+") == 0) {
            xlsx_lex_next(&ev->lx);
            v = xlsx_primary(ev);
        }
        break;
    default: break;
    }
    /* Postfix percent binds tighter than any infix operator. */
    while (ev->lx.cur.kind == XT_OP && strcmp(ev->lx.cur.text, "%") == 0) {
        double d = 0;
        if (xlsx_as_num(v, &d)) { xv_free(v); v = xv_num(d / 100.0); }
        xlsx_lex_next(&ev->lx);
    }
    ev->depth--;
    return v;
}

static XlsxVal xlsx_power(XlsxEval *ev) {
    XlsxVal a = xlsx_primary(ev);
    while (ev->lx.cur.kind == XT_OP && strcmp(ev->lx.cur.text, "^") == 0) {
        xlsx_lex_next(&ev->lx);
        XlsxVal b = xlsx_primary(ev);
        double x = 0, y = 0;
        XlsxVal r = (a.kind == XV_ERR) ? xv_err(a.str) : (b.kind == XV_ERR) ? xv_err(b.str)
                  : (xlsx_as_num(a, &x) && xlsx_as_num(b, &y)) ? xv_num(pow(x, y)) : xv_err("#VALUE!");
        xv_free(a); xv_free(b);
        a = r;
    }
    return a;
}

static XlsxVal xlsx_term(XlsxEval *ev) {
    XlsxVal a = xlsx_power(ev);
    while (ev->lx.cur.kind == XT_OP &&
           (strcmp(ev->lx.cur.text, "*") == 0 || strcmp(ev->lx.cur.text, "/") == 0)) {
        int div = ev->lx.cur.text[0] == '/';
        xlsx_lex_next(&ev->lx);
        XlsxVal b = xlsx_power(ev);
        double x = 0, y = 0;
        XlsxVal r;
        if (a.kind == XV_ERR) r = xv_err(a.str);
        else if (b.kind == XV_ERR) r = xv_err(b.str);
        else if (!xlsx_as_num(a, &x) || !xlsx_as_num(b, &y)) r = xv_err("#VALUE!");
        else if (div && y == 0) r = xv_err("#DIV/0!");
        else r = xv_num(div ? x / y : x * y);
        xv_free(a); xv_free(b);
        a = r;
    }
    return a;
}

static XlsxVal xlsx_arith(XlsxEval *ev) {
    XlsxVal a = xlsx_term(ev);
    while (ev->lx.cur.kind == XT_OP &&
           (strcmp(ev->lx.cur.text, "+") == 0 || strcmp(ev->lx.cur.text, "-") == 0)) {
        int sub = ev->lx.cur.text[0] == '-';
        xlsx_lex_next(&ev->lx);
        XlsxVal b = xlsx_term(ev);
        double x = 0, y = 0;
        XlsxVal r;
        if (a.kind == XV_ERR) r = xv_err(a.str);
        else if (b.kind == XV_ERR) r = xv_err(b.str);
        else if (!xlsx_as_num(a, &x) || !xlsx_as_num(b, &y)) r = xv_err("#VALUE!");
        else r = xv_num(sub ? x - y : x + y);
        xv_free(a); xv_free(b);
        a = r;
    }
    return a;
}

static XlsxVal xlsx_concat(XlsxEval *ev) {
    XlsxVal a = xlsx_arith(ev);
    while (ev->lx.cur.kind == XT_OP && strcmp(ev->lx.cur.text, "&") == 0) {
        xlsx_lex_next(&ev->lx);
        XlsxVal b = xlsx_arith(ev);
        XlsxVal r;
        if (a.kind == XV_ERR) r = xv_err(a.str);
        else if (b.kind == XV_ERR) r = xv_err(b.str);
        else {
            char sa[256], sb[256], joined[512];
            xlsx_to_text(a, sa, sizeof sa);
            xlsx_to_text(b, sb, sizeof sb);
            snprintf(joined, sizeof joined, "%s%s", sa, sb);
            r = xv_str(joined);
        }
        xv_free(a); xv_free(b);
        a = r;
    }
    return a;
}

static XlsxVal xlsx_expr(XlsxEval *ev) {
    XlsxVal a = xlsx_concat(ev);
    while (ev->lx.cur.kind == XT_OP &&
           (strcmp(ev->lx.cur.text, "=") == 0 || strcmp(ev->lx.cur.text, "<>") == 0 ||
            strcmp(ev->lx.cur.text, "<") == 0 || strcmp(ev->lx.cur.text, ">") == 0 ||
            strcmp(ev->lx.cur.text, "<=") == 0 || strcmp(ev->lx.cur.text, ">=") == 0)) {
        char op[4];
        snprintf(op, sizeof op, "%s", ev->lx.cur.text);
        xlsx_lex_next(&ev->lx);
        XlsxVal b = xlsx_concat(ev);
        XlsxVal r;
        if (a.kind == XV_ERR) r = xv_err(a.str);
        else if (b.kind == XV_ERR) r = xv_err(b.str);
        else {
            int cmp = 0;
            double x = 0, y = 0;
            if (a.kind == XV_STR || b.kind == XV_STR) {
                char sa[256], sb[256];
                xlsx_to_text(a, sa, sizeof sa);
                xlsx_to_text(b, sb, sizeof sb);
                cmp = strcmp(sa, sb);
            } else {
                xlsx_as_num(a, &x); xlsx_as_num(b, &y);
                cmp = (x < y) ? -1 : (x > y) ? 1 : 0;
            }
            int res = 0;
            if (!strcmp(op, "=")) res = cmp == 0;
            else if (!strcmp(op, "<>")) res = cmp != 0;
            else if (!strcmp(op, "<")) res = cmp < 0;
            else if (!strcmp(op, ">")) res = cmp > 0;
            else if (!strcmp(op, "<=")) res = cmp <= 0;
            else if (!strcmp(op, ">=")) res = cmp >= 0;
            r = xv_bool(res);
        }
        xv_free(a); xv_free(b);
        a = r;
    }
    return a;
}

/* Evaluate one formula's TEXT (without the leading '='). */
static XlsxVal xlsx_eval_formula_in(XlsxWorkbook *wb, const XlsxSnap *snap,
                                 const char *sheet_name, XlsxSheetPool *pool,
                                 const char *formula, int *unsupported,
                                 char *unsupported_name, size_t un_len) {
    XlsxEval ev;
    memset(&ev, 0, sizeof ev);
    ev.wb = wb;
    ev.snap = snap;
    ev.sheet_name = sheet_name;
    ev.pool = pool;
    ev.lx.p = formula;
    ev.lx.bad = 0;
    xlsx_lex_next(&ev.lx);
    XlsxVal v = xlsx_expr(&ev);
    /* A formula that yields an EMPTY cell is ZERO, not empty. `=Z50` where Z50
     * is blank displays 0 in Excel, and the cached value in the file is 0.
     * Empty already coerces to 0 inside arithmetic (xlsx_as_num), so this is
     * only about the top-level result -- but it is not a rounding error:
     * measured over the corpus it accounts for 351,897 disagreeing cells,
     * about 6% of all disagreements, for this one line.
     *
     * It applies only to a formula RESULT. An empty string produced on purpose
     * -- IF(A1="","",...) -- is XV_STR and untouched, and a cell that does not
     * exist at all is still reported as unknown by xlsx.evaluate. */
    if (v.kind == XV_EMPTY) { xv_free(v); v = xv_num(0); }
    if (unsupported) *unsupported = ev.unsupported;
    if (unsupported_name && un_len) snprintf(unsupported_name, un_len, "%s", ev.unsupported_name);
    return v;
}

/* ------------------------------------------------------- the dependency graph
 *
 * Needed the moment something CHANGES. Until then every input carries Excel's
 * cached value and each formula is evaluable in isolation, which is what let
 * the evaluator be validated before this existed.
 *
 * Order matters and cannot be faked by iterating cells in sheet order: a
 * formula in row 2 may depend on one in row 40, so evaluating top to bottom
 * would feed it a stale input and produce a confidently wrong number. */

/* Every cell a formula reads, ranges expanded. Reuses the formula lexer so the
 * set of things treated as a reference cannot drift from what evaluation
 * treats as one. */
static void xlsx_formula_refs(const char *formula, long **rows, long **cols, size_t *n) {
    size_t cap = 0;
    *rows = NULL; *cols = NULL; *n = 0;
    XlsxLex lx;
    lx.p = formula;
    lx.bad = 0;
    xlsx_lex_next(&lx);
    while (lx.cur.kind != XT_END) {
        if (lx.cur.kind != XT_REF) {
            xlsx_lex_next(&lx);
            continue;
        }
        char first[128];
        snprintf(first, sizeof first, "%s", lx.cur.text);
        long c1, r1, c2, r2;
        if (!xlsx_parse_ref(first, &c1, &r1)) { xlsx_lex_next(&lx); continue; }
        c2 = c1; r2 = r1;
        XlsxLex save = lx;
        xlsx_lex_next(&lx);
        if (lx.cur.kind == XT_COLON) {
            xlsx_lex_next(&lx);
            if (lx.cur.kind == XT_REF && xlsx_parse_ref(lx.cur.text, &c2, &r2)) {
                xlsx_lex_next(&lx);
            } else {
                lx = save;
                xlsx_lex_next(&lx);
                c2 = c1; r2 = r1;
            }
        }
        if (c1 > c2) { long t = c1; c1 = c2; c2 = t; }
        if (r1 > r2) { long t = r1; r1 = r2; r2 = t; }
        for (long r = r1; r <= r2; r++) {
            for (long c = c1; c <= c2; c++) {
                if (*n == cap) {
                    cap = cap ? cap * 2 : 8;
                    long *gr = realloc(*rows, cap * sizeof(long));
                    long *gc = realloc(*cols, cap * sizeof(long));
                    if (!gr || !gc) abort();
                    *rows = gr; *cols = gc;
                }
                (*rows)[*n] = r;
                (*cols)[*n] = c;
                (*n)++;
            }
        }
    }
}

/* Depth-first topological order over the formula cells. `state` is 0 unvisited,
 * 1 in-progress, 2 done; an edge back into an in-progress cell is a CIRCULAR
 * REFERENCE, which is reported rather than iterated toward a fixed point (Excel
 * only does that when the user opts in). */
static void xlsx_topo_visit(const XlsxSnap *snap, size_t i, int *state,
                            size_t *order, size_t *on, int *circular) {
    if (state[i] == 2) return;
    if (state[i] == 1) { circular[i] = 1; return; }
    state[i] = 1;
    const XlsxSnapCell *c = &snap->cells[i];
    if (c->formula) {
        long *rr = NULL, *cc = NULL;
        size_t rn = 0;
        xlsx_formula_refs(c->formula, &rr, &cc, &rn);
        for (size_t k = 0; k < rn; k++) {
            /* Indexed, not scanned. This loop is per-reference inside a
             * per-formula walk, so a linear scan here made building the
             * dependency graph quadratic in the sheet -- the same defect as in
             * xlsx_snap_at, and on the same workbooks. */
            size_t j = xlsx_snap_pos(snap, rr[k], cc[k]);
            if (j != (size_t)-1 && snap->cells[j].formula) {
                xlsx_topo_visit(snap, j, state, order, on, circular);
                if (circular[j]) circular[i] = 1;
            }
        }
        free(rr); free(cc);
    }
    state[i] = 2;
    order[(*on)++] = i;
}

static Value xlsx_val_to_gbasic(XlsxVal v) {
    switch (v.kind) {
    case XV_NUM:  return value_number(v.num);
    case XV_BOOL: return value_bool((int)v.num);
    case XV_STR:  return value_string(v.str ? v.str : "");
    case XV_ERR:  return value_string(v.str ? v.str : "#ERROR");
    default:      return value_unknown();
    }
}

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
        if (!sheet) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return xlsx_raise(message);
        }
        if (!sheet->part) {
            /* A macro/module sheet has no cells; answer as for an empty sheet,
             * which is the same answer a caller gets for an absent cell. */
            value_free(wbv);
            value_free(shv);
            value_free(refv);
            return one ? value_unknown() : value_array(NULL, 0);
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
        /* Shared-formula masters, collected before the walk so continuations
         * can be reported as the formula they stand for. */
        XlsxShared shared;
        xlsx_shared_collect(root, &shared);
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
                        found = xlsx_cell_record(wb, c, &shared);
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
                    items[count++] = xlsx_cell_record(wb, c, &shared);
                }
            }
        }
        xlsx_shared_free(&shared);
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
        if (!sheet) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv);
            value_free(shv);
            return xlsx_raise(message);
        }
        /* A partless (macro/module) sheet leaves sp NULL, so the scan below
         * finds nothing and the record reports an empty sheet -- correct. */
        XlsxPart *sp = sheet->part ? xlsx_find_part(wb, sheet->part) : NULL;
        xmlDocPtr doc = sp ? xlsx_parse_part(sp) : NULL;
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

    /* Set a cell's value, regenerating only that sheet's part.
     *
     * A written cell's CACHED VALUE and FORMULA go out of step the moment one
     * changes: writing a literal over a formula cell must drop the <f>, or
     * Excel recalculates and silently reverts the edit. Until the recalc engine
     * exists (§13.D) this refuses to write over a formula rather than guess
     * which the caller meant. */
    if (strcmp(name, "set") == 0) {
        if (expr->as.call.args.count != 4) {
            return xlsx_raise("xlsx.set expects four arguments (workbook, sheet, ref, value)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        Value refv = eval_expr(expr->as.call.args.items[2]);
        Value newv = eval_expr(expr->as.call.args.items[3]);
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING || refv.kind != VALUE_STRING) {
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx.set expects a workbook, a sheet name and a ref");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        XlsxSheet *sheet = xlsx_find_sheet(wb, shv.as.string);
        if (!sheet) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise(message);
        }
        /* Reads treat a partless sheet as empty, but a WRITE has nowhere to go:
         * there is no worksheet part to put the cell in. Refuse with the actual
         * reason rather than claiming the sheet does not exist. */
        if (!sheet->part) {
            char message[256];
            snprintf(message, sizeof message,
                     "xlsx.set: sheet %s has no worksheet part (it is a macro or "
                     "module sheet); nothing can be written to it", shv.as.string);
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise(message);
        }
        XlsxPart *sp = xlsx_find_part(wb, sheet->part);
        if (!sp) {
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx: sheet part missing from the container");
        }
        xmlDocPtr doc = xlsx_parse_part(sp);
        if (!doc) {
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx: sheet part is not well-formed XML");
        }

        xmlNodePtr target = NULL;
        xmlNodePtr root = xmlDocGetRootElement(doc);
        for (xmlNodePtr n = root ? root->children : NULL; n && !target; n = n->next) {
            if (!xlsx_is(n, "sheetData")) {
                continue;
            }
            for (xmlNodePtr r = n->children; r && !target; r = r->next) {
                if (!xlsx_is(r, "row")) {
                    continue;
                }
                for (xmlNodePtr c = r->children; c; c = c->next) {
                    if (!xlsx_is(c, "c")) {
                        continue;
                    }
                    char *cref = xlsx_prop(c, "r");
                    int hit = cref && strcmp(cref, refv.as.string) == 0;
                    free(cref);
                    if (hit) {
                        target = c;
                        break;
                    }
                }
            }
        }
        if (!target) {
            /* Creating a cell means placing it in the right row in column
             * order, and inserting the row if absent. Not yet implemented, and
             * saying so beats writing it into the wrong place. */
            xmlFreeDoc(doc);
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx.set: creating a new cell is not supported yet; "
                              "only existing cells can be written");
        }

        int has_formula = 0;
        for (xmlNodePtr k = target->children; k; k = k->next) {
            if (xlsx_is(k, "f")) {
                has_formula = 1;
            }
        }
        if (has_formula) {
            xmlFreeDoc(doc);
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx.set: refusing to overwrite a formula cell; "
                              "the formula would recalculate and revert the value");
        }

        /* Rewrite the cell: drop existing children, set type and <v>. */
        xmlNodePtr child = target->children;
        while (child) {
            xmlNodePtr next = child->next;
            xmlUnlinkNode(child);
            xmlFreeNode(child);
            child = next;
        }
        char buf[64];
        if (newv.kind == VALUE_NUMBER) {
            xmlUnsetProp(target, (const xmlChar *)"t");
            snprintf(buf, sizeof buf, "%.15g", newv.as.number);
            xmlNewChild(target, NULL, (const xmlChar *)"v", (const xmlChar *)buf);
        } else if (newv.kind == VALUE_BOOL) {
            xmlSetProp(target, (const xmlChar *)"t", (const xmlChar *)"b");
            xmlNewChild(target, NULL, (const xmlChar *)"v",
                        (const xmlChar *)(newv.as.boolean ? "1" : "0"));
        } else if (newv.kind == VALUE_STRING) {
            /* inlineStr rather than a shared-string index: appending to the
             * shared table would renumber nothing but would make this edit
             * depend on a second part staying in step. Inline is self-contained
             * and Excel accepts it everywhere. */
            xmlSetProp(target, (const xmlChar *)"t", (const xmlChar *)"inlineStr");
            xmlNodePtr is = xmlNewChild(target, NULL, (const xmlChar *)"is", NULL);
            xmlNewChild(is, NULL, (const xmlChar *)"t", (const xmlChar *)newv.as.string);
        } else {
            xmlFreeDoc(doc);
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx.set: value must be a number, boolean or string");
        }

        xmlChar *dumped = NULL;
        int dumped_len = 0;
        xmlDocDumpMemory(doc, &dumped, &dumped_len);
        xmlFreeDoc(doc);
        if (!dumped) {
            value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
            return xlsx_raise("xlsx.set: could not serialize the sheet");
        }
        free(sp->data);
        sp->data = malloc((size_t)dumped_len ? (size_t)dumped_len : 1);
        if (!sp->data) {
            abort();
        }
        memcpy(sp->data, dumped, (size_t)dumped_len);
        sp->length = (size_t)dumped_len;
        xmlFree(dumped);

        value_free(wbv); value_free(shv); value_free(refv); value_free(newv);
        return value_bool(1);
    }

    /* Write the workbook out. Untouched parts are emitted from the bytes they
     * were read as — that is the round-trip guarantee (§13.A). */
    if (strcmp(name, "save") == 0) {
        if (expr->as.call.args.count != 2) {
            return xlsx_raise("xlsx.save expects two arguments (workbook, path)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value pathv = eval_expr(expr->as.call.args.items[1]);
        if (wbv.kind != VALUE_WORKBOOK ||
            (pathv.kind != VALUE_STRING && pathv.kind != VALUE_FILE)) {
            value_free(wbv);
            value_free(pathv);
            return xlsx_raise("xlsx.save expects a workbook and a path");
        }
        const char *path = pathv.kind == VALUE_FILE ? pathv.as.file_path : pathv.as.string;
        size_t len = 0;
        unsigned char *bytes = xlsx_write_container(wbv.as.workbook, &len);
        if (!bytes) {
            value_free(wbv);
            value_free(pathv);
            return xlsx_raise("xlsx.save: could not compress the container");
        }
        FILE *f = fopen(path, "wb");
        if (!f) {
            free(bytes);
            char message[512];
            snprintf(message, sizeof message, "xlsx.save: cannot write %s", path);
            value_free(wbv);
            value_free(pathv);
            return xlsx_raise(message);
        }
        size_t put = fwrite(bytes, 1, len, f);
        int closed = fclose(f);
        free(bytes);
        value_free(wbv);
        value_free(pathv);
        if (put != len || closed != 0) {
            return xlsx_raise("xlsx.save: short write");
        }
        return value_number((double)len);
    }

    /* Evaluate one cell's formula against the sheet's cached values. */
    if (strcmp(name, "evaluate") == 0) {
        if (expr->as.call.args.count != 3) {
            return xlsx_raise("xlsx.evaluate expects three arguments (workbook, sheet, ref)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        Value refv = eval_expr(expr->as.call.args.items[2]);
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING || refv.kind != VALUE_STRING) {
            value_free(wbv); value_free(shv); value_free(refv);
            return xlsx_raise("xlsx.evaluate expects a workbook, a sheet name and a ref");
        }
        XlsxSnap snap;
        XlsxSheetPool pool = {NULL, 0};
        if (!xlsx_snapshot(wbv.as.workbook, shv.as.string, &snap)) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv); value_free(shv); value_free(refv);
            return xlsx_raise(message);
        }
        long col = 0, row = 0;
        Value out = value_unknown();
        if (xlsx_parse_ref(refv.as.string, &col, &row)) {
            const XlsxSnapCell *c = xlsx_snap_at(&snap, row, col);
            if (c && c->formula) {
                int unsup = 0;
                char un[64] = "";
                XlsxVal v = xlsx_eval_formula_in(wbv.as.workbook, &snap, shv.as.string, &pool,
                                                 c->formula, &unsup, un, sizeof un);
                out = xlsx_val_to_gbasic(v);
                xv_free(v);
            } else if (c) {
                /* Not a formula: the literal is its own value. */
                switch (c->kind) {
                case XV_NUM: out = value_number(c->num); break;
                case XV_BOOL: out = value_bool((int)c->num); break;
                case XV_STR: out = value_string(c->str ? c->str : ""); break;
                case XV_ERR: out = value_string(c->str ? c->str : "#ERROR"); break;
                default: break;
                }
            }
        }
        xlsx_pool_free(&pool);
        xlsx_snap_free(&snap);
        value_free(wbv); value_free(shv); value_free(refv);
        return out;
    }

    /* THE ORACLE. Evaluate every formula on a sheet and compare against the
     * value Excel cached for it.
     *
     * No dependency graph is needed: each input cell already carries a cached
     * value, so every formula is checkable in isolation. The graph is only
     * required once something CHANGES.
     *
     * Volatile functions are reported separately rather than counted as
     * agreements or failures — their cached value dates from whenever the
     * workbook last calculated, so comparing against it is meaningless. */
    if (strcmp(name, "check") == 0) {
        if (expr->as.call.args.count != 2) {
            return xlsx_raise("xlsx.check expects two arguments (workbook, sheet)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING) {
            value_free(wbv); value_free(shv);
            return xlsx_raise("xlsx.check expects a workbook and a sheet name");
        }
        XlsxSnap snap;
        XlsxSheetPool pool = {NULL, 0};
        if (!xlsx_snapshot(wbv.as.workbook, shv.as.string, &snap)) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv); value_free(shv);
            return xlsx_raise(message);
        }

        long agree = 0, disagree = 0, volatile_n = 0, unsupported_n = 0;
        Value *rows = NULL;
        size_t rn = 0, rcap = 0;

        for (size_t i = 0; i < snap.count; i++) {
            const XlsxSnapCell *c = &snap.cells[i];
            if (!c->formula) continue;

            /* Volatility is decided BEFORE evaluating, and a volatile cell is
             * then not evaluated at all.
             *
             * Not an optimisation -- a correctness property of this call's
             * output. Once NOW() actually returns the clock, reporting its
             * computed value makes `check`'s result differ on every run, so any
             * golden written over a sheet containing NOW is broken by design --
             * ours was, and so would every user's be. Since the comparison is
             * meaningless anyway (the cached value dates from whenever Excel
             * last calculated), the honest report is that the cell was skipped,
             * not a number that invites exactly the comparison this refuses to
             * make. */
            int is_vol = 0;
            for (const char *q = c->formula; *q; q++) {
                if (isalpha((unsigned char)*q)) {
                    char word[32];
                    size_t w = 0;
                    while (*q && isalpha((unsigned char)*q) && w < sizeof word - 1) word[w++] = (char)toupper((unsigned char)*q++);
                    word[w] = '\0';
                    if (xlsx_is_volatile(word)) { is_vol = 1; break; }
                    if (!*q) break;
                }
            }

            int unsup = 0;
            char un[64] = "";
            XlsxVal got = is_vol
                ? xv_str("(not evaluated: volatile)")
                : xlsx_eval_formula_in(wbv.as.workbook, &snap, shv.as.string, &pool,
                                        c->formula, &unsup, un, sizeof un);

            const char *verdict;
            if (is_vol) { verdict = "volatile"; volatile_n++; }
            else if (unsup) { verdict = "unsupported"; unsupported_n++; }
            else {
                int same = 0;
                if (got.kind == XV_NUM && c->kind == XV_NUM) {
                    /* Compare with a relative tolerance: the cached value is a
                     * decimal rendering of a binary double, so exact equality
                     * would report spurious disagreements on ordinary
                     * arithmetic. */
                    double a2 = got.num, b2 = c->num;
                    double scale = fabs(a2) > fabs(b2) ? fabs(a2) : fabs(b2);
                    same = fabs(a2 - b2) <= (scale > 0 ? scale * 1e-9 : 1e-9);
                } else if (got.kind == XV_BOOL && c->kind == XV_BOOL) {
                    same = ((int)got.num != 0) == ((int)c->num != 0);
                } else if (got.kind == XV_STR && c->kind == XV_STR) {
                    same = strcmp(got.str ? got.str : "", c->str ? c->str : "") == 0;
                } else if (got.kind == XV_ERR && c->kind == XV_ERR) {
                    same = strcmp(got.str ? got.str : "", c->str ? c->str : "") == 0;
                }
                verdict = same ? "agree" : "disagree";
                if (same) agree++; else disagree++;
            }

            if (strcmp(verdict, "agree") != 0) {
                char refbuf[32];
                long cc = c->col + 1;
                char colname[8];
                size_t cl = 0;
                while (cc > 0 && cl < sizeof colname - 1) {
                    long rem = (cc - 1) % 26;
                    colname[cl++] = (char)('A' + rem);
                    cc = (cc - 1) / 26;
                }
                colname[cl] = '\0';
                for (size_t x = 0; x < cl / 2; x++) {
                    char t = colname[x]; colname[x] = colname[cl - 1 - x]; colname[cl - 1 - x] = t;
                }
                snprintf(refbuf, sizeof refbuf, "%s%ld", colname, c->row);

                char gotbuf[256], wantbuf[256];
                XlsxVal cached = xv_empty();
                switch (c->kind) {
                case XV_NUM: cached = xv_num(c->num); break;
                case XV_BOOL: cached = xv_bool((int)c->num); break;
                case XV_STR: cached = xv_str(c->str ? c->str : ""); break;
                case XV_ERR: cached = xv_err(c->str ? c->str : "#ERROR"); break;
                default: break;
                }
                xlsx_to_text(got, gotbuf, sizeof gotbuf);
                xlsx_to_text(cached, wantbuf, sizeof wantbuf);
                xv_free(cached);

                RecordField *fields = calloc(5, sizeof(RecordField));
                if (!fields) abort();
                fields[0].name = copy_string("ref");
                fields[0].value = cell_alloc(); *fields[0].value = value_string(refbuf);
                fields[1].name = copy_string("verdict");
                fields[1].value = cell_alloc(); *fields[1].value = value_string(verdict);
                fields[2].name = copy_string("formula");
                fields[2].value = cell_alloc(); *fields[2].value = value_string(c->formula);
                fields[3].name = copy_string("computed");
                fields[3].value = cell_alloc(); *fields[3].value = value_string(gotbuf);
                fields[4].name = copy_string("cached");
                fields[4].value = cell_alloc(); *fields[4].value = value_string(wantbuf);
                if (rn == rcap) {
                    rcap = rcap ? rcap * 2 : 8;
                    Value *g = realloc(rows, rcap * sizeof(Value));
                    if (!g) abort();
                    rows = g;
                }
                rows[rn++] = value_record(fields, 5);
            }
            xv_free(got);
        }

        xlsx_pool_free(&pool);
        xlsx_snap_free(&snap);
        value_free(wbv); value_free(shv);

        RecordField *fields = calloc(5, sizeof(RecordField));
        if (!fields) abort();
        fields[0].name = copy_string("agree");
        fields[0].value = cell_alloc(); *fields[0].value = value_number((double)agree);
        fields[1].name = copy_string("disagree");
        fields[1].value = cell_alloc(); *fields[1].value = value_number((double)disagree);
        fields[2].name = copy_string("volatile_skipped");
        fields[2].value = cell_alloc(); *fields[2].value = value_number((double)volatile_n);
        fields[3].name = copy_string("unsupported");
        fields[3].value = cell_alloc(); *fields[3].value = value_number((double)unsupported_n);
        fields[4].name = copy_string("notes");
        fields[4].value = cell_alloc(); *fields[4].value = value_array(rows, rn);
        return value_record(fields, 5);
    }

    /* Recalculate a sheet in dependency order and persist the new cached
     * values, so a subsequent xlsx.save writes a workbook Excel will agree
     * with.
     *
     * Order is computed, not assumed: a formula in row 2 may depend on one in
     * row 40, so evaluating in sheet order would feed it a stale input and
     * produce a confidently wrong number. */
    if (strcmp(name, "recalc") == 0) {
        if (expr->as.call.args.count != 2) {
            return xlsx_raise("xlsx.recalc expects two arguments (workbook, sheet)");
        }
        Value wbv = eval_expr(expr->as.call.args.items[0]);
        Value shv = eval_expr(expr->as.call.args.items[1]);
        if (wbv.kind != VALUE_WORKBOOK || shv.kind != VALUE_STRING) {
            value_free(wbv); value_free(shv);
            return xlsx_raise("xlsx.recalc expects a workbook and a sheet name");
        }
        XlsxWorkbook *wb = wbv.as.workbook;
        XlsxSnap snap;
        XlsxSheetPool pool = {NULL, 0};
        if (!xlsx_snapshot(wb, shv.as.string, &snap)) {
            char message[256];
            snprintf(message, sizeof message, "xlsx: no such sheet: %s", shv.as.string);
            value_free(wbv); value_free(shv);
            return xlsx_raise(message);
        }

        int *state = calloc(snap.count ? snap.count : 1, sizeof(int));
        int *circ = calloc(snap.count ? snap.count : 1, sizeof(int));
        size_t *order = calloc(snap.count ? snap.count : 1, sizeof(size_t));
        if (!state || !circ || !order) abort();
        size_t on = 0;
        for (size_t i = 0; i < snap.count; i++) {
            if (snap.cells[i].formula) {
                xlsx_topo_visit(&snap, i, state, order, &on, circ);
            }
        }

        long evaluated = 0, changed = 0, circular = 0, unsupported_n = 0;
        for (size_t k = 0; k < on; k++) {
            size_t i = order[k];
            XlsxSnapCell *c = &snap.cells[i];
            if (!c->formula) continue;
            if (circ[i]) { circular++; continue; }
            int unsup = 0;
            char un[64] = "";
            XlsxVal v = xlsx_eval_formula_in(wb, &snap, shv.as.string, &pool,
                                             c->formula, &unsup, un, sizeof un);
            evaluated++;
            if (unsup) { unsupported_n++; xv_free(v); continue; }

            /* Write the result back into the SNAPSHOT before evaluating
             * anything downstream — that is the whole point of the ordering. */
            int differs = 0;
            if (v.kind != c->kind) differs = 1;
            else if (v.kind == XV_NUM && v.num != c->num) differs = 1;
            else if (v.kind == XV_BOOL && ((int)v.num != 0) != ((int)c->num != 0)) differs = 1;
            else if ((v.kind == XV_STR || v.kind == XV_ERR) &&
                     strcmp(v.str ? v.str : "", c->str ? c->str : "") != 0) differs = 1;
            if (differs) {
                changed++;
                free(c->str);
                c->str = NULL;
                c->kind = v.kind;
                c->num = v.num;
                if (v.str) c->str = copy_string(v.str);
            }
            xv_free(v);
        }

        /* Persist the recalculated values into the sheet part. Only the <v> of
         * formula cells is touched; the formula itself and every other part are
         * left exactly as they were. */
        if (changed > 0) {
            XlsxSheet *sheet = xlsx_find_sheet(wb, shv.as.string);
            XlsxPart *sp = sheet && sheet->part ? xlsx_find_part(wb, sheet->part) : NULL;
            xmlDocPtr doc = sp ? xlsx_parse_part(sp) : NULL;
            if (doc) {
                xmlNodePtr root = xmlDocGetRootElement(doc);
                for (xmlNodePtr n = root ? root->children : NULL; n; n = n->next) {
                    if (!xlsx_is(n, "sheetData")) continue;
                    for (xmlNodePtr r = n->children; r; r = r->next) {
                        if (!xlsx_is(r, "row")) continue;
                        for (xmlNodePtr c = r->children; c; c = c->next) {
                            if (!xlsx_is(c, "c")) continue;
                            char *ref = xlsx_prop(c, "r");
                            long col = 0, row = 0;
                            if (!ref || !xlsx_parse_ref(ref, &col, &row)) { free(ref); continue; }
                            free(ref);
                            const XlsxSnapCell *sc = xlsx_snap_at(&snap, row, col);
                            if (!sc || !sc->formula) continue;
                            char buf[64];
                            const char *newtype = NULL;
                            const char *text = buf;
                            switch (sc->kind) {
                            case XV_NUM: snprintf(buf, sizeof buf, "%.15g", sc->num); break;
                            case XV_BOOL: snprintf(buf, sizeof buf, "%s", sc->num ? "1" : "0"); newtype = "b"; break;
                            case XV_STR: text = sc->str ? sc->str : ""; newtype = "str"; break;
                            case XV_ERR: text = sc->str ? sc->str : "#ERROR"; newtype = "e"; break;
                            default: snprintf(buf, sizeof buf, "0"); break;
                            }
                            if (newtype) xmlSetProp(c, (const xmlChar *)"t", (const xmlChar *)newtype);
                            else xmlUnsetProp(c, (const xmlChar *)"t");
                            xmlNodePtr v = NULL;
                            for (xmlNodePtr k = c->children; k; k = k->next) {
                                if (xlsx_is(k, "v")) { v = k; break; }
                            }
                            if (!v) v = xmlNewChild(c, NULL, (const xmlChar *)"v", NULL);
                            xmlNodeSetContent(v, (const xmlChar *)text);
                        }
                    }
                }
                xmlChar *dumped = NULL;
                int dumped_len = 0;
                xmlDocDumpMemory(doc, &dumped, &dumped_len);
                xmlFreeDoc(doc);
                if (dumped) {
                    free(sp->data);
                    sp->data = malloc((size_t)dumped_len ? (size_t)dumped_len : 1);
                    if (!sp->data) abort();
                    memcpy(sp->data, dumped, (size_t)dumped_len);
                    sp->length = (size_t)dumped_len;
                    xmlFree(dumped);
                }
            }
        }

        free(state); free(circ); free(order);
        xlsx_pool_free(&pool);
        xlsx_snap_free(&snap);
        value_free(wbv); value_free(shv);

        RecordField *fields = calloc(4, sizeof(RecordField));
        if (!fields) abort();
        fields[0].name = copy_string("evaluated");
        fields[0].value = cell_alloc(); *fields[0].value = value_number((double)evaluated);
        fields[1].name = copy_string("changed");
        fields[1].value = cell_alloc(); *fields[1].value = value_number((double)changed);
        fields[2].name = copy_string("circular");
        fields[2].value = cell_alloc(); *fields[2].value = value_number((double)circular);
        fields[3].name = copy_string("unsupported");
        fields[3].value = cell_alloc(); *fields[3].value = value_number((double)unsupported_n);
        return value_record(fields, 4);
    }

    char message[160];
    snprintf(message, sizeof message, "unknown xlsx function: %s", name);
    return xlsx_raise(message);
#endif
}
