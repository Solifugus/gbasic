/* src/modules/xml.c — native XML module over libxml2 (xml_design.md, WP-XML-1).
 *
 * This is a TRANSLATION-UNIT INCLUDE: it is `#include`d into src/eval.c (not a
 * separate object file) so it can use eval.c's static Value API (value_record,
 * record_set, value_array, value_string, runtime_error_raise, ...). eval.c's
 * Value type and its constructors are file-static, matching how every other
 * optional module (sqlite/pg/webclient/gui) lives inside eval.c; this file keeps
 * the XML code in its own unit per the plan's repo layout while reusing that API.
 *
 * The whole file is guarded by HAVE_LIBXML2. When libxml2 is absent the module
 * compiles out and `load xml` / `xml.*` degrade to a clean runtime error (wired
 * in eval.c), exactly like the other optional modules.
 *
 * WP-XML-1 scope: xml.parse(text[, keep_space]) and xml.parse_file(path) — the
 * §2 node-record mapping, §7 security defaults, §8 structured errors. Navigation
 * helpers (find/find_all/text/attr) are WP-XML-2.
 */
#if HAVE_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/HTMLparser.h>

#define XML_ERROR_CODE 5001
#define XML_MAX_DEPTH 256

/* §7 security defaults (non-negotiable):
 *   NONET      — no network access from the parser (no external entity/DTD fetch)
 *   NOCDATA    — CDATA sections surface as text (coalesced with adjacent text)
 * NOENT is deliberately NOT set: without it, only the five predefined entities
 * and character references are expanded, while DTD-declared (custom) entities are
 * left unexpanded — closing the billion-laughs / XXE vectors. DTD loading is off
 * by default (DTDLOAD not set). The DOCTYPE is skipped, never processed. */
#define XML_SECURE_OPTS (XML_PARSE_NONET | XML_PARSE_NOCDATA)

/* §6 lenient HTML options. The HTML parser repairs rather than rejects, so it
 * always yields a tree. NONET keeps the same no-network guarantee as the XML
 * path; RECOVER makes tag soup non-fatal; NOERROR/NOWARNING silence libxml2's
 * repair chatter (we already install a silent structured handler). NODEFDTD
 * avoids injecting a default DOCTYPE. */
#define XML_HTML_OPTS (HTML_PARSE_NONET | HTML_PARSE_RECOVER | \
                       HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NODEFDTD)

/* Swallow libxml2's default error printing to stderr — the error is captured
 * from the parser context and re-raised as a structured gBASIC error instead. */
static void xml_silent_error(void *user, const xmlError *err) {
    (void)user;
    (void)err;
}

static void xml_install_silent_errors(void) {
    xmlSetStructuredErrorFunc(NULL, xml_silent_error);
}

/* Raise a structured xml error carrying the libxml2 message + line/column. When
 * no parser context is given (e.g. the streaming reader), fall back to libxml2's
 * global last error so the real message (depth cap, malformed) still surfaces. */
static void xml_raise_from_ctxt(xmlParserCtxtPtr ctxt, const char *fallback) {
    const xmlError *e = ctxt ? xmlCtxtGetLastError(ctxt) : xmlGetLastError();
    char message[600];
    if (e && e->message) {
        char msg[480];
        snprintf(msg, sizeof(msg), "%s", e->message);
        size_t len = strlen(msg);
        while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
            msg[--len] = '\0';
        }
        if (e->line > 0) {
            snprintf(message, sizeof(message), "xml: %s (line %d, column %d)",
                     msg, e->line, e->int2);
        } else {
            snprintf(message, sizeof(message), "xml: %s", msg);
        }
    } else {
        snprintf(message, sizeof(message), "%s", fallback);
    }
    runtime_error_raise(message, XML_ERROR_CODE, "xml");
}

/* Grow-append a Value into a heap buffer (ownership stays with the buffer). */
static void xml_items_push(Value **items, size_t *count, size_t *cap, Value v) {
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2 : 8;
        Value *grown = realloc(*items, sizeof(Value) * next);
        if (!grown) {
            abort();
        }
        *items = grown;
        *cap = next;
    }
    (*items)[(*count)++] = v;
}

static int xml_all_whitespace(const char *s) {
    for (; *s; s++) {
        if (!isspace((unsigned char)*s)) {
            return 0;
        }
    }
    return 1;
}

/* Convert a libxml2 element node into a §2 node record. Returns 1 on success,
 * 0 on error (a structured error has been raised and *out is untouched). */
static int xml_element_to_record(xmlNode *elem, int depth, int keep_space, Value *out) {
    if (depth > XML_MAX_DEPTH) {
        char m[128];
        snprintf(m, sizeof(m), "xml: maximum nesting depth (%d) exceeded", XML_MAX_DEPTH);
        runtime_error_raise(m, XML_ERROR_CODE, "xml");
        return 0;
    }

    Value rec = value_record(NULL, 0);

    /* name (local) + qname (as written) */
    record_set(&rec, "name", value_string((const char *)elem->name));
    if (elem->ns && elem->ns->prefix) {
        char qname[512];
        snprintf(qname, sizeof(qname), "%s:%s",
                 (const char *)elem->ns->prefix, (const char *)elem->name);
        record_set(&rec, "qname", value_string(qname));
    } else {
        record_set(&rec, "qname", value_string((const char *)elem->name));
    }

    /* ns URI or nothing */
    if (elem->ns && elem->ns->href) {
        record_set(&rec, "ns", value_string((const char *)elem->ns->href));
    } else {
        record_set(&rec, "ns", value_null());
    }

    /* attrs record (values are strings, entities already decoded). Namespace
     * DECLARATIONS (xmlns / xmlns:prefix) live in libxml2's nsDef list, not
     * properties; the design (§2) keeps them in `attrs` like any other
     * attribute so a parsed tree round-trips through xml.encode. Emitted first,
     * matching source order and keeping parse->encode->parse deterministic. */
    Value attrs = value_record(NULL, 0);
    for (xmlNs *nsdef = elem->nsDef; nsdef; nsdef = nsdef->next) {
        char aname[512];
        if (nsdef->prefix) {
            snprintf(aname, sizeof(aname), "xmlns:%s", (const char *)nsdef->prefix);
        } else {
            snprintf(aname, sizeof(aname), "xmlns");
        }
        record_set(&attrs, aname,
                   value_string(nsdef->href ? (const char *)nsdef->href : ""));
    }
    for (xmlAttr *a = elem->properties; a; a = a->next) {
        xmlChar *val = xmlNodeListGetString(elem->doc, a->children, 1);
        record_set(&attrs, (const char *)a->name,
                   value_string(val ? (const char *)val : ""));
        if (val) {
            xmlFree(val);
        }
    }
    record_set(&rec, "attrs", attrs);

    /* children: ordered list of element records and coalesced text strings.
     * Adjacent text/CDATA runs are merged; a whitespace-only run is dropped
     * unless keep_space. Comments and PIs are skipped. */
    Value *items = NULL;
    size_t count = 0, cap = 0;
    char *textbuf = NULL;
    size_t textlen = 0;

    for (xmlNode *c = elem->children; c; c = c->next) {
        if (c->type == XML_ELEMENT_NODE) {
            if (textbuf) {
                if (keep_space || !xml_all_whitespace(textbuf)) {
                    xml_items_push(&items, &count, &cap, value_string(textbuf));
                }
                free(textbuf);
                textbuf = NULL;
                textlen = 0;
            }
            Value child;
            if (!xml_element_to_record(c, depth + 1, keep_space, &child)) {
                free(textbuf);
                for (size_t i = 0; i < count; i++) {
                    value_free(items[i]);
                }
                free(items);
                value_free(rec);
                return 0;
            }
            xml_items_push(&items, &count, &cap, child);
        } else if (c->type == XML_TEXT_NODE || c->type == XML_CDATA_SECTION_NODE) {
            if (c->content) {
                size_t add = strlen((const char *)c->content);
                char *grown = realloc(textbuf, textlen + add + 1);
                if (!grown) {
                    abort();
                }
                textbuf = grown;
                memcpy(textbuf + textlen, c->content, add);
                textlen += add;
                textbuf[textlen] = '\0';
            }
        }
        /* other node types (comment, PI, ...) are dropped */
    }
    if (textbuf) {
        if (keep_space || !xml_all_whitespace(textbuf)) {
            xml_items_push(&items, &count, &cap, value_string(textbuf));
        }
        free(textbuf);
    }

    record_set(&rec, "children", value_array(items, count));
    *out = rec;
    return 1;
}

/* Parse an in-memory document; returns the root element record or raises. */
static Value xml_parse_memory(const char *text, size_t len, int keep_space) {
    xml_install_silent_errors();
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    if (!ctxt) {
        runtime_error_raise("xml: could not create parser context",
                            XML_ERROR_CODE, "xml");
        return value_null();
    }
    xmlDocPtr doc = xmlCtxtReadMemory(ctxt, text, (int)len, NULL, NULL, XML_SECURE_OPTS);
    if (!doc) {
        xml_raise_from_ctxt(ctxt, "xml: document is not well-formed");
        xmlFreeParserCtxt(ctxt);
        return value_null();
    }
    xmlNode *root = xmlDocGetRootElement(doc);
    Value result;
    if (!root) {
        runtime_error_raise("xml: document has no root element", XML_ERROR_CODE, "xml");
        result = value_null();
    } else if (!xml_element_to_record(root, 1, keep_space, &result)) {
        result = value_null();
    }
    xmlFreeDoc(doc);
    xmlFreeParserCtxt(ctxt);
    return result;
}

/* Parse a file by path; libxml2 reads it (NONET still forbids network). */
static Value xml_parse_path(const char *path, int keep_space) {
    xml_install_silent_errors();
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    if (!ctxt) {
        runtime_error_raise("xml: could not create parser context",
                            XML_ERROR_CODE, "xml");
        return value_null();
    }
    xmlDocPtr doc = xmlCtxtReadFile(ctxt, path, NULL, XML_SECURE_OPTS);
    if (!doc) {
        xml_raise_from_ctxt(ctxt, "xml: could not read file as XML");
        xmlFreeParserCtxt(ctxt);
        return value_null();
    }
    xmlNode *root = xmlDocGetRootElement(doc);
    Value result;
    if (!root) {
        runtime_error_raise("xml: document has no root element", XML_ERROR_CODE, "xml");
        result = value_null();
    } else if (!xml_element_to_record(root, 1, keep_space, &result)) {
        result = value_null();
    }
    xmlFreeDoc(doc);
    xmlFreeParserCtxt(ctxt);
    return result;
}

/* Parse tag-soup HTML (xml_design.md §6). libxml2's HTML parser repairs rather
 * than rejects, so this always yields a §2 node tree — find/find_all/text work
 * unchanged on it. Node names are lowercased by the HTML parser. Only a NULL doc
 * (e.g. empty input) or a rootless result raises; malformed markup never does. */
static Value xml_parse_html_memory(const char *text, size_t len) {
    xml_install_silent_errors();
    /* encoding NULL: auto-detect from BOM / <meta charset> like a real 10-K */
    htmlDocPtr doc = htmlReadMemory(text, (int)len, NULL, NULL, XML_HTML_OPTS);
    if (!doc) {
        xml_raise_from_ctxt(NULL, "xml: could not parse HTML");
        return value_null();
    }
    xmlNode *root = xmlDocGetRootElement(doc);
    Value result;
    if (!root) {
        runtime_error_raise("xml: HTML document has no root element",
                            XML_ERROR_CODE, "xml");
        result = value_null();
    } else if (!xml_element_to_record(root, 1, 0, &result)) {
        result = value_null();
    }
    xmlFreeDoc(doc);
    return result;
}

/* ===== Navigation helpers (xml_design.md §3) =============================
 * Pure functions over the §2 node-record shape, so they also work on hand-built
 * records. Implemented in C (not stdlib .bas) because the compiled `xml` module
 * owns the `xml.` qualifier — `load xml` is intercepted before any stdlib file,
 * so `xml.find`/`text`/`attr` must live here. Mini-path is slash-separated local
 * names or `*`, matched against element children, descending. find/attr return
 * `unknown` on absence; text returns "" (§8) — none of them raise. */

static int xml_is_element(const Value *v) {
    return v->kind == VALUE_RECORD && record_find_const(v, "name") != NULL;
}

static const char *xml_local_name(Value *elem) {
    RecordField *f = record_find(elem, "name");
    if (!f || f->value->kind != VALUE_STRING) {
        return NULL;
    }
    return f->value->as.string;
}

static Value *xml_children_of(Value *elem) {
    RecordField *f = record_find(elem, "children");
    if (!f || f->value->kind != VALUE_ARRAY) {
        return NULL;
    }
    return f->value;
}

static Value *xml_first_child_named(Value *elem, const char *step) {
    Value *ch = xml_children_of(elem);
    if (!ch) {
        return NULL;
    }
    for (size_t i = 0; i < ch->as.array.count; i++) {
        Value *c = &ch->as.array.items[i];
        if (!xml_is_element(c)) {
            continue;
        }
        const char *nm = xml_local_name(c);
        if (nm && (strcmp(step, "*") == 0 || strcmp(step, nm) == 0)) {
            return c;
        }
    }
    return NULL;
}

/* split a mini-path into non-empty steps (pointers into the mutable buf) */
static size_t xml_split_path(char *buf, const char **steps, size_t max) {
    size_t n = 0;
    char *p = buf;
    while (*p && n < max) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        steps[n++] = p;
        while (*p && *p != '/') {
            p++;
        }
        if (*p == '/') {
            *p = '\0';
            p++;
        }
    }
    return n;
}

static Value xml_do_find(Value *node, const char *path) {
    if (!xml_is_element(node)) {
        return value_unknown();
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    const char *steps[64];
    size_t n = xml_split_path(buf, steps, 64);
    if (n == 0) {
        return value_unknown();
    }
    Value *cur = node;
    for (size_t i = 0; i < n; i++) {
        cur = xml_first_child_named(cur, steps[i]);
        if (!cur) {
            return value_unknown();
        }
    }
    return value_copy(*cur);
}

static Value xml_do_find_all(Value *node, const char *path) {
    if (!xml_is_element(node)) {
        return value_array(NULL, 0);
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    const char *steps[64];
    size_t n = xml_split_path(buf, steps, 64);
    if (n == 0) {
        return value_array(NULL, 0);
    }
    /* descend first-match through all but the final step */
    Value *cur = node;
    for (size_t i = 0; i + 1 < n; i++) {
        cur = xml_first_child_named(cur, steps[i]);
        if (!cur) {
            return value_array(NULL, 0);
        }
    }
    /* collect ALL matches at the final step */
    const char *last = steps[n - 1];
    Value *ch = xml_children_of(cur);
    if (!ch) {
        return value_array(NULL, 0);
    }
    Value *items = NULL;
    size_t count = 0, cap = 0;
    for (size_t i = 0; i < ch->as.array.count; i++) {
        Value *c = &ch->as.array.items[i];
        if (!xml_is_element(c)) {
            continue;
        }
        const char *nm = xml_local_name(c);
        if (nm && (strcmp(last, "*") == 0 || strcmp(last, nm) == 0)) {
            xml_items_push(&items, &count, &cap, value_copy(*c));
        }
    }
    return value_array(items, count);
}

static void xml_gather_text(Value *node, char **buf, size_t *len, size_t *cap) {
    if (node->kind == VALUE_STRING) {
        size_t add = strlen(node->as.string);
        if (*len + add + 1 > *cap) {
            size_t next = *cap ? *cap * 2 : 64;
            while (next < *len + add + 1) {
                next *= 2;
            }
            char *grown = realloc(*buf, next);
            if (!grown) {
                abort();
            }
            *buf = grown;
            *cap = next;
        }
        memcpy(*buf + *len, node->as.string, add);
        *len += add;
        (*buf)[*len] = '\0';
        return;
    }
    if (xml_is_element(node)) {
        Value *ch = xml_children_of(node);
        if (ch) {
            for (size_t i = 0; i < ch->as.array.count; i++) {
                xml_gather_text(&ch->as.array.items[i], buf, len, cap);
            }
        }
    }
}

static Value xml_do_text(Value *node) {
    if (node->kind != VALUE_STRING && !xml_is_element(node)) {
        return value_string("");
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    xml_gather_text(node, &buf, &len, &cap);
    const char *s = buf ? buf : "";
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    size_t end = strlen(s);
    while (end > 0 && isspace((unsigned char)s[end - 1])) {
        end--;
    }
    Value result = value_string_n(s, end);
    free(buf);
    return result;
}

static Value xml_do_attr(Value *node, const char *attr_name, int has_default, Value defval) {
    if (xml_is_element(node)) {
        RecordField *af = record_find(node, "attrs");
        if (af && af->value->kind == VALUE_RECORD) {
            RecordField *v = record_find(af->value, attr_name);
            if (v) {
                return value_copy(*v->value);
            }
        }
    }
    if (has_default) {
        return value_copy(defval);
    }
    return value_unknown();
}

/* ===== Encoding (xml_design.md §5): record tree -> XML string ============ */

static void xml_str_append(char **b, size_t *n, size_t *c, const char *s) {
    size_t add = strlen(s);
    if (*n + add + 1 > *c) {
        size_t next = *c ? *c * 2 : 128;
        while (next < *n + add + 1) {
            next *= 2;
        }
        char *grown = realloc(*b, next);
        if (!grown) {
            abort();
        }
        *b = grown;
        *c = next;
    }
    memcpy(*b + *n, s, add);
    *n += add;
    (*b)[*n] = '\0';
}

/* escape a run: text escapes & < > ; attribute values additionally escape " */
static void xml_append_escaped(char **b, size_t *n, size_t *c, const char *s, int is_attr) {
    for (; *s; s++) {
        if (*s == '&') {
            xml_str_append(b, n, c, "&amp;");
        } else if (*s == '<') {
            xml_str_append(b, n, c, "&lt;");
        } else if (*s == '>') {
            xml_str_append(b, n, c, "&gt;");
        } else if (*s == '"' && is_attr) {
            xml_str_append(b, n, c, "&quot;");
        } else {
            char t[2] = {*s, '\0'};
            xml_str_append(b, n, c, t);
        }
    }
}

static void xml_indent(char **b, size_t *n, size_t *c, int depth) {
    for (int i = 0; i < depth; i++) {
        xml_str_append(b, n, c, "  ");
    }
}

/* Write one element; returns 0 (and raises) on a malformed node record. */
static int xml_write_element(char **b, size_t *n, size_t *c, Value *elem, int depth, int pretty) {
    RecordField *nf = record_find(elem, "name");
    RecordField *cf = record_find(elem, "children");
    if (!nf || nf->value->kind != VALUE_STRING || !cf || cf->value->kind != VALUE_ARRAY) {
        runtime_error_raise("xml.encode: an element record needs a string name and a children list",
                            XML_ERROR_CODE, "xml");
        return 0;
    }
    /* tag: qname (as written) if present, else local name */
    const char *tag = nf->value->as.string;
    RecordField *qf = record_find(elem, "qname");
    if (qf && qf->value->kind == VALUE_STRING) {
        tag = qf->value->as.string;
    }

    xml_str_append(b, n, c, "<");
    xml_str_append(b, n, c, tag);

    RecordField *af = record_find(elem, "attrs");
    if (af && af->value->kind == VALUE_RECORD) {
        Value *attrs = af->value;
        for (size_t i = 0; i < attrs->as.record.count; i++) {
            RecordField *at = &attrs->as.record.fields[i];
            xml_str_append(b, n, c, " ");
            xml_str_append(b, n, c, at->name);
            xml_str_append(b, n, c, "=\"");
            /* builtin_string_value CONSUMES its argument, so stringify a copy */
            Value sv = builtin_string_value(value_copy(*at->value));
            xml_append_escaped(b, n, c, sv.kind == VALUE_STRING ? sv.as.string : "", 1);
            value_free(sv);
            xml_str_append(b, n, c, "\"");
        }
    }

    Value *children = cf->value;
    size_t ccount = children->as.array.count;
    if (ccount == 0) {
        xml_str_append(b, n, c, "/>");
        return 1;
    }

    int has_elem = 0;
    for (size_t i = 0; i < ccount; i++) {
        if (xml_is_element(&children->as.array.items[i])) {
            has_elem = 1;
            break;
        }
    }
    xml_str_append(b, n, c, ">");

    if (pretty && has_elem) {
        for (size_t i = 0; i < ccount; i++) {
            Value *ch = &children->as.array.items[i];
            if (xml_is_element(ch)) {
                xml_str_append(b, n, c, "\n");
                xml_indent(b, n, c, depth + 1);
                if (!xml_write_element(b, n, c, ch, depth + 1, pretty)) {
                    return 0;
                }
            } else if (ch->kind == VALUE_STRING && !xml_all_whitespace(ch->as.string)) {
                xml_str_append(b, n, c, "\n");
                xml_indent(b, n, c, depth + 1);
                xml_append_escaped(b, n, c, ch->as.string, 0);
            }
        }
        xml_str_append(b, n, c, "\n");
        xml_indent(b, n, c, depth);
    } else {
        for (size_t i = 0; i < ccount; i++) {
            Value *ch = &children->as.array.items[i];
            if (xml_is_element(ch)) {
                if (!xml_write_element(b, n, c, ch, depth + 1, pretty)) {
                    return 0;
                }
            } else if (ch->kind == VALUE_STRING) {
                xml_append_escaped(b, n, c, ch->as.string, 0);
            }
        }
    }
    xml_str_append(b, n, c, "</");
    xml_str_append(b, n, c, tag);
    xml_str_append(b, n, c, ">");
    return 1;
}

static Value xml_do_encode(Value *node, int pretty) {
    if (!xml_is_element(node)) {
        runtime_error_raise("xml.encode: node must be an element record",
                            XML_ERROR_CODE, "xml");
        return value_null();
    }
    char *buf = NULL;
    size_t n = 0, cap = 0;
    if (!xml_write_element(&buf, &n, &cap, node, 0, pretty)) {
        free(buf);
        return value_null();
    }
    Value result = value_string_n(buf ? buf : "", n);
    free(buf);
    return result;
}

/* ===== Streaming reader (xml_design.md §4) ===============================
 * A pull cursor over xmlTextReader. Memory is bounded by the current element's
 * depth, not file size. The handle is an opaque VALUE_XML_READER, refcounted so
 * it closes on scope cleanup; xml.close is explicit + idempotent; any use after
 * close is a structured error. skip_to/subtree are WP-XML-5. */

#define XML_READER_MAX_DEPTH 256

/* map an xmlReader node type to the event `kind`, or NULL to skip it */
static const char *xml_reader_kind(int node_type) {
    switch (node_type) {
    case XML_READER_TYPE_ELEMENT:
        return "element";
    case XML_READER_TYPE_END_ELEMENT:
        return "end";
    case XML_READER_TYPE_TEXT:
    case XML_READER_TYPE_CDATA:
        return "text";
    default:
        return NULL; /* comments, PIs, whitespace, doctype, ... are skipped */
    }
}

/* Build the §4 event record for the reader's current node. */
static Value xml_reader_event(xmlTextReaderPtr r, const char *kind) {
    Value ev = value_record(NULL, 0);
    record_set(&ev, "kind", value_string(kind));
    int depth = xmlTextReaderDepth(r);
    int line = xmlTextReaderGetParserLineNumber(r);
    record_set(&ev, "depth", value_number((double)depth));
    record_set(&ev, "line", value_number((double)line));

    if (strcmp(kind, "text") == 0) {
        const xmlChar *val = xmlTextReaderConstValue(r);
        record_set(&ev, "text", value_string(val ? (const char *)val : ""));
        return ev;
    }

    /* element / end: name, qname, ns */
    const xmlChar *local = xmlTextReaderConstLocalName(r);
    const xmlChar *qname = xmlTextReaderConstName(r);
    const xmlChar *ns = xmlTextReaderConstNamespaceUri(r);
    record_set(&ev, "name", value_string(local ? (const char *)local : ""));
    record_set(&ev, "qname", value_string(qname ? (const char *)qname : ""));
    if (ns) {
        record_set(&ev, "ns", value_string((const char *)ns));
    } else {
        record_set(&ev, "ns", value_null());
    }

    /* attrs on start-elements (namespace decls included, like the tree parser) */
    Value attrs = value_record(NULL, 0);
    if (strcmp(kind, "element") == 0 && xmlTextReaderHasAttributes(r)) {
        if (xmlTextReaderMoveToFirstAttribute(r) == 1) {
            do {
                const xmlChar *an = xmlTextReaderConstName(r);
                const xmlChar *av = xmlTextReaderConstValue(r);
                if (an) {
                    record_set(&attrs, (const char *)an,
                               value_string(av ? (const char *)av : ""));
                }
            } while (xmlTextReaderMoveToNextAttribute(r) == 1);
            xmlTextReaderMoveToElement(r);
        }
    }
    record_set(&ev, "attrs", attrs);
    return ev;
}

/* Get the reader handle from an argument, or raise. Sets *out and returns 1,
 * else raises and returns 0. Rejects use-after-close. */
static int xml_reader_arg(Value *v, XmlReaderValue **out, const char *fn) {
    if (v->kind != VALUE_XML_READER) {
        char m[64];
        snprintf(m, sizeof(m), "xml.%s expects an xml reader", fn);
        runtime_error_raise(m, XML_ERROR_CODE, "xml");
        return 0;
    }
    XmlReaderValue *rv = v->as.xml_reader;
    if (rv->closed || !rv->reader) {
        char m[64];
        snprintf(m, sizeof(m), "xml.%s on a closed reader", fn);
        runtime_error_raise(m, XML_ERROR_CODE, "xml");
        return 0;
    }
    *out = rv;
    return 1;
}

static Value xml_eval_reader(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        runtime_error_raise("xml.reader expects one argument", XML_ERROR_CODE, "xml");
        return value_null();
    }
    Value pathv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(pathv);
        return value_null();
    }
    const char *path = NULL;
    if (pathv.kind == VALUE_STRING) {
        path = pathv.as.string;
    } else if (pathv.kind == VALUE_FILE) {
        path = pathv.as.file_path;
    } else {
        value_free(pathv);
        runtime_error_raise("xml.reader expects a path string or file reference",
                            XML_ERROR_CODE, "xml");
        return value_null();
    }
    xml_install_silent_errors();
    xmlTextReaderPtr reader = xmlReaderForFile(path, NULL, XML_SECURE_OPTS);
    if (!reader) {
        char m[512];
        snprintf(m, sizeof(m), "xml.reader: could not open '%s' as XML", path);
        value_free(pathv);
        runtime_error_raise(m, XML_ERROR_CODE, "xml");
        return value_null();
    }
    value_free(pathv);
    XmlReaderValue *rv = calloc(1, sizeof(XmlReaderValue));
    if (!rv) {
        xmlFreeTextReader(reader);
        abort();
    }
    rv->reader = reader;
    rv->ref_count = 1;
    rv->closed = 0;
    return value_xml_reader(rv);
}

static Value xml_eval_read(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        runtime_error_raise("xml.read expects one argument", XML_ERROR_CODE, "xml");
        return value_null();
    }
    Value rvv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(rvv);
        return value_null();
    }
    XmlReaderValue *rv;
    if (!xml_reader_arg(&rvv, &rv, "read")) {
        value_free(rvv);
        return value_null();
    }
    /* advance until a meaningful node (element/end/text) or end-of-document */
    while (1) {
        int rc = xmlTextReaderRead(rv->reader);
        if (rc < 0) {
            value_free(rvv);
            xml_raise_from_ctxt(NULL, "xml.read: parse error");
            /* xmlTextReader errors go through the structured handler; surface a
             * generic message if none was captured */
            return value_null();
        }
        if (rc == 0) {
            value_free(rvv);
            return value_null(); /* end of document -> nothing */
        }
        if (xmlTextReaderDepth(rv->reader) > XML_READER_MAX_DEPTH) {
            value_free(rvv);
            char m[96];
            snprintf(m, sizeof(m), "xml.read: maximum nesting depth (%d) exceeded",
                     XML_READER_MAX_DEPTH);
            runtime_error_raise(m, XML_ERROR_CODE, "xml");
            return value_null();
        }
        const char *kind = xml_reader_kind(xmlTextReaderNodeType(rv->reader));
        if (!kind) {
            continue;
        }
        /* skip whitespace-only text nodes (data-XML default, matches the tree) */
        if (strcmp(kind, "text") == 0) {
            const xmlChar *val = xmlTextReaderConstValue(rv->reader);
            if (val && xml_all_whitespace((const char *)val)) {
                continue;
            }
        }
        Value ev = xml_reader_event(rv->reader, kind);
        value_free(rvv);
        return ev;
    }
}

static Value xml_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        runtime_error_raise("xml.close expects one argument", XML_ERROR_CODE, "xml");
        return value_null();
    }
    Value rvv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(rvv);
        return value_null();
    }
    if (rvv.kind != VALUE_XML_READER) {
        value_free(rvv);
        runtime_error_raise("xml.close expects an xml reader", XML_ERROR_CODE, "xml");
        return value_null();
    }
    /* idempotent: closing an already-closed reader is a no-op */
    XmlReaderValue *rv = rvv.as.xml_reader;
    if (!rv->closed && rv->reader) {
        xmlFreeTextReader(rv->reader);
        rv->reader = NULL;
        rv->closed = 1;
    }
    value_free(rvv);
    return value_null();
}

/* xml.skip_to(reader, name) — advance the cursor to the next start-element whose
 * local name equals `name`; returns true when positioned on one, false at
 * end-of-document. The current node is checked BEFORE reading forward, so the
 * windowing loop `while xml.skip_to(r, X): subtree` handles adjacent <X> siblings
 * correctly (subtree leaves the cursor on the node past the just-read element;
 * if that node is itself an <X>, skip_to reports it rather than stepping over). */
static Value xml_eval_skip_to(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        runtime_error_raise("xml.skip_to expects two arguments", XML_ERROR_CODE, "xml");
        return value_null();
    }
    Value rvv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(rvv);
        return value_null();
    }
    Value namev = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(rvv);
        value_free(namev);
        return value_null();
    }
    if (namev.kind != VALUE_STRING) {
        value_free(rvv);
        value_free(namev);
        runtime_error_raise("xml.skip_to name must be a string", XML_ERROR_CODE, "xml");
        return value_null();
    }
    XmlReaderValue *rv;
    if (!xml_reader_arg(&rvv, &rv, "skip_to")) {
        value_free(rvv);
        value_free(namev);
        return value_null();
    }
    const char *target = namev.as.string;
    int found = 0;
    while (1) {
        if (xmlTextReaderNodeType(rv->reader) == XML_READER_TYPE_ELEMENT) {
            const xmlChar *local = xmlTextReaderConstLocalName(rv->reader);
            if (local && strcmp((const char *)local, target) == 0) {
                found = 1;
                break;
            }
        }
        int rc = xmlTextReaderRead(rv->reader);
        if (rc < 0) {
            value_free(rvv);
            value_free(namev);
            xml_raise_from_ctxt(NULL, "xml.skip_to: parse error");
            return value_null();
        }
        if (rc == 0) {
            found = 0; /* end of document */
            break;
        }
        if (xmlTextReaderDepth(rv->reader) > XML_READER_MAX_DEPTH) {
            value_free(rvv);
            value_free(namev);
            char m[96];
            snprintf(m, sizeof(m), "xml.skip_to: maximum nesting depth (%d) exceeded",
                     XML_READER_MAX_DEPTH);
            runtime_error_raise(m, XML_ERROR_CODE, "xml");
            return value_null();
        }
    }
    value_free(rvv);
    value_free(namev);
    return value_bool(found);
}

/* xml.subtree(reader) — materialize the element under the cursor (which MUST be a
 * start-element) into a §2 node record, then advance past it. Memory is bounded
 * by the subtree, not the whole document: xmlTextReaderExpand reads only the
 * current element's descendants into a transient DOM owned by the reader, which
 * we copy into a Value before xmlTextReaderNext frees it. */
static Value xml_eval_subtree(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        runtime_error_raise("xml.subtree expects one argument", XML_ERROR_CODE, "xml");
        return value_null();
    }
    Value rvv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(rvv);
        return value_null();
    }
    XmlReaderValue *rv;
    if (!xml_reader_arg(&rvv, &rv, "subtree")) {
        value_free(rvv);
        return value_null();
    }
    if (xmlTextReaderNodeType(rv->reader) != XML_READER_TYPE_ELEMENT) {
        value_free(rvv);
        runtime_error_raise("xml.subtree requires the cursor on a start element",
                            XML_ERROR_CODE, "xml");
        return value_null();
    }
    xmlNodePtr node = xmlTextReaderExpand(rv->reader);
    if (!node) {
        value_free(rvv);
        xml_raise_from_ctxt(NULL, "xml.subtree: could not materialize the current element");
        return value_null();
    }
    Value out;
    if (!xml_element_to_record(node, 0, 0, &out)) {
        /* xml_element_to_record already raised a structured error (e.g. depth) */
        value_free(rvv);
        return value_null();
    }
    /* advance the cursor past the just-materialized subtree */
    int rc = xmlTextReaderNext(rv->reader);
    if (rc < 0) {
        value_free(out);
        value_free(rvv);
        xml_raise_from_ctxt(NULL, "xml.subtree: parse error advancing past the element");
        return value_null();
    }
    value_free(rvv);
    return out;
}

static Value xml_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    size_t argc = expr->as.call.args.count;

    if (strcmp(name, "parse") == 0) {
        if (argc < 1 || argc > 2) {
            runtime_error_raise("xml.parse expects one or two arguments",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        if (text.kind != VALUE_STRING) {
            value_free(text);
            runtime_error_raise("xml.parse expects a string", XML_ERROR_CODE, "xml");
            return value_null();
        }
        int keep_space = 0;
        if (argc == 2) {
            Value ks = eval_expr(expr->as.call.args.items[1]);
            if (error_action_pending()) {
                value_free(text);
                value_free(ks);
                return value_null();
            }
            keep_space = value_truthy(ks);
            value_free(ks);
        }
        Value result = xml_parse_memory(text.as.string, strlen(text.as.string), keep_space);
        value_free(text);
        return result;
    }

    if (strcmp(name, "parse_html") == 0) {
        if (argc != 1) {
            runtime_error_raise("xml.parse_html expects one argument",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        if (text.kind != VALUE_STRING) {
            value_free(text);
            runtime_error_raise("xml.parse_html expects a string", XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value result = xml_parse_html_memory(text.as.string, strlen(text.as.string));
        value_free(text);
        return result;
    }

    if (strcmp(name, "parse_file") == 0) {
        if (argc != 1) {
            runtime_error_raise("xml.parse_file expects one argument",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value pathv = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(pathv);
            return value_null();
        }
        const char *path = NULL;
        if (pathv.kind == VALUE_STRING) {
            path = pathv.as.string;
        } else if (pathv.kind == VALUE_FILE) {
            path = pathv.as.file_path;
        } else {
            value_free(pathv);
            runtime_error_raise("xml.parse_file expects a path string or file reference",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value result = xml_parse_path(path, 0);
        value_free(pathv);
        return result;
    }

    if (strcmp(name, "find") == 0 || strcmp(name, "find_all") == 0) {
        if (argc != 2) {
            char m[64];
            snprintf(m, sizeof(m), "xml.%s expects two arguments", name);
            runtime_error_raise(m, XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value node = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(node);
            return value_null();
        }
        Value path = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(node);
            value_free(path);
            return value_null();
        }
        if (path.kind != VALUE_STRING) {
            value_free(node);
            value_free(path);
            char m[64];
            snprintf(m, sizeof(m), "xml.%s path must be a string", name);
            runtime_error_raise(m, XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value result = (strcmp(name, "find") == 0)
                           ? xml_do_find(&node, path.as.string)
                           : xml_do_find_all(&node, path.as.string);
        value_free(node);
        value_free(path);
        return result;
    }

    if (strcmp(name, "text") == 0) {
        if (argc != 1) {
            runtime_error_raise("xml.text expects one argument", XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value node = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(node);
            return value_null();
        }
        Value result = xml_do_text(&node);
        value_free(node);
        return result;
    }

    if (strcmp(name, "attr") == 0) {
        if (argc < 2 || argc > 3) {
            runtime_error_raise("xml.attr expects two or three arguments",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value node = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(node);
            return value_null();
        }
        Value aname = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(node);
            value_free(aname);
            return value_null();
        }
        if (aname.kind != VALUE_STRING) {
            value_free(node);
            value_free(aname);
            runtime_error_raise("xml.attr name must be a string", XML_ERROR_CODE, "xml");
            return value_null();
        }
        int has_default = (argc == 3);
        Value defval = value_null();
        if (has_default) {
            defval = eval_expr(expr->as.call.args.items[2]);
            if (error_action_pending()) {
                value_free(node);
                value_free(aname);
                value_free(defval);
                return value_null();
            }
        }
        Value result = xml_do_attr(&node, aname.as.string, has_default, defval);
        value_free(node);
        value_free(aname);
        if (has_default) {
            value_free(defval);
        }
        return result;
    }

    if (strcmp(name, "encode") == 0) {
        if (argc < 1 || argc > 2) {
            runtime_error_raise("xml.encode expects one or two arguments",
                                XML_ERROR_CODE, "xml");
            return value_null();
        }
        Value node = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(node);
            return value_null();
        }
        int pretty = 0;
        if (argc == 2) {
            Value p = eval_expr(expr->as.call.args.items[1]);
            if (error_action_pending()) {
                value_free(node);
                value_free(p);
                return value_null();
            }
            pretty = value_truthy(p);
            value_free(p);
        }
        Value result = xml_do_encode(&node, pretty);
        value_free(node);
        return result;
    }

    if (strcmp(name, "reader") == 0) {
        return xml_eval_reader(expr);
    }
    if (strcmp(name, "read") == 0) {
        return xml_eval_read(expr);
    }
    if (strcmp(name, "skip_to") == 0) {
        return xml_eval_skip_to(expr);
    }
    if (strcmp(name, "subtree") == 0) {
        return xml_eval_subtree(expr);
    }
    if (strcmp(name, "close") == 0) {
        return xml_eval_close(expr);
    }

    char m[128];
    snprintf(m, sizeof(m), "unknown xml function: %s", name);
    runtime_error_raise(m, XML_ERROR_CODE, "xml");
    return value_null();
}

#endif /* HAVE_LIBXML2 */
