// Runs test/suite.js inside a real VS Code (downloaded by @vscode/test-electron
// into .vscode-test/, git-ignored), with this extension loaded from source and
// every other extension disabled.
//
// The server is the gbasic-lsp built at the repository root (make gbasic-lsp),
// put on PATH for the extension host -- so the test also exercises the PATH
// lookup an installed gBASIC relies on, not only the setting.
const fs = require("fs");
const os = require("os");
const path = require("path");
const { runTests } = require("@vscode/test-electron");

async function main() {
    const ext = path.resolve(__dirname, "..");
    const repo = path.resolve(ext, "../../..");
    // GBASIC_TEST_INSTALLED=1: use whatever gbasic-lsp an INSTALLED gBASIC put
    // on PATH (the MSIX alias), not the repository build -- the path a user has.
    const installed = process.env.GBASIC_TEST_INSTALLED === "1";
    const lsp = installed
        ? null
        : ["gbasic-lsp.exe", "gbasic-lsp"].map((n) => path.join(repo, n)).find((p) => fs.existsSync(p));
    if (!installed && !lsp) {
        console.error("test/run.js: no gbasic-lsp at the repository root -- run `make gbasic-lsp` first");
        process.exit(1);
    }
    const ws = fs.mkdtempSync(path.join(os.tmpdir(), "gbasic-vscode-"));
    // One file with a syntax error on line 1 (the LSP handshake golden's case).
    fs.writeFileSync(path.join(ws, "broken.gb"), "x = )\nprint(x)\n");
    try {
        await runTests({
            extensionDevelopmentPath: ext,
            extensionTestsPath: path.join(__dirname, "suite.js"),
            launchArgs: [ws, "--disable-extensions"],
            extensionTestsEnv: installed
                ? {}
                : { PATH: path.dirname(lsp) + path.delimiter + (process.env.PATH || "") },
        });
    } catch (err) {
        console.error("test/run.js: tests failed", err);
        process.exit(1);
    } finally {
        fs.rmSync(ws, { recursive: true, force: true });
    }
}

main();
