// KFWTheme.qml — single source of truth for the KFW design language
// Copyright (C) 2026 Kishan G A
// KFW is an independent project and is not affiliated with or endorsed by VideoLAN.
//
// Professional dual-mode palette (dark / light). `mode` picks the active set;
// every color token is a binding on `mode`, so the whole UI – including the
// window background, icons and status colors – restyles transitively. The
// choice is persisted per-device under the "ui" category.

pragma Singleton
import QtQuick
import QtCore

QtObject {
    id: theme

    // ── Mode ───────────────────────────────────────────────────────────────
    property string mode: "dark"            // "dark" | "light"
    readonly property bool dark: mode !== "light"

    readonly property Settings store: Settings {
        category: "ui"
        property alias themeMode: theme.mode
    }

    function setMode(m) {
        const next = (m === "light") ? "light" : "dark"
        if (next === mode)
            return
        mode = next
    }

    function toggle() { setMode(dark ? "light" : "dark") }

    // ── Typography ────────────────────────────────────────────────────────
    readonly property string fontFamily: "Segoe UI"
    readonly property string fontMono:   "Consolas"

    // ── Surfaces (neutral graphite — professional) ─────────────────────────
    readonly property color bg:        dark ? "#12161c" : "#f4f6f8"   // window base
    readonly property color surface:   dark ? "#171c23" : "#eaeef3"   // sidebar / statusbar
    readonly property color panel:     dark ? "#1c232b" : "#e3e9ef"   // panels, headers
    readonly property color card:      dark ? "#212933" : "#ffffff"   // cards, rows
    readonly property color cardHover: dark ? "#29333f" : "#eef3f8"   // hovered rows/cards
    readonly property color viewport:  dark ? "#0c0f13" : "#0c0f13"   // video area (always dim)
    readonly property color inset:     dark ? "#181e26" : "#f7f9fb"   // inputs, wells

    // ── Borders ───────────────────────────────────────────────────────────
    readonly property color border:     dark ? "#2b3540" : "#d7dde3"
    readonly property color borderMid:  dark ? "#39444f" : "#c6cfd6"
    readonly property color borderHigh: dark ? "#47535f" : "#b1bcc5"

    // ── Accent (steel blue) ───────────────────────────────────────────────
    readonly property color accent:       dark ? "#4a83f0" : "#2c62c9"
    readonly property color accentBright: dark ? "#74a2f6" : "#3d76e0"
    readonly property color accentDeep:   dark ? "#2f63c6" : "#1f4ea8"
    readonly property color accentSoft:   dark ? "#17253f" : "#e6eefb"   // tinted background
    readonly property color accentBorder: dark ? "#2f63c6" : "#2c62c9"   // accent outline
    readonly property color accentText:   dark ? "#ffffff" : "#ffffff"   // text on accent fill

    // ── Semantic status ───────────────────────────────────────────────────
    readonly property color ok:    dark ? "#58c98b" : "#2e7d4f"
    readonly property color okBg:  dark ? "#17251c" : "#e2f2e8"
    readonly property color warn:  dark ? "#d9a13c" : "#9a6b10"
    readonly property color warnBg:dark ? "#27200f" : "#f8eed3"
    readonly property color bad:   dark ? "#e56b6b" : "#c23e3e"
    readonly property color badBg: dark ? "#2a181b" : "#fae5e5"
    readonly property color info:  dark ? "#7d9bd6" : "#356098"
    readonly property color infoBg:dark ? "#18202f" : "#e4ebf4"
    readonly property color neutral:    dark ? "#8794a3" : "#5f6b7a"
    readonly property color neutralBg:  dark ? "#1e252e" : "#e6ebee"

    // ── Text ──────────────────────────────────────────────────────────────
    readonly property color textHi:  dark ? "#e5ebf2" : "#1c2530"
    readonly property color textMid: dark ? "#9aa7b5" : "#4c5968"
    readonly property color textLow: dark ? "#667689" : "#74808d"

    // ── Spacing scale ─────────────────────────────────────────────────────
    readonly property int sp1: 4
    readonly property int sp2: 8
    readonly property int sp3: 12
    readonly property int sp4: 16
    readonly property int sp5: 24
    readonly property int sp6: 32

    // ── Radii ─────────────────────────────────────────────────────────────
    readonly property int rSm: 5
    readonly property int rMd: 8
    readonly property int rLg: 12
    readonly property int rFull: 99

    // ── Component metrics ─────────────────────────────────────────────────
    readonly property int controlH: 34
    readonly property int btnH: 36

    // ── Motion ────────────────────────────────────────────────────────────
    readonly property int animFast: 120
    readonly property int anim: 180

    // ── Helpers ───────────────────────────────────────────────────────────
    function statusColor(status) {
        switch (status) {
        case "Verified":      return ok
        case "Mismatch":      return bad
        case "Open":          return ok
        case "Pending Review": return warn
        case "Pending":       return warn
        case "Closed":        return textLow
        default:              return neutral
        }
    }

    function statusBg(status) {
        switch (status) {
        case "Verified":      return okBg
        case "Mismatch":      return badBg
        case "Open":          return okBg
        case "Pending Review": return warnBg
        case "Pending":       return warnBg
        case "Closed":        return neutralBg
        default:              return neutralBg
        }
    }

    // Timeline event kinds — mode-tinted so dots/borders stay legible on both
    // surfaces.
    readonly property var kindColor: ({
        "Marker":         dark ? "#4a83f0" : "#2c62c9",
        "Bookmark":       dark ? "#d9a13c" : "#9a6b10",
        "Note":           dark ? "#7d9bd6" : "#356098",
        "FrameExport":    dark ? "#58c98b" : "#2e7d4f",
        "IntegrityCheck": dark ? "#59b9c8" : "#1e7f8c",
        "BlackFrame":     dark ? "#8a93a3" : "#4d5560",
        "Freeze":         dark ? "#a58ce0" : "#6b4fc4",
        "Flicker":        dark ? "#e58a4a" : "#b35a1a",
        "RapidCuts":      dark ? "#e56b6b" : "#b04343"
    })
}
