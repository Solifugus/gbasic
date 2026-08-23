' Rank the Excel functions actually used across a directory of workbooks.
'
'   gbasic tools/xlsx_function_scan.bas <directory> [min_percent]
'
' WHY THIS EXISTS. docs/xlsx_design.md §13.G settles the DURABLE core from
' measurement — the Enron corpus, where the top nine functions cover 63.6% of
' spreadsheets — but that corpus is from 2001 and predates SUMIFS (2007) and
' XLOOKUP (2019) entirely. It is evidence about what endures, not about what a
' particular organization uses now.
'
' The better corpus is your own, and it is obtainable without disclosing
' anything: a LIST OF FUNCTION NAMES is not proprietary the way the data in the
' cells is. This scanner reads only formulas, counts only function names, and
' prints only aggregates — no cell values, no sheet contents, no file contents
' beyond the tally. Run it over real workbooks and the output is a ranked list
' from actual work, which beats any public corpus for deciding what to build.
'
' It deliberately reports COVERAGE the same way the Enron analysis did
' (cumulative percentage of formula cells), so the two are directly comparable.
'
' It also marks which functions the evaluator already supports, so the output
' reads as a work list rather than as trivia.

function supported_now()
  ' Kept in step with xlsx_call in src/modules/xlsx.c. If this drifts the
  ' report merely mislabels; it cannot affect the counts.
  return ["SUM", "AVERAGE", "MIN", "MAX", "COUNT", "COUNTA", "ROUND",
          "ROUNDUP", "ROUNDDOWN", "ABS", "IF", "IFERROR", "TRUE", "FALSE"]
end function

function pct(part, whole)
  if whole = 0 then
    return "0.0"
  end if
  v = floor(part * 1000.0 / whole + 0.5) / 10.0
  return string(v)
end function

program main(args)
  if count(args) < 1 then
    print to error "usage: gbasic tools/xlsx_function_scan.bas <directory> [min_percent]"
    return
  end if
  dir = args[0]
  floor_pct = 0.0
  if count(args) > 1 then
    floor_pct = number(args[1])
  end if

  files = list_files(dir)
  names = []
  counts = []
  workbooks = 0
  sheets_seen = 0
  formula_cells = 0
  skipped = []

  for each f in files
    path = string(f)
    is_xlsx = ends_with(lower(path), ".xlsx")
    if not is_xlsx then
      is_xlsx = ends_with(lower(path), ".xlsm")
    end if
    if not is_xlsx then
      continue
    end if

    ' LIMIT, stated because the earlier comment here claimed a guard that was
    ' never in the code: `xlsx.open` RAISES on an unreadable workbook and
    ' A raise here would abort this whole scan mid-directory, so ONE bad file
    ' yields nothing. `list_files` also does not recurse (plain opendir), so a
    ' nested corpus is under-scanned rather than refused.
    '
    ' For a directory you control, this is fine. For an untrusted corpus, drive
    ' it one process per file from a shell loop instead — that is how the
    ' 15,871-workbook Enron scan in docs/xlsx_design.md §13.I was run, and the
    ' per-file exit status is what located the one reader bug it found.
    ' See the 2026-08-03 DOGFOOD entry.
    wb = xlsx.open(path)
    workbooks = workbooks + 1

    for each sheet in xlsx.sheets(wb)
      sheets_seen = sheets_seen + 1
      for each c in xlsx.cells(wb, sheet)
        if is_unknown(c.formula) then
          continue
        end if
        formula_cells = formula_cells + 1
        ' Every NAME immediately followed by "(" is a function call in Excel
        ' formula syntax. Dogfoods TEXT-0's match_all.
        for each m in match_all(c.formula, regex("[A-Za-z][A-Za-z0-9_.]*\\("))
          nm = upper(mid(m.text, 0, m.length - 1))
          at = find(names, nm)
          if is_nothing(at) then
            append(names, nm)
            append(counts, 1)
          else
            counts[at] = counts[at] + 1
          end if
        end for
      end for
    end for
  end for

  print "workbooks scanned = " + workbooks
  print "sheets scanned    = " + sheets_seen
  print "formula cells     = " + formula_cells
  print "distinct functions= " + count(names)
  if formula_cells = 0 then
    print "(no formulas found; nothing to rank)"
    return
  end if

  ' Selection sort by count, descending. The list is at most a few hundred
  ' entries, so the simple thing is the right thing.
  order = []
  used = []
  i = 0
  while i < count(names)
    append(used, false)
    i = i + 1
  end while
  picked = 0
  while picked < count(names)
    best = 0 - 1
    bestn = 0 - 1
    j = 0
    while j < count(names)
      if not used[j] then
        if counts[j] > bestn then
          bestn = counts[j]
          best = j
        end if
      end if
      j = j + 1
    end while
    used[best] = true
    append(order, best)
    picked = picked + 1
  end while

  known = supported_now()
  total_uses = 0
  for each c2 in counts
    total_uses = total_uses + c2
  end for

  print ""
  print "rank  uses    share  cumulative  status      function"
  running = 0
  rank = 0
  for each idx in order
    rank = rank + 1
    running = running + counts[idx]
    share = counts[idx] * 100.0 / total_uses
    if share < floor_pct then
      continue
    end if
    mark = "TO BUILD"
    if contains(known, names[idx]) then
      mark = "supported"
    end if
    print "  " + rank + "   " + counts[idx] + "   " + pct(counts[idx], total_uses) + "%   " + pct(running, total_uses) + "%      " + mark + "   " + names[idx]
  end for

  print ""
  print "-- what is not yet implemented, in usage order --"
  todo = []
  for each idx in order
    if not contains(known, names[idx]) then
      append(todo, names[idx] + " (" + counts[idx] + ")")
    end if
  end for
  if count(todo) = 0 then
    print "  nothing: every function used here is already supported"
  else
    print "  " + join(todo, ", ")
  end if

  print ""
  print "Reported: function names and counts only. No cell values, sheet names"
  print "or file contents are printed, so this output can be shared."
end program
