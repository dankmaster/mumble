# Theme Color Audit

Date: 2026-03-30

## Scope

This audit covers the Windows-facing theme work for:

- `Mumble / Dark`
- `Mumble / Lite`
- `Catppuccin / Mocha`

The audit was rerun after the theme-token refactor and the runtime cleanup in
`src/mumble/`.

## Inventory Commands

The post-change inventory used these searches:

```powershell
$files = Get-ChildItem src,themes -Recurse -File -Include *.qss,*.cpp,*.h,*.ui
$files | Select-String -Pattern '#[0-9a-fA-F]{6}\b'
$files | Select-String -Pattern '\brgba?\s*\('

$srcFiles = Get-ChildItem src -Recurse -File -Include *.cpp,*.h
$srcFiles | Select-String -Pattern 'QPalette|setPalette|setStyleSheet'
```

An extra broad pass was also used to catch color sources that those commands do
not find, for example `QColor(...)` and `Qt::red`:

```powershell
$srcFiles | Select-String -Pattern 'QColor\s*\(|QColorConstants|Qt::(red|green|yellow|blue|cyan|magenta|black|white|gray|darkGray|lightGray|transparent)'
```

## Central Theme Source

The active runtime theme tokens now live in:

- `src/mumble/UiTheme.cpp`

This file is the single runtime source of truth for:

- `crust`
- `mantle`
- `base`
- `surface0`
- `surface1`
- `surface2`
- `text`
- `subtext0`
- `overlay0`
- `accent`
- `accentSubtle`
- `red`
- `green`
- `yellow`
- `peach`
- `mauve`
- `lavender`
- `teal`
- `pink`
- `rosewater`

The token tables in `UiTheme.cpp` are intentional theme definitions, not stray
hardcoded colors.

## Runtime Sources Converted To Theme Tokens

These files now consume `activeUiThemeTokens()` and no longer carry their own
UI palette values for the themed surfaces they control:

- `src/mumble/MainWindow.cpp`
- `src/mumble/ConfigDialog.cpp`
- `src/mumble/UserView.cpp`
- `src/mumble/AudioConfigDialog.cpp`
- `src/mumble/SearchDialog.cpp`
- `src/mumble/BanEditor.cpp`
- `src/mumble/Cert.cpp`
- `src/mumble/PluginUpdater.cpp`
- `src/mumble/Themes.cpp`
- `src/mumble/ConnectDialog.cpp`
- `src/mumble/UserModel.cpp`

These cover the main window chrome, chat area, server navigator/sidebar, input
bar, configuration dialog, validation states, notice surfaces, and several
status highlights.

## Remaining `src/` Hex Hits

After cleanup, the remaining hex hits in `src/` are:

### Intentional theme definitions

- `src/mumble/UiTheme.cpp`
  - Contains the runtime token values for the three supported themes.
  - Classification: intentional

### Intentional functional exceptions

- `src/mumble/AudioWizard.cpp`
  - Positional-audio graph colors:
    - `#56b4e9`
    - `#009e73`
    - `#d55e00`
  - Classification: intentional functional visualization colors

- `src/mumble/AudioWizard.ui`
  - Rich-text explanation matching the same positional-audio graph colors.
  - Classification: intentional functional visualization colors

### Non-runtime text/comment

- `src/mumble/ApplicationPaletteTemplate.h`
  - Example documentation comment containing `#ff0000`.
  - Classification: comment only, non-runtime

## Remaining `src/` `rgb(...)` / `rgba(...)` Hits

After cleanup, the remaining `rgb` / `rgba` hits in `src/` are:

- `src/mumble/UiTheme.cpp`
  - `uiThemeQssColor(...)` serializes token colors into QSS-safe `rgba(...)`.
  - Classification: helper, not a hardcoded palette source

- `src/mumble/MainWindow.cpp`
  - Win32 `RGB(...)` conversion helper used for native title-bar colors.
  - Classification: token bridge, not a hardcoded palette source

## Remaining Theme Definition Files

These files still contain many hex literals by design because they are the
theme definition source files:

- `themes/Default/Dark.qss` - 194 hits
- `themes/Default/Lite.qss` - 166 hits
- `themes/Catppuccin/Mocha.qss` - 187 hits

These are intentional.

The following files also contain theme definitions, but they are macOS-specific
variants and were out of scope for the Windows task:

- `themes/Default/OSX Dark.qss` - 194 hits
- `themes/Default/OSX Lite.qss` - 166 hits

## Remaining Non-Theme Color Sources From The Broad `QColor(...)` Audit

These are still present and intentionally excluded from the theme-token system:

### Functional audio meters and charts

- `src/mumble/AudioConfigDialog.cpp`
- `src/mumble/AudioStats.cpp`
- `src/mumble/AudioWizard.cpp`

Reason:

- The VU meter and related audio graphs intentionally keep their red/yellow/green
  semantics.
- This matches the design requirement to preserve the transmission meter
  gradient.

### Overlay and editor tooling

- `src/mumble/Overlay.cpp`
- `src/mumble/OverlayClient.cpp`
- `src/mumble/OverlayConfig.cpp`
- `src/mumble/OverlayEditorScene.cpp`
- `src/mumble/OverlayPositionableItem.cpp`
- `src/mumble/OverlayText.cpp`
- `src/mumble/OverlayUserGroup.cpp`
- `src/tests/OverlayTest.cpp`

Reason:

- These colors belong to overlay rendering, editor handles, instructional text,
  checker/guide visuals, alpha buffers, or test fixtures rather than the main
  application theme surfaces.

### User-configurable or utility visuals

- `src/mumble/Settings.cpp`
- `src/mumble/Settings.h`
- `src/mumble/TalkingUI.cpp`
- `src/mumble/TalkingUIEntry.cpp`
- `src/mumble/LookConfig.cpp`

Reason:

- These colors are user-configurable overlay/talking UI defaults, preview
  swatches, or transparent utility buffers.

### Other intentional exceptions

- `src/mumble/ManualPlugin.cpp`
  - Manual positional speaker dots.
  - Classification: functional visualization

- `src/mumble/SvgIcon.cpp`
  - Transparent rendering buffer.
  - Classification: non-theme utility

- `src/mumble/Messages.cpp`
  - Uses stylesheet injection for special message/theme overrides.
  - Classification: intentional special-case behavior

## Visual Verification

A local capture pass was run with the dev profile already configured for:

- `theme = Catppuccin`
- `theme_style = Mocha`

Artifacts:

- Screenshot: `C:\Users\fquist\AppData\Local\MumbleDevClient\state\ui-review\captures\20260330-091921\main-window.png`
- Manifest: `C:\Users\fquist\AppData\Local\MumbleDevClient\state\ui-review\captures\20260330-091921\capture.json`

Result:

- The captured main window renders with the Catppuccin Mocha palette rather
  than the older Mumble Dark chrome.

