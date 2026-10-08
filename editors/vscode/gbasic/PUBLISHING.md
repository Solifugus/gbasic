# Publishing the gBASIC extension

The extension is Marketplace-ready (icon, license, changelog, metadata). These
are the steps to publish — do them when you want the listing live. Nothing here
runs automatically.

## One-time setup

1. **Install the dependencies** (`vsce` is one of them, so nothing global):
   ```sh
   npm ci
   ```
2. **Create a publisher** at <https://marketplace.visualstudio.com/manage>.
   The `publisher` field in `package.json` is currently `tedderland` — change it
   if you register a different publisher id.
3. **Create an Azure DevOps Personal Access Token** (scope: *Marketplace →
   Manage*) — see <https://code.visualstudio.com/api/working-with-extensions/publishing-extension>.
   Then:
   ```sh
   npx vsce login tedderland     # paste the PAT
   ```

## Each release

```sh
cd editors/vscode/gbasic

npm run package              # builds gbasic-<version>.vsix (respects .vscodeignore)
# inspect what's bundled:
npx vsce ls --no-dependencies

npx vsce publish --no-dependencies   # or add patch|minor|major to bump the version
```

Bump `version` in `package.json` and add a `CHANGELOG.md` entry each release.

## Also publish to Open VSX (for VSCodium / Cursor / Gitpod users)

```sh
npm install -g ovsx
ovsx create-namespace tedderland -p <openvsx-token>   # one-time
ovsx publish gbasic-<version>.vsix -p <openvsx-token>
```

## What ships in the .vsix

Included (measured with `npx vsce ls --no-dependencies`, 11 files, about 116 KB):
`package.json`, `README.md`, `CHANGELOG.md`, `LICENSE`, `icon.png`,
`language-configuration.json`, `syntaxes/gbasic.tmLanguage.json`, `snippets/`,
and `dist/extension.js` (the client, with `vscode-languageclient` bundled in).
Excluded (via `.vscodeignore`): `install.sh`, `PUBLISHING.md` source helpers,
SVGs, dotfiles.

> The `install.sh` path (copying into `~/.vscode/extensions`) is for local dev
> use; published users install from the Marketplace instead.
