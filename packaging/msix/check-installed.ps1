# Checks an INSTALLED gBASIC MSIX package the way a user meets it: through the
# `gbasic` execution alias, from an ordinary directory, with no GBASIC_PATH.
# Each check prints `ok` or `FAIL`; the last line is `failures: N`.
#
# What a package can break that a plain gbasic.exe cannot, and so what this asks:
#   - the ALIAS resolves and runs a CONSOLE program in this console;
#   - the stdlib is found beside the binary inside WindowsApps;
#   - `spawn` RE-EXECS the binary (by its WindowsApps path) and the child runs;
#   - actor sockets work from inside the package (the inbox lives in TEMP);
#   - process.run can start an ordinary program from inside the package;
#   - a file the program writes lands where the user asked, not in a
#     virtualised copy.
# Usage: check-installed.ps1 [-Repo <repository root>]
param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)

$failures = 0
function Check($label, $ok, $detail) {
    if ($ok) { Write-Output "ok   $label" }
    else { Write-Output "FAIL $label -- $detail"; $script:failures++ }
}

$env:GBASIC_PATH = $null
$alias = Get-Command gbasic -ErrorAction SilentlyContinue
Check 'gbasic resolves to the package alias' ($alias -and $alias.Source -like '*\WindowsApps\*') "$($alias.Source)"

$v = (& gbasic --version 2>&1 | Out-String).Trim()
Check 'gbasic --version runs in this console' ($v -like 'gBASIC *') $v

$work = Join-Path $env:TEMP ("gbasic-msix-check-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $work | Out-Null
Push-Location $work
try {
    # -1498.88 is Excel's and LibreOffice's PMT for the same loan.
    Set-Content -Path lib.bas -Encoding ascii -Value @'
load finance
print(finance.pmt(0.005, 360, {USD}250000))
'@
    $o = (& gbasic lib.bas 2>&1 | Out-String).Trim()
    Check 'the stdlib is found beside the binary (load finance)' ($o -eq '-1498.88') $o

    Set-Content -Path actor.bas -Encoding ascii -Value @'
function worker(parent, n)
    msg = receive()
    send(parent, msg + " " + string(n * 2))
end function
w = spawn worker(self(), 21)
send(w, "child says")
print(receive(10 seconds))
'@
    $o = (& gbasic actor.bas 2>&1 | Out-String).Trim()
    Check 'spawn re-execs the packaged binary and the child answers' ($o -eq 'child says 42') $o

    $t = (& gbasic (Join-Path $Repo 'tests\windows\actor_transport.bas') 2>&1 | Select-Object -Last 1 | Out-String).Trim()
    Check 'the actor transport suite passes inside the package' ($t -eq 'mismatches: 0') $t

    Set-Content -Path run.bas -Encoding ascii -Value @'
r = process.run({ command: "cmd", args: ["/c", "echo", "from-cmd"] })
print(trim(r.stdout))
'@
    $o = (& gbasic run.bas 2>&1 | Out-String).Trim()
    Check 'process.run starts an ordinary program' ($o -eq 'from-cmd') $o

    # The PROMPT keeps its program in a session cache, which is also what a
    # spawned actor re-reads; a Windows terminal has no HOME, so the cache must
    # come from %LOCALAPPDATA%. Run with HOME unset, as PowerShell and cmd are.
    $replIn = "function worker(parent)`nsend(parent, `"hi from child`")`nend function`nme = self()`nh = spawn worker(me)`nprint(receive(5 seconds))`nquit`n"
    $saveHome = $env:HOME
    $env:HOME = $null
    $o = ($replIn | & gbasic --repl 2>&1 | Out-String).Trim()
    $env:HOME = $saveHome
    Check 'spawn works at the prompt with no HOME' ($o -like '*hi from child*') $o

    # Where the cache really went: a packaged app's writes to AppData may be
    # redirected into its own package folder. Either is correct; say which.
    $plain = Join-Path $env:LOCALAPPDATA 'gbasic'
    $pkg = Get-AppxPackage gBASIC.gbasic -ErrorAction SilentlyContinue
    $redirected = if ($pkg) { Join-Path $env:LOCALAPPDATA "Packages\$($pkg.PackageFamilyName)\LocalCache\Local\gbasic" } else { '' }
    if ($redirected -and (Test-Path $redirected)) { Write-Output "note the session cache is redirected to $redirected" }
    elseif (Test-Path $plain) { Write-Output "note the session cache is at $plain (not redirected)" }
    else { Write-Output "note no session cache directory was found" }

    # The language server, through ITS alias, speaking the protocol: initialize,
    # open a file with an error, and require the error back. Bytes are written
    # through a .NET process rather than a PowerShell pipe, which would add a
    # byte-order mark and rewrite line endings -- exactly what JSON-RPC's
    # Content-Length framing cannot survive.
    $lspAlias = Get-Command gbasic-lsp -ErrorAction SilentlyContinue
    Check 'gbasic-lsp resolves to the package alias' ($lspAlias -and $lspAlias.Source -like '*\WindowsApps\*') "$($lspAlias.Source)"
    if ($lspAlias) {
        # Frames go in from a FILE through cmd's redirect. (Fed through a .NET
        # Process pipe from Windows PowerShell, the server saw nothing -- the
        # same server answers over Node's pipes in the VS Code test, so that is
        # PowerShell's plumbing, not the server.)
        $msgs = @(
            '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}',
            '{"jsonrpc":"2.0","method":"initialized","params":{}}',
            '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///c:/x/a.gb","languageId":"gbasic","version":1,"text":"x = )\n"}}}',
            '{"jsonrpc":"2.0","id":2,"method":"shutdown"}',
            '{"jsonrpc":"2.0","method":"exit"}')
        $ms = New-Object IO.MemoryStream
        foreach ($m in $msgs) {
            $b = [Text.Encoding]::UTF8.GetBytes($m)
            $h = [Text.Encoding]::ASCII.GetBytes("Content-Length: $($b.Length)`r`n`r`n")
            $ms.Write($h, 0, $h.Length)
            $ms.Write($b, 0, $b.Length)
        }
        $framesFile = Join-Path $work 'lsp_frames.bin'
        [IO.File]::WriteAllBytes($framesFile, $ms.ToArray())
        $reply = (cmd /c "gbasic-lsp < `"$framesFile`"" 2>&1 | Out-String)
        Check 'gbasic-lsp answers through the alias with the error' ($reply -match 'publishDiagnostics' -and $reply -match 'syntax error') ($reply.Substring(0, [Math]::Min(200, $reply.Length)))
    }

    Set-Content -Path write.bas -Encoding ascii -Value @'
f {file}= "written.txt"
write(f, "here")
print("done")
'@
    & gbasic write.bas | Out-Null
    $p = Join-Path $work 'written.txt'
    Check 'a written file lands where the program asked' ((Test-Path $p) -and ((Get-Content $p -Raw) -eq 'here')) "$p"
}
finally {
    Pop-Location
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
Write-Output "failures: $failures"
