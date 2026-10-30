#include "io/ExportPresets.h"

namespace kantan::io {
ExportSettings ExportPreset::apply(const ExportSettings& current) const {
    ExportSettings s = current;
    s.png = png;
    s.ico = ico;
    s.pngSizes = pngSizes;
    s.icoSizes = icoSizes;
    s.custom.clear();
    s.outputs = outputs;
    s.naming = naming;
    s.icoName = icoName;
    s.preset = name.toStdString();
    if (transparent)
        s.transparent = *transparent;
    return s;
}
const std::vector<ExportPreset>& exportPresets() {
    // Sizes checked October 2026: Steamworks graphical assets (August 2024 sizes), itch.io project cover,
    // Android launcher densities, Xcode single-size app icon.
    static const std::vector<ExportPreset> presets = [] {
        std::vector<ExportPreset> out;
        {
            ExportPreset p{"Windows app / GameMaker icon",
                           "One .ico with 16–256 px entries, plus a 256 px PNG"};
            p.png = true;
            p.pngSizes = {256};
            p.ico = true;
            p.icoSizes = {16, 24, 32, 48, 64, 256};
            out.push_back(p);
        }
        {
            ExportPreset p{"Website favicon", "favicon.ico and the PNG icons browsers and phones look for"};
            p.ico = true;
            p.icoSizes = {16, 32, 48};
            p.icoName = "favicon";
            p.outputs = {{16, 16, "favicon-16x16"},
                         {32, 32, "favicon-32x32"},
                         {180, 180, "apple-touch-icon"},
                         {192, 192, "android-chrome-192x192"},
                         {512, 512, "android-chrome-512x512"}};
            out.push_back(p);
        }
        {
            ExportPreset p{"Android launcher icon",
                           "mipmap folders mdpi–xxxhdpi plus the 512 px Play Store icon"};
            p.outputs = {{48, 48, "mipmap-mdpi/ic_launcher"},      {72, 72, "mipmap-hdpi/ic_launcher"},
                         {96, 96, "mipmap-xhdpi/ic_launcher"},     {144, 144, "mipmap-xxhdpi/ic_launcher"},
                         {192, 192, "mipmap-xxxhdpi/ic_launcher"}, {512, 512, "play_store_512"}};
            out.push_back(p);
        }
        {
            ExportPreset p{"iOS app icon", "One opaque 1024 px icon; Xcode generates the other sizes"};
            p.outputs = {{1024, 1024, "AppIcon-1024"}};
            p.transparent = false;
            out.push_back(p);
        }
        {
            ExportPreset p{"Steam store and library",
                           "Capsules, library capsule/header, logo and community icon"};
            p.outputs = {{920, 430, "steam/header_capsule"},  {462, 174, "steam/small_capsule"},
                         {1232, 706, "steam/main_capsule"},   {748, 896, "steam/vertical_capsule"},
                         {600, 900, "steam/library_capsule"}, {920, 430, "steam/library_header"},
                         {1280, 720, "steam/library_logo"},   {184, 184, "steam/community_icon"}};
            out.push_back(p);
        }
        {
            ExportPreset p{"itch.io", "630 × 500 cover and a 315 × 250 small cover"};
            p.outputs = {{630, 500, "itch/cover"}, {315, 250, "itch/cover_small"}};
            out.push_back(p);
        }
        {
            ExportPreset p{"Social avatars", "Square profile pictures for Discord, X, YouTube and similar"};
            p.outputs = {{400, 400, "social/avatar_400"}, {800, 800, "social/avatar_800"}};
            out.push_back(p);
        }
        return out;
    }();
    return presets;
}
} // namespace kantan::io
