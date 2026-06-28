#!/bin/sh
# Install the gBASIC Kate/KSyntaxHighlighting definition for the current user.
set -e

SRC="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gbasic.xml"

# Modern KSyntaxHighlighting (Kate/KWrite 5.x/6.x) and the older katepart5 path.
DIRS="$HOME/.local/share/org.kde.syntax-highlighting/syntax $HOME/.local/share/katepart5/syntax"

for dir in $DIRS; do
    mkdir -p "$dir"
    cp "$SRC" "$dir/gbasic.xml"
    echo "installed: $dir/gbasic.xml"
done

echo "Done. Fully restart Kate, then open a .bas or .gb file."
