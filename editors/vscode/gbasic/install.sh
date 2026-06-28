#!/bin/sh
# Install the gBASIC VS Code extension for the current user (no packaging needed).
set -e

SRC="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DEST="$HOME/.vscode/extensions/tedderland.gbasic-0.1.0"

mkdir -p "$DEST"
cp -R "$SRC/package.json" "$SRC/language-configuration.json" "$SRC/syntaxes" "$SRC/snippets" "$DEST/"
[ -f "$SRC/README.md" ] && cp "$SRC/README.md" "$DEST/"

echo "installed: $DEST"
echo "Reload VS Code (Command Palette -> 'Developer: Reload Window') and open a .gb file."
echo "For .bas files, open them inside a workspace that sets files.associations (see editors/README.md)."
