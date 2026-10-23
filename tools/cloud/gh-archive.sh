#!/usr/bin/env bash
# Part of tools/cloud (see prepare-cloud-build.sh). Exits non-zero only if the curl fallback fails too.
# vcpkg x-script asset source: rebuilds GitHub /archive/ tarballs via git (proxy blocks archive endpoint).
# usage: gh-archive.sh <url> <sha512> <dst>
set -uo pipefail
url="$1"; sha="${2,,}"; dst="$3"
re='^https://(github\.com/([^/]+)/([^/]+)/archive/(.+)\.tar\.gz|codeload\.github\.com/([^/]+)/([^/]+)/tar\.gz/(.+))$'
if [[ "$url" =~ $re ]]; then
  if [ -n "${BASH_REMATCH[2]}" ]; then o=${BASH_REMATCH[2]}; r=${BASH_REMATCH[3]}; ref=${BASH_REMATCH[4]}; else o=${BASH_REMATCH[5]}; r=${BASH_REMATCH[6]}; ref=${BASH_REMATCH[7]}; fi
  ref=${ref#refs/tags/}; ref=${ref#refs/heads/}
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  git init -q "$tmp/g"
  if git -C "$tmp/g" fetch -q --depth 1 "https://github.com/$o/$r" "refs/tags/$ref:refs/x" 2>/dev/null \
     || git -C "$tmp/g" fetch -q --depth 1 "https://github.com/$o/$r" "$ref:refs/x" 2>/dev/null \
     || { git -C "$tmp/g" fetch -q --depth 1 "https://github.com/$o/$r" "$ref" && git -C "$tmp/g" update-ref refs/x FETCH_HEAD; }; then
    # GitHub names the top directory after the canonical repo name, whose case the URL may not keep.
    case "${r,,}" in ffmpeg) cr=FFmpeg ;; *) cr=$r ;; esac
    for p in "$cr-$ref" "$cr-${ref#v}" "$r-$ref" "$r-${ref#v}" "$r-$(git -C "$tmp/g" rev-parse refs/x^{commit})"; do
      git -C "$tmp/g" archive --format=tar --prefix="$p/" refs/x | gzip -n > "$tmp/out"
      if [ -z "$sha" ] || [ "$(sha512sum "$tmp/out" | cut -d' ' -f1)" = "$sha" ]; then mv "$tmp/out" "$dst"; echo "gh-archive: $url <- git ($p)"; exit 0; fi
    done
    echo "gh-archive: sha512 mismatch for $url" >&2
  fi
fi
curl -fsSL --retry 2 -o "$dst" "$url"
