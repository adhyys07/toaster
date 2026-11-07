#include "theme.h"

#include <cstring>

namespace emu::frontend::ui {

const ThemeField kThemeFields[] = {
    {"bg",             &Theme::bg,             "Window background"},
    {"surface",        &Theme::surface,        "Panel"},
    {"surface_hover",  &Theme::surface_hover,  "Panel, hovered"},
    {"surface_active", &Theme::surface_active, "Panel, pressed"},
    {"border",         &Theme::border,         "Borders and scrollbar"},
    {"accent",         &Theme::accent,         "Accent"},
    {"accent_soft",    &Theme::accent_soft,    "Selected row"},
    {"gold",           &Theme::gold,           "Favorites and titles"},
    {"text",           &Theme::text,           "Text"},
    {"text_muted",     &Theme::text_muted,     "Secondary text"},
    {"text_on_accent", &Theme::text_on_accent, "Text on accent"},
    {"danger",         &Theme::danger,         "Errors"},
    {"success",        &Theme::success,        "Confirmations"},
    {"shadow",         &Theme::shadow,         "Drop shadow"},
};

const int kThemeFieldCount = 
    static_cast<int>(sizeof(kThemeFields) / sizeof(kThemeFields[0]));

const char* themeName(ThemeId id) {
    switch (id) {
    case ThemeId::Midnight: return "midnight";
    case ThemeId::Slate:    return "slate";
    case ThemeId::Nord:     return "nord";
    case ThemeId::Gruvbox:  return "gruvbox";
    case ThemeId::Paper:    return "paper";
    case ThemeId::Contrast: return "contrast";
    case ThemeId::Custom:   return "custom";
    case ThemeId::Count:
    default:                return "midnight";
    }
}

ThemeId themeFromName(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const auto id = static_cast<ThemeId>(i);
        if (name == themeName(id)) return id;
    }
    return ThemeId::Midnight;
}

Theme themePreset(ThemeId id) {
    Theme theme;

    switch (id) {
    case ThemeId::Slate:
        theme.bg = {24, 26, 31, 255};
        theme.surface = {34, 37, 44, 255};
        theme.surface_hover = {44, 48, 57, 255};
        theme.surface_active = {55, 60, 71, 255};
        theme.border = {62, 68, 80, 255};
        theme.accent = {137, 180, 250, 255};
        theme.accent_soft = {49, 62, 84, 255};
        theme.gold = {249, 200, 128, 255};
        theme.text = {230, 233, 239, 255};
        theme.text_muted = {148, 155, 170, 255};
        theme.text_on_accent = {20, 24, 32, 255};
        theme.danger = {237, 135, 150, 255};
        theme.success = {166, 218, 149, 255};
        theme.shadow = {0, 0, 0, 120};
        break;
    
    case ThemeId::Nord:
        theme.bg = {46, 52, 64, 255};
        theme.surface = {59, 66, 82, 255};
        theme.surface_hover = {67, 76, 94, 255};
        theme.surface_active = {76, 86, 106, 255};
        theme.border = {76, 86, 106, 255};
        theme.accent = {136, 192, 208, 255};
        theme.accent_soft = {59, 85, 97, 255};
        theme.gold = {235, 203, 139, 255};
        theme.text = {236, 239, 244, 255};
        // Lightened from Nord's own 8f9bad and bf616a: at 3.6:1 and 2.1:1
        // they were below the readable and visible thresholds on this
        // palette's own panels.
        theme.text_muted = {167, 179, 196, 255};
        theme.text_on_accent = {46, 52, 64, 255};
        theme.danger = {213, 134, 142, 255};
        theme.success = {163, 190, 140, 255};
        theme.shadow = {0, 0, 0, 110};
        break;

    case ThemeId::Gruvbox:
        theme.bg = {29, 32, 33, 255};
        theme.surface = {40, 40, 40, 255};
        theme.surface_hover = {60, 56, 54, 255};
        theme.surface_active = {80, 73, 69, 255};
        theme.border = {80, 73, 69, 255};
        theme.accent = {131, 165, 152, 255};
        theme.accent_soft = {60, 69, 64, 255};
        theme.gold = {250, 189, 47, 255};
        theme.text = {235, 219, 178, 255};
        theme.text_muted = {168, 153, 132, 255};
        theme.text_on_accent = {29, 32, 33, 255};
        theme.danger = {251, 73, 52, 255};
        theme.success = {184, 187, 38, 255};
        theme.shadow = {0, 0, 0, 130};
        break;

    case ThemeId::Paper:
        theme.bg = {246, 245, 242, 255};
        theme.surface = {255, 255, 255, 255};
        theme.surface_hover = {237, 238, 240, 255};
        theme.surface_active = {224, 226, 230, 255};
        theme.border = {205, 208, 214, 255};
        theme.accent = {20, 110, 160, 255};
        theme.accent_soft = {206, 228, 242, 255};
        theme.gold = {158, 109, 10, 255};
        theme.text = {25, 28, 33, 255};
        theme.text_muted = {98, 106, 118, 255};
        theme.text_on_accent = {255, 255, 255, 255};
        theme.danger = {176, 36, 42, 255};
        theme.success = {28, 115, 65, 255};
        theme.shadow = {40, 44, 52, 40};
        break;
    
    case ThemeId::Contrast:
        theme.bg = {0, 0, 0, 255};
        theme.surface = {0, 0, 0, 255};
        theme.surface_hover = {38, 38, 38, 255};
        theme.surface_active = {64, 64, 64, 255};
        theme.border = {255, 255, 255, 255};
        theme.accent = {255, 221, 0, 255};
        theme.accent_soft = {64, 56, 0, 255};
        theme.gold = {255, 221, 0, 255};
        theme.text = {255, 255, 255, 255};
        theme.text_muted = {208, 208, 208, 255};
        theme.text_on_accent = {0, 0, 0, 255};
        theme.danger = {255, 110, 110, 255};
        theme.success = {120, 255, 150, 255};
        theme.shadow = {0, 0, 0, 0};
        break;
    
    case ThemeId::Custom:
    case ThemeId::Midnight:
    case ThemeId::Count:
    default:
        break;
    }

    return theme;
}

}