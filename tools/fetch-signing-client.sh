#!/usr/bin/env bash
# Fetch Microsoft's Artifact Signing client -- the signtool plug-in
# (Azure.CodeSigning.Dlib.dll) that tools/build-msix.sh uses with MSIX_SIGN=azure
# -- into build/signing/client (git-ignored). PINNED by version and SHA-256, so
# a release is signed by the plug-in that was checked, not by whatever nuget.org
# serves that day. To move the pin: change both lines, from a download you have
# looked at.
#
# The client needs the .NET 8 runtime (x64) and an `az login`.
set -euo pipefail
cd "$(dirname "$0")/.."

version="1.0.128"
sha256="74bd7d27e6ce1051409c38d9b46bc8df0400ecd643d51ffbf2ac00869061e40b"
url="https://api.nuget.org/v3-flatcontainer/microsoft.artifactsigning.client/$version/microsoft.artifactsigning.client.$version.nupkg"

mkdir -p build/signing
pkg="build/signing/client.nupkg"
if [ ! -f "$pkg" ] || ! printf '%s  %s\n' "$sha256" "$pkg" | sha256sum -c --status; then
    curl -sSfL -o "$pkg" "$url"
fi
if ! printf '%s  %s\n' "$sha256" "$pkg" | sha256sum -c --status; then
    echo "fetch-signing-client: $pkg does not match the pinned SHA-256" >&2
    exit 1
fi
rm -rf build/signing/client
unzip -q -o "$pkg" -d build/signing/client
printf 'Artifact Signing client %s in build/signing/client\n' "$version"
