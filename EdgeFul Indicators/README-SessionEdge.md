# Session Edge Probabilities — setup & notes

Pine v6 script. Paste into TradingView → Pine Editor → **Save** → *Add to chart*.

| File | Purpose |
|---|---|
| `SessionEdgeProbabilities.pine` | On-chart dashboard: table + price label showing which rules are live and their probability |

---

## How it works

All seven rules are evaluated on a **fixed calculation timeframe** (default 5m) through
`request.security`, then displayed on whatever chart you have open. So a 5-minute TSLA
chart, a 1-hour chart and a daily chart all report the *same* state — which is what you
asked for by "regardless of the timeframe."

The session frame is **futures-style**: the trading day starts at 18:00 ET, so
"Sunday's open" is the 18:00 Sunday bar and "yesterday's close" is the 17:00 ET close.

Every rule shows two numbers:

- **STATIC** — the probability you supplied (all seven are editable inputs)
- **LIVE (n)** — the hit rate the script measured on *that symbol's own history*, with
  the number of qualifying trading days observed. Green when it meets or beats your
  static number, orange when it falls short, grey until `n` reaches your minimum
  sample size (default 20).

That comparison is the point: it tells you whether TSLA actually behaves like the
instrument those percentages were derived from.

### Rule state
- `—` not applicable today (wrong weekday, condition never armed)
- `LIVE` armed and the target has not been reached yet — this is the tradeable state
- `HIT` the target was already reached today

---

## The seven rules as implemented

1. **95% — Monday → Sunday open.** On the Monday trading day, target = the 18:00 ET
   Sunday open. Armed at 09:30, hit when price trades through that level before 17:00.
2. **72% — Midnight level.** Target = the 00:00 ET price. Armed at 09:30, hit on a touch
   before 17:00.
3. **70% — ORB first extreme.** During 09:30–10:30 the script records *which* extreme
   printed first. Low first → predicts a break of the ORB high; high first → predicts a
   break of the ORB low.
4. **97% — Tuesday ORB breakout.** Either side of the ORB, before the deadline input.
5. **77% — Green first hour → green day.** Trigger comes from `CME_MINI:NQ1!` by default
   (settings → Rule 5 reference symbol; toggle to use the chart symbol instead). The
   LIVE % measures "NQ's first hour was green → *this* symbol closed green."
6. **82% — Open below prior close.** If the 09:30 price is below the previous session's
   17:00 close, target = that close.
7. **88% — Inside day.** If yesterday's session bar was inside the day before, targets =
   yesterday's open/close body edges, in either direction. Armed from 18:00, so an
   overnight breakout counts.

---

## One thing to set

Rule 4 says "1H ORB breakout **before 10:30am**", but a 09:30–10:30 opening range cannot
break out before it has finished forming. Two ways to resolve it, both in settings:

- Keep ORB length at 60 and leave the deadline at 690 (11:30) — "breaks out within the
  hour after the range closes."
- Or set **ORB length = 30** and **Rule 4 deadline = 630** if your actual definition is a
  9:30–10:00 range breaking before 10:30.

Pick whichever matches how the 97% was measured.

