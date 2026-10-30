// cfg.hpp - all settings of the mod live here (JSON file in the mod's save folder), not in Geode.
// Pure C++ (no Geode / Cocos / Windows headers) so it can be unit-tested on its own.
// Flat JSON only: {"key": true|number|"text", ...}. Thread-safe.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cfg {

    // ---------- tiny flat-JSON reader / writer ----------
    struct Val {
        enum T { None, Bool, Num, Str } t = None;
        bool b = false;
        double n = 0;
        std::string s;
        static Val boolean(bool v) { Val x; x.t = Bool; x.b = v; return x; }
        static Val number(double v) { Val x; x.t = Num; x.n = v; return x; }
        static Val text(std::string v) { Val x; x.t = Str; x.s = std::move(v); return x; }
    };
    using Obj = std::map<std::string, Val>;

    inline std::string jstr(std::string const& in) {
        std::string o = "\"";
        for (unsigned char c : in) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                    else o += static_cast<char>(c);
            }
        }
        return o + "\"";
    }

    inline std::string jnum(double v) {
        char b[64];
        if (std::floor(v) == v && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
        else std::snprintf(b, sizeof b, "%.10g", v);
        return b;
    }

    inline std::string jval(Val const& v) {
        switch (v.t) {
            case Val::Bool: return v.b ? "true" : "false";
            case Val::Num: return jnum(v.n);
            case Val::Str: return jstr(v.s);
            default: return "null";
        }
    }

    namespace detail {
        inline void ws(std::string const& s, size_t& i) { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
        inline void utf8(std::string& o, unsigned cp) {
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
            else { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
        }
        inline bool str(std::string const& s, size_t& i, std::string& out) {
            if (i >= s.size() || s[i] != '"') return false;
            ++i;
            out.clear();
            while (i < s.size()) {
                char c = s[i++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return false;
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                            else return false;
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }
        inline bool value(std::string const& s, size_t& i, Val& v) {
            ws(s, i);
            if (i >= s.size()) return false;
            if (s[i] == '"') { std::string t; if (!str(s, i, t)) return false; v = Val::text(t); return true; }
            if (s.compare(i, 4, "true") == 0) { i += 4; v = Val::boolean(true); return true; }
            if (s.compare(i, 5, "false") == 0) { i += 5; v = Val::boolean(false); return true; }
            if (s.compare(i, 4, "null") == 0) { i += 4; v = Val(); return true; }
            size_t j = i;
            while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '-' || s[j] == '+' || s[j] == '.' || s[j] == 'e' || s[j] == 'E')) ++j;
            if (j == i) return false;
            char* end = nullptr;
            std::string num = s.substr(i, j - i);
            double d = std::strtod(num.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(d)) return false;
            i = j;
            v = Val::number(d);
            return true;
        }
    }

    // Parses one flat object. Nested objects / arrays are rejected on purpose.
    inline bool parseFlat(std::string const& s, Obj& out, std::string* err = nullptr) {
        auto fail = [&](char const* m) { if (err) *err = m; return false; };
        out.clear();
        size_t i = 0;
        detail::ws(s, i);
        if (i >= s.size() || s[i] != '{') return fail("not an object");
        ++i;
        detail::ws(s, i);
        if (i < s.size() && s[i] == '}') { ++i; detail::ws(s, i); return i == s.size() ? true : fail("trailing text"); }
        for (;;) {
            detail::ws(s, i);
            std::string key;
            if (!detail::str(s, i, key)) return fail("bad key");
            detail::ws(s, i);
            if (i >= s.size() || s[i] != ':') return fail("missing ':'");
            ++i;
            Val v;
            if (!detail::value(s, i, v)) return fail("bad value (only flat objects are accepted)");
            out[key] = v;
            detail::ws(s, i);
            if (i >= s.size()) return fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            return fail("expected ',' or '}'");
        }
        detail::ws(s, i);
        return i == s.size() ? true : fail("trailing text");
    }

    // ---------- keys (hotkeys are plain strings like "F8", "Shift+F6", "Ctrl+F1") ----------
    enum Mod : int { CTRL = 1, SHIFT = 2, ALT = 4 };

    inline std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

    inline bool keyTable(std::string const& name, int& vk) {
        std::string n = lower(name);
        if (n.size() >= 2 && n[0] == 'f' && std::isdigit(static_cast<unsigned char>(n[1]))) {
            int f = std::atoi(n.c_str() + 1);
            if (f >= 1 && f <= 24) { vk = 0x70 + f - 1; return true; }
            return false;
        }
        if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') { vk = 'A' + (n[0] - 'a'); return true; }
        if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') { vk = n[0]; return true; }
        if (n.size() == 4 && n.compare(0, 3, "num") == 0 && n[3] >= '0' && n[3] <= '9') { vk = 0x60 + (n[3] - '0'); return true; }
        static const std::map<std::string, int> names = {
            {"space", 0x20}, {"tab", 0x09}, {"enter", 0x0D}, {"esc", 0x1B}, {"backspace", 0x08}, {"insert", 0x2D},
            {"delete", 0x2E}, {"home", 0x24}, {"end", 0x23}, {"pageup", 0x21}, {"pagedown", 0x22}, {"left", 0x25},
            {"up", 0x26}, {"right", 0x27}, {"down", 0x28}, {"pause", 0x13}, {"scrolllock", 0x91}, {"backquote", 0xC0},
            {"minus", 0xBD}, {"equals", 0xBB}, {"comma", 0xBC}, {"period", 0xBE}, {"slash", 0xBF} };
        auto it = names.find(n);
        if (it == names.end()) return false;
        vk = it->second;
        return true;
    }

    inline bool parseKey(std::string const& text, int& vk, int& mods) {
        vk = 0; mods = 0;
        std::string cur;
        std::vector<std::string> parts;
        for (char c : text) { if (c == '+') { parts.push_back(cur); cur.clear(); } else if (c != ' ') cur += c; }
        parts.push_back(cur);
        bool haveKey = false;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string p = lower(parts[i]);
            if (p == "ctrl" || p == "control") mods |= CTRL;
            else if (p == "shift") mods |= SHIFT;
            else if (p == "alt") mods |= ALT;
            else if (i + 1 == parts.size() && !p.empty() && keyTable(p, vk)) haveKey = true;
            else return false;
        }
        return haveKey;
    }

    inline std::string keyName(int vk, int mods) {
        std::string s;
        if (mods & CTRL) s += "Ctrl+";
        if (mods & SHIFT) s += "Shift+";
        if (mods & ALT) s += "Alt+";
        if (vk >= 0x70 && vk <= 0x87) return s + "F" + std::to_string(vk - 0x70 + 1);
        if (vk >= 0x60 && vk <= 0x69) return s + "Num" + std::to_string(vk - 0x60);
        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return s + static_cast<char>(vk);
        static const std::map<int, char const*> names = {
            {0x20, "Space"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x1B, "Esc"}, {0x08, "Backspace"}, {0x2D, "Insert"}, {0x2E, "Delete"},
            {0x24, "Home"}, {0x23, "End"}, {0x21, "PageUp"}, {0x22, "PageDown"}, {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"},
            {0x28, "Down"}, {0x13, "Pause"}, {0x91, "ScrollLock"}, {0xC0, "Backquote"}, {0xBD, "Minus"}, {0xBB, "Equals"},
            {0xBC, "Comma"}, {0xBE, "Period"}, {0xBF, "Slash"} };
        auto it = names.find(vk);
        return s + (it != names.end() ? it->second : "?");
    }

    // ---------- the settings table ----------
    enum class Kind : char { Bool = 'b', Int = 'i', Float = 'f', Str = 's' };

    struct Spec {
        std::string key;
        Kind kind;
        double def;
        double lo, hi;
        std::string sdef;
        std::string info;
    };

    // Actions a key can run. "toggle:<setting>" exists for every bool setting.
    struct ActionDef { char const* name; char const* defKey; char const* info; };
    inline const std::vector<ActionDef>& actions() {
        static const std::vector<ActionDef> a = {
            {"stage", "F6", "Cycle the buffer stage (forward)"},
            {"stage-back", "Shift+F6", "Cycle the buffer stage (backward)"},
            {"export", "F8", "Copy the log to the clipboard and save console-export.txt"},
            {"run-check", "Shift+F8", "Run check: replay your best recorded run from the start of the next attempt"},
            {"stats", "F12", "Print session stats to the log"},
            {"path", "F10", "Toggle the path view"},
            {"forget", "F11", "Forget this level's memory"},
            {"preset-safe", "Ctrl+F1", "Preset Safe (stage 2)"},
            {"preset-balanced", "Ctrl+F2", "Preset Balanced (stage 3)"},
            {"preset-aggressive", "Ctrl+F3", "Preset Aggressive (stage 5)"},
        };
        return a;
    }
    inline char const* defaultToggleKey(std::string const& setting) {
        if (setting == "low-latency") return "F7";
        if (setting == "performance-mode") return "Shift+F7";
        if (setting == "training-mode") return "F9";
        if (setting == "wave-auto-release") return "Shift+F9";
        if (setting == "practice-only") return "Shift+F10";
        return "";
    }

    inline const std::vector<Spec>& specs() {
        static const std::vector<Spec> all = [] {
            std::vector<Spec> v = {
        {"custom-ticks", Kind::Int, 40, 1, 64, "", R"~(Custom stage: max delay (ticks): Max delay for the CUSTOM F6 stage, all modes except wave. 4 ticks = 1 frame at 60fps.)~"},
        {"custom-wave-ticks", Kind::Int, 20, 0, 64, "", R"~(Custom stage: max wave delay (ticks): Max delay for the CUSTOM F6 stage while in wave.)~"},
        {"sim-budget-ms", Kind::Float, 8.0, 1.0, 30.0, "", R"~(Prediction time budget (ms): Max time spent predicting per click. Higher = finds longer saves on big windows, but may hitch on dense levels.)~"},
        {"smart-prediction", Kind::Bool, 1, 0, 0, "", R"~(Orb / pad / portal prediction: Let the ghost use orbs, pads, gravity portals and speed portals. Turn off if you see glitches with objects.)~"},
        {"ground-horizon", Kind::Int, 130, 48, 240, "", R"~(Cube / ball lookahead (ticks): How far ahead the prediction looks for cube, ball and spider. A cube jump lasts about 100 ticks, so the old 48 never saw the landing. Higher = catches later hits but costs more time per click.)~"},
        {"practice-only", Kind::Bool, 0, 0, 0, "", R"~(Practice mode only: ON: only buffer inputs in practice mode. OFF: buffer in every run.)~"},
        {"wave-horizon", Kind::Int, 65, 20, 160, "", R"~(Wave lookahead (ticks): How far ahead the wave prediction looks (mini wave uses 15 ticks less). 65 = 65 normal / 50 mini. Longer sees crashes further away but costs more time per click.)~"},
        {"orb-assume-hit", Kind::Bool, 1, 0, 0, "", R"~(Orbs: assume you click them (calculation mode): The prediction clicks every orb it touches, as if you hit it on time. Turn off if it predicts deaths for orbs you normally skip.)~"},
        {"orb-try-both", Kind::Bool, 1, 0, 0, "", R"~(Orbs: also try the other choice: If a prediction touches an orb and dies, run it again with the opposite orb choice (click / skip) and keep the better one, like a player who only clicks the orb when needed.)~"},
        {"avoid-moving-objects", Kind::Bool, 0, 0, 0, "", R"~(Don't buffer near moving objects: Experimental. The prediction sees the level frozen at your click. If the ghost's path touches spikes or blocks that moved recently (move/rotate triggers), the input goes through untouched instead of being delayed. Can cancel good saves on levels with lots of movement.)~"},
        {"wave-auto-release", Kind::Bool, 0, 0, 0, "", R"~(Wave auto-release: While you hold in wave, if holding would crash within the window below and letting go clearly survives, the mod lets go for you. Your own release is then ignored. Practice mode only, unless 'Auto-release in normal runs' is on.)~"},
        {"auto-release-window", Kind::Int, 6, 2, 24, "", R"~(Auto-release window (ticks): Auto-release only fires when holding would crash within this many ticks. Small = lets go at the last moment. 4 ticks = 1 frame at 60fps.)~"},
        {"drift-check", Kind::Bool, 0, 0, 0, "", R"~(Drift check (debug): After each input, compare where the prediction said you'd go with where you actually went, and log the biggest gap in the live console. Use it to see how precise the predictions are on a level.)~"},
        {"wave-spam-gap", Kind::Int, 16, 4, 40, "", R"~(Wave spam: max gap (ticks): Presses/releases closer together than this count as spam. After 'Spam: inputs in a row' of them, wave inputs go straight to the game until you pause. 16 ticks = 4 frames at 60fps (about 7 clicks/sec). Raise it if spam still gets buffered, lower it if tight wave parts stop being buffered.)~"},
        {"wave-spam-streak", Kind::Int, 3, 2, 6, "", R"~(Spam: inputs in a row: How many fast presses/releases in a row it takes before spam goes straight to the game (wave, ship, and smart UFO / ball / swing). Lower = spam is let through sooner; higher = more fast inputs are still buffered.)~"},
        {"orb-align", Kind::Bool, 1, 0, 0, "", R"~(Orb alignment: If you press a little before reaching an orb (and the press wouldn't hit it), the press is held back until you're inside the orb so it hits. Works for every mode except ship and wave. Needs 'Orb / pad / portal prediction' on.)~"},
        {"orb-align-ticks", Kind::Int, 16, 1, 64, "", R"~(Orb alignment: max delay (ticks): How early a press may be and still be moved onto the orb. 16 ticks = 4 frames at 60fps.)~"},
        {"wave-v46", Kind::Bool, 0, 0, 0, "", R"~(Wave: old v4.6 logic: Use the old v4.6 wave rules (big-gain bar, 1-tick queued opposite). Off = the cube-style wave: a crash you could still turn away from counts as safe, and the smallest delay that clears the obstacle wins.)~"},
        {"wave-safety-margin", Kind::Bool, 1, 0, 0, "", R"~(Wave: safety margin: When a wave save is found, use a delay with one tick of room on both sides instead of the one that only just clears the spike, so tiny differences in the real run can't clip it. Can add 1 tick (0.25f) to some saves.)~"},
        {"show-path", Kind::Bool, 0, 0, 0, "", R"~(Path view (in-game drawing, off by default). Draws the predicted path after each input. Red = what the input would do now, green = what the game gets, white dot = delayed fire, red dot = crash. Costs two extra predictions per click while on.)~"},
        {"orb-align-ground", Kind::Bool, 0, 0, 0, "", R"~(Orb alignment: also from the ground: Off: presses made while standing on the ground are normal jumps and never get moved onto an orb. On: a ground jump made just before an orb can be turned into an orb hit.)~"},
        {"full-ghost-state", Kind::Bool, 1, 0, 0, "", R"~(Full ghost state (v2.8): Copy the full player state (slopes, timers, forces, dash...) into the prediction ghost. Turn off to go back to the v2.7 ghost if predictions feel worse.)~"},
        {"robot-smart", Kind::Bool, 1, 0, 0, "", R"~(Robot: smart jumps: A robot press only counts as a crash if no hold length saves it (your usual hold, then short to long holds). Saves are found for presses that need a different jump height, and presses you can still control aren't delayed.)~"},
        {"robot-auto-release", Kind::Bool, 0, 0, 0, "", R"~(Robot auto-release: While you hold a robot jump, if holding longer would crash within the auto-release window and letting go survives, the mod lets go for you. Your own release is then ignored. Practice mode only, unless 'Auto-release in normal runs' is on.)~"},
        {"level-memory", Kind::Bool, 1, 0, 0, "", R"~(Level memory (learns from your runs): Remembers, per level and per spot, which delays worked, where saves ended in deaths and where you keep dying. Delays that worked before are tried first; spots where saves keep failing are left alone; orange dots in the path view mark spots where you die a lot. F11 forgets the current level.)~"},
        {"auto-click", Kind::Bool, 0, 0, 0, "", R"~(Auto-click: When you're not holding and doing nothing would crash within the auto-release window, but a click right now (with your usual hold) is fully safe, the mod clicks for you. Your own click, if it still comes, is ignored. Cube, ball, UFO, robot, spider and swing (not wave / ship). Practice mode only, unless 'Auto-release in normal runs' is on; then AUTO INPUTS ON shows on screen. Runs with this aren't legit.)~"},
        {"predict-through-portals", Kind::Bool, 1, 0, 0, "", R"~(Predict through portals: Predictions keep going through mode portals (cube, ship, ball, UFO, wave, robot, spider, swing): the hidden ghost switches mode there instead of the prediction stopping. Dual, solo and teleport portals still stop it. Needs 'Full ghost state' on.)~"},
        {"auto-release-normal-runs", Kind::Bool, 0, 0, 0, "", R"~(Auto-release in normal runs: Let auto-release (wave / robot / dash) and auto-click also work outside practice mode. While it can act, an 'AUTO INPUTS ON' tag is shown on screen. Never use it for verified or leaderboard runs.)~"},
        {"spider-smart", Kind::Bool, 1, 0, 0, "", R"~(Spider: smart flips: A spider flip counts as done once you stand on the new surface for 12 ticks, and a crash you could have avoided by flipping back counts as safe. Stops the spider from delaying flips because of obstacles that need your next click.)~"},
        {"dash-auto-release", Kind::Bool, 0, 0, 0, "", R"~(Dash orb auto-release: While you hold a dash, if holding longer would crash within the auto-release window and letting go survives, the mod lets go for you (your own release is then ignored). Practice mode only, unless 'Auto-release in normal runs' is on.)~"},
        {"perf-log", Kind::Bool, 0, 0, 0, "", R"~(Performance log (debug): After each click, log in the live console how long its predictions took, how many runs and ghost ticks it used, and the cost per ghost tick. Changes nothing about the decisions.)~"},
        {"ship-buffer", Kind::Bool, 1, 0, 0, "", R"~(Ship: buffer inputs: Buffer ship presses and releases. A crash only counts if you couldn't have recovered (the opposite input 8 / 16 / 24 ticks before it is tried), the smallest delay that clears wins, and only very fast taps go straight through (ship spam gap). Off = ship is never touched.)~"},
        {"ship-max-delay", Kind::Int, 4, 0, 16, "", R"~(Ship: max delay (ticks): Ship's own max delay, used instead of the F6 stage. Ship has momentum, so small delays are enough and big ones feel laggy. 4 ticks = 1 frame at 60fps.)~"},
        {"orb-timing", Kind::Bool, 1, 0, 0, "", R"~(Orb timing: For orbs that only work at a specific moment inside them: a press already inside an orb that would crash is moved 1-6 ticks later in the orb if that survives, the orb timing that worked at a spot before is tried first, and predictions also try clicking later orbs 3 and 6 ticks into them.)~"},
        {"cube-late-crash", Kind::Int, 90, 0, 240, "", R"~(Cube: ignore crashes after (ticks): If pressing now only crashes after a normal cube / ball jump would have landed, the crash is the next obstacle's, not this click's: no delay can help, so the mod skips the search (saves time and stutter). 90 ticks = about 0.37 s. 0 = always search.)~"},
        {"predict-moving-objects", Kind::Bool, 0, 0, 0, "", R"~(Predict moving objects (experimental): Spikes, saws and blocks near your path that are moving right now (move / rotate triggers) are moved along their current motion during predictions: straight lines, curved / circular paths and spinning, so saves account for where they will be. Easing (speeding up / slowing down) isn't predicted. Turn 'Don't buffer near moving objects' off when using this.)~"},
        {"robust-saves", Kind::Bool, 1, 0, 0, "", R"~(Robust saves: Before a save is used, it is also tested with you slightly ahead of and behind your real position (about half a tick). Saves that only work at the exact spot are swapped for a nearby delay that survives all three, if there is one. Costs a few extra predictions per save, never per normal click.)~"},
        {"ignore-touching-hazards", Kind::Bool, 1, 0, 0, "", R"~(Ignore hazards you're touching: Spikes / saws that overlap you at the moment of the click while you're alive can't be what kills you (e.g. a spike the level makes follow the player, or noclip), so predictions ignore them. Fixes predictions that died on their very first tick (GRIEF's wave).)~"},
        {"death-log", Kind::Bool, 0, 0, 0, "", R"~(Death log (debug): On every 'no save', log what killed the prediction ghost (object ID, type, where it is relative to you, hitboxes). Ghost deaths in the first 3 ticks are always logged. Use it with F8 when a level predicts badly.)~"},
        {"explain-inputs", Kind::Bool, 0, 0, 0, "", R"~(Explain every input (debug): Also log the inputs that were passed straight to the game and why: safe as it is, spam guard, late-crash rule, max delay 0. Busy log; turn on to see what the mod does in a part, then use F8.)~"},
        {"cbf-sync", Kind::Bool, 1, 0, 0, "", R"~(Click Between Frames sync: With the Click Between Frames mod, a click lands partway through a physics tick. The prediction then starts from that exact moment, and delayed inputs land on the tick the prediction tested. Has no effect without CBF.)~"},
        {"low-latency", Kind::Bool, 0, 0, 0, "", R"~(Low latency mode: For players who feel the buffer as lag or delayed clicks: clicks made while a delay is waiting are not shifted after it (no chain of delayed clicks), delays are at most 1 frame, searches are capped at 3 ms (no frame hitches), and robust saves / wave margin (which can add a tick) are off.)~"},
        {"ufo-smart", Kind::Bool, 1, 0, 0, "", R"~(UFO: smart flaps: A UFO crash only counts if one more flap 6 / 12 / 18 ticks before it couldn't have saved you, landing on something counts as safe, the smallest delay that works wins, and fast flapping goes straight through (spam guard). Off = UFO uses the plain rules like before.)~"},
        {"more-time-death-spots", Kind::Bool, 1, 0, 0, "", R"~(More time at spots you keep dying: At a spot where level memory has seen you die 2+ times, an input that is only safe if your next input comes in time (wave / ship / UFO / spider) gets the small delay that pushes the crash furthest away, so you have more time to react. Normal play elsewhere is not affected. Needs Level memory.)~"},
        {"ship-spam-gap", Kind::Int, 6, 2, 40, "", R"~(Ship: spam gap (ticks): Ship is steered by feathering, so only taps closer together than this count as spam and go straight through. The normal spam gap (16) made almost every ship input spam, so ship was barely buffered. 4 ticks = 1 frame at 60fps.)~"},
        {"ball-smart", Kind::Bool, 1, 0, 0, "", R"~(Ball: smart flips: Ball flips that land after only a few ticks (tight corridors) count as landings, a flip is only called safe after rolling 8 ticks on the new surface (spikes right after the landing spot), and fast flipping goes straight through (spam guard).)~"},
        {"swing-smart", Kind::Bool, 1, 0, 0, "", R"~(Swing: smart flips: A swing flip counts as safe if one more flip in time would have saved it (like the UFO), and fast flipping goes straight through (spam guard).)~"},
        {"training-mode", Kind::Bool, 0, 0, 0, "", R"~(Training mode (hints only): The buffer still predicts every input but never delays it. When an input would have been saved, a hint shows how early you were (e.g. 'click 0.5f early'), so you learn the timing yourself.)~"},
        {"per-level-settings", Kind::Bool, 0, 0, 0, "", R"~(Per-level settings: The switches are remembered for each level and come back when you open it again. The stage (F6) and low latency are the same for every level.)~"},
        {"death-heatmap", Kind::Bool, 0, 0, 0, "", R"~(Death heatmap on the progress bar (in-game output, off by default). Red marks where level memory saw you die.)~"},
        {"save-ding", Kind::Bool, 0, 0, 0, "", R"~(Save ding (in-game sound, off by default). Plays a short sound when a click is saved.)~"},
        {"performance-mode", Kind::Bool, 0, 0, 0, "", R"~(Performance mode: For PCs that drop frames when you click: predictions get at most 4 ms per click, no extra robustness checks, orbs are only tried with a normal click (no late-click retries), and the drift check and live path view are off. Saves can be a little less precise.)~"},
        {"auto-budget", Kind::Bool, 1, 0, 0, "", R"~(Auto time budget: While the game is already dropping frames (a frame took over twice as long as it should, and over 20 ms), clicks get at most 4 ms of prediction time so it doesn't get worse. Otherwise the time budget setting is used as it is.)~"},
        {"tps", Kind::Int, 240, 240, 1000, "", R"~(Physics TPS: Physics steps per second in levels. 240 is the normal game. Higher values change the physics, so runs at anything but 240 aren't legit completions. The buffer's windows scale with it automatically.)~"},
        {"show-hint-text", Kind::Bool, 0, 0, 0, "", R"~(Training hint text (in-game text, off by default). Shows "click 2f early" at the top of the screen in training mode.)~"},
        {"show-auto-tag", Kind::Bool, 0, 0, 0, "", R"~(AUTO INPUTS ON tag (in-game text, off by default). Small label while any auto-release or auto-click option is on.)~"},
        {"show-tps-text", Kind::Bool, 0, 0, 0, "", R"~(TPS text (in-game text, off by default). Small label with the physics TPS (anything but 240 is not a legit run).)~"},
            };
            std::vector<Spec> out(v.begin(), v.end());
            for (auto const& a : actions()) {
                out.push_back({ std::string("bind.") + a.name, Kind::Str, 0, 0, 0, a.defKey, std::string("Key for: ") + a.info });
            }
            for (auto const& s : v) {
                if (s.kind == Kind::Bool) {
                    out.push_back({ "bind.toggle:" + s.key, Kind::Str, 0, 0, 0, defaultToggleKey(s.key), "Key that toggles: " + s.key });
                }
            }
            return out;
        }();
        return all;
    }

    inline Spec const* findSpec(std::string const& key) {
        static const std::unordered_map<std::string, Spec const*> index = [] {
            std::unordered_map<std::string, Spec const*> m;
            for (auto const& s : specs()) m[s.key] = &s;
            return m;
        }();
        auto it = index.find(key);
        return it == index.end() ? nullptr : it->second;
    }

    inline bool isBind(std::string const& key) { return key.rfind("bind.", 0) == 0; }

    class Config {
    public:
        // Loads <dir>/config.json (falls back to config.bak.json, then defaults).
        void init(std::filesystem::path dir) {
            std::lock_guard lock(m_mutex);
            m_dir = std::move(dir);
            std::error_code ec;
            std::filesystem::create_directories(m_dir, ec);
            defaults();
            bool ok = loadFile(m_dir / "config.json", true);
            if (!ok) {
                defaults();
                ok = loadFile(m_dir / "config.bak.json", false);
                if (!ok) defaults();
                m_loadNote = ok ? "config.json was unreadable, restored the last valid copy" : "no valid config found, defaults used";
            }
            saveLocked();
            m_ready = true;
        }
        std::string loadNote() const { std::lock_guard lock(m_mutex); return m_loadNote; }

        bool b(std::string const& k) const { std::lock_guard lock(m_mutex); auto it = m_vals.find(k); return it != m_vals.end() && it->second.t == Val::Bool && it->second.b; }
        double d(std::string const& k) const { std::lock_guard lock(m_mutex); auto it = m_vals.find(k); return it != m_vals.end() ? it->second.n : 0.0; }
        int64_t i(std::string const& k) const { return static_cast<int64_t>(std::llround(d(k))); }
        std::string s(std::string const& k) const { std::lock_guard lock(m_mutex); auto it = m_vals.find(k); return it != m_vals.end() ? it->second.s : std::string(); }

        // Checks a value against the table. Returns "" when fine (and normalizes `v`), else the reason.
        std::string validate(std::string const& key, Val& v) const {
            std::lock_guard lock(m_mutex);
            return validateLocked(key, v);
        }

        // Validates and stores. persist=false keeps the value in memory only (per-level profile swaps).
        std::string set(std::string const& key, Val v, bool persist = true) {
            std::lock_guard lock(m_mutex);
            std::string err = validateLocked(key, v);
            if (!err.empty()) return err;
            m_vals[key] = v;
            if (persist) saveLocked();
            return "";
        }

        // what: "settings" (everything but keys), "binds" (keys only), "all"
        void reset(std::string const& what) {
            std::lock_guard lock(m_mutex);
            for (auto const& sp : specs()) {
                bool bind = isBind(sp.key);
                if (what == "all" || (what == "binds" && bind) || (what == "settings" && !bind)) m_vals[sp.key] = defaultOf(sp);
            }
            saveLocked();
        }

        std::string dump() const {
            std::lock_guard lock(m_mutex);
            return dumpLocked(true);
        }
        std::string schema() const {
            std::string o = "{\"type\":\"schema\",\"settings\":[";
            bool first = true;
            for (auto const& sp : specs()) {
                if (!first) o += ',';
                first = false;
                o += "{\"key\":" + jstr(sp.key) + ",\"kind\":" + jstr(std::string(1, static_cast<char>(sp.kind)));
                if (sp.kind == Kind::Str) o += ",\"default\":" + jstr(sp.sdef);
                else if (sp.kind == Kind::Bool) o += std::string(",\"default\":") + (sp.def != 0 ? "true" : "false");
                else o += ",\"default\":" + jnum(sp.def) + ",\"min\":" + jnum(sp.lo) + ",\"max\":" + jnum(sp.hi);
                o += ",\"info\":" + jstr(sp.info) + "}";
            }
            return o + "],\"actions\":[" + [] {
                std::string a;
                for (auto const& x : actions()) { if (!a.empty()) a += ','; a += "{\"name\":" + jstr(x.name) + ",\"info\":" + jstr(x.info) + "}"; }
                return a;
            }() + "]}";
        }

    private:
        static Val defaultOf(Spec const& sp) {
            switch (sp.kind) {
                case Kind::Bool: return Val::boolean(sp.def != 0);
                case Kind::Str: return Val::text(sp.sdef);
                default: return Val::number(sp.def);
            }
        }
        void defaults() { m_vals.clear(); for (auto const& sp : specs()) m_vals[sp.key] = defaultOf(sp); }

        std::string validateLocked(std::string const& key, Val& v) const {
            auto const* sp = findSpec(key);
            if (!sp) return "unknown setting '" + key + "'";
            switch (sp->kind) {
                case Kind::Bool:
                    if (v.t != Val::Bool) return "'" + key + "' needs true or false";
                    return "";
                case Kind::Int:
                case Kind::Float: {
                    if (v.t != Val::Num) return "'" + key + "' needs a number";
                    if (sp->kind == Kind::Int) {
                        if (std::fabs(v.n - std::round(v.n)) > 1e-9) return "'" + key + "' needs a whole number";
                        v.n = std::round(v.n);
                    }
                    if (v.n < sp->lo || v.n > sp->hi) return "'" + key + "' must be between " + jnum(sp->lo) + " and " + jnum(sp->hi);
                    return "";
                }
                case Kind::Str: {
                    if (v.t != Val::Str) return "'" + key + "' needs text";
                    if (v.s.empty()) return "";  // unbound
                    int vk = 0, mods = 0;
                    if (!parseKey(v.s, vk, mods)) return "'" + v.s + "' is not a valid key (examples: F8, Shift+F6, Ctrl+F1)";
                    v.s = keyName(vk, mods);
                    for (auto const& [k, other] : m_vals) {
                        if (k != key && isBind(k) && other.t == Val::Str && other.s == v.s) return "'" + v.s + "' is already used by " + k.substr(5);
                    }
                    return "";
                }
            }
            return "";
        }

        bool loadFile(std::filesystem::path const& p, bool makeBackup) {
            std::ifstream in(p, std::ios::binary);
            if (!in) return false;
            std::stringstream ss;
            ss << in.rdbuf();
            Obj o;
            if (!parseFlat(ss.str(), o)) return false;
            for (auto const& sp : specs()) {
                auto it = o.find(sp.key);
                if (it == o.end()) continue;
                Val v = it->second;
                if (validateLocked(sp.key, v).empty()) m_vals[sp.key] = v;  // bad or conflicting values keep the default
            }
            if (makeBackup) {
                std::error_code ec;
                std::filesystem::copy_file(p, m_dir / "config.bak.json", std::filesystem::copy_options::overwrite_existing, ec);
            }
            return true;
        }

        std::string dumpLocked(bool typed) const {
            std::string o = typed ? "{\"type\":\"config\"" : "{";
            bool first = !typed;
            for (auto const& sp : specs()) {
                auto it = m_vals.find(sp.key);
                if (it == m_vals.end()) continue;
                if (!first) o += ",\n ";
                first = false;
                o += jstr(sp.key) + ":" + jval(it->second);
            }
            return o + "}";
        }

        void saveLocked() {
            if (m_dir.empty()) return;
            auto tmp = m_dir / "config.json.tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out) return;
                out << dumpLocked(false) << "\n";
                if (!out.good()) return;
            }
            std::error_code ec;
            std::filesystem::rename(tmp, m_dir / "config.json", ec);
        }

        mutable std::mutex m_mutex;
        std::map<std::string, Val> m_vals;
        std::filesystem::path m_dir;
        std::string m_loadNote;
        bool m_ready = false;
    };

    inline Config& config() {
        static Config* c = new Config();  // never freed, threads may still use it at exit
        return *c;
    }
    inline bool b(char const* k) { return config().b(k); }
    inline double d(char const* k) { return config().d(k); }
    inline int64_t i(char const* k) { return config().i(k); }
    inline std::string s(char const* k) { return config().s(k); }
    // in-memory only (per-level profile swaps must not rewrite the file on every level)
    inline void setTemp(char const* k, bool v) { (void)config().set(k, Val::boolean(v), false); }

}  // namespace cfg
