# Windows Direct Installer Design

Date: 2026-11-14
Status: Written design approved by the user on 2026-11-14; implementation in progress.
Repositories: `iyw-claw-installer`, `iyw-claw`, and `iyw-fusion-api`.

## Problem

The current Windows bootstrapper is a wrapper around the Tauri NSIS installer.
`iyw-claw-installer/src/install_windows.cpp` starts the downloaded `setup.exe`
with `/S /UPDATE`; `iyw-claw` still downloads and installs the `nsis` artifact;
Fusion's public `bootstrapper` artifact is only the C++ entry point. The UI and
progress are unified, but NSIS still owns application deployment, WebView2,
environment preparation, registration, and rollback.

That does not meet the requested outcome: the new program must perform both
fresh Windows installation and Windows application updates itself.

## Goals and Acceptance

1. On supported Windows x64, fresh install and update use one C++ program and
   neither path launches an NSIS executable.
2. The new path downloads a complete application payload, verifies its release
   identity, SHA-256, Minisign signature, and Authenticode signatures, then
   deploys it into the existing `root/app` layout.
3. Healthy managed environment components are reused. Missing or changed
   components are prepared through the existing environment helper; the app
   version changing alone does not trigger a full environment scan.
4. Deployment stages beside the live app, retains the old app as a recovery copy
   until the new app and required environment checks pass, and restores it on
   failure or interrupted transaction recovery.
5. Existing `nsis` artifacts and Tauri updater response fields remain available
   to old clients. Historical releases that lack the new payload keep the old
   update path.
6. macOS/Linux native archive installation is unchanged. Windows 7 and i686
   remain on their existing NSIS path until matching payloads are explicitly
   built and verified.

## Chosen Approach

Add a signed `app_payload_zip` release artifact alongside the existing Windows
`nsis` artifact. New Windows x64 clients select and cache the payload, then
handoff its release identity and local cache path to the installed C++ wizard.
Direct launch of the wizard selects the latest compatible payload. Older apps
continue receiving the original NSIS URL and old Tauri clients ignore the new
optional payload metadata.

The downloaded payload contains only the installed app files, environment helper,
Tauri resources, staged worker resources and native-share resources. The standalone
bootstrapper is served separately by Fusion and downloads this payload in the same
way the existing installer downloads its application package. Do not embed the
bootstrapper into NSIS or the app payload. The payload contains no user data or
managed runtime directories.

The payload archive uses the existing Tauri Minisign release key and Fusion
artifact SHA-256 metadata. Each executable within it also keeps its required
Authenticode identity. The C++ program verifies the archive signature and
digest before extraction, validates every relative path against traversal and
reparse-point escape, and verifies the installed main executable and helper.
Windows statically links the existing portable crypto/archive choices,
libsodium for Minisign verification and libarchive for ZIP extraction, so
verification and extraction do not depend on optional target-machine tools.

## Components and Data Flow

### `iyw-claw`

- Build the normal signed app as today, then create the deterministic app
  payload ZIP from the exact staged app tree.
- Keep producing the NSIS artifact for legacy clients and the existing Tauri
  updater contract.
- New Windows x64 update checks consume optional same-release payload metadata.
  New clients predownload/cache that payload and pass its version, release ID,
  channel, target, architecture, size, SHA-256, signature, URL, and cache path
  to the C++ wizard.
- If payload metadata is absent, use the existing NSIS update path. This is
  required for historical releases and remains available to old clients.
- Preserve activity guards, rollout decisions, user confirmation, and silent
  update policy. The new installer must not choose a different release from the
  one the app offered.

### `iyw-fusion-api`

- Accept `app_payload_zip` only for `desktop/windows/x86_64`.
- Keep existing `/app-updates/v1/check`, `/tauri/check`, and `latest` behavior
  for legacy clients and public bootstrapper discovery.
- For new payload-capable checks, select the payload artifact bound to the same
  release ID as the normal NSIS manifest. Return payload URL, size, SHA-256,
  and Minisign signature as optional additive metadata; never substitute a
  payload from another release.
- New Windows x64 releases must publish both NSIS and payload artifacts before
  they are considered ready for payload-capable updates. Historical releases
  remain valid without a payload.

### `iyw-claw-installer`

- Add payload-aware resolution for direct fresh install and update handoff.
- Replace the Windows `InstallPackage` NSIS launch with direct deployment of
  the verified payload archive.
- Use the existing Windows process-control compatibility path and serialized
  deployment lock. Do not widen process termination to unrelated processes.
- Stage to a unique directory under the existing installation root, validate
  required files and signatures, and prepare WebView2 before touching `app`.
- Check WebView2 with a fast local probe. If missing, fetch the pinned Microsoft
  Evergreen bootstrapper over HTTPS, verify its Microsoft Authenticode
  publisher, and install it using the existing per-user/elevation policy.
- Close the application, retain the current app backup, and activate the staged
  app tree. Run the staged environment helper's existing `install --phase
  prepare` against the shared managed-runtime root, then commit the returned
  pending environment transaction only after app and registration checks pass.
  Commit the outer app transaction last. A journal records the app-swap and
  environment-commit states for crash recovery.
- Persist the install root and preserve current-user registration and shortcuts.
  Add an uninstall entry point to the same signed wizard so direct installs do
  not become orphaned. Uninstall preserves user data by default and only removes
  application-owned files; a full purge remains an explicit separate choice.
- Copy/update the signed wizard outside `app`, so replacing the app cannot
  replace the running installer.
- Keep NSIS launch only as an explicit compatibility fallback when payload
  metadata is missing or a platform does not support the direct payload.

## Failure and Recovery

- Before deployment, failures in metadata validation, download, signature
  verification, or extraction leave the live app untouched and return to the
  existing app update UI with a diagnostic reason.
- During deployment, keep the previous `app` as the recovery copy until the new
  executable, helper identity, environment receipt, registration, and required
  launch files validate. A journal distinguishes prepared, swapped,
  environment-committed, and app-committed states after process interruption.
- If environment prepare or commit fails after the app swap, the environment
  helper rolls back its uncommitted component activation and the wizard restores
  the old app. If interruption occurs after environment commit, recovery checks
  the new app and receipt and finishes the app commit; it does not silently
  restore only half of the transaction. Do not start an NSIS update concurrently.
- If the new installer is unavailable before deployment, old clients continue
  with NSIS. If a new payload is absent for a historical release, the new
  client uses the existing NSIS fallback.
- Never delete runtime, inventory, settings, sessions, projects, or other user
  data as part of app replacement.

## Release and Migration Order

1. Add and validate the payload artifact contract while leaving NSIS selection
   unchanged for all existing clients.
2. Publish a signed C++ wizard that can install and update `app_payload_zip`,
   and update the public installer download page to that wizard.
3. Publish an application transition release containing both NSIS and payload
   artifacts plus the new client handoff. Existing apps update once through
   NSIS; the new client uses the C++ wizard afterward.
4. Require payload artifacts for subsequent Windows x64 releases. Keep NSIS
   artifacts as long as supported old clients need them.

The Windows installer payload rollout does not change macOS/Linux installer
formats or server updates. It does not add backend database fields unless
implementation proves the current artifact model cannot express the payload.

## Validation

- Build validation checks the archive layout, required files, release version,
  SHA-256, Minisign signature, Authenticode signatures, and absence of paths
  outside the app root.
- Installer validation covers clean install, same-version reuse, upgrade,
  rollback, interrupted swap recovery, occupied files, missing/corrupt cache,
  invalid signature, WebView2 missing, one missing managed component, and
  uninstall with user-data preservation.
- Update validation covers old clients and historical releases (NSIS path), new
  clients with payload metadata (direct path), active-task deferral, silent
  policy, and preservation of the selected release identity.
- Fusion validation covers package-kind acceptance, same-release artifact
  binding, publication readiness, and unchanged legacy/Tauri response shapes.
- Release acceptance verifies signed Windows EXE and ZIP, timestamp, signer,
  archive contents, and an actual launch of the signed wizard. Full Windows 7
  support remains unclaimed until a dedicated payload is built and tested there.
