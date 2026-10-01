format_version = "1.0"

-- =============================================================================
-- Saturator custom displays.
--   Input_Meter_Draw : vertical stereo input meter (9 x 31 native).
--   (The mode selector and everything else on the front is the SDK 5 C++
--    touch screen, src/cpp/ui/SaturatorDisplay.cpp.)
-- =============================================================================

-- ---- input meter ----------------------------------------------------------
local M_W = 9    -- 45 panel px / 5
local M_H = 31   -- 155 panel px / 5 (top 55 px sit under the switch shadow)
local MARGIN   = 1
local DIV      = 1
local BAR_W    = (M_W - 2 * MARGIN - DIV) / 2   -- 3.0
local L_X      = MARGIN
local R_X      = MARGIN + BAR_W + DIV
local Y_TOP    = MARGIN
local Y_BOTTOM = M_H - MARGIN
local BAR_H    = Y_BOTTOM - Y_TOP

-- One flat colour, the knobs' #EA0808 (Erik, 2026-09-26; was a deep-red ->
-- #EA0808 gradient), on the #240404 well (the touch screen's darkest cell).
-- The four zone names are kept so the drawing code is unchanged.
local COL_BG     = { r = 36,  g = 4,  b = 4,  a = 255 }   -- #240404 well
local COL_GREEN  = { r = 234, g = 8,  b = 8,  a = 255 }   -- #EA0808 = the knobs
local COL_YELLOW = { r = 234, g = 8,  b = 8,  a = 255 }   -- #EA0808 = the knobs
local COL_ORANGE = { r = 234, g = 8,  b = 8,  a = 255 }   -- #EA0808 = the knobs
local COL_RED    = { r = 234, g = 8,  b = 8,  a = 255 }   -- #EA0808 = the knobs
-- Scale -60 .. +12 dBFS (SpiritLevel's bars; Saturator.h meter01FromDb).
local Z_0DB  = 60.0 / 72.0
local Z_6DB  = 66.0 / 72.0
local Z_10DB = 70.0 / 72.0
local GHOST_ALPHA = 50

local function clamp(x, lo, hi)
  x = tonumber(x) or 0
  if x < lo then return lo end
  if x > hi then return hi end
  return x
end
local function col_alpha(c, a01)
  return { r = c.r, g = c.g, b = c.b, a = math.floor(c.a * clamp(a01, 0, 1) + 0.5) }
end
local function y_of(v) return Y_BOTTOM - (v * BAR_H) end
local function fill_band(x, vLo, vHi, c)
  if vHi <= vLo then return end
  local top, bottom = y_of(vHi), y_of(vLo)
  if bottom <= top then return end
  jbox_display.draw_rect({ left = x, top = top, right = x + BAR_W, bottom = bottom }, c)
end
local function draw_zones(x, fromV, toV, alpha)
  if toV <= fromV then return end
  local g = alpha and col_alpha(COL_GREEN,  alpha) or COL_GREEN
  local y = alpha and col_alpha(COL_YELLOW, alpha) or COL_YELLOW
  local o = alpha and col_alpha(COL_ORANGE, alpha) or COL_ORANGE
  local r = alpha and col_alpha(COL_RED,    alpha) or COL_RED
  fill_band(x, math.max(fromV, 0.0),   math.min(toV, Z_0DB),  g)
  fill_band(x, math.max(fromV, Z_0DB),  math.min(toV, Z_6DB),  y)
  fill_band(x, math.max(fromV, Z_6DB),  math.min(toV, Z_10DB), o)
  fill_band(x, math.max(fromV, Z_10DB), toV,                   r)
end
local function draw_channel(x, live, peak)
  jbox_display.draw_rect({ left = x, top = Y_TOP, right = x + BAR_W, bottom = Y_BOTTOM }, COL_BG)
  if peak > live then draw_zones(x, live, peak, GHOST_ALPHA / 255.0) end
  if live > 0.0 then draw_zones(x, 0.0, live, nil) end
end

function Input_Meter_Draw(values, display_info, dirty_rects)
  local l  = clamp(values and values[1] or 0.0, 0.0, 1.0)
  local r  = clamp(values and values[2] or 0.0, 0.0, 1.0)
  local lp = clamp(values and values[3] or 0.0, 0.0, 1.0)
  local rp = clamp(values and values[4] or 0.0, 0.0, 1.0)
  draw_channel(L_X, l, lp)
  draw_channel(R_X, r, rp)
end
