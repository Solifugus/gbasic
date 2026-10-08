' This file BEGINS WITH A UTF-8 BYTE-ORDER MARK (EF BB BF), as Notepad's
' "UTF-8 with BOM" and PowerShell 5.1's `Set-Content -Encoding utf8` write it.
' The mark says how the text is encoded and is not text, so the lexer skips it;
' before 2026-10-04 this file was refused with `lexer error at 1:1: unexpected token` (src/lexer.c).
' Do not re-save it without the mark: that would leave a test of nothing.
print("ran: the byte-order mark was skipped")
