/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * ldap module (docs/ldap_design.md) -- bind and search, and nothing else.
 * The gBASIC layer above it (DN construction, memberOf walking, group-name
 * normalisation) is deliberately NOT here: those are policy and belong to the
 * application, which is the design's §7 and the reason this stays bounded.
 *
 * IT IS AN AUTHENTICATION PATH, which decides three things.
 *
 * `security` is REQUIRED with no default, so a cleartext bind is a word
 * somebody typed and is greppable in a configuration review rather than the
 * consequence of an omitted field.
 *
 * REFERRAL CHASING IS OFF AND CANNOT BE TURNED ON. A referral is the server
 * telling the client to go and ask a different server; on this path that is an
 * instruction to send credentials somewhere the operator never named.
 *
 * BIND REPORTS FAILURE AS A VALUE. "Wrong password" and "the directory is
 * unreachable" are both ordinary here, and they must not be the same outcome:
 * the first is shown to a user, the second is an operator's problem and must
 * never reach a viewer as a bad password. A caller has to read `reason` to
 * learn anything, so the two cannot be conflated by accident.
 *
 * Translation-unit include, following modules/smtp.c. Guarded by HAVE_LDAP.
 */

#if HAVE_LDAP

static void ldap_raise(const char *message) {
    runtime_error_raise(message, LDAP_ERROR_CODE, "ldap");
}

/* Map a libldap result to the reason vocabulary the design fixes. The point is
 * that a caller can branch on it without parsing English.
 *
 * TLS FAILURE AND NETWORK FAILURE ARE THE SAME RESULT CODE, and telling them
 * apart matters here: a bad certificate is a configuration problem an operator
 * must be told about, while an unreachable host may just be a restart. libldap
 * returns LDAP_SERVER_DOWN ("Can't contact LDAP server") for both, and the only
 * thing that separates them is the DIAGNOSTIC, which carries the OpenSSL error
 * for a TLS failure and is NULL for a refused connection. Measured, both ways,
 * before this was written.
 *
 * That makes the detection a string test, which is fragile in the usual way --
 * it depends on OpenSSL's wording. It is used only to pick between two reasons
 * that are both failures, so the worst case is a TLS problem reported as
 * `unreachable`, never a failure reported as success. */
static const char *ldap_reason_for(int rc, const char *diag) {
    switch (rc) {
    case LDAP_SUCCESS:              return "";
    case LDAP_INVALID_CREDENTIALS:
    case LDAP_INAPPROPRIATE_AUTH:
    case LDAP_INVALID_DN_SYNTAX:    return "invalid_credentials";
    case LDAP_SERVER_DOWN:
    case LDAP_CONNECT_ERROR:
        if (diag && (strstr(diag, "SSL") || strstr(diag, "TLS")
                     || strstr(diag, "certificate"))) {
            return "tls_failed";
        }
        return "unreachable";
    case LDAP_UNAVAILABLE:
    case LDAP_BUSY:                 return "unreachable";
    case LDAP_TIMEOUT:
    case LDAP_TIMELIMIT_EXCEEDED:   return "timeout";
    default:                        return "server_error";
    }
}

static LdapConnectionValue *ldap_conn_arg(AstExpr *expr, size_t index,
                                          const char *what) {
    Value v = eval_expr(expr->as.call.args.items[index]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
    if (v.kind != VALUE_LDAP_CONNECTION) {
        value_free(v);
        ldap_raise(what);
        return NULL;
    }
    LdapConnectionValue *c = v.as.ldap_connection;
    if (c->closed) {
        value_free(v);
        ldap_raise("ldap: this connection is closed");
        return NULL;
    }
    /* The Value is a borrow: the record still owns the refcount. */
    value_free(v);
    return c;
}

static int ldap_record_string(Value *record, const char *field, char **out,
                              const char *missing) {
    *out = NULL;
    RecordField *f = record_find(record, field);
    if (!f || !f->value || f->value->kind == VALUE_UNKNOWN
        || f->value->kind == VALUE_NULL) {
        if (missing) {
            ldap_raise(missing);
            return 0;
        }
        return 1;
    }
    if (f->value->kind != VALUE_STRING) {
        ldap_raise(missing ? missing : "ldap: expected a string");
        return 0;
    }
    *out = copy_string(f->value->as.string);
    return 1;
}

/* A borrowed field, or NULL. */
static Value *ldap_field(Value *record, const char *name) {
    RecordField *f = record_find(record, name);
    return (f && f->value) ? f->value : NULL;
}

static Value ldap_bind_result(int ok, const char *reason, int code,
                              const char *message) {
    Value r = value_record(NULL, 0);
    record_set(&r, "ok", value_bool(ok));
    record_set(&r, "reason", value_string(reason ? reason : ""));
    record_set(&r, "code", value_number(code));
    record_set(&r, "message", value_string(message ? message : ""));
    return r;
}

/* --- connect -------------------------------------------------------------- */

static Value ldap_eval_connect(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        ldap_raise("ldap.connect expects one configuration record");
        return value_null();
    }
    Value config = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(config);
        return value_null();
    }
    if (config.kind != VALUE_RECORD) {
        value_free(config);
        ldap_raise("ldap.connect expects a configuration record");
        return value_null();
    }

    char *host = NULL, *security = NULL, *ca_file = NULL;
    if (!ldap_record_string(&config, "host", &host,
                            "ldap.connect needs a host")) {
        value_free(config);
        return value_null();
    }
    /* THE DECLARED CHOICE. No default: see docs/ldap_design.md §3. */
    if (!ldap_record_string(&config, "security", &security,
                            "ldap.connect needs a security of \"ldaps\","
                            " \"starttls\" or \"plain\" -- there is no default,"
                            " because a default would make a cleartext bind"
                            " either silent or accidental")) {
        free(host);
        value_free(config);
        return value_null();
    }
    int is_ldaps = strcmp(security, "ldaps") == 0;
    int is_starttls = strcmp(security, "starttls") == 0;
    int is_plain = strcmp(security, "plain") == 0;
    if (!is_ldaps && !is_starttls && !is_plain) {
        free(host); free(security);
        value_free(config);
        ldap_raise("ldap.connect: security must be \"ldaps\", \"starttls\""
                   " or \"plain\"");
        return value_null();
    }

    Value *port_v = ldap_field(&config, "port");
    long port = is_ldaps ? 636 : 389;
    if (port_v && port_v->kind == VALUE_NUMBER) {
        port = (long)port_v->as.number;
    }

    /* Verification is ON unless explicitly turned off. */
    int verify = 1;
    Value *verify_v = ldap_field(&config, "verify");
    if (verify_v && verify_v->kind == VALUE_BOOL) {
        verify = verify_v->as.boolean;
    } else if (verify_v && verify_v->kind != VALUE_UNKNOWN
               && verify_v->kind != VALUE_NULL) {
        free(host); free(security);
        value_free(config);
        ldap_raise("ldap.connect: verify must be true or false");
        return value_null();
    }
    if (!ldap_record_string(&config, "ca_file", &ca_file, NULL)) {
        free(host); free(security);
        value_free(config);
        return value_null();
    }

    Value *timeout_v = ldap_field(&config, "timeout");
    double timeout = 10.0;
    if (timeout_v && timeout_v->kind == VALUE_NUMBER && timeout_v->as.number > 0) {
        timeout = timeout_v->as.number;
    }
    value_free(config);

    char uri[1024];
    snprintf(uri, sizeof(uri), "%s://%s:%ld",
             is_ldaps ? "ldaps" : "ldap", host, port);

    LDAP *ld = NULL;
    int rc = ldap_initialize(&ld, uri);
    if (rc != LDAP_SUCCESS || !ld) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ldap.connect could not initialize %s: %s",
                 uri, ldap_err2string(rc));
        free(host); free(security); free(ca_file);
        ldap_raise(msg);
        return value_null();
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    /* Not configurable. See the header comment and design §4. */
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    struct timeval tv;
    tv.tv_sec = (time_t)timeout;
    tv.tv_usec = (suseconds_t)((timeout - (double)tv.tv_sec) * 1000000.0);
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);
    ldap_set_option(ld, LDAP_OPT_TIMEOUT, &tv);

    int require = verify ? LDAP_OPT_X_TLS_HARD : LDAP_OPT_X_TLS_NEVER;
    ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &require);
    if (ca_file && ca_file[0]) {
        ldap_set_option(ld, LDAP_OPT_X_TLS_CACERTFILE, ca_file);
    }
    /* OpenLDAP caches TLS context per handle; force a fresh one so the options
     * above are the ones actually used. */
    int newctx = 0;
    ldap_set_option(ld, LDAP_OPT_X_TLS_NEWCTX, &newctx);

    /* A STARTTLS FAILURE IS RECORDED, NOT RAISED, and that is a correction.
     *
     * It used to raise from `connect` while an LDAPS failure surfaced as a
     * VALUE from `bind` -- the same condition (TLS could not be established)
     * behaving differently depending on which security mode was declared, with
     * the documented contract ("bind reports failure as a value") holding for
     * two modes out of three.
     *
     * The consequence was not cosmetic. gdash reported it from a real
     * directory: a caller who guards `bind`, as the documentation steers them
     * to, takes the raise -- and a raise inside a web handler kills the worker
     * under the let-it-crash rule, so an intranet directory with an expired
     * certificate would crash a worker on EVERY login attempt instead of
     * showing "sign-in is unavailable".
     *
     * So the failure is carried on the handle and answered by `bind`, where
     * every other operational failure is answered. It also makes `tls_failed`
     * reachable from StartTLS, which it previously documented and could not
     * produce. */
    char *fail_reason = NULL, *fail_message = NULL;
    int fail_code = 0;
    if (is_starttls) {
        rc = ldap_start_tls_s(ld, NULL, NULL);
        if (rc != LDAP_SUCCESS) {
            char msg[512];
            char *diag = NULL;
            ldap_get_option(ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &diag);
            snprintf(msg, sizeof(msg), "StartTLS failed against %s: %s%s%s",
                     uri, ldap_err2string(rc),
                     (diag && diag[0]) ? " -- " : "",
                     (diag && diag[0]) ? diag : "");
            fail_reason = copy_string("tls_failed");
            fail_message = copy_string(msg);
            fail_code = rc;
            if (diag) ldap_memfree(diag);
        }
    }

    free(host); free(security); free(ca_file);

    LdapConnectionValue *conn = calloc(1, sizeof(LdapConnectionValue));
    if (!conn) {
        ldap_unbind_ext_s(ld, NULL, NULL);
        abort();
    }
    conn->ld = ld;
    conn->ref_count = 1;
    conn->fail_reason = fail_reason;
    conn->fail_message = fail_message;
    conn->fail_code = fail_code;
    return value_ldap_connection(conn);
}

/* --- bind ----------------------------------------------------------------- */

static Value ldap_eval_bind(AstExpr *expr) {
    if (expr->as.call.args.count != 3) {
        ldap_raise("ldap.bind expects a connection, a DN and a password");
        return value_null();
    }
    LdapConnectionValue *conn =
        ldap_conn_arg(expr, 0, "ldap.bind expects a connection");
    if (!conn) {
        return value_null();
    }
    Value dn_v = eval_expr(expr->as.call.args.items[1]);
    Value pw_v = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) {
        value_free(dn_v); value_free(pw_v);
        return value_null();
    }
    if (dn_v.kind != VALUE_STRING || pw_v.kind != VALUE_STRING) {
        value_free(dn_v); value_free(pw_v);
        ldap_raise("ldap.bind expects the DN and password as strings");
        return value_null();
    }

    /* A connect-time TLS failure is answered here, so every operational
     * failure of every security mode arrives by the same route. */
    if (conn->fail_reason) {
        value_free(dn_v); value_free(pw_v);
        return ldap_bind_result(0, conn->fail_reason, conn->fail_code,
                                conn->fail_message ? conn->fail_message : "");
    }

    /* An empty password is an UNAUTHENTICATED bind in LDAP: the server answers
     * success and the caller believes the credentials were checked. Refused
     * here rather than passed on, because it is the classic way an LDAP login
     * ends up accepting everyone. */
    if (pw_v.as.string[0] == '\0') {
        /* Its OWN reason, not `invalid_credentials`. Both fail closed, but a
         * caller cannot otherwise tell "I passed an empty password" -- a bug in
         * their code -- from "the directory rejected these credentials".
         * Reported by gdash against a real directory. */
        value_free(dn_v); value_free(pw_v);
        return ldap_bind_result(0, "empty_password",
                                LDAP_INVALID_CREDENTIALS,
                                "an empty password is an unauthenticated bind,"
                                " which the directory would answer with success");
    }

    struct berval cred;
    cred.bv_val = pw_v.as.string;
    cred.bv_len = string_length(pw_v.as.string);
    struct berval *server_cred = NULL;

    int rc = ldap_sasl_bind_s(conn->ld, dn_v.as.string, LDAP_SASL_SIMPLE,
                              &cred, NULL, NULL, &server_cred);
    if (server_cred) {
        ber_bvfree(server_cred);
    }
    /* The password value is freed immediately and never reaches a message. */
    value_free(pw_v);

    char *diag = NULL;
    ldap_get_option(conn->ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &diag);
    Value out;
    if (rc == LDAP_SUCCESS) {
        conn->bound = 1;
        out = ldap_bind_result(1, "", rc, "");
    } else {
        out = ldap_bind_result(0, ldap_reason_for(rc, diag), rc,
                               (diag && diag[0]) ? diag : ldap_err2string(rc));
    }
    if (diag) {
        ldap_memfree(diag);
    }
    value_free(dn_v);
    return out;
}

/* --- search --------------------------------------------------------------- */

static int ldap_scope_of(const char *s, int *out) {
    if (strcmp(s, "base") == 0)     { *out = LDAP_SCOPE_BASE;     return 1; }
    if (strcmp(s, "one") == 0)      { *out = LDAP_SCOPE_ONELEVEL; return 1; }
    if (strcmp(s, "sub") == 0)      { *out = LDAP_SCOPE_SUBTREE;  return 1; }
    return 0;
}

static Value ldap_eval_search(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        ldap_raise("ldap.search expects a connection and a spec record");
        return value_null();
    }
    LdapConnectionValue *conn =
        ldap_conn_arg(expr, 0, "ldap.search expects a connection");
    if (!conn) {
        return value_null();
    }
    if (conn->fail_reason) {
        char msg[640];
        snprintf(msg, sizeof(msg), "ldap.search: this connection never came up: %s",
                 conn->fail_message ? conn->fail_message : conn->fail_reason);
        ldap_raise(msg);
        return value_null();
    }
    Value spec = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(spec);
        return value_null();
    }
    if (spec.kind != VALUE_RECORD) {
        value_free(spec);
        ldap_raise("ldap.search expects a spec record");
        return value_null();
    }

    char *base = NULL, *scope_s = NULL, *filter = NULL;
    if (!ldap_record_string(&spec, "base", &base, "ldap.search needs a base DN")
        || !ldap_record_string(&spec, "scope", &scope_s,
                               "ldap.search needs a scope of \"base\","
                               " \"one\" or \"sub\"")
        || !ldap_record_string(&spec, "filter", &filter,
                               "ldap.search needs a filter")) {
        free(base); free(scope_s); free(filter);
        value_free(spec);
        return value_null();
    }
    int scope = LDAP_SCOPE_SUBTREE;
    if (!ldap_scope_of(scope_s, &scope)) {
        free(base); free(scope_s); free(filter);
        value_free(spec);
        ldap_raise("ldap.search: scope must be \"base\", \"one\" or \"sub\"");
        return value_null();
    }

    char **attrs = NULL;
    size_t attr_count = 0;
    Value *attrs_v = ldap_field(&spec, "attributes");
    if (attrs_v && attrs_v->kind == VALUE_ARRAY) {
        size_t n = attrs_v->as.array.store ? attrs_v->as.array.store->count : 0;
        attrs = calloc(n + 1, sizeof(char *));
        for (size_t i = 0; i < n; i++) {
            Value *a = &attrs_v->as.array.store->items[i];
            if (a->kind != VALUE_STRING) {
                for (size_t k = 0; k < attr_count; k++) free(attrs[k]);
                free(attrs);
                value_free(spec);
                free(base); free(scope_s); free(filter);
                ldap_raise("ldap.search: attributes must be strings");
                return value_null();
            }
            attrs[attr_count++] = copy_string(a->as.string);
        }
    }

    Value *limit_v = ldap_field(&spec, "limit");
    int limit = 0;
    if (limit_v && limit_v->kind == VALUE_NUMBER && limit_v->as.number > 0) {
        limit = (int)limit_v->as.number;
    }
    value_free(spec);

    LDAPMessage *res = NULL;
    int rc = ldap_search_ext_s(conn->ld, base, scope, filter, attrs, 0,
                               NULL, NULL, NULL, limit, &res);
    for (size_t k = 0; k < attr_count; k++) free(attrs[k]);
    free(attrs);
    free(base); free(scope_s); free(filter);

    if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ldap.search failed: %s (%s)",
                 ldap_err2string(rc), ldap_reason_for(rc, NULL));
        if (res) ldap_msgfree(res);
        ldap_raise(msg);
        return value_null();
    }

    Value out = value_array(NULL, 0);
    for (LDAPMessage *e = ldap_first_entry(conn->ld, res); e;
         e = ldap_next_entry(conn->ld, e)) {
        Value entry = value_record(NULL, 0);
        char *dn = ldap_get_dn(conn->ld, e);
        record_set(&entry, "dn", value_string(dn ? dn : ""));
        if (dn) ldap_memfree(dn);

        Value attributes = value_record(NULL, 0);
        BerElement *ber = NULL;
        for (char *a = ldap_first_attribute(conn->ld, e, &ber); a;
             a = ldap_next_attribute(conn->ld, e, ber)) {
            struct berval **vals = ldap_get_values_len(conn->ld, e, a);
            /* ALWAYS an array, even for one value: see design §6. */
            Value list = value_array(NULL, 0);
            for (int i = 0; vals && vals[i]; i++) {
                /* `append_to_array_ref` mutates in place AND returns an OWNED
                 * copy, so the return has to be freed -- assigning it reads
                 * like a fluent API and leaks a reference per element. Found
                 * by valgrind, not by reading. */
                value_free(append_to_array_ref(&list,
                        value_string_n(vals[i]->bv_val, vals[i]->bv_len), 0));
            }
            if (vals) ldap_value_free_len(vals);
            record_set(&attributes, a, list);
            ldap_memfree(a);
        }
        if (ber) ber_free(ber, 0);
        record_set(&entry, "attributes", attributes);
        value_free(append_to_array_ref(&out, entry, 0));
    }
    if (res) ldap_msgfree(res);
    return out;
}

/* --- close ---------------------------------------------------------------- */

static Value ldap_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        ldap_raise("ldap.close expects one argument");
        return value_null();
    }
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return value_null();
    }
    if (v.kind != VALUE_LDAP_CONNECTION) {
        value_free(v);
        ldap_raise("ldap.close expects a connection");
        return value_null();
    }
    LdapConnectionValue *c = v.as.ldap_connection;
    if (!c->closed) {
        if (c->ld) {
            ldap_unbind_ext_s(c->ld, NULL, NULL);
            c->ld = NULL;
        }
        c->closed = 1;
    }
    value_free(v);
    return value_bool(1);
}

static Value ldap_eval_call(AstExpr *expr) {
    const char *fn = expr->as.call.name;
    if (strcmp(fn, "connect") == 0) return ldap_eval_connect(expr);
    if (strcmp(fn, "bind") == 0)    return ldap_eval_bind(expr);
    if (strcmp(fn, "search") == 0)  return ldap_eval_search(expr);
    if (strcmp(fn, "close") == 0)   return ldap_eval_close(expr);
    char msg[256];
    snprintf(msg, sizeof(msg), "unknown ldap function: %s", fn);
    ldap_raise(msg);
    return value_null();
}

#endif /* HAVE_LDAP */
