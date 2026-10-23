#!/usr/bin/env bash
# BFME1 Mobile: fail if the tree tracks game assets, build outputs or secrets.
# Game data from EA (archives, models, maps, text, audio, video) and signing keys never go into git.
# The few binaries below came with EA's original GPL source release and are allowed by path.
#
#   tools/ci/check-forbidden-files.sh          # check the tracked files of the working tree
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

forbidden='\.(big|bik|vp6|csf|str|map|wak|w3d|apk|aab|apks|keystore|jks|p12|pfx|exe|dll|msi|mp3|wav)$|(^|/)google-services\.json$|(^|/)GeneralsOnlineRelease/'

# Paths shipped in EA's source release (or synthetic test fixtures) that match the pattern above.
allowed='^(Core/Libraries/Source/WWVegas/WW3D2/RequiredAssets/[^/]+\.w3d|Core/Tools/W3DView/Res/[^/]+\.w3d|Core/Tools/Autorun/(English/)?[^/]+\.wav|Core/Tools/wolSetup/WOLAPI/[^/]+\.dll|Generals(MD)?/Run/BrowserEngine\.dll|tests/.+/fixtures(-synthetic)?/.+)$'

bad=$(git ls-files | grep -iP "$forbidden" | grep -vP "$allowed" || true)
if [ -n "$bad" ]; then
    echo "Forbidden files are tracked (game assets, build outputs or secrets):" >&2
    echo "$bad" | sed 's/^/  /' >&2
    echo "See CONTRIBUTING.md. Remove them with: git rm --cached <file>" >&2
    exit 1
fi
echo "check-forbidden-files: ok ($(git ls-files | wc -l) tracked files)"
