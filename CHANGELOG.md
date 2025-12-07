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

## [Phase 2] Group editing in Settings

- Extended `CameraDetailsWidget` with a group editing UI (list of groups with checkboxes per camera, plus add/rename/delete controls) backed by `GroupRepository`.
- Camera grouping changes are persisted to the database and a `cameraGroupsChanged` signal notifies `MainWindow`, which reloads groups and updates live-view pagination immediately.
- Simplified the Settings window archive section to avoid crashes from the legacy archive browser while archive browsing remains available via the Playback window.
