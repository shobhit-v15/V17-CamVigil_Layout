# CamVigil Changelog

## [Phase 0] GroupRepository and grouping schema bootstrap

- Added GroupRepository (`group_repository.h` / `group_repository.cpp`) to manage camera grouping schema.
- GroupRepository opens the main SQLite DB and ensures `camera_groups` and `camera_group_members` tables on startup.
- Integrated GroupRepository schema check into the archive/database initialisation path (no UI or behaviour changes).

## [Phase 1] Live view grouping and toolbar selector

- Integrated GroupRepository into `MainWindow` to map `cameras.json` entries into the `cameras` table.
- On startup, if no groups exist in the database, a default `All Cameras` group is created containing all cameras.
- Added an in-memory group model in `MainWindow` and a group selector combo box in the toolbar.
- Live view pagination now respects the selected group, while preserving existing behaviour via the `All Cameras` group.
- Tweaked the toolbar group selector combo box to be more compact and equal in height to other toolbar buttons, and added logging when opening the Settings window.
- Wired the toolbar group selector combo box to emit `groupChanged` so that selecting a group updates the live view grid.

## [Phase 2] Grouping bar in Settings

- Added `CameraGroupingWidget`, a reusable toolbar-like widget showing group and camera selectors plus an edit button.
- Extended the grouping widget to support creating/deleting user groups, enforcing single-group membership (besides the default All Cameras), and assigning cameras via an inline panel.
- Refactored `CameraDetailsWidget` to embed the new grouping bar (groups combo, cameras combo, Edit button) while keeping the existing “Name + Save” logic for renaming cameras.
- Simplified the Settings window archive section to avoid crashes from the legacy archive browser while archive browsing remains available via the Playback window.

## [Phase 3A] Live view reacts to group edits from Settings

- Propagated group/membership changes from `CameraGroupingWidget` (in Settings) up through `CameraDetailsWidget` and `SettingsWindow` to `MainWindow`.
- `MainWindow::reloadGroupsFromDb()` is now invoked whenever groups are created, deleted, or camera assignments are edited in Settings.
- Toolbar group selector and live-view pagination update immediately after changes, without restarting the application.

## [Phase 4A] Group-aware camera selection in Playback

- Extended `PlaybackControlsWidget` with a group selector combo next to the camera combo.
- Added a playback-side group model backed by `GroupRepository`, intersecting groups with cameras that currently have recordings.
- Playback now filters the camera combo by the selected group: “All Cameras” shows every recorded camera, while user groups list only their own cameras that have footage.
- When the grouping schema is unavailable, playback falls back to the previous all-camera behaviour with the group combo disabled.

## [Phase 4B] Playback camera-list fallback without recordings

- Added `GroupRepository::listAllCameras()` and taught `PlaybackWindow` to fall back to configured cameras when `DbReader` reports no recordings yet.
- The playback group selector now filters real cameras even before any segments exist, so groups remain visible/usable in an empty archive.
- Removed the fake `"All Cameras"` camera entry; the label is now used only for the group selector while the camera combo stays empty when no cameras are available.
