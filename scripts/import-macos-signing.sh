#!/bin/bash
set -euo pipefail
: "${APPLE_CERTIFICATE:?Missing APPLE_CERTIFICATE secret}"
: "${APPLE_CERTIFICATE_PASSWORD?Missing APPLE_CERTIFICATE_PASSWORD secret}"
signing_root=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/yuan-signing.XXXXXX")
keychain="$signing_root/signing.keychain-db"
keychain_password=$(openssl rand -hex 24)
original_keychains=()
while IFS= read -r entry; do
  original_keychains+=("$entry")
done < <(security list-keychains -d user | sed -e 's/^[[:space:]]*//' -e 's/^"//' -e 's/"$//')
cleanup() {
  security list-keychains -d user -s "${original_keychains[@]}" || true
  security delete-keychain "$keychain" >/dev/null 2>&1 || true
  rm -f "$signing_root/certificate.p12"
  rmdir "$signing_root" 2>/dev/null || true
}
trap cleanup EXIT
umask 077
printf '%s' "$APPLE_CERTIFICATE" | base64 --decode > "$signing_root/certificate.p12"
security create-keychain -p "$keychain_password" "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 1800 "$keychain"
security list-keychains -d user -s "$keychain" "${original_keychains[@]}"
security import "$signing_root/certificate.p12" -k "$keychain" \
  -P "$APPLE_CERTIFICATE_PASSWORD" -T /usr/bin/codesign >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
  -k "$keychain_password" "$keychain" >/dev/null
identity=$(security find-identity -v -p codesigning "$keychain" \
  | awk '/Developer ID Application/ { print $2; exit }')
if [[ -z "$identity" ]]; then
  echo '::error::Certificate must be a valid Developer ID Application identity with a private key.'
  exit 1
fi
export APPLE_SIGNING_IDENTITY="$identity"
export APPLE_KEYCHAIN_PATH="$keychain"
unset APPLE_CERTIFICATE APPLE_CERTIFICATE_PASSWORD keychain_password
python3 scripts/sign-macos.py
