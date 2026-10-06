// gBASIC for VS Code: starts gbasic-lsp, the language server in the gBASIC
// repository (src/lsp), so errors are underlined AS YOU TYPE rather than only
// when a run task fails.
//
// FINDING THE SERVER, in order:
//   1. the `gbasic.lsp.path` setting, if set -- an explicit choice wins;
//   2. `gbasic-lsp` on PATH -- what an installed gBASIC provides (the MSIX
//      package adds it as an execution alias, `make install` puts it in bin/).
// If neither exists the extension still highlights, and says ONCE, in a
// message with a link to the setting, that live errors need the server. It does
// not fail activation: a missing server costs one feature, not the extension.
import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

/** The first file named `name` (with Windows' executable extensions) on PATH. */
function onPath(name: string): string | undefined {
    const dirs = (process.env.PATH || "").split(path.delimiter).filter(Boolean);
    const exts = process.platform === "win32"
        ? (process.env.PATHEXT || ".EXE;.CMD;.BAT").split(";").map((e) => e.toLowerCase())
        : [""];
    for (const dir of dirs) {
        for (const ext of exts) {
            const candidate = path.join(dir, name + ext);
            try {
                // An MSIX execution alias is a zero-byte reparse point that
                // stat() cannot follow, so existence is asked with lstat.
                fs.lstatSync(candidate);
                return candidate;
            } catch {
                // keep looking
            }
        }
    }
    return undefined;
}

export function findServer(): { path?: string; why: string } {
    const configured = vscode.workspace.getConfiguration("gbasic").get<string>("lsp.path", "").trim();
    if (configured) {
        return fs.existsSync(configured)
            ? { path: configured, why: "the gbasic.lsp.path setting" }
            : { why: `gbasic.lsp.path is set to "${configured}", which does not exist` };
    }
    const found = onPath("gbasic-lsp");
    return found ? { path: found, why: "PATH" } : { why: "gbasic-lsp is not on PATH" };
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const server = findServer();
    if (!server.path) {
        const open = "Open setting";
        vscode.window
            .showInformationMessage(
                `gBASIC: highlighting only -- live errors need the gbasic-lsp server (${server.why}).`,
                open,
            )
            .then((choice) => {
                if (choice === open) {
                    vscode.commands.executeCommand("workbench.action.openSettings", "gbasic.lsp.path");
                }
            });
        return;
    }

    const serverOptions: ServerOptions = {
        run: { command: server.path, transport: TransportKind.stdio },
        debug: { command: server.path, transport: TransportKind.stdio },
    };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: "file", language: "gbasic" },
            { scheme: "untitled", language: "gbasic" },
        ],
    };
    client = new LanguageClient("gbasic", "gBASIC language server", serverOptions, clientOptions);
    context.subscriptions.push(client);
    await client.start();
}

export async function deactivate(): Promise<void> {
    if (client) {
        await client.stop();
        client = undefined;
    }
}
