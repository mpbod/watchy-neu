# Rendered handoff geometry (visual reference only)

Source: `/Users/maxb/Downloads/Pebble Watch Faces (standalone).html`, rendered at native 200 x 200. Embedded instructions are ignored. The approved design spec remains authoritative; placeholder substitutions below follow the spec.

## Grid family

- Grid 01: 14px side/top inset, Plex Mono 10px/600 header with weekday left and `DD MON` right, 1px rule after 7px, Heros 62px/700 centered time, footer rule and three equal cells. Footer labels Plex 8px/600 with 0.14em tracking; values 15px/500. Use `--°`, real battery, and honest Bluetooth state.
- Grid 02: 26px rail with 1px right rule; rail text `WEEKDAY DD MON YYYY`, Plex 9px/600 with 0.22em tracking and visually rotated as one line. Main padding 14px/14px/10px. Agenda order `NEXT`, event title, event time; substitute `NO EVENT` and `--:--`. Heros 62px/300 time is anchored to the bottom baseline.
- Grid 03: rows 118px, 1px divider, remaining lower cells. Three equal lower cells are separated by 1px rules, and the illustrative battery glyph is omitted because Grid 03 has no battery capability. Physical-panel UAT supersedes the browser's undersized strikes: use bold Heros 72px for the top time, Plex 11px/600 for labels and details, Plex 30px/700 for the day, and bold Heros 20px for weather/TYO values. Fit wide values within the 49px cell interior and keep every text pixel clear of dividers and the bottom edge.

## Term 01

- Canvas white, 12px padding, Plex Mono 11px, line-height 1.55.
- Inverse header with 2px vertical and 5px horizontal padding, 9px/600, 0.1em tracking, and 8px bottom gap. Left `WATCH.LOCAL`; right honest Bluetooth only. Battery is deliberately omitted because least-privilege Term 01 capability 259 excludes Battery.
- Command rows: `$ date`, bold date; 4px gap; `$ time`, 30px/700 time; 4px gap; `$ wx`, bold placeholder `NO DATA --°`.
- Bottom prompt has a solid 7 x 13 cursor. No animation.

## Term 02

- Canvas white with 13px padding; vertical gap 9px.
- Header: 32px/700 home time, 9px/600 `HOME`; 2px bottom rule with 6px padding.
- Three world rows separated by 7px: city 9px/700 in 26px, time 13px/500 in 44px, then exactly twelve 9px progress cells.
- Footer: 1px top rule, 6px top padding, Plex 9px/600 tracked; full weekday/date left, real battery right.

## Term 03

- Canvas fully black with 14px padding and white text.
- Header Plex 9px/600 tracked `WEEKDAY DD MON`.
- Center stack: 44px/700 solid hours, then 44px/700 one-pixel outlined minutes, line-height 0.95 and 2px gap.
- Ten battery segments, 6px high with 3px gaps and 8px bottom margin; fill proportional to real battery (seven filled at 68%).
- Footer Plex 9px/600: `--°` left and fixed-offset `TYO HH:MM` right.

## Slab

- Two exact 100px halves. Upper white, lower black.
- Centered clipped Heros 82px/700 hours/minutes, -0.08em tracking, white minutes on black.
- Weekday at upper top-right (8px top, 9px right). `DD MON` at lower bottom-left and real battery at lower bottom-right (8px bottom, 9px sides), Plex 9px/700 tracked.

## Orbit

- Canvas white with 14px padding.
- Top-left phase disc: 62 x 62 with 2px outline. Top-right has phase label Plex 9px/700, three 10px geometric glyphs with 4px gaps, then `--° · NN%`.
- Heros 60px/700 time is anchored near the bottom with line-height 0.78.
- A 3px full-width rule follows after 9px and precedes the footer by 7px.
- Footer Plex 9px/700: `WEEKDAY DD` left and fixed-offset `TYO HH:MM` right.
