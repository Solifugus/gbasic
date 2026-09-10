/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE EVIDENCE BEHIND tests/odbc.supp.
 *
 * That file suppresses memory defects INSIDE an ODBC driver, and its own rule
 * is that a suppression is reproduced with no gBASIC in the stack BEFORE it is
 * written -- otherwise it is indistinguishable from hiding our own bug. Until
 * now the isolation was done by hand and thrown away, so the claim was true
 * when made and unverifiable afterwards, and unrepeatable when the driver was
 * upgraded.
 *
 * This is that program, committed. Every argument it passes is a STRING
 * LITERAL or SQL_NTS: nothing here can be uninitialised, so a defect valgrind
 * reports under it is the driver's.
 *
 * Build and run:
 *   cc -o probe tests/odbc_driver_probe.c -lodbc
 *   valgrind --error-exitcode=99 --suppressions=tests/odbc.supp \
 *            ./probe "$GBASIC_ODBC_CONNECTION" [--dirty]
 *
 * `--dirty` IS THE OTHER HALF AND THE MORE IMPORTANT ONE. The entries in
 * odbc.supp name a driver object and an entry point, and the standing worry --
 * written into that file's own header long before this program existed -- is
 * that they would ALSO hide a caller passing uninitialised data in. So this
 * program can BE that caller: `--dirty` hands SQLTables an unwritten stack
 * buffer.
 *
 * MEASURED, and it settles the worry in the good direction: a caller's
 * uninitialised data is reported at `strlen (vg_replace_strmem.c)` --
 * valgrind's own interceptor, OUTSIDE libmaodbc -- while the driver's own
 * defect is reported at a frame INSIDE it. An entry whose innermost frame is
 * required to be the driver object therefore cannot match the first.
 * run_odbc.sh asserts both directions, and a blanket entry is proven red
 * against the dirty run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sql.h>
#include <sqlext.h>

static void bail(const char *what, SQLSMALLINT type, SQLHANDLE h)
{
    SQLCHAR state[6], msg[512];
    SQLINTEGER native;
    SQLSMALLINT len, rec = 1;
    int any = 0;
    /* EVERY record, not the first. A driver may put the real reason in the
     * second, and "failed" with no text is a diagnosis of nothing -- which is
     * how the first version of this program reported an ordinary SQL error as
     * a suppression problem. */
    while (SQLGetDiagRec(type, h, rec, state, &native, msg, sizeof msg, &len) == SQL_SUCCESS) {
        fprintf(stderr, "%s: [%s] %s\n", what, state, msg);
        any = 1;
        rec++;
    }
    if (!any) fprintf(stderr, "%s: failed with no diagnostic\n", what);
    exit(2);
}

#define OK(r) ((r) == SQL_SUCCESS || (r) == SQL_SUCCESS_WITH_INFO)

/* SQL_NO_DATA IS SUCCESS FOR A STATEMENT THAT RETURNS NO ROWS, and FreeTDS
 * returns it for DDL. Treating it as a failure is what made this program
 * report "create table failed with no diagnostic" -- no diagnostic because
 * there was no error: the table had been created. The wrong-diagnosis class
 * again, one layer down. */
#define OK_EXEC(r) (OK(r) || (r) == SQL_NO_DATA)

/* PREPARE THEN EXECUTE, which is what gBASIC does -- not SQLExecDirect.
 * MEASURED: FreeTDS against SQL Server refuses DDL through SQLExecDirect and
 * returns SQL_ERROR WITH NO DIAGNOSTIC AT ALL, while the same statement
 * prepared and executed succeeds. The probe's fixture setup should take the
 * library's own path anyway; it took the other one only because it was
 * shorter, and that cost a wrong diagnosis on the one database where it
 * matters. */
static void exec1(SQLHDBC dbc, const char *sql, int required)
{
    SQLHSTMT st = NULL;
    SQLRETURN r;
    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    r = SQLPrepare(st, (SQLCHAR *)sql, SQL_NTS);
    if (OK(r)) r = SQLExecute(st);
    if (required && !OK_EXEC(r)) {
        fprintf(stderr, "while running: %s\n", sql);
        bail("exec", SQL_HANDLE_STMT, st);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, st);
}

static void cleanup(const char *conn)
{
    const char *stmts[2] = { "drop table vgprobe_child", "drop table vgprobe_parent" };
    for (int i = 0; i < 2; i++) {
        SQLHENV e = NULL; SQLHDBC d = NULL; SQLHSTMT s = NULL;
        if (!OK(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &e))) return;
        SQLSetEnvAttr(e, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        if (OK(SQLAllocHandle(SQL_HANDLE_DBC, e, &d))
            && OK(SQLDriverConnect(d, NULL, (SQLCHAR *)conn, SQL_NTS,
                                   NULL, 0, NULL, SQL_DRIVER_NOPROMPT))) {
            if (OK(SQLAllocHandle(SQL_HANDLE_STMT, d, &s))) {
                if (OK(SQLPrepare(s, (SQLCHAR *)stmts[i], SQL_NTS))) SQLExecute(s);
                SQLFreeHandle(SQL_HANDLE_STMT, s);
            }
            SQLDisconnect(d);
        }
        if (d) SQLFreeHandle(SQL_HANDLE_DBC, d);
        SQLFreeHandle(SQL_HANDLE_ENV, e);
    }
}

int main(int argc, char **argv)
{
    SQLHENV env = NULL;
    SQLHDBC dbc = NULL;
    SQLHSTMT st = NULL;
    SQLRETURN r;

    int dirty = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <odbc connection string> [--dirty]\n", argv[0]);
        return 2;
    }
    if (argc > 2 && strcmp(argv[2], "--dirty") == 0)
        dirty = 1;

    if (!OK(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)))
        return fprintf(stderr, "alloc env failed\n"), 2;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!OK(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)))
        return fprintf(stderr, "alloc dbc failed\n"), 2;

    r = SQLDriverConnect(dbc, NULL, (SQLCHAR *)argv[1], SQL_NTS,
                         NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (!OK(r)) bail("connect", SQL_HANDLE_DBC, dbc);

    /* SPECULATIVE CLEANUP ON A CONNECTION OF ITS OWN, and that is not
     * fastidiousness. MEASURED on FreeTDS: a statement that FAILS poisons the
     * connection, so a `drop table` for a table that is not there made the
     * NEXT statement -- the create -- fail with SQL_ERROR and no diagnostic at
     * all. Doing it on a throwaway connection means a leftover table from an
     * interrupted run is cleared without a failure that anything else can see.
     * `drop table if exists` is not an option: FreeTDS refuses it. */
    cleanup(argv[1]);

    /* A table for the key calls to have something to describe. Dropped at the
     * end; the name is deliberately unlike anything a test fixture uses.
     *
     * ONE STATEMENT HANDLE PER EXECUTION. Reusing one across several
     * SQLExecDirect calls made FreeTDS return SQL_ERROR WITH NO DIAGNOSTIC on
     * the second, which the first version of this program reported as a
     * suppression problem -- an ordinary SQL error diagnosed as something else
     * entirely. */
    exec1(dbc, "create table vgprobe_parent (id integer not null primary key)", 1);
    exec1(dbc, "create table vgprobe_child (id integer not null primary key, "
               "parent_id integer, foreign key (parent_id) references vgprobe_parent(id))", 1);

    /* 1. SQLPrepare -- the defect tests/odbc.supp already carries. */
    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    r = SQLPrepare(st, (SQLCHAR *)"select 1", SQL_NTS);
    if (!OK(r)) bail("SQLPrepare", SQL_HANDLE_STMT, st);
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    /* 2-5. The four catalog calls, in the shape discovery.scan makes them:
     * an absent qualifier is a NULL pattern (ODBC's "any"), and the table
     * pattern is an explicit "%" because one driver refuses an omitted one. */
    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    if (dirty) {
        /* THE CONTROL FOR THE SUPPRESSIONS THEMSELVES. tests/odbc.supp names
         * the driver object and an entry point, and the inner frames are
         * unsymbolised -- so the open question is whether those entries would
         * ALSO hide a caller passing uninitialised data in. This is that
         * caller: a stack buffer that is never written, handed to SQLTables as
         * the table pattern. What valgrind says about it under the suppression
         * file is a measurement, not an argument. */
        SQLCHAR pattern[16];
        r = SQLTables(st, NULL, 0, NULL, 0, pattern, SQL_NTS, NULL, 0);
    } else
    r = SQLTables(st, NULL, 0, NULL, 0, (SQLCHAR *)"vgprobe%", SQL_NTS, NULL, 0);
    if (!OK(r)) bail("SQLTables", SQL_HANDLE_STMT, st);
    while (SQLFetch(st) == SQL_SUCCESS) { }
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    r = SQLColumns(st, NULL, 0, NULL, 0, (SQLCHAR *)"vgprobe%", SQL_NTS, (SQLCHAR *)"%", SQL_NTS);
    if (!OK(r)) bail("SQLColumns", SQL_HANDLE_STMT, st);
    while (SQLFetch(st) == SQL_SUCCESS) { }
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    r = SQLPrimaryKeys(st, NULL, 0, NULL, 0, (SQLCHAR *)"vgprobe_parent", SQL_NTS);
    if (!OK(r)) bail("SQLPrimaryKeys", SQL_HANDLE_STMT, st);
    while (SQLFetch(st) == SQL_SUCCESS) { }
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    if (!OK(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) bail("alloc stmt", SQL_HANDLE_DBC, dbc);
    r = SQLForeignKeys(st, NULL, 0, NULL, 0, NULL, 0,
                             NULL, 0, NULL, 0, (SQLCHAR *)"vgprobe_child", SQL_NTS);
    if (!OK(r)) bail("SQLForeignKeys", SQL_HANDLE_STMT, st);
    while (SQLFetch(st) == SQL_SUCCESS) { }
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    cleanup(argv[1]);
    printf("probe ok\n");
    return 0;
}
