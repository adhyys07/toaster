#include "theme_screen.h"
#include "layout.h"
#include "theme_io.h"

#include <algorithm>

namespace emu::frontend::ui {
namespace {

enum : WidgetId {
    kIdSave = 9000,
    kIdReset,
    kIdCloseTheme,
    kIdPresetFirst = 9100,
    kIdFieldFirst = 9200
};

const char* kChannelNames[4] = {"R", "G", "B", "A"};

Uint8 channelOf(SDL_Color color, int channel) {
    switch (channel) {
    case 0: return color.r;
    case 1: return color.g;
    case 2: return color.b;
    default: return color.a;
    }
}

void setChannel(SDL_Color& color, int channel, int value) {
    const auto clamped = static_cast<Uint8>(std::clamp(value, 0, 255));
    switch (channel) {
    case 0: color.r = clamped; break;
    case 1: color.g = clamped; break;
    case 2: color.b = clamped; break;
    default: color.a = clamped; break;
    }
}

}

void ThemeScreen::open(ThemeId current, const Theme& theme) {
    open_ = true;
    preset_ = current;
    edited_ = theme;
    preview_ = repairTheme(edited_);
    focus_ = Focus::Presets;
    field_ = 0;
    channel_ = 0;
    refreshProblems();
}

void ThemeScreen::applyPreset(ThemeId id) {
    preset_ = id;
    if (id != ThemeId::Custom) edited_ = themePreset(id);
    preview_ = repairTheme(edited_);
    refreshProblems();
}

void ThemeScreen::nudge(int channel, int delta) {
    if (field_ < 0 || field_ >= kThemeFieldCount) return;

    SDL_Color& color = edited_.*(kThemeFields[field_].member);
    setChannel(color, channel, channelOf(color, channel) + delta);
    preset_ = ThemeId::Custom;
    preview_ = repairTheme(edited_);
    refreshProblems();
}

void ThemeScreen::refreshProblems() {
    problems_ = validateTheme(edited_);
}

SDL_Rect ThemeScreen::panelRect(const SDL_Rect& window,
                                const Metrics& m) const {
    const int width = std::min(window.w - 2 * m.pad(), m.px(720));
    const int height = std::min(window.h - 2 * m.pad(), m.px(560));
    return {window.x + (window.w - width) / 2,
            window.y + (window.h - height) / 2, width, height};
}

bool ThemeScreen::handleAction(UiAction action) {
    if (!open_) return false;

    switch (action) {
    case UiAction::Cancel:
        close();
        return true;

    case UiAction::NextTab:
        focus_ = focus_ == Focus::Presets ? Focus::Fields : Focus::Presets;
        return true;

    case UiAction::Up:
        if (focus_ == Focus::Presets) {
            const int count = static_cast<int>(ThemeId::Count);
            preset_ = static_cast<ThemeId>(
                (static_cast<int>(preset_) + count - 1) % count);
            applyPreset(preset_);
        } else {
            field_ = (field_ + kThemeFieldCount - 1) % kThemeFieldCount;
        }
        return true;
    case UiAction::Down:
        if (focus_ == Focus::Presets) {
            const int count = static_cast<int>(ThemeId::Count);
            preset_ = static_cast<ThemeId>(
                (static_cast<int>(preset_) + 1) % count);
            applyPreset(preset_);
        } else {
            field_ = (field_ + 1) % kThemeFieldCount;
        }
        return true;

    case UiAction::Left:
        if (focus_ == Focus::Fields) nudge(channel_, -8);
        return true;

    case UiAction::Right:
        if (focus_ == Focus::Fields) nudge(channel_, 8);
        return true;

    case UiAction::PageUp:
        channel_ = (channel_ + 3) % 4;
        return true;
    
    case UiAction::PageDown:
        channel_ = (channel_ + 1) % 4;
        return true;
    
    case UiAction::Confirm:
        wants_save_ = true;
        return true;
    
    default:
        return false;
    }
}

bool ThemeScreen::handleEvent(const SDL_Event& event, const SDL_Rect& bounds,
                              const Metrics& metrics, float display_scale) {
    if (!open_) return false;

    const SDL_Rect panel = panelRect(bounds, metrics);

    if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN) {
        int x = event.type == SDL_MOUSEMOTION ? event.motion.x
                                              : event.button.x;
        int y = event.type == SDL_MOUSEMOTION ? event.motion.y
                                              : event.button.y;
        toRenderCoords(display_scale, &x, &y);

        const WidgetId under = hitTest(buttons_, x, y);
        const bool changed = under != hot_;
        hot_ = under;

        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (!pointInside(panel, x, y)) {
                close();
                return true;
            }
            if (under == kIdSave) {
                wants_save_ = true;
            } else if (under == kIdReset) {
                wants_reset_ = true;
                applyPreset(ThemeId::Midnight);
            } else if (under == kIdCloseTheme) {
                close();
            } else if (under >= kIdPresetFirst &&
                       under < kIdPresetFirst +
                                   static_cast<WidgetId>(ThemeId::Count)) {
                focus_ = Focus::Presets;
                applyPreset(static_cast<ThemeId>(under - kIdPresetFirst));
            } else if (under >= kIdFieldFirst) {
                focus_ = Focus::Fields;
                field_ = static_cast<int>(under - kIdFieldFirst);
            }
            return true;
        }
        return changed;
    }

    if (event.type == SDL_MOUSEWHEEL && focus_ == Focus::Fields) {
        nudge(channel_, event.wheel.y * 8);
        return true;
    }

    return false;
}

void ThemeScreen::draw(SDL_Renderer* renderer, Painter& painter,
                       TextCache& text, const Metrics& m,
                       const SDL_Rect& window) {
    if (!open_) return;

    const Theme& t = preview_;

    painter.rect(window, SDL_Color{0, 0, 0, 170});

    const SDL_Rect panel = panelRect(window, m);
    painter.shadow(panel, m.radius(), m.px(6), SDL_Color{0, 0, 0, 160});
    painter.roundedRectBordered(panel, m.radius(), t.surface, t.border,
                                m.border());

    int y = panel.y + m.pad();
    text.draw(renderer, FontRole::Title, "Theme", panel.x + m.pad(), y,
              t.accent);
    y += text.lineHeight(FontRole::Title) + m.px(10);

    buttons_.clear();

    const int column_w = m.px(170);
    const SDL_Rect presets{panel.x + m.pad(), y, column_w,
                           panel.h - (y - panel.y) - m.px(70)};

    text.draw(renderer, FontRole::Small, "PRESET", presets.x, presets.y,
              t.text_muted);
    int row_y = presets.y + text.lineHeight(FontRole::Small) + m.px(6);

    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const auto id = static_cast<ThemeId>(i);
        const SDL_Rect row{presets.x, row_y, presets.w, m.px(30)};

        const bool selected = id == preset_;
        const bool hovered = hot_ == kIdPresetFirst + static_cast<WidgetId>(i);
        if (selected) {
            painter.roundedRect(row, m.radius_small(), t.accent_soft);
        } else if (hovered) {
            painter.roundedRect(row, m.radius_small(), t.surface_hover);
        }

        const Theme sample =
            id == ThemeId::Custom ? edited_ : themePreset(id);
        const SDL_Rect swatch{row.x + m.px(8), row.y + m.px(8), m.px(14),
                              m.px(14)};
        painter.roundedRect(swatch, m.px(3), sample.accent);
        painter.roundedRect({swatch.x + m.px(16), swatch.y, m.px(14),
                             m.px(14)},
                            m.px(3), sample.bg);

        text.draw(renderer, FontRole::Body, themeName(id),
                  row.x + m.px(46),
                  row.y + (row.h - text.lineHeight(FontRole::Body)) / 2,
                  selected ? t.text : t.text_muted);

        Button hit;
        hit.id = kIdPresetFirst + static_cast<WidgetId>(i);
        hit.rect = row;
        buttons_.push_back(hit);

        row_y += row.h + m.px(2);
    }

    const int fields_x = presets.x + column_w + m.gap();
    const int fields_w = panel.x + panel.w - m.pad() - fields_x;

    text.draw(renderer, FontRole::Small, "COLOUR", fields_x, presets.y,
              t.text_muted);
    text.drawRight(renderer, FontRole::Small,
                   std::string("channel ") + kChannelNames[channel_] +
                       "   PgUp/PgDn switches, Left/Right adjusts",
                   fields_x + fields_w, presets.y, t.text_muted);

    row_y = presets.y + text.lineHeight(FontRole::Small) + m.px(6);
    const int field_h = m.px(26);

    for (int i = 0; i < kThemeFieldCount; ++i) {
        const SDL_Rect row{fields_x, row_y, fields_w, field_h};
        if (row.y + row.h > presets.y + presets.h) break;

        const bool selected = i == field_ && focus_ == Focus::Fields;
        const bool hovered = hot_ == kIdFieldFirst + static_cast<WidgetId>(i);
        if (selected) {
            painter.roundedRect(row, m.radius_small(), t.accent_soft);
        } else if (hovered) {
            painter.roundedRect(row, m.radius_small(), t.surface_hover);
        }
                
        const SDL_Color value = edited_.*(kThemeFields[i].member);
        const SDL_Rect swatch{row.x + m.px(6), row.y + m.px(5), m.px(30),
                              row.h - m.px(10)};
        painter.rect(swatch, SDL_Color{120, 120, 120, 255});
        painter.rect({swatch.x, swatch.y, swatch.w / 2, swatch.h / 2},
                     SDL_Color{70, 70, 70, 255});
        painter.rect({swatch.x + swatch.w / 2, swatch.y + swatch.h / 2,
                      swatch.w / 2, swatch.h / 2},
                     SDL_Color{70, 70, 70, 255});
        painter.rect(swatch, value);

        text.draw(renderer, FontRole::Small, kThemeFields[i].label,
                  row.x + m.px(44), row.y + m.px(5),
                  selected ? t.text : t.text_muted);
        text.drawRight(renderer, FontRole::Small, formatColor(value),
                       row.x + row.w - m.px(8), row.y + m.px(5),
                       selected ? t.accent : t.text_muted);

        Button hit;
        hit.id = kIdFieldFirst + static_cast<WidgetId>(i);
        hit.rect = row;
        buttons_.push_back(hit);
        row_y += field_h + m.px(2);
    }

    int warn_y = panel.y + panel.h - m.px(64);
    if (!problems_.empty()) {
        const ThemeProblem& worst = problems_.front();
        painter.roundedRect({panel.x + m.pad(), warn_y - m.px(4),
                             panel.w - 2 * m.pad(), m.px(22)},
                            m.px(4),
                            worst.fatal ? t.danger : t.surface_hover);
        text.draw(renderer, FontRole::Small,
                  (problems_.size() > 1
                       ? std::to_string(problems_.size()) + " issues: "
                       : std::string()) +
                      worst.message,
                  panel.x + m.pad() + m.px(8), warn_y,
                  worst.fatal ? t.text_on_accent : t.text_muted);
    }

    const int button_y = panel.y + panel.h - m.pad() - m.button_height();
    int right = panel.x + panel.w - m.pad();

    const std::pair<WidgetId, const char*> actions[] = {
        {kIdSave, "Save"}, {kIdReset, "Reset"}, {kIdCloseTheme, "Close"}};

    for (const auto& action : actions) {
        const int width = text.width(FontRole::Body, action.second) + m.px(28);
        right -= width;

        Button button;
        button.id = action.first;
        button.rect = {right, button_y, width, m.button_height()};
        button.label = action.second;
        button.primary = action.first == kIdSave;
        drawButton(renderer, painter, text, t, m, button, hot_, kNoWidget,
                   false);
        buttons_.push_back(button);

        right -= m.px(8);
    }
}

}