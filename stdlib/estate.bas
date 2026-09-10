' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' estate -- a fabricated business estate whose TRUTH IS WRITTEN DOWN.
' See docs/estate_design.md for the requirements this satisfies.
'
' R1 IS THE RULE EVERYTHING RESTS ON: ONE DECLARATION PRODUCES BOTH THE
' DATABASE AND THE TRUTH. A relationship is declared once, saying whether the
' database ENFORCES it; `ddl` emits a constraint only when it does, and `truth`
' reports it either way. A hand-written answer key drifts the first time either
' side changes, and a fixture whose answer key is wrong TEACHES THE TOOL TO BE
' WRONG AND THEN CERTIFIES IT.
'
' R2: the truth is a SEPARATE VALUE, never a marker in the data. No `is_fk`
' column, no naming convention only the answer key relies on.
'
' THE VOCABULARY IS REAL. Names are taken from the Enron corpus rather than
' invented -- counterparty, enron_entity, contract_price, gross_vol_mmbtu,
' pvr_pct, ctp -- because an author's naming imagination is one of the two
' blind spots docs/estate_design.md §8 admits to. Measured at an 18% yield over
' 260 workbooks, and the corpus supplied the pathology too: repeated column
' names in one header, undocumented abbreviations, and a production typo.
'
' THIS INCREMENT DECLARES STRUCTURE AND CODE, NOT ROWS. `discovery` currently
' reads catalogs and module source; rows matter for INFERENCE (value overlap),
' which is not built and which needs a null model first.
library estate

    ' ---- the declaration ---------------------------------------------------
    '
    ' `enforced: true`  -> a REFERENCES clause is emitted AND the truth reports it
    ' `enforced: false` -> NOTHING is emitted; the truth reports it anyway
    '
    ' `kind` follows the estate design's vocabulary, corrected on gdash's
    ' contribution from a boolean to a KIND: a denormalised copy is a REAL
    ' relationship whose lineage does not run that way, and scoring it true
    ' rewards a mistake while scoring it a decoy punishes noticing something
    ' real.
    function spec()
        return {
            schemas: ["trading", "finance", "warehouse", "staging"],
            tables: [
                ' -- trading: the operational system ------------------------
                { schema: "trading", name: "counterparty",
                  columns: ["id integer primary key", "counterparty_name varchar(60)",
                            "enron_entity varchar(40)", "status varchar(10)"] },
                { schema: "trading", name: "contract",
                  columns: ["id integer primary key", "counterparty_id integer",
                            "contract_date date", "contract_price decimal(19,4)",
                            "status varchar(10)"] },
                { schema: "trading", name: "deal",
                  columns: ["id integer primary key", "contract_id integer",
                            "effective_date date", "gross_vol_mmbtu decimal(19,4)",
                            "pvr_pct decimal(9,4)", "ctp_code varchar(10)",
                            "status varchar(10)"] },
                ' A LOOKUP REACHED TWO WAYS (R10) and used as a FILTER (R34):
                ' `is_active` decides which deals reach the fact table while
                ' contributing no column to it.
                { schema: "trading", name: "ctp",
                  columns: ["code varchar(10) primary key", "ctp_name varchar(60)",
                            "is_active integer"] },
                ' -- finance: a different vendor, different conventions ------
                { schema: "finance", name: "gl_account",
                  columns: ["acct_no varchar(12) primary key", "description varchar(60)"] },
                { schema: "finance", name: "gl_entry",
                  columns: ["entry_id integer primary key", "acct_no varchar(12)",
                            "reference varchar(40)", "amount decimal(19,4)",
                            "posted integer"] },
                ' R6: the same entity, RENAMED across a system boundary.
                { schema: "finance", name: "ar_invoice",
                  columns: ["inv_no integer primary key", "cust_id integer",
                            "inv_date date", "amount decimal(19,4)", "status varchar(10)"] },
                ' -- warehouse: where the ETL lands -------------------------
                { schema: "warehouse", name: "stg_deal",
                  columns: ["id integer", "contract_id integer", "effective_date date",
                            "gross_vol_mmbtu decimal(19,4)", "pvr_pct decimal(9,4)",
                            "ctp_code varchar(10)", "status varchar(10)"] },
                { schema: "warehouse", name: "fact_volume",
                  columns: ["deal_id integer", "effective_date date",
                            "gross_vol_mmbtu decimal(19,4)",
                            "avail_after_pvr decimal(19,4)", "status varchar(10)"] },
                ' R7: THE BEST DECOY AVAILABLE. `row_id` holds integers
                ' overlapping every table's ids and references none of them --
                ' it looks like a foreign key to everything.
                { schema: "warehouse", name: "audit_log",
                  columns: ["id integer primary key", "table_name varchar(60)",
                            "row_id integer", "changed_at date"] },
                ' -- staging: R9's NULL REGION and R21's load-bearing temp ---
                ' No keys, no relationships, and nothing may be reported here.
                { schema: "staging", name: "tmp_rebate_2019",
                  columns: ["col_a varchar(40)", "col_b varchar(40)",
                            "col_c decimal(19,4)", "manual_entery varchar(40)"] }
            ],
            relationships: [
                { from: "trading.contract.counterparty_id", to: "trading.counterparty.id",
                  enforced: true, kind: "enforced" },
                ' R4: the most common real case -- convention only.
                { from: "trading.deal.contract_id", to: "trading.contract.id",
                  enforced: false, kind: "real_undeclared" },
                { from: "trading.deal.ctp_code", to: "trading.ctp.code",
                  enforced: false, kind: "real_undeclared" },
                ' R13: a DENORMALISED COPY. Every value is a real id and the
                ' lineage does not run this way -- it runs through the ETL.
                { from: "warehouse.fact_volume.deal_id", to: "trading.deal.id",
                  enforced: false, kind: "real_indirect" },
                ' R6 + R12: renamed, and across a source boundary.
                { from: "finance.ar_invoice.cust_id", to: "trading.counterparty.id",
                  enforced: false, kind: "real_undeclared" },
                ' R19: through a CONCATENATION -- no value-overlap method finds
                ' it, and a general ledger is where "where did this number come
                ' from" is asked most.
                { from: "finance.gl_entry.reference", to: "trading.deal.id",
                  enforced: false, kind: "real_non_equality",
                  note: "reference = 'DEAL-' || deal.id" },
                { from: "finance.gl_entry.acct_no", to: "finance.gl_account.acct_no",
                  enforced: false, kind: "real_undeclared" }
            ],
            ' R3: THE DECOYS ARE NAMED, not merely omitted. "This pair is NOT a
            ' relationship" is the assertion a false-positive test makes, and a
            ' record listing only what is true cannot express it.
            decoys: [
                { from: "warehouse.audit_log.row_id", to: "trading.deal.id",
                  why: "R7: row_id overlaps every table's ids and references none" },
                { from: "warehouse.audit_log.row_id", to: "trading.counterparty.id",
                  why: "R7: the same, against a different table" },
                { from: "trading.deal.status", to: "finance.ar_invoice.status",
                  why: "R8: four unrelated status columns share a name and a vocabulary" },
                { from: "trading.contract.status", to: "warehouse.fact_volume.status",
                  why: "R8: the same" }
            ],
            ' R9: nothing in this schema relates to anything. Lineage reported
            ' here is INVENTED, and no other check can reveal that.
            null_region: "staging",
            ' R21/R28: facts that are NOT IN THE DATABASE AT ALL. The right
            ' answer is "unanswerable from the catalog", not a guess.
            undiscoverable: [
                { fact: "staging.tmp_rebate_2019 is load-bearing: a nightly job reads it and month-end breaks without it",
                  why: "R21: it lives in a job schedule, not in the schema" }
            ],
            ' The ETL. R31 (tables -> views -> procedures -> tables),
            ' R32 (fan-in and fan-out), R33 (the headline), R34 (lookup as a
            ' filter), and one deliberate GAP.
            modules: [
                { schema: "warehouse", name: "p_load_stg_deal", kind: "procedure",
                  body: "truncate table warehouse.stg_deal; insert into warehouse.stg_deal (id, contract_id, effective_date, gross_vol_mmbtu, pvr_pct, ctp_code, status) select d.id, d.contract_id, d.effective_date, d.gross_vol_mmbtu, d.pvr_pct, d.ctp_code, d.status from trading.deal d join trading.contract c on c.id = d.contract_id",
                  reads: ["trading.deal", "trading.contract"],
                  writes: ["warehouse.stg_deal"] },
                ' R34: the lookup FILTERS. `ctp` contributes no column to
                ' fact_volume and decides which rows reach it.
                { schema: "warehouse", name: "p_build_fact", kind: "procedure",
                  body: "insert into warehouse.fact_volume (deal_id, effective_date, gross_vol_mmbtu, avail_after_pvr, status) select s.id, s.effective_date, s.gross_vol_mmbtu, s.gross_vol_mmbtu * (1 - s.pvr_pct), s.status from warehouse.stg_deal s join trading.ctp k on k.code = s.ctp_code where k.is_active = 1",
                  reads: ["warehouse.stg_deal", "trading.ctp"],
                  writes: ["warehouse.fact_volume"] },
                ' R32: FAN-OUT. fact_volume feeds the ledger AND both reports.
                { schema: "warehouse", name: "p_post_gl", kind: "procedure",
                  body: "insert into finance.gl_entry (entry_id, acct_no, reference, amount, posted) select f.deal_id, '4000', 'DEAL-' + cast(f.deal_id as varchar(20)), f.avail_after_pvr, 0 from warehouse.fact_volume f",
                  reads: ["warehouse.fact_volume"],
                  writes: ["finance.gl_entry"] },
                ' R33: THE HEADLINE. Two views, the same column name, both
                ' correct, DIFFERENT NUMBERS -- one counts everything, the
                ' other only active deals. "Why are these different?" is the
                ' question people actually ask, and the answer is the
                ' predicate, not a missing relationship.
                { schema: "warehouse", name: "rpt_volume_gross", kind: "view",
                  body: "select effective_date, sum(gross_vol_mmbtu) as total_volume from warehouse.fact_volume group by effective_date",
                  reads: ["warehouse.fact_volume"], writes: [] },
                { schema: "warehouse", name: "rpt_volume_net", kind: "view",
                  body: "select effective_date, sum(avail_after_pvr) as total_volume from warehouse.fact_volume where status = 'ACTIVE' group by effective_date",
                  reads: ["warehouse.fact_volume"], writes: [] },
                ' A GAP, deliberately: dynamic SQL cannot be read, and a tracer
                ' that drops it silently is confidently incomplete.
                { schema: "staging", name: "p_dynamic_rebate", kind: "procedure",
                  body: "declare @src varchar(60); set @src = 'trading.deal'; exec ('insert into staging.tmp_rebate_2019 select * from ' + @src)",
                  reads: [], writes: [], gap: true,
                  ' T-SQL only: a PostgreSQL `language sql` function has no
                  ' variables, so the dynamic-SQL case is expressed where it is
                  ' natural rather than contorted into both dialects.
                  dialects: ["sqlserver"] }
            ],
            ' R33 stated as a QUESTION the estate can be asked, with the answer
            ' recorded. This is what discovery must eventually explain.
            divergences: [
                { a: "warehouse.rpt_volume_gross.total_volume",
                  b: "warehouse.rpt_volume_net.total_volume",
                  share: "warehouse.fact_volume",
                  because: "rpt_volume_net filters status = 'ACTIVE' and sums avail_after_pvr; rpt_volume_gross sums gross_vol_mmbtu over every row" }
            ]
        }
    end function

    ' ---- the truth: derived from the SAME declaration ----------------------
    function truth(sp)
        return { relationships: sp.relationships,
                 decoys: sp.decoys,
                 null_region: sp.null_region,
                 undiscoverable: sp.undiscoverable,
                 flows: sp.modules,
                 divergences: sp.divergences }
    end function

    function _type_for(dialect, decl)
        ' The dialect map stays deliberately small.
        if dialect = "postgres" then
            return replace(decl, " + ", " || ")
        end if
        return decl
    end function

    ' ---- the database, from the same declaration ---------------------------
    function ddl(sp, dialect)
        if not contains(["postgres", "sqlserver"], dialect) then
            error "estate: ddl supports \"postgres\" and \"sqlserver\"; SQLite has no stored procedures and MariaDB has no schemas, so neither can carry this estate"
        end if
        out = []
        for each s in sp.schemas
            if dialect = "sqlserver" then
                append(out, "if schema_id('" + s + "') is null exec('create schema " + s + "')")
            else
                append(out, "create schema if not exists " + s)
            end if
        end for
        for each t in sp.tables
            cols = []
            for each cdef in t.columns
                append(cols, cdef)
            end for
            ' ONLY an enforced relationship becomes a constraint. That is R1:
            ' the declaration decides both sides, so they cannot disagree.
            for each r in sp.relationships
                if r.enforced then
                    parts = split(r.from, ".")
                    if parts[0] = t.schema and parts[1] = t.name then
                        tgt = split(r.to, ".")
                        append(cols, "foreign key (" + parts[2] + ") references " +
                                     tgt[0] + "." + tgt[1] + "(" + tgt[2] + ")")
                    end if
                end if
            end for
            append(out, "create table " + t.schema + "." + t.name + " (" + join(cols, ", ") + ")")
        end for
        for each m in sp.modules
            skip = false
            if has(m, "dialects") then
                if not contains(m.dialects, dialect) then
                    skip = true
                end if
            end if
            if skip then
                ' declared for another dialect
            else
            if m.kind = "view" then
                append(out, "create view " + m.schema + "." + m.name + " as " + _type_for(dialect, m.body))
            else
                if dialect = "sqlserver" then
                    append(out, "create procedure " + m.schema + "." + m.name + " as " + m.body)
                else
                    append(out, "create function " + m.schema + "." + m.name +
                                "() returns void as $x$ " + _type_for(dialect, m.body) + "; $x$ language sql")
                end if
            end if
            end if
        end for
        return out
    end function

    ' Every object the estate declares, as a flat list of qualified names.
    function objects(sp)
        out = []
        for each t in sp.tables
            append(out, t.schema + "." + t.name)
        end for
        for each m in sp.modules
            append(out, m.schema + "." + m.name)
        end for
        return out
    end function
end library
