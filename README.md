# Testing

Practice use only; do not use for verified or leaderboard runs.

This mod runs in the background and has no settings, menus or buttons inside Geometry Dash or Geode. It draws nothing and plays nothing unless you switch that output on (config.json or the external console).

- All settings are in `config.json` in the mod's save folder; a live console window opens with the game.
- External console: named pipe `\\.\pipe\abhyu-input-buffer-testing` (see `tools/pipe-test.ps1`).
- Keys work only while Geometry Dash is the active window. Defaults: F6 / Shift+F6 stage next / back, F7 low latency, Shift+F7 performance mode, F8 export, Shift+F8 run check, F9 training mode, Shift+F9 wave auto-release, F10 path view, Shift+F10 practice only, F11 forget level, F12 stats, Ctrl+F1/F2/F3 presets Safe / Balanced / Aggressive.
- Custom slots A-D (stages 6-9, up to 128 ticks, nameable): Alt+1..4 jump to a slot, Ctrl+F12 / Ctrl+F11 add / remove 1 frame live. Every key can be changed in config.json (`bind.<action>`).
- In-game output toggles (all off by default): Shift+F11 death heatmap, Shift+F12 save ding, Ctrl+F6 hint text, Ctrl+F7 AUTO INPUTS tag, Ctrl+F8 TPS text, Ctrl+F9 watermark, Ctrl+F10 level complete label.
