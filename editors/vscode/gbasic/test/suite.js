// Inside VS Code: the extension must start gbasic-lsp and the server's
// diagnostics must reach the editor -- and LEAVE it when the error is fixed.
// The second half is the control: a client that never cleared would pass a
// test that only looked for an error.
const assert = require("assert");
const path = require("path");
const vscode = require("vscode");

async function waitFor(what, fn, ms) {
    const deadline = Date.now() + ms;
    for (;;) {
        const got = fn();
        if (got) {
            return got;
        }
        if (Date.now() > deadline) {
            throw new Error(`timed out after ${ms} ms waiting for ${what}`);
        }
        await new Promise((r) => setTimeout(r, 100));
    }
}

exports.run = async function run() {
    const ws = vscode.workspace.workspaceFolders[0].uri.fsPath;
    const doc = await vscode.workspace.openTextDocument(path.join(ws, "broken.gb"));
    await vscode.window.showTextDocument(doc);
    assert.strictEqual(doc.languageId, "gbasic", "a .gb file is gBASIC");

    const diags = await waitFor("a diagnostic on broken.gb", () => {
        const d = vscode.languages.getDiagnostics(doc.uri);
        return d.length ? d : null;
    }, 30000);
    console.log(`diagnostic: [${diags[0].source}] ${diags[0].message} at ${diags[0].range.start.line}:${diags[0].range.start.character}`);
    assert.strictEqual(diags[0].source, "gbasic", "it came from the gBASIC server");
    assert.match(diags[0].message, /syntax error/);
    assert.strictEqual(diags[0].range.start.line, 0, "on the line the error is on");
    assert.strictEqual(diags[0].severity, vscode.DiagnosticSeverity.Error);

    // Fix it IN THE EDITOR (unsaved): the server works from the open buffer.
    const edit = new vscode.WorkspaceEdit();
    edit.replace(doc.uri, new vscode.Range(0, 0, 0, doc.lineAt(0).text.length), "x = 1");
    assert.ok(await vscode.workspace.applyEdit(edit));
    await waitFor("the diagnostic to clear", () => vscode.languages.getDiagnostics(doc.uri).length === 0, 30000);
    console.log("diagnostics cleared after the fix");
};
