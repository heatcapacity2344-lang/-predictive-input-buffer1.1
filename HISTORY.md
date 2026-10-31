# History

## v2.87.0
- Renamed: developer abhyu, id `abhyu.input-buffer`, pipe `abhyu-input-buffer-testing`. In-game name stays "Testing". The mod picture is blank.
- First start copies the data of the previous mod folder once (config, level memory, run-check data, per-level profiles, stage). The old folder is not changed.
- Four named custom slots A-D (stages 6-9), up to 128 ticks (wave up to 64), live up/down keys and Alt+1..4 slot keys. The stage key cycles through them. The stage is stored in config.json and the "off" stage is now number 10.
- Default keys for the in-game output toggles; new off-by-default toggles for the watermark and the level complete label.
- config.json has a version number; a file from another version is backed up before it is upgraded.
