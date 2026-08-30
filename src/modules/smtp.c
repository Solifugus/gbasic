/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * smtp module (docs/mail_design.md) -- the SMTP conversation, and nothing
 * above it. Composition lives in stdlib/mail.bas, which is pure gBASIC and so
 * testable with no network at all; this file owns the envelope, TLS, AUTH and
 * -- critically -- the DATA framing, because framing is a property of the wire
 * and not of the message.
 *
 * CRLF normalization is this layer's job: SMTP lines end \r\n, the composer
 * works in ordinary text with \n, and this normalizes on the way out so an
 * author never has to know. It is also load-bearing for the NEXT rule.
 *
 * DOT-STUFFING IS LIBCURL'S, NOT OURS, AND THAT WAS MEASURED. A line that is
 * exactly "." ends the DATA phase, so a body containing one would be truncated
 * there with the server reporting success and nothing raising anywhere.
 * libcurl escapes the end-of-block sequence itself; stuffing here as well put
 * THREE dots on the wire where the body had one, which the receiver un-stuffs
 * to two -- a corrupted message that is delivered, accepted and plausible.
 * Found by reading the sink's wire capture, not by reading the docs. What
 * curl looks for is the CRLF-framed sequence, which is why normalization here
 * has to happen and has to happen first.
 *
 * Translation-unit include, following modules/xml.c: it uses the static Value
 * API defined above the include point. Guarded internally by HAVE_LIBCURL.
 */

#if HAVE_LIBCURL

typedef struct {
    char *data;
    size_t length;
    size_t offset;
} SmtpPayload;

static void smtp_raise(const char *message) {
    runtime_error_raise(message, SMTP_ERROR_CODE, "smtp");
}

/* One pass: any of \r\n, lone \n and lone \r becomes \r\n. Nothing else --
 * see the note above about who does the dot-stuffing. */
static char *smtp_frame(const char *text, size_t length, size_t *out_length) {
    size_t cap = length * 2 + 8;   /* worst case: every byte is a lone LF */
    char *out = malloc(cap);
    if (!out) {
        return NULL;
    }
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        char ch = text[i];
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < length && text[i + 1] == '\n') {
                i++;
            }
            out[n++] = '\r';
            out[n++] = '\n';
            continue;
        }
        out[n++] = ch;
    }
    /* The payload must end on a complete line or the terminating <CRLF>.<CRLF>
     * would attach itself to whatever the last line was. */
    if (n < 2 || out[n - 2] != '\r' || out[n - 1] != '\n') {
        out[n++] = '\r';
        out[n++] = '\n';
    }
    *out_length = n;
    return out;
}

/* curl reports "RCPT failed: 550" and stops there, which with five recipients
 * does not say WHICH one or why. The server said both. Keep the most recent
 * 4xx/5xx reply line so the raise can carry the relay's own words -- an
 * undiagnosable failed notification is nearly as useless as none. */
typedef struct {
    char last_refusal[512];
} SmtpTrace;

static int smtp_debug_callback(CURL *handle, curl_infotype type,
                               char *data, size_t size, void *userdata) {
    (void)handle;
    SmtpTrace *trace = (SmtpTrace *)userdata;
    if (type != CURLINFO_HEADER_IN || size == 0) {
        return 0;
    }
    if (data[0] != '4' && data[0] != '5') {
        return 0;
    }
    size_t n = size;
    while (n > 0 && (data[n - 1] == '\r' || data[n - 1] == '\n')) {
        n--;
    }
    if (n >= sizeof(trace->last_refusal)) {
        n = sizeof(trace->last_refusal) - 1;
    }
    memcpy(trace->last_refusal, data, n);
    trace->last_refusal[n] = '\0';
    return 0;
}

static size_t smtp_read_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    SmtpPayload *payload = (SmtpPayload *)userdata;
    size_t room = size * nitems;
    size_t left = payload->length - payload->offset;
    size_t take = left < room ? left : room;
    if (take == 0) {
        return 0;
    }
    memcpy(buffer, payload->data + payload->offset, take);
    payload->offset += take;
    return take;
}

/* The envelope's own boundary. A CR, LF or NUL in an address is the same
 * defect as header injection one layer up, expressed in SMTP commands
 * instead of headers. */
static int smtp_clean_address(const char *label, const char *address) {
    char message[256];
    if (!address || address[0] == '\0') {
        snprintf(message, sizeof(message), "smtp.send: %s is empty", label);
        smtp_raise(message);
        return 0;
    }
    for (const char *p = address; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\r' || ch == '\n' || ch == '\0' || ch < 0x20) {
            /* The address is deliberately NOT echoed. Printing it back would
             * put the caller's control characters into the diagnostic -- the
             * same injection one layer along, into whatever reads the log. */
            snprintf(message, sizeof(message),
                     "smtp.send: %s contains a control character (byte %u), which would "
                     "let it write its own SMTP commands", label, (unsigned)ch);
            smtp_raise(message);
            return 0;
        }
        if (ch == '<' || ch == '>') {
            snprintf(message, sizeof(message),
                     "smtp.send: %s must be a bare address without angle brackets: %s",
                     label, address);
            smtp_raise(message);
            return 0;
        }
    }
    return 1;
}

static int smtp_reject_unknown(Value record,
                               const char *what,
                               const char **allowed,
                               size_t allowed_count) {
    for (size_t i = 0; i < record.as.record.count; i++) {
        int known = 0;
        for (size_t j = 0; j < allowed_count; j++) {
            if (strcmp(record.as.record.fields[i].name, allowed[j]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            char message[320];
            size_t used = (size_t)snprintf(message, sizeof(message),
                                           "smtp.send: unknown %s field '%s'; expected one of ",
                                           what, record.as.record.fields[i].name);
            for (size_t j = 0; j < allowed_count && used < sizeof(message) - 2; j++) {
                used += (size_t)snprintf(message + used, sizeof(message) - used,
                                         "%s%s", j ? ", " : "", allowed[j]);
            }
            smtp_raise(message);
            return 0;
        }
    }
    return 1;
}

static Value smtp_eval_send(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        smtp_raise("smtp.send expects a configuration record and a composed message");
        return value_null();
    }

    Value config = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(config);
        return value_null();
    }
    Value message = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(config);
        value_free(message);
        return value_null();
    }
    if (config.kind != VALUE_RECORD || message.kind != VALUE_RECORD) {
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send expects a configuration record and a composed message "
                   "(the record mail.compose returns)");
        return value_null();
    }

    static const char *config_fields[] = {
        "host", "port", "security", "username", "password", "timeout", "verify"
    };
    static const char *message_fields[] = { "from", "recipients", "text", "message_id" };
    if (!smtp_reject_unknown(config, "configuration", config_fields,
                             sizeof(config_fields) / sizeof(config_fields[0])) ||
        !smtp_reject_unknown(message, "message", message_fields,
                             sizeof(message_fields) / sizeof(message_fields[0]))) {
        value_free(config);
        value_free(message);
        return value_null();
    }

    RecordField *host = record_find(&config, "host");
    if (!host || host->value->kind != VALUE_STRING || host->value->as.string[0] == '\0') {
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: host is required and must be a non-empty string");
        return value_null();
    }

    const char *security = "starttls";
    RecordField *sec = record_find(&config, "security");
    if (sec) {
        if (sec->value->kind != VALUE_STRING) {
            value_free(config);
            value_free(message);
            smtp_raise("smtp.send: security must be a string");
            return value_null();
        }
        security = sec->value->as.string;
        if (strcmp(security, "starttls") != 0 &&
            strcmp(security, "tls") != 0 &&
            strcmp(security, "plain") != 0) {
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "smtp.send: unknown security '%s'; expected starttls, tls or plain",
                     security);
            value_free(config);
            value_free(message);
            smtp_raise(detail);
            return value_null();
        }
    }

    long port = strcmp(security, "tls") == 0 ? 465 : 587;
    RecordField *port_field = record_find(&config, "port");
    if (port_field) {
        if (port_field->value->kind != VALUE_NUMBER ||
            port_field->value->as.number < 1 ||
            port_field->value->as.number > 65535 ||
            port_field->value->as.number != floor(port_field->value->as.number)) {
            value_free(config);
            value_free(message);
            smtp_raise("smtp.send: port must be a whole number from 1 to 65535");
            return value_null();
        }
        port = (long)port_field->value->as.number;
    }

    double timeout = 30.0;
    RecordField *timeout_field = record_find(&config, "timeout");
    if (timeout_field) {
        if (timeout_field->value->kind != VALUE_NUMBER ||
            !isfinite(timeout_field->value->as.number) ||
            timeout_field->value->as.number <= 0) {
            value_free(config);
            value_free(message);
            smtp_raise("smtp.send: timeout must be a positive number of seconds");
            return value_null();
        }
        timeout = timeout_field->value->as.number;
    }

    int verify = 1;
    RecordField *verify_field = record_find(&config, "verify");
    if (verify_field) {
        if (verify_field->value->kind != VALUE_BOOL) {
            value_free(config);
            value_free(message);
            smtp_raise("smtp.send: verify must be true or false");
            return value_null();
        }
        verify = verify_field->value->as.boolean ? 1 : 0;
    }

    RecordField *from = record_find(&message, "from");
    RecordField *rcpt = record_find(&message, "recipients");
    RecordField *text = record_find(&message, "text");
    if (!from || from->value->kind != VALUE_STRING ||
        !text || text->value->kind != VALUE_STRING ||
        !rcpt || rcpt->value->kind != VALUE_ARRAY) {
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: the message needs from (string), recipients (array) "
                   "and text (string) -- the record mail.compose returns");
        return value_null();
    }
    if (rcpt->value->as.array.store->count == 0) {
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: the message has no recipients");
        return value_null();
    }
    if (string_length(text->value->as.string) == 0) {
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: the message text is empty");
        return value_null();
    }
    if (!smtp_clean_address("from", from->value->as.string)) {
        value_free(config);
        value_free(message);
        return value_null();
    }

    struct curl_slist *recipients = NULL;
    for (size_t i = 0; i < rcpt->value->as.array.store->count; i++) {
        Value *item = &rcpt->value->as.array.store->items[i];
        if (!item || item->kind != VALUE_STRING) {
            curl_slist_free_all(recipients);
            value_free(config);
            value_free(message);
            smtp_raise("smtp.send: every recipient must be a string");
            return value_null();
        }
        if (!smtp_clean_address("recipient", item->as.string)) {
            curl_slist_free_all(recipients);
            value_free(config);
            value_free(message);
            return value_null();
        }
        char boxed[512];
        snprintf(boxed, sizeof(boxed), "<%s>", item->as.string);
        recipients = curl_slist_append(recipients, boxed);
    }

    size_t framed_length = 0;
    char *framed = smtp_frame(text->value->as.string,
                              string_length(text->value->as.string),
                              &framed_length);
    if (!framed) {
        curl_slist_free_all(recipients);
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: out of memory framing the message");
        return value_null();
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(framed);
        curl_slist_free_all(recipients);
        value_free(config);
        value_free(message);
        smtp_raise("smtp.send: could not create the libcurl handle");
        return value_null();
    }

    char url[512];
    snprintf(url, sizeof(url), "%s://%s:%ld",
             strcmp(security, "tls") == 0 ? "smtps" : "smtp",
             host->value->as.string, port);
    char mail_from[512];
    snprintf(mail_from, sizeof(mail_from), "<%s>", from->value->as.string);

    SmtpPayload payload = { framed, framed_length, 0 };
    SmtpTrace trace = {{0}};
    char error_buffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "smtp,smtps");
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, smtp_read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)framed_length);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, smtp_debug_callback);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &trace);
    long timeout_ms = (long)ceil(timeout * 1000.0);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms < 1 ? 1L : timeout_ms);
    /* Any rejected RCPT TO fails the whole send. libcurl can be told to carry
     * on (CURLOPT_MAIL_RCPT_ALLLOWFAILS) and deliberately is not: partial
     * delivery reported as success is the failure mode that costs the most to
     * find out about later. */
    if (strcmp(security, "starttls") == 0) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    } else if (strcmp(security, "plain") == 0) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_NONE);
    }
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);

    RecordField *username = record_find(&config, "username");
    RecordField *password = record_find(&config, "password");
    if (username && username->value->kind == VALUE_STRING) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, username->value->as.string);
    }
    if (password && password->value->kind == VALUE_STRING) {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password->value->as.string);
    }

    CURLcode code = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    Value result = value_null();
    if (code != CURLE_OK) {
        char detail[1024];
        const char *why = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
        if (trace.last_refusal[0]) {
            snprintf(detail, sizeof(detail),
                     "smtp.send failed (%s): %s -- the server said: %s",
                     host->value->as.string, why, trace.last_refusal);
        } else {
            snprintf(detail, sizeof(detail),
                     "smtp.send failed (%s): %s", host->value->as.string, why);
        }
        smtp_raise(detail);
    } else {
        RecordField *mid = record_find(&message, "message_id");
        Value record = value_record(NULL, 0);
        record_set(&record, "recipients",
                   value_number((double)rcpt->value->as.array.store->count));
        record_set(&record, "code", value_number((double)response_code));
        record_set(&record, "message_id",
                   (mid && mid->value->kind == VALUE_STRING)
                       ? value_string(mid->value->as.string)
                       : value_string(""));
        result = record;
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(recipients);
    free(framed);
    value_free(config);
    value_free(message);
    return result;
}

static Value smtp_eval_call(AstExpr *expr) {
    if (strcmp(expr->as.call.name, "send") == 0) {
        return smtp_eval_send(expr);
    }
    char message[192];
    snprintf(message, sizeof(message),
             "unknown smtp function: %s (the module provides smtp.send)",
             expr->as.call.name);
    smtp_raise(message);
    return value_null();
}

#endif /* HAVE_LIBCURL */
