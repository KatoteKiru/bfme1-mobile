# BFME1 SAF importer (E1-12a)

Package: `com.generalsx.zerohour.importer`

- **Manifest source of truth:** `docs/bfme1-manifest.json` (E1-24 / Formats schemaVersion 1). Gradle copies it into the APK as `assets/bfme/bfme1-manifest.json` (see `android/app/build.gradle` task `copyBfme1Manifest`). Do not commit a copy under `assets/`.
- **Schema (PR #33):** top-level `schemaVersion`, `defaultProfile`, `profiles`; each profile has `game`, `displayVersion`, `files[]` with `path` / `size` / `sha256` / `required` / `kind` (`big` | `loose` | `win-binary`). Profile is selected by root `defaultProfile` (or an explicit key / sole profile). UI shows `displayVersion` via `GameDataManifest.displayLabel()`.
- **Layout:** `getExternalFilesDir(null)/GameData/` + sibling `import-state.json`.
- **Gate:** `GameDataReady` — `status: complete` + size check against verified entries.
- **FGS:** `GameDataImportService` (`dataSync`; always FGS even if `POST_NOTIFICATIONS` denied).
- **Tree walk:** `SafTreeWalker` via `DocumentsContract` (case-folded index).
- **N in «Готово, версия …, N файлов проверено»:** number of importable (non `win-binary`) files actually verified in GameData. N = 336 only with the full set; optional files absent from the source folder are not counted. A folder lacking any `required` file never reaches the import (red precheck).
- **Manifest in the APK is fixed** (`assets/bfme/bfme1-manifest.json` from docs); there is no runtime switch to a test manifest.
