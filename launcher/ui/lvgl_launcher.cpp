#include "launcher/ui/lvgl_launcher.h"

#include "launcher/display/display.h"
#include "launcher/model/library.h"
#include "launcher/model/paths.h"
#include "launcher/model/settings.h"

#include "lvgl.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace launcher
{
namespace
{
namespace fs = std::filesystem;

// Design canvas. The display backend exposes canvasWidth()/canvasHeight()
// (design size multiplied by renderScale, 1 or 2) and the UI scales every
// layout metric and font size by renderScale, so HiDPI windows stay sharp.
constexpr int kLogicalWidth  = 1920;
constexpr int kLogicalHeight = 1080;
constexpr int kColumns       = 9;

constexpr int kMargin      = 48;
constexpr int kNavHeight   = 88;
constexpr int kStatusY     = 1028;
constexpr int kStatusH     = 52;

// The main content uses the full 1920px canvas width while the detail panel
// is closed. The panel is a drawer that overlays the right edge on demand.
constexpr int kLeftX = 48;
constexpr int kLeftW = 1824;
constexpr int kMainY = 120;

constexpr int kRightX = 1472;
constexpr int kRightY = 120;
constexpr int kRightW = 400;
constexpr int kRightH = 876;

constexpr int kCardW    = 181;
constexpr int kCoverH   = 215;
constexpr int kCardH    = 274;
constexpr int kCardGapX = 20;
constexpr int kCardGapY = 16;

constexpr int kBannerY = kMainY;
constexpr int kBannerH = 320;
constexpr int kBannerHEmpty = 288;  // shorter hero when "recent" is collapsed

constexpr int kShelfHeaderY = 460;
constexpr int kShelfY       = 492;
constexpr int kShelfH       = 196;
constexpr int kShelfCardW   = 352;  // 5 cards fill the 1824px content width
constexpr int kShelfGap     = 16;

constexpr int kHomeGridHeaderY = 700;
constexpr int kHomeGridY       = 736;
constexpr int kHomeGridH       = 280;
constexpr int kHomeGridEmptyH  = 564;  // two full card rows when "recent" is collapsed
constexpr int kHomeEmptyHeaderY = 428; // "游戏库" header when "recent" is collapsed
constexpr int kHomeEmptyGridY   = 460; // grid top when "recent" is collapsed
constexpr int kLibraryGridY    = 158;
constexpr int kLibraryGridH    = 854;

constexpr int kLibraryFilterX   = 168;
constexpr int kLibraryFilterY   = 105;
constexpr int kLibraryFilterW   = 92;
constexpr int kLibraryFilterH   = 34;
constexpr int kLibraryFilterGap = 8;

// Settings page: left sidebar + scrollable content.
constexpr int kSettingsSidebarX = kLeftX;
constexpr int kSettingsSidebarY = 110;  // vertically centered in the settings area
constexpr int kSettingsSidebarW = 256;
constexpr int kSettingsSidebarH = 896;
constexpr int kSettingsContentX = kSettingsSidebarX + kSettingsSidebarW + 12;
constexpr int kSettingsContentY = 110;  // vertically centered in the settings area
constexpr int kSettingsContentW = kLeftX + kLeftW - kSettingsContentX;
constexpr int kSettingsContentH = 896;
constexpr int kSettingsCardW = kSettingsContentW;  // cards span the content area
constexpr int kSettingsCardX = 0;

const char* const kPadButtonNames[display::PadButtonCount] = {
    "A", "B", "X", "Y", "LB", "RB", "Select", "Start",
    "L3", "R3", "Home", "↑", "↓", "←", "→",
};
const char* const kPadAxisNames[display::PadAxisCount] = {
    "左摇杆 X", "左摇杆 Y", "右摇杆 X", "右摇杆 Y", "LT", "RT",
};

struct BindEntry { const char* label; const char* binding; };
const BindEntry kDefaultBinds[] = {
    { "十字键 上", "W" },   { "十字键 下", "S" },
    { "十字键 左", "A" },   { "十字键 右", "D" },
    { "A 键", "J" },        { "B 键", "K" },
    { "Select", "RShift" }, { "Start", "Return" },
    { "连发 A", "—" },      { "连发 B", "—" },
    { "即时存档", "F1" },   { "即时读档", "F2" },
};

constexpr int kNavX        = 380;
constexpr int kNavY        = 16;
constexpr int kNavH        = 56;
constexpr int kNavGap      = 4;
constexpr int kNavWidths[3] = { 112, 116, 108 };

constexpr int kSearchX = 1512;
constexpr int kSearchY = 22;
constexpr int kSearchW = 300;
constexpr int kSearchH = 44;

constexpr int kAvatarX    = 1828;
constexpr int kAvatarY    = 22;
constexpr int kAvatarSize = 44;

constexpr int kFullscreenX = 1840;
constexpr int kFullscreenY = 1038;
constexpr int kFullscreenW = 32;
constexpr int kFullscreenH = 32;

constexpr int kBannerTextX    = kLeftX + 36;
constexpr int kBannerPlayX    = kBannerTextX;
constexpr int kBannerPlayY    = 314;
constexpr int kBannerPlayW    = 190;
constexpr int kBannerPlayH    = 50;
constexpr int kBannerRescanX  = kBannerPlayX + kBannerPlayW + 14;
constexpr int kBannerRescanW  = 150;

constexpr int kRightBodyX   = kRightX + 22;
constexpr int kRightBodyW   = kRightW - 44;
constexpr int kRightPlayY   = 700;
constexpr int kRightPlayH   = 50;
constexpr int kRightPillY   = 610;
constexpr int kRightPillW   = 108;
constexpr int kRightPillH   = 30;
constexpr int kRightPillGap = 8;

constexpr int kSettingsX         = 84;
constexpr int kSettingsPillY     = 372;
constexpr int kSettingsPillW     = 180;
constexpr int kSettingsPillH     = 42;
constexpr int kSettingsPillGap   = 10;
constexpr int kSettingsRescanX   = 1166;
constexpr int kSettingsRescanY   = 194;
constexpr int kSettingsRescanW   = 190;
constexpr int kSettingsRescanH   = 42;

constexpr int kHomeVisibleRows    = 1;
constexpr int kLibraryVisibleRows = 3;

constexpr uint32_t kTextHex       = 0xE8EAF0;
constexpr uint32_t kDimHex        = 0x9AA1AD;
constexpr uint32_t kMutedHex      = 0x6B7280;
constexpr uint32_t kFaintHex      = 0x565D68;
constexpr uint32_t kAccentHex     = 0x58A2FF;
constexpr uint32_t kGreenHex      = 0x4CAF7D;
constexpr uint32_t kOrangeHex     = 0xFF6B4A;
constexpr uint32_t kOrangeTextHex = 0xFF8A66;
constexpr uint32_t kWhiteHex      = 0xFFFFFF;
constexpr uint32_t kNavActiveHex  = 0x1E2230;

inline lv_color_t col(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

bool pointIn(int x, int y, int w, int h, int px, int py)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

std::string defaultConfigPath()
{
    return defaultLauncherConfigPath();
}

std::string defaultLibraryPath()
{
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec || cwd.empty())
        return ".";

    const fs::path roms = cwd / "roms";
    if (fs::is_directory(roms, ec))
        return roms.string();
    return cwd.string();
}

std::string folded(const std::string& text)
{
    std::string result = text;
    for (char& c : result)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool containsFold(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    return folded(haystack).find(folded(needle)) != std::string::npos;
}

int maxScrollFor(int count, int visibleRows)
{
    if (visibleRows <= 0)
        return 0;
    const int rows = (count + kColumns - 1) / kColumns;
    return std::max(0, rows - visibleRows);
}

std::string formatSize(long long bytes)
{
    if (bytes <= 0)
        return "0 KB";
    if (bytes < 1024)
        return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)
        return std::to_string((bytes + 1023) / 1024) + " KB";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

std::string mapperText(const RomEntry& rom)
{
    if (rom.mapper < 0)
        return "无效 ROM";
    std::string text = "Mapper " + std::to_string(rom.mapper);
    if (!rom.supported)
        text += " · 不支持";
    return text;
}

std::string firstGlyph(const std::string& text)
{
    if (text.empty())
        return "?";
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t length = 1;
    if ((lead & 0xE0) == 0xC0)
        length = 2;
    else if ((lead & 0xF0) == 0xE0)
        length = 3;
    else if ((lead & 0xF8) == 0xF0)
        length = 4;
    if (length > text.size())
        length = 1;
    return text.substr(0, length);
}

std::uint32_t fnv1a(const std::string& text)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : text)
    {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

struct Palette
{
    uint32_t top;
    uint32_t bottom;
    uint32_t glyph;
};

const Palette kPalettes[] = {
    { 0x12240F, 0x577C2E, 0xE8D878 },
    { 0x5B1A0E, 0xE05A24, 0xFFE08A },
    { 0x241A10, 0xB8862E, 0xF5E9B0 },
    { 0x101018, 0x2A6AE0, 0xE0C22A },
    { 0x12081A, 0x6A2E7A, 0xF5A05A },
    { 0x081018, 0x1E7AB8, 0xC9F0F5 },
    { 0x2A0E1E, 0xC95A8A, 0xFFE0EE },
    { 0x14120A, 0x6A5A22, 0xE8D88A },
    { 0x100A1E, 0x5E2A8A, 0xE8A8F5 },
    { 0x101418, 0x8A5A2A, 0xF5D89A },
};

const Palette& paletteFor(const std::string& title)
{
    return kPalettes[fnv1a(title) % (sizeof(kPalettes) / sizeof(kPalettes[0]))];
}

// ------------------------------------------------------------- font setup ---

struct Fonts
{
    lv_font_t* f11 = nullptr;
    lv_font_t* f12 = nullptr;
    lv_font_t* f13 = nullptr;
    lv_font_t* f14 = nullptr;
    lv_font_t* f15 = nullptr;
    lv_font_t* f16 = nullptr;
    lv_font_t* f17 = nullptr;
    lv_font_t* f18 = nullptr;
    lv_font_t* f20 = nullptr;
    lv_font_t* f21 = nullptr;
    lv_font_t* f22 = nullptr;
    lv_font_t* f24 = nullptr;
    lv_font_t* f52 = nullptr;
};

std::vector<std::string> fontCandidates()
{
    std::vector<std::string> paths;
    if (const char* env = std::getenv("ALLNES_FONT"))
    {
        if (*env)
            paths.emplace_back(env);
    }

    // Bundled UI font (repo's freefonts/ directory). A single .otf rasterizes
    // crisper than the system .ttc collections, so it is preferred when found.
    {
        std::error_code ec;
        const fs::path cwd = fs::current_path(ec);
        if (!ec)
        {
            static const char* kBundled[] = {
                "SourceHanSansCN-Regular.otf",
                "SourceHanSansCN-Medium.otf",
                "阿里巴巴普惠体-REGULAR.OTF",
            };
            for (const char* name : kBundled)
            {
                const fs::path candidate = cwd / "freefonts" / name;
                if (fs::is_regular_file(candidate, ec))
                {
                    paths.push_back(candidate.string());
                    break;
                }
            }
        }
    }

#if defined(_WIN32)
    paths.insert(paths.end(), {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
    });
#elif defined(__APPLE__)
    paths.insert(paths.end(), {
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Songti.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    });
#else
    paths.insert(paths.end(), {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });
#endif
    return paths;
}

lv_font_t* makeFont(const std::string& path, int size)
{
    return lv_freetype_font_create(path.c_str(),
                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                   static_cast<uint32_t>(size),
                                   LV_FREETYPE_FONT_STYLE_NORMAL);
}

bool loadFonts(Fonts& fonts, float scale)
{
    if (scale <= 0.0f)
        scale = 1.0f;

    auto sized = [scale](int design) {
        return std::max(1, static_cast<int>(std::lround(design * scale)));
    };

    for (const std::string& path : fontCandidates())
    {
        lv_font_t* probe = makeFont(path, sized(15));
        if (!probe)
            continue;
        lv_freetype_font_delete(probe);

        fonts.f11 = makeFont(path, sized(11));
        fonts.f12 = makeFont(path, sized(12));
        fonts.f13 = makeFont(path, sized(13));
        fonts.f14 = makeFont(path, sized(14));
        fonts.f15 = makeFont(path, sized(15));
        fonts.f16 = makeFont(path, sized(16));
        fonts.f17 = makeFont(path, sized(17));
        fonts.f18 = makeFont(path, sized(18));
        fonts.f20 = makeFont(path, sized(20));
        fonts.f21 = makeFont(path, sized(21));
        fonts.f22 = makeFont(path, sized(22));
        fonts.f24 = makeFont(path, sized(24));
        fonts.f52 = makeFont(path, sized(52));
        if (fonts.f11 && fonts.f12 && fonts.f13 && fonts.f14 && fonts.f15 &&
            fonts.f16 && fonts.f17 && fonts.f18 && fonts.f20 && fonts.f21 &&
            fonts.f22 && fonts.f24 && fonts.f52)
        {
            return true;
        }
        return false;
    }
    return false;
}

} // namespace

// ================================================================ Impl =====

struct LvglLauncherApp::Impl
{
    // Display backend (window, frame submission, events, timing) and LVGL.
    std::unique_ptr<display::Display> surface;
    float         renderScale = 1.0f;
    lv_display_t* display     = nullptr;
    void*         drawBuf     = nullptr;
    std::size_t   drawBufSize = 0;
    lv_obj_t*     screen      = nullptr;
    lv_group_t*   group       = nullptr;

    bool lvInitialized = false;
    bool needsPresent  = false;

    bool mouseDown = false;
    int  mouseX    = 0;
    int  mouseY    = 0;

    // LVGL's pointer indev asks for one state per timer run. Feeding it only
    // the latest state drops a tap whose press and release both arrive
    // between two timer runs (common on touchscreens) and leaves LVGL acting
    // on stale coordinates. Queue every transition and let the read callback
    // drain the queue within the same frame.
    struct PointerSample
    {
        int  x          = 0;
        int  y          = 0;
        bool down       = false;
        bool transition = false;  // button press/release, never coalesced
    };
    std::deque<PointerSample> pointerQueue;

    // Layout values in the source are design units (1920x1080 canvas); every
    // widget factory converts them to canvas pixels through this. The scale is
    // fractional so the canvas can match the window's drawable pixels 1:1.
    int px(int value) const
    {
        if (value == LV_SIZE_CONTENT)
            return value;
        return static_cast<int>(std::lround(static_cast<double>(value) * renderScale));
    }

    Fonts fonts;

    // Application state (mirrors launcher.cpp).
    std::string    configPath;
    LauncherConfig config;
    std::string    library;
    std::string    error;
    std::string    status;

    std::vector<RomEntry> roms;
    std::vector<int>      visible;
    std::vector<int>      recent;

    int         tab       = 0;
    int         selected  = 0;
    int         scrollRow = 0;
    float       wheelAccum = 0.0f;  // precise wheel deltas still to be consumed
    int         libraryFilter = 0;  // 0 = all, 1 = favorites
    std::string query;
    bool        searchFocused = false;
    bool        searchInGroup = false;
    std::uint32_t swallowSlashUntil = 0;

    bool          quit            = false;
    bool          launchRequested = false;
    LaunchRequest pending;

    long menuFrames = 0;
    long frames     = 0;
    long shotFrame  = 0;

    // ---------------------------------------------------------- widgets ---
    struct Art
    {
        lv_obj_t*    box     = nullptr;
        lv_obj_t*    image   = nullptr;
        lv_obj_t*    initial = nullptr;
        std::string  src;
        std::string  title;
    };

    struct GridCard
    {
        Impl*      app    = nullptr;
        lv_obj_t*  root   = nullptr;
        lv_obj_t*  mask   = nullptr;
        Art        art;
        lv_obj_t*  border = nullptr;
        lv_obj_t*  hover  = nullptr;
        lv_obj_t*  title  = nullptr;
        lv_obj_t*  dot    = nullptr;
        lv_obj_t*  meta   = nullptr;
        int        position = -1;
        int        romIndex = -1;
        bool       hovered  = false;
    };

    struct ShelfCard
    {
        Impl*      app      = nullptr;
        lv_obj_t*  root     = nullptr;
        Art        art;
        lv_obj_t*  title    = nullptr;
        lv_obj_t*  meta     = nullptr;
        int        romIndex = -1;
        bool       hovered  = false;
    };

    struct Tag
    {
        lv_obj_t* root  = nullptr;
        lv_obj_t* label = nullptr;
    };

    lv_obj_t* homePage    = nullptr;
    lv_obj_t* libraryPage = nullptr;
    lv_obj_t* settingsPage = nullptr;

    lv_obj_t* navButtons[3] = { nullptr, nullptr, nullptr };
    lv_obj_t* navLabels[3]  = { nullptr, nullptr, nullptr };

    lv_obj_t* searchBox  = nullptr;
    lv_obj_t* searchArea = nullptr;

    // Home.
    lv_obj_t* banner        = nullptr;
    Art       bannerArt;
    lv_obj_t* bannerTags    = nullptr;
    Tag       bannerTag[3];
    lv_obj_t* bannerTitle   = nullptr;
    lv_obj_t* bannerMeta    = nullptr;
    lv_obj_t* bannerPlay    = nullptr;
    lv_obj_t* bannerPlayLbl = nullptr;
    lv_obj_t* bannerRescan  = nullptr;
    lv_obj_t* bannerBorder  = nullptr;
    struct Dot { Impl* app; int index; lv_obj_t* obj; };
    Dot       dots[5];
    lv_obj_t* shelfHeader   = nullptr;
    lv_obj_t* shelfSub      = nullptr;
    lv_obj_t* shelfEmpty    = nullptr;
    ShelfCard shelfCards[5];
    lv_obj_t* homeCount     = nullptr;
    lv_obj_t* homeGridHeader = nullptr;

    // Grids.
    lv_obj_t*  homeGrid       = nullptr;
    GridCard   homeCards[18];
    lv_obj_t*  homeEmptyMsg   = nullptr;
    lv_obj_t*  homeEmptyHint  = nullptr;
    lv_obj_t*  homeTrack      = nullptr;
    lv_obj_t*  homeThumb      = nullptr;

    lv_obj_t*  libraryGrid    = nullptr;
    GridCard   libraryCards[27];
    lv_obj_t*  libraryCount   = nullptr;
    lv_obj_t*  libFilterPills[2] = {};
    lv_obj_t*  libFilterLbl[2]   = {};
    lv_obj_t*  libraryEmptyMsg  = nullptr;
    lv_obj_t*  libraryEmptyHint = nullptr;
    lv_obj_t*  libraryTrack   = nullptr;
    lv_obj_t*  libraryThumb   = nullptr;

    // Right panel.
    // Transparent dismiss layers for the drawer: top bar, content, status bar.
    // Each one sits below the controls of its region.
    lv_obj_t* rightCatchers[3] = {};
    lv_obj_t* rightPanel     = nullptr;
    Art       rightArt;
    lv_obj_t* rightAmbient   = nullptr;
    lv_obj_t* rightFade      = nullptr;
    lv_obj_t* rightBadge     = nullptr;
    lv_obj_t* rightBadgeLbl  = nullptr;
    lv_obj_t* rightTitle     = nullptr;
    lv_obj_t* rightFile      = nullptr;
    lv_obj_t* rightTags      = nullptr;
    Tag       rightTag[3];
    lv_obj_t* rightDesc      = nullptr;
    lv_obj_t* rightResLabel  = nullptr;
    lv_obj_t* rightPills[6]  = {};
    lv_obj_t* rightPillLbl[6] = {};
    lv_obj_t* rightPlay      = nullptr;
    lv_obj_t* rightPlayLbl   = nullptr;
    lv_obj_t* rightHint      = nullptr;
    lv_obj_t* rightFooter    = nullptr;
    lv_obj_t* rightClose     = nullptr;
    lv_obj_t* rightFav       = nullptr;
    lv_obj_t* rightFavLbl    = nullptr;
    // The details panel is a drawer: hidden by default and shown when a game
    // is clicked. Keyboard navigation updates it without opening it.
    bool      rightOpen      = false;

    // Settings page: a left sidebar plus a scrollable content area with six
    // sections. Widgets are grouped by section.
    lv_obj_t* settingsSidebar   = nullptr;
    lv_obj_t* settingsNav[6]    = {};
    lv_obj_t* settingsNavLbl[6] = {};
    lv_obj_t* settingsContent   = nullptr;
    lv_obj_t* settingsPanels[6] = {};
    int       settingsSection   = 0;  // 0..5

    lv_obj_t* libPathLabel    = nullptr;
    lv_obj_t* libStatsLabel   = nullptr;
    lv_obj_t* toggleRecursive = nullptr;
    lv_obj_t* togglePatches   = nullptr;

    lv_obj_t* resPills[6]      = {};
    lv_obj_t* resPillLbl[6]    = {};
    lv_obj_t* filterPills[4]   = {};
    lv_obj_t* filterPillLbl[4] = {};
    lv_obj_t* toggleAspect     = nullptr;
    lv_obj_t* toggleInteger    = nullptr;
    lv_obj_t* toggleVsync      = nullptr;

    lv_obj_t* coverDirLabel     = nullptr;
    lv_obj_t* toggleCovers      = nullptr;
    lv_obj_t* coverStylePills[3] = {};
    lv_obj_t* coverStyleLbl[3]   = {};

    lv_obj_t* playerPills[2]   = {};
    lv_obj_t* playerPillLbl[2] = {};
    lv_obj_t* gpStatusLed      = nullptr;
    lv_obj_t* gpStatusLbl      = nullptr;
    struct GpChip { lv_obj_t* root = nullptr; lv_obj_t* lbl = nullptr; };
    GpChip    gpChips[display::PadButtonCount];
    struct AxisBar { lv_obj_t* fill = nullptr; lv_obj_t* val = nullptr; };
    AxisBar   gpAxes[display::PadAxisCount];
    lv_obj_t* gpLastInput      = nullptr;
    lv_obj_t* toggleStickDpad  = nullptr;
    lv_obj_t* sliderDeadzone   = nullptr;
    lv_obj_t* deadzoneVal      = nullptr;
    lv_obj_t* sliderTurbo      = nullptr;
    lv_obj_t* turboVal         = nullptr;
    int       gpPlayer         = 0;
    bool      gpPrevBtn[display::PadButtonCount] = {};
    bool      gpPrevTrigger[2] = {};
    struct PadShape
    {
        lv_obj_t* obj         = nullptr;
        lv_obj_t* label       = nullptr;
        int       buttonIndex = -1;
        uint32_t  baseColor   = 0x3D4557;
        uint32_t  activeColor = 0x58A2FF;
        uint32_t  labelColor  = 0xFFFFFF;
        lv_opa_t  baseBorderOpa = 30;
    };
    PadShape  padShapes[display::PadButtonCount];
    lv_obj_t* padStickNubL = nullptr;
    lv_obj_t* padStickNubR = nullptr;
    lv_obj_t* gpTriggerShapes[2] = {};
    lv_obj_t* gpDpadLbl = nullptr;

    // Status.
    lv_obj_t* statusLib      = nullptr;
    lv_obj_t* statusSize     = nullptr;
    lv_obj_t* statusCovers   = nullptr;
    lv_obj_t* statusHint     = nullptr;
    lv_obj_t* statusMessage  = nullptr;
    lv_obj_t* statusSep      = nullptr;
    lv_obj_t* statusRes      = nullptr;
    lv_obj_t* fullscreenBtn  = nullptr;

    // LVGL's delay callback has no user data; there is only one launcher app.
    static Impl* s_active;

    // ---------------------------------------------------------- methods ---
    ~Impl();
    bool init(const std::string& libraryOverride, long menuFrameLimit,
              display::Backend backend);
    bool run(LaunchRequest& out);
    void hideWindow();
    void showWindow();
    void setError(const std::string& message);
    void rescan();

    void handleEvent(const display::Event& event);
    void handleKey(const display::Event& key);
    void handleTextInput(const std::string& text);
    void handleMouseWheel(float wheelY);

    void focusSearch();
    void unfocusSearch();
    void updateSearchStyle();
    void syncQueryFromTextarea();
    void setQuery(const std::string& text);

    void setTab(int newTab);
    void rebuildVisible();
    void rebuildRecent();
    void ensureSelectionVisible();
    int  visibleRows() const;
    void selectRelative(int delta);
    void selectRomIndex(int romIndex);
    void changeResolution(int delta);
    void setResolution(int index);
    void launchSelected();
    bool saveConfig();
    bool isFavoritePath(const std::string& path) const;
    void toggleFavorite();
    void setLibraryFilter(int filter);
    void setSettingsSection(int section);
    void pollGamepad();

    int  selectedRomIndex() const;
    const RomEntry* selectedRom() const;

    void toggleFullscreen();
    void startDoubleClick(const std::string& path, std::uint32_t ticks);
    bool isDoubleClick(const std::string& path, std::uint32_t ticks);

    // UI construction.
    void createUi();
    void createTopBar();
    void createHomePage();
    void createLibraryPage();
    void createSettingsPage();
    void createRightPanel();
    void createStatusBar();
    void openRight();
    void closeRight();
    void createArt(Art& art, lv_obj_t* parent, int x, int y, int w, int h, int radius);
    void createGridCards(lv_obj_t* parent, GridCard* cards, int count);
    void createShelfCards();
    lv_obj_t* makeBox(lv_obj_t* parent, int x, int y, int w, int h,
                      int radius, uint32_t bg, lv_opa_t bgOpa);
    lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                        uint32_t color, int x, int y);
    lv_obj_t* makeCenteredLabel(lv_obj_t* parent, const char* text,
                                const lv_font_t* font, uint32_t color);
    lv_obj_t* makeButton(lv_obj_t* parent, int x, int y, int w, int h,
                         int radius, uint32_t bg, lv_opa_t bgOpa,
                         uint32_t border, lv_opa_t borderOpa,
                         const char* text, const lv_font_t* font,
                         uint32_t textColor);
    lv_obj_t* makePill(lv_obj_t* parent, int x, int y, int w, int h,
                       int radius, const char* text, const lv_font_t* font);
    lv_obj_t* makeToggle(lv_obj_t* parent, int x, int y, bool on);
    void      setToggle(lv_obj_t* toggle, bool on);
    lv_obj_t* makeCard(lv_obj_t* parent, int y, int h,
                       const char* title, const char* hint);
    lv_obj_t* makeCard(lv_obj_t* parent, int x, int y, int w, int h,
                       const char* title, const char* hint);
    Tag       makeTag(lv_obj_t* parent);
    lv_obj_t* makeTagRow(lv_obj_t* parent, int x, int y);
    void      setTag(Tag& tag, const char* text, uint32_t textColor,
                     uint32_t bg, lv_opa_t bgOpa,
                     uint32_t border, lv_opa_t borderOpa);

    // Refresh.
    void refreshAll();
    void refreshPages();
    void refreshHome();
    void refreshBanner();
    void refreshShelf();
    void refreshGrid(bool home);
    void refreshRight();
    void refreshSettings();
    void refreshStatus();
    void refreshLibraryFilter();
    void refreshCardHover(GridCard& card);
    void refreshShelfHover(ShelfCard& card);
    void updateArt(Art& art, const RomEntry* rom, bool darkFallback);
    void setPillActive(lv_obj_t* pill, lv_obj_t* label, bool active);

    // Flush + screenshot.
    void flush(const lv_area_t* area, uint8_t* pxMap);
    bool saveScreenshot(const std::string& path);
    static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap);

    // Event callbacks.
    static void navCb(lv_event_t* event);
    static void searchCb(lv_event_t* event);
    static void fullscreenCb(lv_event_t* event);
    static void bannerPlayCb(lv_event_t* event);
    static void bannerRescanCb(lv_event_t* event);
    static void dotCb(lv_event_t* event);
    static void shelfCb(lv_event_t* event);
    static void gridCb(lv_event_t* event);
    static void rightPlayCb(lv_event_t* event);
    static void rightPillCb(lv_event_t* event);
    static void rightCatcherCb(lv_event_t* event);
    static void rightCloseCb(lv_event_t* event);
    static void rightFavCb(lv_event_t* event);
    static void libFilterCb(lv_event_t* event);
    static void settingsNavCb(lv_event_t* event);
    static void settingsToggleCb(lv_event_t* event);
    static void settingsSegCb(lv_event_t* event);
    static void settingsRescanCb(lv_event_t* event);
    static void settingsBrowseCb(lv_event_t* event);
    static void deadzoneSliderCb(lv_event_t* event);
    static void turboSliderCb(lv_event_t* event);
    static void gpVibCb(lv_event_t* event);
    static void gpResetCb(lv_event_t* event);
    static void padPollCb(lv_timer_t* timer);
    static void pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    std::uint32_t lastClickTicks = 0;
    std::string   lastClickPath;
};

// ------------------------------------------------------------- lifecycle ---

LvglLauncherApp::Impl* LvglLauncherApp::Impl::s_active = nullptr;

bool LvglLauncherApp::Impl::init(const std::string& libraryOverride,
                                 long menuFrameLimit,
                                 display::Backend backend)
{
    menuFrames = menuFrameLimit;

    display::Config displayConfig;
    displayConfig.logicalWidth  = kLogicalWidth;
    displayConfig.logicalHeight = kLogicalHeight;

    std::string displayError;
    surface = display::createDisplay(backend, displayError);
    if (!surface)
    {
        std::fprintf(stderr, "Launcher: %s\n", displayError.c_str());
        return false;
    }
    if (!surface->init(displayConfig, displayError))
    {
        std::fprintf(stderr, "Launcher: %s\n", displayError.c_str());
        surface.reset();
        return false;
    }

    renderScale = surface->renderScale();
    if (renderScale <= 0.0f)
        renderScale = 1.0f;
    // Text input (and the IME on macOS) stays off until the search box is
    // focused; see focusSearch()/showWindow().
    surface->setTextInput(false);
    s_active = this;

    // LVGL core: no SDL driver, no OS/thread port. The UI owns input state and
    // the display backend owns the presentation surface.
    lv_init();
    lvInitialized = true;

    // The backend supplies the only clock: the loop calls lv_tick_inc() once
    // per iteration. LVGL's default lv_delay_ms() busy-waits on that tick, so
    // route internal delays to the backend's real sleep instead.
    lv_delay_set_cb([](uint32_t ms) {
        if (s_active)
            s_active->surface->delay(ms);
    });

    const int canvasWidth  = surface->canvasWidth();
    const int canvasHeight = surface->canvasHeight();

    display = lv_display_create(canvasWidth, canvasHeight);
    if (!display)
    {
        std::fprintf(stderr, "Launcher: lv_display_create failed\n");
        return false;
    }
    lv_display_set_default(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_user_data(display, this);

    // Partial rendering into one band-sized buffer keeps latency low and
    // avoids a full-canvas frame buffer copy per redraw. The band scales with
    // the render scale so it covers the same layout area at any scale.
    const unsigned bandHeight = std::max(1u, static_cast<unsigned>(
        std::lround(120.0 * renderScale)));
    drawBufSize = static_cast<std::size_t>(canvasWidth) *
                  static_cast<std::size_t>(bandHeight) * 4u;
    drawBuf     = std::malloc(drawBufSize);
    if (!drawBuf)
    {
        std::fprintf(stderr, "Launcher: out of memory for the LVGL draw buffer\n");
        return false;
    }
    lv_display_set_buffers(display, drawBuf, nullptr,
                           static_cast<uint32_t>(drawBufSize),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, &Impl::flushCb);

    if (lv_indev_t* pointer = lv_indev_create())
    {
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer, &Impl::pointerReadCb);
        lv_indev_set_driver_data(pointer, this);
    }

    if (!loadFonts(fonts, renderScale))
    {
        std::fprintf(stderr, "Launcher: no usable font found (set ALLNES_FONT to a TTF/TTC)\n");
        return false;
    }

    group = lv_group_create();

    configPath = defaultConfigPath();
    config     = loadLauncherConfig(configPath);

    if (!libraryOverride.empty())
    {
        library            = expandUserPath(libraryOverride);
        config.libraryPath = library;
        saveConfig();
    }
    else if (!config.libraryPath.empty())
    {
        library = expandUserPath(config.libraryPath);
    }
    else
    {
        library = defaultLibraryPath();
    }
    if (library.empty())
        library = ".";
    if (libraryOverride.empty() && config.libraryPath.empty())
    {
        config.libraryPath = library;
        saveConfig();
    }

    // Scan before building the UI so the first frame is already complete.
    rescan();
    rebuildRecent();
    rebuildVisible();

    if (const char* shotFrameFromEnv = std::getenv("ALLNES_LAUNCHER_SHOT_FRAME"))
        shotFrame = std::max(0, std::atoi(shotFrameFromEnv));
    if (const char* tabFromEnv = std::getenv("ALLNES_LAUNCHER_TAB"))
    {
        tab = std::clamp(std::atoi(tabFromEnv), 0, 2);
        ensureSelectionVisible();
    }
    if (const char* sectionFromEnv = std::getenv("ALLNES_LAUNCHER_SETTINGS"))
        settingsSection = std::clamp(std::atoi(sectionFromEnv), 0, 5);

    if (!config.recent.empty())
    {
        for (std::size_t i = 0; i < roms.size(); ++i)
        {
            if (roms[i].path == config.recent.front())
            {
                selectRomIndex(static_cast<int>(i));
                break;
            }
        }
    }

    createUi();
    refreshAll();

    // Poll raw gamepad state for the settings "input" panel at ~30 Hz. LVGL
    // deletes this timer on lv_deinit(), before the Impl is destroyed.
    lv_timer_create(&Impl::padPollCb, 33, this);

    // Screenshot/debug hook: open the detail drawer in the first frame.
    if (const char* details = std::getenv("ALLNES_LAUNCHER_DETAILS"))
    {
        if (*details && std::strcmp(details, "0") != 0)
            openRight();
    }

    lv_obj_invalidate(screen);
    return true;
}

LvglLauncherApp::Impl::~Impl()
{
    if (lvInitialized)
    {
        // The group keeps raw pointers to widgets and a focus callback, so it
        // must be deleted before the screen. Fonts are released after the
        // widget tree, before lv_deinit() shuts the font module down.
        if (group)
        {
            lv_group_delete(group);
            group = nullptr;
        }
        if (display)
        {
            lv_display_delete(display);
            display = nullptr;
        }

        auto dropFont = [](lv_font_t*& font) {
            if (font)
            {
                lv_freetype_font_delete(font);
                font = nullptr;
            }
        };
        dropFont(fonts.f11);
        dropFont(fonts.f12);
        dropFont(fonts.f13);
        dropFont(fonts.f14);
        dropFont(fonts.f15);
        dropFont(fonts.f16);
        dropFont(fonts.f17);
        dropFont(fonts.f18);
        dropFont(fonts.f20);
        dropFont(fonts.f21);
        dropFont(fonts.f22);
        dropFont(fonts.f24);
        dropFont(fonts.f52);

        lv_deinit();
        lvInitialized = false;
    }

    std::free(drawBuf);
    drawBuf = nullptr;

    if (s_active == this)
        s_active = nullptr;
    surface.reset();
}

void LvglLauncherApp::Impl::hideWindow()
{
    // A button release that happens while the window is hidden is never
    // delivered, so do not leave the pointer pressed across a game session.
    mouseDown = false;
    pointerQueue.clear();
    if (surface)
        surface->hide();
}

void LvglLauncherApp::Impl::showWindow()
{
    if (!surface)
        return;
    mouseDown = false;
    pointerQueue.clear();
    surface->show();
    surface->setTextInput(searchFocused);
    if (screen)
        lv_obj_invalidate(screen);
}

void LvglLauncherApp::Impl::setError(const std::string& message)
{
    error = message;
    refreshStatus();
}

// ------------------------------------------------------------ app logic ---

int LvglLauncherApp::Impl::selectedRomIndex() const
{
    if (visible.empty())
        return -1;
    const int position = std::clamp(selected, 0, static_cast<int>(visible.size()) - 1);
    const int index    = visible[static_cast<std::size_t>(position)];
    if (index < 0 || index >= static_cast<int>(roms.size()))
        return -1;
    return index;
}

const RomEntry* LvglLauncherApp::Impl::selectedRom() const
{
    const int index = selectedRomIndex();
    return index < 0 ? nullptr : &roms[static_cast<std::size_t>(index)];
}

bool LvglLauncherApp::Impl::saveConfig()
{
    return saveLauncherConfig(configPath, config);
}

bool LvglLauncherApp::Impl::isFavoritePath(const std::string& path) const
{
    return std::find(config.favorites.begin(), config.favorites.end(), path) !=
           config.favorites.end();
}

void LvglLauncherApp::Impl::toggleFavorite()
{
    const RomEntry* rom = selectedRom();
    if (!rom)
        return;

    auto& favorites = config.favorites;
    const auto it   = std::find(favorites.begin(), favorites.end(), rom->path);
    if (it != favorites.end())
    {
        favorites.erase(it);
        status = "已取消收藏: " + rom->title;
    }
    else
    {
        favorites.push_back(rom->path);
        status = "已收藏: " + rom->title;
    }
    saveConfig();

    // Unfavoriting the selected game removes it from the list when the
    // favorites filter is active, so recompute the visible set.
    rebuildVisible();
    refreshAll();
}

void LvglLauncherApp::Impl::setLibraryFilter(int filter)
{
    libraryFilter = std::clamp(filter, 0, 1);
    scrollRow = 0;
    rebuildVisible();
    refreshAll();
}

void LvglLauncherApp::Impl::rebuildVisible()
{
    visible.clear();
    // The favorites filter is a library-page concept: the home page keeps
    // showing the full library regardless of the selected library filter.
    const bool favoritesOnly = (tab == 1) && (libraryFilter == 1);
    for (std::size_t i = 0; i < roms.size(); ++i)
    {
        if (favoritesOnly && !isFavoritePath(roms[i].path))
            continue;
        if (query.empty() ||
            containsFold(roms[i].title, query) ||
            containsFold(roms[i].name, query))
        {
            visible.push_back(static_cast<int>(i));
        }
    }

    if (visible.empty())
        selected = 0;
    else
        selected = std::clamp(selected, 0, static_cast<int>(visible.size()) - 1);
    ensureSelectionVisible();
}

void LvglLauncherApp::Impl::rebuildRecent()
{
    recent.clear();
    for (const std::string& path : config.recent)
    {
        for (std::size_t i = 0; i < roms.size(); ++i)
        {
            // Match by full path, or by file name so older relative-path
            // entries still resolve against the current absolute scan paths.
            const bool match = roms[i].path == path ||
                fs::path(path).filename() == fs::path(roms[i].path).filename();
            if (match)
            {
                // Dedupe: the same game may appear under both path forms.
                if (std::find(recent.begin(), recent.end(),
                              static_cast<int>(i)) == recent.end())
                    recent.push_back(static_cast<int>(i));
                break;
            }
        }
        if (recent.size() >= 8)
            break;
    }
}

void LvglLauncherApp::Impl::rescan()
{
    const int oldIndex = selectedRomIndex();
    const std::string keepPath =
        oldIndex >= 0 ? roms[static_cast<std::size_t>(oldIndex)].path : std::string();

    std::string scanError;
    roms      = scanLibrary(library, scanError, config.recursiveScan);
    error     = scanError;
    scrollRow = 0;
    wheelAccum = 0.0f;
    rebuildRecent();
    rebuildVisible();

    if (!keepPath.empty())
    {
        for (std::size_t i = 0; i < visible.size(); ++i)
        {
            if (roms[static_cast<std::size_t>(visible[i])].path == keepPath)
            {
                selected = static_cast<int>(i);
                ensureSelectionVisible();
                break;
            }
        }
    }

    if (screen)
        refreshAll();
}

int LvglLauncherApp::Impl::visibleRows() const
{
    // The home page shows two rows when the "recent" section is collapsed, so
    // the freed vertical space is filled instead of left blank.
    return tab == 0 ? (recent.empty() ? 2 : 1) : kLibraryVisibleRows;
}

void LvglLauncherApp::Impl::ensureSelectionVisible()
{
    const int count = static_cast<int>(visible.size());
    if (count <= 0)
    {
        selected  = 0;
        scrollRow = 0;
        return;
    }

    selected = std::clamp(selected, 0, count - 1);
    if (tab == 2)
        return;

    const int rows        = visibleRows();
    const int row         = selected / kColumns;

    if (row < scrollRow)
        scrollRow = row;
    if (row >= scrollRow + rows)
        scrollRow = row - rows + 1;

    scrollRow = std::clamp(scrollRow, 0, maxScrollFor(count, rows));
    if (selected < scrollRow * kColumns)
        scrollRow = selected / kColumns;
}

void LvglLauncherApp::Impl::selectRelative(int delta)
{
    if (visible.empty())
        return;
    selected = std::clamp(selected + delta, 0, static_cast<int>(visible.size()) - 1);
    ensureSelectionVisible();
    error.clear();
    refreshAll();
}

void LvglLauncherApp::Impl::selectRomIndex(int romIndex)
{
    if (!query.empty())
    {
        query.clear();
        if (searchArea)
        {
            lv_textarea_set_text(searchArea, "");
            lv_textarea_set_cursor_pos(searchArea, LV_TEXTAREA_CURSOR_LAST);
        }
        rebuildVisible();
    }

    for (std::size_t i = 0; i < visible.size(); ++i)
    {
        if (visible[i] == romIndex)
        {
            selected = static_cast<int>(i);
            ensureSelectionVisible();
            error.clear();
            refreshAll();
            return;
        }
    }
}

void LvglLauncherApp::Impl::changeResolution(int delta)
{
    const int count = resolutionPresetCount();
    if (count <= 0)
        return;
    setResolution(((config.resolutionIndex + delta) % count + count) % count);
}

void LvglLauncherApp::Impl::setResolution(int index)
{
    const int count = resolutionPresetCount();
    if (count <= 0)
        return;
    config.resolutionIndex = std::clamp(index, 0, count - 1);
    saveConfig();
    status = "窗口分辨率: " + std::string(resolutionPresets()[config.resolutionIndex].label);
    refreshAll();
}

void LvglLauncherApp::Impl::launchSelected()
{
    if (visible.empty())
        return;

    const int position = std::clamp(selected, 0, static_cast<int>(visible.size()) - 1);
    const int romIndex = visible[static_cast<std::size_t>(position)];
    if (romIndex < 0 || romIndex >= static_cast<int>(roms.size()))
        return;

    const RomEntry& rom = roms[static_cast<std::size_t>(romIndex)];
    if (!rom.supported)
    {
        if (rom.mapper < 0)
            error = "不是有效的 iNES 文件: " + rom.name;
        else
            error = "不支持 Mapper " + std::to_string(rom.mapper) + ": " + rom.name;
        refreshStatus();
        return;
    }

    const std::string path = rom.path;
    config.recent.erase(std::remove(config.recent.begin(), config.recent.end(), path),
                        config.recent.end());
    config.recent.insert(config.recent.begin(), path);
    if (config.recent.size() > 8)
        config.recent.resize(8);

    const ResolutionPreset& preset = resolutionPresets()[config.resolutionIndex];
    pending.romPath    = rom.path;
    pending.width      = preset.width > 0 ? preset.width : 768;
    pending.height     = preset.height > 0 ? preset.height : 720;
    pending.fullscreen = preset.fullscreen;
    launchRequested    = true;
    closeRight();
    error.clear();
    status.clear();
    saveConfig();
    rebuildRecent();
}

void LvglLauncherApp::Impl::setTab(int newTab)
{
    tab = std::clamp(newTab, 0, 2);
    wheelAccum = 0.0f;
    closeRight();
    rebuildVisible();
    ensureSelectionVisible();
    refreshAll();
}

void LvglLauncherApp::Impl::focusSearch()
{
    if (!searchArea)
        return;
    searchFocused = true;
    if (group && !searchInGroup)
    {
        lv_group_add_obj(group, searchArea);
        searchInGroup = true;
    }
    if (group)
        lv_group_focus_obj(searchArea);
    lv_textarea_set_cursor_pos(searchArea, LV_TEXTAREA_CURSOR_LAST);
    if (surface)
        surface->setTextInput(true);
    updateSearchStyle();
    refreshStatus();
}

void LvglLauncherApp::Impl::unfocusSearch()
{
    searchFocused = false;
    if (group && searchInGroup && searchArea)
    {
        lv_group_remove_obj(searchArea);
        searchInGroup = false;
    }
    if (surface)
        surface->setTextInput(false);
    updateSearchStyle();
    refreshStatus();
}

void LvglLauncherApp::Impl::updateSearchStyle()
{
    if (!searchBox)
        return;
    lv_obj_set_style_border_color(searchBox, col(searchFocused ? kAccentHex : kWhiteHex), 0);
    lv_obj_set_style_border_opa(searchBox,
                                searchFocused ? LV_OPA_COVER : 22, 0);
}

void LvglLauncherApp::Impl::syncQueryFromTextarea()
{
    if (!searchArea)
        return;
    const char* text = lv_textarea_get_text(searchArea);
    query = text ? text : "";
    selected  = 0;
    scrollRow = 0;
    if (tab == 2)
        tab = 1;
    rebuildVisible();
    error.clear();
    refreshAll();
}

void LvglLauncherApp::Impl::setQuery(const std::string& text)
{
    query = text;
    if (searchArea)
    {
        lv_textarea_set_text(searchArea, query.c_str());
        lv_textarea_set_cursor_pos(searchArea, LV_TEXTAREA_CURSOR_LAST);
    }
    selected  = 0;
    scrollRow = 0;
    rebuildVisible();
    refreshAll();
}

void LvglLauncherApp::Impl::toggleFullscreen()
{
    if (!surface)
        return;
    surface->setFullscreen(!surface->fullscreen());
    if (screen)
        lv_obj_invalidate(screen);
}

void LvglLauncherApp::Impl::openRight()
{
    if (!rightPanel || rightOpen)
        return;
    rightOpen = true;
    refreshRight();
    for (lv_obj_t* catcher : rightCatchers)
    {
        if (catcher)
            lv_obj_set_hidden(catcher, false);
    }
    lv_obj_set_hidden(rightPanel, false);
    lv_obj_move_foreground(rightPanel);
}

void LvglLauncherApp::Impl::closeRight()
{
    if (!rightOpen)
        return;
    rightOpen = false;
    for (lv_obj_t* catcher : rightCatchers)
    {
        if (catcher)
            lv_obj_set_hidden(catcher, true);
    }
    if (rightPanel)
        lv_obj_set_hidden(rightPanel, true);
}

void LvglLauncherApp::Impl::startDoubleClick(const std::string& path,
                                             std::uint32_t ticks)
{
    lastClickPath  = path;
    lastClickTicks = ticks;
}

bool LvglLauncherApp::Impl::isDoubleClick(const std::string& path,
                                          std::uint32_t ticks)
{
    return !path.empty() && path == lastClickPath &&
           ticks - lastClickTicks <= 400;
}

// ------------------------------------------------------------- run loop ---

bool LvglLauncherApp::Impl::run(LaunchRequest& out)
{
    if (!surface)
        return false;

    quit            = false;
    launchRequested = false;
    frames          = 0;

    std::uint32_t nextFrame = surface->ticks();
    std::uint32_t lastTick  = nextFrame;

    while (!quit && !launchRequested)
    {
        display::Event event;
        while (surface->pollEvent(event))
            handleEvent(event);

        if (quit || launchRequested)
            break;

        const std::uint32_t now = surface->ticks();
        lv_tick_inc(now - lastTick);
        lastTick = now;
        lv_timer_handler();

        if (frames == shotFrame)
        {
            if (const char* shot = std::getenv("ALLNES_LAUNCHER_SHOT"))
            {
                if (*shot)
                    saveScreenshot(shot);
            }
        }

        if (needsPresent)
        {
            surface->present();
            needsPresent = false;
        }

        ++frames;
        if (menuFrames > 0 && frames >= menuFrames)
        {
            quit = true;
            break;
        }

        nextFrame += 16;
        const std::uint32_t after = surface->ticks();
        if (nextFrame > after)
            surface->delay(nextFrame - after);
        else if (after - nextFrame > 100)
            nextFrame = after;
    }

    if (launchRequested)
    {
        out = pending;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- events ---

void LvglLauncherApp::Impl::handleEvent(const display::Event& event)
{
    switch (event.type)
    {
    case display::EventType::Quit:
        quit = true;
        break;
    case display::EventType::KeyDown:
        handleKey(event);
        break;
    case display::EventType::TextInput:
        handleTextInput(event.text);
        break;
    case display::EventType::MouseMove:
        mouseX = event.x;
        mouseY = event.y;
        // Hover motion does not need every intermediate point; keep the most
        // recent one so a burst of motion events cannot grow the queue.
        if (!pointerQueue.empty() && !pointerQueue.back().transition &&
            pointerQueue.back().down == mouseDown)
            pointerQueue.back() = { event.x, event.y, mouseDown, false };
        else
            pointerQueue.push_back({ event.x, event.y, mouseDown, false });
        break;
    case display::EventType::MouseButtonDown:
    case display::EventType::MouseButtonUp:
        mouseX    = event.x;
        mouseY    = event.y;
        mouseDown = event.type == display::EventType::MouseButtonDown;
        pointerQueue.push_back({ event.x, event.y, mouseDown, true });
        break;
    case display::EventType::MouseWheel:
        handleMouseWheel(event.wheelY);
        break;
    case display::EventType::FocusLost:
        mouseDown = false;
        pointerQueue.clear();
        break;
    case display::EventType::Expose:
    case display::EventType::Resize:
        if (screen)
            lv_obj_invalidate(screen);
        break;
    default:
        break;
    }
}

void LvglLauncherApp::Impl::handleKey(const display::Event& key)
{
    const display::Key code   = key.key;
    const bool         repeat = key.repeat;

    if (key.ctrl && code == display::Key::Q)
    {
        quit = true;
        return;
    }

    if (code == display::Key::F11 && !repeat)
    {
        toggleFullscreen();
        return;
    }

    if (!searchFocused && code == display::Key::F && !repeat)
    {
        toggleFavorite();
        return;
    }

    if (code == display::Key::F5 && !repeat)
    {
        rescan();
        status = "已重新扫描游戏库";
        refreshAll();
        return;
    }

    if (searchFocused && code == display::Key::Escape)
    {
        if (!query.empty())
        {
            setQuery("");
            status = "已清除搜索";
        }
        else
        {
            unfocusSearch();
        }
        return;
    }

    if (!searchFocused && code == display::Key::Slash && !repeat)
    {
        focusSearch();
        if (surface)
            swallowSlashUntil = surface->ticks() + 250;
        return;
    }

    if (!searchFocused && code == display::Key::Tab && !repeat)
    {
        setTab((tab + 1) % 3);
        return;
    }

    switch (code)
    {
    case display::Key::Escape:
        if (rightOpen)
            closeRight();
        else
            quit = true;
        break;
    case display::Key::Up:
        selectRelative(-kColumns);
        break;
    case display::Key::Down:
        selectRelative(kColumns);
        break;
    case display::Key::Left:
        if (!searchFocused)
            selectRelative(-1);
        break;
    case display::Key::Right:
        if (!searchFocused)
            selectRelative(1);
        break;
    case display::Key::W:
        if (!searchFocused)
            selectRelative(-kColumns);
        break;
    case display::Key::S:
        if (!searchFocused)
            selectRelative(kColumns);
        break;
    case display::Key::A:
        if (!searchFocused)
            selectRelative(-1);
        break;
    case display::Key::D:
        if (!searchFocused)
            selectRelative(1);
        break;
    case display::Key::PageUp:
        selectRelative(-kColumns * visibleRows());
        break;
    case display::Key::PageDown:
        selectRelative(kColumns * visibleRows());
        break;
    case display::Key::Home:
        selected = 0;
        ensureSelectionVisible();
        refreshAll();
        break;
    case display::Key::End:
        if (!visible.empty())
            selected = static_cast<int>(visible.size()) - 1;
        ensureSelectionVisible();
        refreshAll();
        break;
    case display::Key::Enter:
        if (!repeat)
            launchSelected();
        break;
    case display::Key::Backspace:
        if (searchFocused && searchArea)
        {
            lv_textarea_delete_char(searchArea);
            syncQueryFromTextarea();
        }
        break;
    case display::Key::LeftBracket:
        if (!repeat)
            changeResolution(-1);
        break;
    case display::Key::RightBracket:
        if (!repeat)
            changeResolution(1);
        break;
    default:
        break;
    }
}

void LvglLauncherApp::Impl::handleTextInput(const std::string& text)
{
    if (!searchFocused || !searchArea || text.empty())
        return;

    if (swallowSlashUntil != 0)
    {
        const bool fresh = surface && surface->ticks() <= swallowSlashUntil;
        swallowSlashUntil = 0;
        if (fresh && text == "/")
            return;
    }

    lv_textarea_add_text(searchArea, text.c_str());
    syncQueryFromTextarea();
}

void LvglLauncherApp::Impl::handleMouseWheel(float wheelY)
{
    if (tab == 2 || wheelY == 0.0f)
        return;

    // Accumulate precise deltas and advance one row per full scroll step.
    // Mouse wheel notches are whole steps; trackpads emit many fractions, so
    // this keeps a single flick to a few rows instead of one row per event.
    constexpr float kRowStep = 1.0f;
    wheelAccum += wheelY;

    int rows = 0;
    while (wheelAccum >= kRowStep)
    {
        wheelAccum -= kRowStep;
        --rows;
    }
    while (wheelAccum <= -kRowStep)
    {
        wheelAccum += kRowStep;
        ++rows;
    }
    if (rows != 0)
        selectRelative(rows * kColumns);
}

// --------------------------------------------------------- UI factories ---

lv_obj_t* LvglLauncherApp::Impl::makeBox(lv_obj_t* parent, int x, int y, int w, int h,
                                         int radius, uint32_t bg, lv_opa_t bgOpa)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_pos(obj, px(x), px(y));
    lv_obj_set_size(obj, px(w), px(h));
    lv_obj_set_style_radius(obj, px(radius), 0);
    lv_obj_set_style_bg_color(obj, col(bg), 0);
    lv_obj_set_style_bg_opa(obj, bgOpa, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    return obj;
}

lv_obj_t* LvglLauncherApp::Impl::makeLabel(lv_obj_t* parent, const char* text,
                                           const lv_font_t* font, uint32_t color,
                                           int x, int y)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_clickable(label, false);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, col(color), 0);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_pos(label, px(x), px(y));
    return label;
}

lv_obj_t* LvglLauncherApp::Impl::makeCenteredLabel(lv_obj_t* parent, const char* text,
                                                   const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_clickable(label, false);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, col(color), 0);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_center(label);
    return label;
}

lv_obj_t* LvglLauncherApp::Impl::makeButton(lv_obj_t* parent, int x, int y, int w, int h,
                                            int radius, uint32_t bg, lv_opa_t bgOpa,
                                            uint32_t border, lv_opa_t borderOpa,
                                            const char* text, const lv_font_t* font,
                                            uint32_t textColor)
{
    lv_obj_t* button = makeBox(parent, x, y, w, h, radius, bg, bgOpa);
    lv_obj_set_clickable(button, true);
    if (borderOpa > 0)
    {
        lv_obj_set_style_border_width(button, px(1), 0);
        lv_obj_set_style_border_color(button, col(border), 0);
        lv_obj_set_style_border_opa(button, borderOpa, 0);
    }
    lv_obj_set_style_bg_opa(button, bgOpa, LV_STATE_PRESSED);
    makeCenteredLabel(button, text, font, textColor);
    return button;
}

lv_obj_t* LvglLauncherApp::Impl::makePill(lv_obj_t* parent, int x, int y, int w, int h,
                                          int radius, const char* text, const lv_font_t* font)
{
    lv_obj_t* pill = makeButton(parent, x, y, w, h, radius,
                                0xFFFFFF, 12, 0xFFFFFF, 22,
                                text, font, kDimHex);
    return pill;
}

lv_obj_t* LvglLauncherApp::Impl::makeToggle(lv_obj_t* parent, int x, int y, bool on)
{
    lv_obj_t* toggle = makeBox(parent, x, y, 44, 24, 12, 0xFFFFFF, 30);
    lv_obj_set_clickable(toggle, true);
    lv_obj_t* knob = makeBox(toggle, 3, 3, 18, 18, 9, 0xFFFFFF, LV_OPA_COVER);
    lv_obj_set_clickable(knob, false);
    setToggle(toggle, on);
    return toggle;
}

void LvglLauncherApp::Impl::setToggle(lv_obj_t* toggle, bool on)
{
    if (!toggle)
        return;
    lv_obj_set_style_bg_color(toggle, col(on ? kGreenHex : 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(toggle, on ? LV_OPA_COVER : 30, 0);
    if (lv_obj_t* knob = lv_obj_get_child(toggle, 0))
        lv_obj_set_pos(knob, px(on ? 23 : 3), px(3));
}

lv_obj_t* LvglLauncherApp::Impl::makeCard(lv_obj_t* parent, int y, int h,
                                          const char* title, const char* hint)
{
    return makeCard(parent, kSettingsCardX, y, kSettingsCardW, h, title, hint);
}

lv_obj_t* LvglLauncherApp::Impl::makeCard(lv_obj_t* parent, int x, int y, int w, int h,
                                          const char* title, const char* hint)
{
    lv_obj_t* card = makeBox(parent, x, y, w, h, 14, 0x1B1E26, LV_OPA_COVER);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_border_width(card, px(1), 0);
    lv_obj_set_style_border_color(card, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(card, 20, 0);
    if (title)
        makeLabel(card, title, fonts.f17, kTextHex, 30, 24);
    if (hint)
    {
        lv_obj_t* hintLabel = makeLabel(card, hint, fonts.f12, kDimHex, 30, 54);
        lv_obj_set_width(hintLabel, px(w - 60));
        lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_MODE_DOTS);
    }
    return card;
}

LvglLauncherApp::Impl::Tag LvglLauncherApp::Impl::makeTag(lv_obj_t* parent)
{
    Tag tag;
    tag.root = makeBox(parent, 0, 0, LV_SIZE_CONTENT, 24, 8, 0xFFFFFF, 12);
    lv_obj_set_style_pad_hor(tag.root, px(12), 0);
    lv_obj_set_style_border_width(tag.root, px(1), 0);
    lv_obj_set_style_border_color(tag.root, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(tag.root, 22, 0);
    lv_obj_set_flex_flow(tag.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tag.root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tag.label = lv_label_create(tag.root);
    lv_obj_remove_style_all(tag.label);
    lv_obj_set_clickable(tag.label, false);
    lv_label_set_text(tag.label, "");
    lv_obj_set_style_text_font(tag.label, fonts.f12, 0);
    lv_obj_set_style_text_color(tag.label, col(kDimHex), 0);
    return tag;
}

lv_obj_t* LvglLauncherApp::Impl::makeTagRow(lv_obj_t* parent, int x, int y)
{
    lv_obj_t* row = makeBox(parent, x, y, LV_SIZE_CONTENT, 24, 0, 0x000000, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, px(10), 0);
    return row;
}

void LvglLauncherApp::Impl::setTag(Tag& tag, const char* text, uint32_t textColor,
                                   uint32_t bg, lv_opa_t bgOpa,
                                   uint32_t border, lv_opa_t borderOpa)
{
    if (!tag.root)
        return;
    lv_obj_set_hidden(tag.root, false);
    lv_label_set_text(tag.label, text ? text : "");
    lv_obj_set_style_text_color(tag.label, col(textColor), 0);
    lv_obj_set_style_bg_color(tag.root, col(bg), 0);
    lv_obj_set_style_bg_opa(tag.root, bgOpa, 0);
    lv_obj_set_style_border_color(tag.root, col(border), 0);
    lv_obj_set_style_border_opa(tag.root, borderOpa, 0);
}

void LvglLauncherApp::Impl::createArt(Art& art, lv_obj_t* parent,
                                      int x, int y, int w, int h, int radius)
{
    art.box = makeBox(parent, x, y, w, h, radius, 0x222632, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(art.box, col(0x101116), 0);
    lv_obj_set_style_bg_grad_dir(art.box, LV_GRAD_DIR_VER, 0);

    // The image is a child of the art box, not of the panel: COVER scaling
    // makes the image larger than its widget, and LVGL extends the clip area
    // by the image's ext draw size. Parenting it to the exactly-sized art box
    // keeps that overflow clipped to the artwork rectangle (on a 2x canvas the
    // scaled bitmap is bigger than the source and would otherwise paint over
    // the panel content below it).
    art.image = lv_image_create(art.box);
    lv_obj_remove_style_all(art.image);
    lv_obj_set_clickable(art.image, false);
    lv_obj_set_scrollable(art.image, false);
    lv_obj_set_pos(art.image, 0, 0);
    lv_obj_set_size(art.image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(art.image, LV_IMAGE_ALIGN_COVER);
    lv_obj_set_hidden(art.image, true);

    art.initial = lv_label_create(parent);
    lv_obj_remove_style_all(art.initial);
    lv_obj_set_clickable(art.initial, false);
    lv_label_set_text(art.initial, "");
    lv_obj_set_style_text_font(art.initial, fonts.f52, 0);
    lv_obj_set_style_text_color(art.initial, col(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(art.initial, 38, 0);
    lv_obj_set_width(art.initial, LV_SIZE_CONTENT);
    lv_obj_set_height(art.initial, LV_SIZE_CONTENT);
    // Center on the artwork box, not on the parent: the right panel's cover is
    // only the top 250 px of an 876 px panel.
    lv_obj_align_to(art.initial, art.box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(art.initial, true);
}

void LvglLauncherApp::Impl::createGridCards(lv_obj_t* parent, GridCard* cards, int count)
{
    for (int i = 0; i < count; ++i)
    {
        GridCard& card = cards[i];
        card.app  = this;
        card.root = makeBox(parent, 0, 0, kCardW, kCardH, 0, 0x000000, LV_OPA_TRANSP);
        lv_obj_set_clickable(card.root, true);
        lv_obj_set_overflow_visible(card.root, true);

        card.mask = makeBox(card.root, 0, 0, kCardW, kCoverH, 12, 0x1A1D26, LV_OPA_COVER);
        lv_obj_set_style_clip_corner(card.mask, true, 0);
        createArt(card.art, card.mask, 0, 0, kCardW, kCoverH, 12);

        card.hover = makeBox(card.mask, 1, 1, kCardW - 2, kCoverH - 2,
                             11, 0x0A0B0F, 150);
        lv_obj_set_hidden(card.hover, true);
        lv_obj_t* disc = makeBox(card.hover, 0, 0, 52, 52, 26,
                                 0xFFFFFF, 235);
        lv_obj_center(disc);
        lv_obj_t* play = lv_label_create(disc);
        lv_obj_remove_style_all(play);
        lv_obj_set_clickable(play, false);
        lv_label_set_text(play, "▶");
        lv_obj_set_style_text_font(play, fonts.f20, 0);
        lv_obj_set_style_text_color(play, col(0x0E0F13), 0);
        lv_obj_center(play);

        card.border = makeBox(card.mask, 0, 0, kCardW, kCoverH, 12,
                              0x000000, LV_OPA_TRANSP);
        lv_obj_set_style_border_width(card.border, px(1), 0);
        lv_obj_set_style_border_color(card.border, col(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(card.border, 22, 0);

        card.title = makeLabel(card.root, "", fonts.f14, kTextHex, 3, kCoverH + 10);
        lv_obj_set_width(card.title, px(kCardW - 6));
        lv_label_set_long_mode(card.title, LV_LABEL_LONG_MODE_DOTS);

        card.dot = makeBox(card.root, 5, kCoverH + 38, 6, 6, 3,
                           kAccentHex, LV_OPA_COVER);

        card.meta = makeLabel(card.root, "", fonts.f12, kMutedHex, 17, kCoverH + 34);
        lv_obj_set_width(card.meta, px(kCardW - 22));
        lv_label_set_long_mode(card.meta, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_add_event_cb(card.root, &Impl::gridCb, LV_EVENT_CLICKED, &card);
        lv_obj_add_event_cb(card.root, &Impl::gridCb, LV_EVENT_HOVER_OVER, &card);
        lv_obj_add_event_cb(card.root, &Impl::gridCb, LV_EVENT_HOVER_LEAVE, &card);

        lv_obj_set_hidden(card.root, true);
    }
}

void LvglLauncherApp::Impl::createShelfCards()
{
    for (int i = 0; i < 5; ++i)
    {
        ShelfCard& card = shelfCards[i];
        card.app  = this;
        card.root = makeBox(homePage, kLeftX + i * (kShelfCardW + kShelfGap), kShelfY,
                            kShelfCardW, kShelfH, 12, 0xFFFFFF, 8);
        lv_obj_set_clickable(card.root, true);
        lv_obj_set_style_clip_corner(card.root, true, 0);
        lv_obj_set_style_border_width(card.root, px(1), 0);
        lv_obj_set_style_border_color(card.root, col(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(card.root, 22, 0);

        createArt(card.art, card.root, 1, 1, kShelfCardW - 2, 150, 11);

        lv_obj_t* fade = makeBox(card.root, 1, 106, kShelfCardW - 2, 44,
                                 0, 0x0C0D11, LV_OPA_TRANSP);
        lv_obj_set_style_bg_grad_color(fade, col(0x14161C), 0);
        lv_obj_set_style_bg_grad_opa(fade, 220, 0);
        lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);

        card.title = makeLabel(card.root, "", fonts.f14, kTextHex, 12, 158);
        lv_obj_set_width(card.title, px(kShelfCardW - 24));
        lv_label_set_long_mode(card.title, LV_LABEL_LONG_MODE_DOTS);

        card.meta = makeLabel(card.root, "", fonts.f12, kMutedHex, 12, 178);
        lv_obj_set_width(card.meta, px(kShelfCardW - 24));
        lv_label_set_long_mode(card.meta, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_add_event_cb(card.root, &Impl::shelfCb, LV_EVENT_CLICKED, &card);
        lv_obj_add_event_cb(card.root, &Impl::shelfCb, LV_EVENT_HOVER_OVER, &card);
        lv_obj_add_event_cb(card.root, &Impl::shelfCb, LV_EVENT_HOVER_LEAVE, &card);

        lv_obj_set_hidden(card.root, true);
    }
}

// ------------------------------------------------------------- UI build ---

void LvglLauncherApp::Impl::createUi()
{
    screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, col(0x14161D), 0);
    lv_obj_set_style_bg_grad_color(screen, col(0x0D0E12), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    homePage = makeBox(screen, 0, 0, kLogicalWidth, kLogicalHeight,
                       0, 0x000000, LV_OPA_TRANSP);
    libraryPage = makeBox(screen, 0, 0, kLogicalWidth, kLogicalHeight,
                          0, 0x000000, LV_OPA_TRANSP);
    settingsPage = makeBox(screen, 0, 0, kLogicalWidth, kLogicalHeight,
                           0, 0x000000, LV_OPA_TRANSP);

    // Dismiss layers for the details drawer. A click anywhere outside the
    // drawer closes it. Each layer is created before the bars it covers so
    // those bars' buttons stay on top and keep their normal behaviour.
    const auto makeCatcher = [&](int x, int y, int w, int h) {
        lv_obj_t* catcher = makeBox(screen, x, y, w, h, 0, 0x000000, LV_OPA_TRANSP);
        lv_obj_set_clickable(catcher, true);
        lv_obj_add_event_cb(catcher, &Impl::rightCatcherCb, LV_EVENT_CLICKED, this);
        lv_obj_set_hidden(catcher, true);
        return catcher;
    };
    rightCatchers[0] = makeCatcher(0, 0, kLogicalWidth, kNavHeight);

    createTopBar();
    createHomePage();
    createLibraryPage();
    createSettingsPage();

    rightCatchers[2] = makeCatcher(0, kStatusY, kLogicalWidth, kStatusH);
    createStatusBar();

    // Content layer sits above the pages but below the drawer.
    rightCatchers[1] = makeCatcher(0, kNavHeight, kLogicalWidth,
                                   kStatusY - kNavHeight);

    // Last, so the detail drawer paints above the pages and status bar.
    createRightPanel();

    lv_obj_set_hidden(homePage, false);
    lv_obj_set_hidden(libraryPage, true);
    lv_obj_set_hidden(settingsPage, true);
}

void LvglLauncherApp::Impl::createTopBar()
{
    lv_obj_t* tile = makeBox(screen, kMargin, 24, 40, 40, 11,
                             kAccentHex, 28);
    lv_obj_set_style_border_width(tile, px(1), 0);
    lv_obj_set_style_border_color(tile, col(kAccentHex), 0);
    lv_obj_set_style_border_opa(tile, 110, 0);
    makeCenteredLabel(tile, "A", fonts.f22, kAccentHex);

    makeLabel(screen, "AllNES", fonts.f24, kTextHex, 100, 20);
    makeLabel(screen, "NES 模拟器 · 启动器", fonts.f12, kMutedHex, 100, 50);

    makeBox(screen, kNavX, kNavY, 356, kNavH, kNavH / 2, 0xFFFFFF, 13);

    static const char* names[3] = { "首页", "游戏库", "设置" };
    int x = kNavX + 6;
    for (int i = 0; i < 3; ++i)
    {
        navButtons[i] = makeBox(screen, x, kNavY + 6, kNavWidths[i], kNavH - 12,
                                (kNavH - 12) / 2, kNavActiveHex, 0);
        lv_obj_set_clickable(navButtons[i], true);
        navLabels[i] = makeCenteredLabel(navButtons[i], names[i], fonts.f15, kDimHex);
        lv_obj_add_event_cb(navButtons[i], &Impl::navCb, LV_EVENT_CLICKED, this);
        x += kNavWidths[i] + kNavGap;
    }

    searchBox = makeBox(screen, kSearchX, kSearchY, kSearchW, kSearchH,
                        kSearchH / 2, 0xFFFFFF, 13);
    lv_obj_set_clickable(searchBox, true);
    lv_obj_set_style_border_width(searchBox, px(1), 0);
    lv_obj_set_style_border_color(searchBox, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(searchBox, 22, 0);
    lv_obj_add_event_cb(searchBox, &Impl::searchCb, LV_EVENT_CLICKED, this);

    lv_obj_t* lens = makeBox(searchBox, 14, 13, 16, 16, 8,
                             0x000000, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(lens, px(2), 0);
    lv_obj_set_style_border_color(lens, col(kMutedHex), 0);
    lv_obj_set_style_border_opa(lens, LV_OPA_COVER, 0);
    lv_obj_t* handle = makeBox(searchBox, 26, 26, 10, 3, 2, kMutedHex, LV_OPA_COVER);
    lv_obj_set_style_transform_rotation(handle, 450, 0);

    searchArea = lv_textarea_create(searchBox);
    lv_obj_set_pos(searchArea, px(42), px(0));
    lv_obj_set_size(searchArea, px(240), px(44));
    lv_textarea_set_one_line(searchArea, true);
    lv_textarea_set_max_length(searchArea, 64);
    lv_textarea_set_placeholder_text(searchArea, "搜索游戏库…");
    lv_textarea_set_cursor_pos(searchArea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_scrollbar_mode(searchArea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(searchArea, false);
    lv_obj_set_style_bg_opa(searchArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(searchArea, 0, 0);
    lv_obj_set_style_pad_all(searchArea, 0, 0);
    lv_obj_set_style_pad_top(searchArea, px(13), 0);  // center the text vertically
    lv_obj_set_style_text_font(searchArea, fonts.f15, 0);
    lv_obj_set_style_text_color(searchArea, col(kTextHex), 0);
    lv_obj_set_style_text_color(searchArea, col(kFaintHex),
                                LV_PART_TEXTAREA_PLACEHOLDER);
    // LVGL keeps drawing a textarea's cursor even when it is not focused.
    // Hide it by default and show it only for the focused state, which we
    // grant solely while the search box owns keyboard input.
    lv_obj_set_style_bg_color(searchArea, col(kAccentHex), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(searchArea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(searchArea, LV_OPA_COVER,
                            LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(searchArea, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(searchArea, &Impl::searchCb, LV_EVENT_CLICKED, this);

    lv_obj_t* avatar = makeBox(screen, kAvatarX, kAvatarY, kAvatarSize, kAvatarSize,
                               kAvatarSize / 2, kAccentHex, 40);
    makeCenteredLabel(avatar, "A", fonts.f17, kAccentHex);

    makeBox(screen, 0, kNavHeight - 1, kLogicalWidth, 1, 0, 0xFFFFFF, 22);
}

void LvglLauncherApp::Impl::createHomePage()
{
    banner = makeBox(homePage, kLeftX, kBannerY, kLeftW, kBannerH,
                     16, 0x222632, LV_OPA_COVER);
    lv_obj_set_style_clip_corner(banner, true, 0);

    createArt(bannerArt, banner, 0, 0, kLeftW, kBannerH, 16);
    // The art fallback needs a vertical gradient, the banner art a horizontal
    // dark-to-transparent gradient plus a bottom fade.
    lv_obj_set_style_bg_grad_color(bannerArt.box, col(0x101116), 0);
    lv_obj_set_style_bg_grad_dir(bannerArt.box, LV_GRAD_DIR_VER, 0);

    lv_obj_t* leftShade = makeBox(banner, 0, 0, kLeftW, kBannerH,
                                  16, 0x0C0D11, 150);
    lv_obj_set_style_clip_corner(leftShade, true, 0);
    lv_obj_set_style_bg_grad_color(leftShade, col(0x0C0D11), 0);
    lv_obj_set_style_bg_grad_opa(leftShade, 30, 0);
    lv_obj_set_style_bg_grad_dir(leftShade, LV_GRAD_DIR_HOR, 0);

    lv_obj_t* bottomShade = makeBox(banner, 0, kBannerH / 2, kLeftW, kBannerH / 2,
                                    16, 0x0C0D11, LV_OPA_TRANSP);
    lv_obj_set_style_clip_corner(bottomShade, true, 0);
    lv_obj_set_style_bg_grad_color(bottomShade, col(0x0C0D11), 0);
    lv_obj_set_style_bg_grad_opa(bottomShade, 210, 0);
    lv_obj_set_style_bg_grad_dir(bottomShade, LV_GRAD_DIR_VER, 0);

    bannerTags = makeTagRow(banner, 36, 196 - kBannerY);
    for (Tag& tag : bannerTag)
        tag = makeTag(bannerTags);

    bannerTitle = makeLabel(banner, "", fonts.f52, kWhiteHex, 36, 222 - kBannerY);
    lv_obj_set_width(bannerTitle, px(kLeftW - 240));
    lv_label_set_long_mode(bannerTitle, LV_LABEL_LONG_MODE_DOTS);

    bannerMeta = makeLabel(banner, "", fonts.f15, kDimHex, 36, 286 - kBannerY);
    lv_obj_set_width(bannerMeta, px(kLeftW - 240));
    lv_label_set_long_mode(bannerMeta, LV_LABEL_LONG_MODE_DOTS);

    bannerPlay = makeButton(banner, kBannerPlayX - kLeftX, kBannerPlayY - kBannerY,
                            kBannerPlayW, kBannerPlayH, 12,
                            0xFFFFFF, LV_OPA_COVER, 0, 0,
                            "▶  开始游戏", fonts.f16, 0x0E0F13);
    bannerPlayLbl = lv_obj_get_child(bannerPlay, 0);
    lv_obj_add_event_cb(bannerPlay, &Impl::bannerPlayCb, LV_EVENT_CLICKED, this);

    bannerRescan = makeButton(banner, kBannerRescanX - kLeftX, kBannerPlayY - kBannerY,
                              kBannerRescanW, kBannerPlayH, 12,
                              0xFFFFFF, 18, 0xFFFFFF, 70,
                              "重新扫描", fonts.f15, kTextHex);
    lv_obj_add_event_cb(bannerRescan, &Impl::bannerRescanCb, LV_EVENT_CLICKED, this);

    for (int i = 0; i < 5; ++i)
    {
        dots[i].app   = this;
        dots[i].index = i;
        dots[i].obj   = makeBox(banner, 0, 293, 9, 9, 4, 0xFFFFFF, 72);
        lv_obj_set_clickable(dots[i].obj, true);
        lv_obj_add_event_cb(dots[i].obj, &Impl::dotCb, LV_EVENT_CLICKED, &dots[i]);
        lv_obj_set_hidden(dots[i].obj, true);
    }

    bannerBorder = makeBox(banner, 0, 0, kLeftW, kBannerH, 16,
                           0x000000, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(bannerBorder, px(1), 0);
    lv_obj_set_style_border_color(bannerBorder, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(bannerBorder, 22, 0);

    shelfHeader = makeLabel(homePage, "最近游玩", fonts.f21, kTextHex,
                            kLeftX, kShelfHeaderY);
    shelfSub = makeLabel(homePage, "最近启动过的游戏", fonts.f12, kMutedHex,
                         kLeftX + 96, kShelfHeaderY + 7);

    shelfEmpty = makeBox(homePage, kLeftX, kShelfY, kLeftW, kShelfH,
                         12, 0xFFFFFF, 6);
    lv_obj_set_style_border_width(shelfEmpty, px(1), 0);
    lv_obj_set_style_border_color(shelfEmpty, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(shelfEmpty, 22, 0);
    makeCenteredLabel(shelfEmpty, "还没有启动记录，双击游戏封面开始第一次冒险",
                      fonts.f14, kMutedHex);

    createShelfCards();

    homeGridHeader = makeLabel(homePage, "游戏库", fonts.f21, kTextHex,
                               kLeftX, kHomeGridHeaderY);
    homeCount = makeLabel(homePage, "", fonts.f12, kMutedHex,
                          kLeftX + kLeftW - 400, kHomeGridHeaderY + 8);
    lv_obj_set_width(homeCount, px(400));
    lv_obj_set_style_text_align(homeCount, LV_TEXT_ALIGN_RIGHT, 0);

    homeGrid = makeBox(homePage, kLeftX, kHomeGridY, kLeftW, kHomeGridH,
                       0, 0x000000, LV_OPA_TRANSP);
    createGridCards(homeGrid, homeCards, 18);

    homeEmptyMsg = makeLabel(homeGrid, "", fonts.f18, kDimHex, 0, kHomeGridH / 2 - 34);
    lv_obj_set_width(homeEmptyMsg, px(kLeftW));
    lv_obj_set_style_text_align(homeEmptyMsg, LV_TEXT_ALIGN_CENTER, 0);
    homeEmptyHint = makeLabel(homeGrid, "", fonts.f13, kMutedHex, 0, kHomeGridH / 2 + 2);
    lv_obj_set_width(homeEmptyHint, px(kLeftW));
    lv_obj_set_style_text_align(homeEmptyHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(homeEmptyMsg, true);
    lv_obj_set_hidden(homeEmptyHint, true);

    homeTrack = makeBox(homeGrid, kLeftW - 4, kHomeGridY + 4, 3, kHomeGridH - 8,
                        1, 0xFFFFFF, 16);
    homeThumb = makeBox(homeGrid, kLeftW - 4, kHomeGridY + 4, 3, 24,
                        1, 0xFFFFFF, 90);
    lv_obj_set_hidden(homeTrack, true);
    lv_obj_set_hidden(homeThumb, true);
}

void LvglLauncherApp::Impl::createLibraryPage()
{
    makeLabel(libraryPage, "游戏库", fonts.f21, kTextHex, kLeftX, 112);

    // Category filters. "收藏" is the first; more categories can be appended
    // here without touching the grid logic.
    static const char* filterNames[2] = { "全部", "收藏" };
    for (int i = 0; i < 2; ++i)
    {
        const int fx = kLibraryFilterX + i * (kLibraryFilterW + kLibraryFilterGap);
        libFilterPills[i] = makePill(libraryPage, fx, kLibraryFilterY,
                                     kLibraryFilterW, kLibraryFilterH,
                                     10, filterNames[i], fonts.f13);
        libFilterLbl[i] = lv_obj_get_child(libFilterPills[i], 0);
        lv_obj_add_event_cb(libFilterPills[i], &Impl::libFilterCb,
                            LV_EVENT_CLICKED, this);
    }

    libraryCount = makeLabel(libraryPage, "", fonts.f12, kMutedHex,
                             kLeftX + kLeftW - 420, 116);
    lv_obj_set_width(libraryCount, px(420));
    lv_obj_set_style_text_align(libraryCount, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(libraryCount, LV_LABEL_LONG_MODE_DOTS);

    libraryGrid = makeBox(libraryPage, kLeftX, kLibraryGridY, kLeftW, kLibraryGridH,
                          0, 0x000000, LV_OPA_TRANSP);
    createGridCards(libraryGrid, libraryCards, 27);

    libraryEmptyMsg = makeLabel(libraryGrid, "", fonts.f18, kDimHex,
                                0, kLibraryGridH / 2 - 34);
    lv_obj_set_width(libraryEmptyMsg, px(kLeftW));
    lv_obj_set_style_text_align(libraryEmptyMsg, LV_TEXT_ALIGN_CENTER, 0);
    libraryEmptyHint = makeLabel(libraryGrid, "", fonts.f13, kMutedHex,
                                 0, kLibraryGridH / 2 + 2);
    lv_obj_set_width(libraryEmptyHint, px(kLeftW));
    lv_obj_set_style_text_align(libraryEmptyHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(libraryEmptyMsg, true);
    lv_obj_set_hidden(libraryEmptyHint, true);

    libraryTrack = makeBox(libraryGrid, kLeftW - 4, kLibraryGridY + 4, 3, kLibraryGridH - 8,
                           1, 0xFFFFFF, 16);
    libraryThumb = makeBox(libraryGrid, kLeftW - 4, kLibraryGridY + 4, 3, 24,
                           1, 0xFFFFFF, 90);
    lv_obj_set_hidden(libraryTrack, true);
    lv_obj_set_hidden(libraryThumb, true);
}

void LvglLauncherApp::Impl::createSettingsPage()
{
    static const char* navNames[6] = { "游戏库", "显示", "输入 · 手柄", "快捷键", "封面", "关于" };

    // ---- left sidebar ----
    settingsSidebar = makeBox(settingsPage, kSettingsSidebarX, kSettingsSidebarY,
                              kSettingsSidebarW, kSettingsSidebarH, 16, 0x101218, LV_OPA_COVER);
    lv_obj_set_style_clip_corner(settingsSidebar, true, 0);
    lv_obj_set_style_border_width(settingsSidebar, px(1), 0);
    lv_obj_set_style_border_color(settingsSidebar, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(settingsSidebar, 18, 0);

    // The top bar already shows the brand, so the sidebar starts directly with
    // the section nav, top-aligned.
    for (int i = 0; i < 6; ++i)
    {
        const int y = 8 + i * 50;
        settingsNav[i] = makeBox(settingsSidebar, 12, y, kSettingsSidebarW - 24, 44,
                                 10, kNavActiveHex, 0);
        lv_obj_set_clickable(settingsNav[i], true);
        settingsNavLbl[i] = makeLabel(settingsSidebar, navNames[i], fonts.f15,
                                      kDimHex, 26, y + 12);
        lv_obj_set_clickable(settingsNavLbl[i], false);
        lv_obj_add_event_cb(settingsNav[i], &Impl::settingsNavCb, LV_EVENT_CLICKED, this);
    }

    makeLabel(settingsSidebar, "AllNES v0.3.0\nLVGL + FreeType\nWindows / macOS / Linux",
              fonts.f11, kFaintHex, 24, kSettingsSidebarH - 64);

    // ---- scrollable content area + six panels ----
    settingsContent = makeBox(settingsPage, kSettingsContentX, kSettingsContentY,
                              kSettingsContentW, kSettingsContentH, 16, 0x000000,
                              LV_OPA_TRANSP);
    lv_obj_set_scrollable(settingsContent, true);
    lv_obj_set_scroll_dir(settingsContent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settingsContent, LV_SCROLLBAR_MODE_AUTO);

    const int panelHeights[6] = { 470, 560, 896, 440, 380, 260 };
    for (int i = 0; i < 6; ++i)
    {
        settingsPanels[i] = makeBox(settingsContent, 0, 0, kSettingsContentW, panelHeights[i],
                                    0, 0x000000, LV_OPA_TRANSP);
        lv_obj_set_hidden(settingsPanels[i], i != 0);
    }

    auto pageHead = [&](lv_obj_t* panel, const char* title, const char* sub) {
        makeLabel(panel, title, fonts.f24, kWhiteHex, kSettingsCardX, 0);
        makeLabel(panel, sub, fonts.f12, kDimHex, kSettingsCardX, 36);
    };
    auto addToggleRow = [&](lv_obj_t* card, int y, const char* label, lv_obj_t*& toggle,
                            bool on, int cardW = kSettingsCardW) {
        makeLabel(card, label, fonts.f14, kTextHex, 30, y);
        toggle = makeToggle(card, cardW - 74, y - 10, on);
        lv_obj_add_event_cb(toggle, &Impl::settingsToggleCb, LV_EVENT_CLICKED, this);
    };

    // ================= 0 游戏库 =================
    lv_obj_t* lib = settingsPanels[0];
    pageHead(lib, "游戏库", "ROM 目录与扫描");
    lv_obj_t* c = makeCard(lib, 64, 200, "ROM 目录", nullptr);
    makeLabel(c, "启动器直接扫描该目录下的 .nes 文件；点「选择文件夹」或启动时用 --library 指定。",
              fonts.f12, kDimHex, 30, 54);
    lv_obj_t* pathBox = makeBox(c, 30, 92, 1080, 42, 10, 0x21252F, LV_OPA_COVER);
    lv_obj_set_style_border_width(pathBox, px(1), 0);
    lv_obj_set_style_border_color(pathBox, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(pathBox, 20, 0);
    libPathLabel = makeLabel(pathBox, "", fonts.f13, kAccentHex, 16, 12);
    lv_obj_set_width(libPathLabel, px(1048));
    lv_label_set_long_mode(libPathLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t* browse = makeButton(c, 1126, 92, 150, 42, 10, 0x21252F, LV_OPA_COVER,
                                  0xFFFFFF, 20, "选择文件夹…", fonts.f13, kTextHex);
    lv_obj_add_event_cb(browse, &Impl::settingsBrowseCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rescan = makeButton(c, 1288, 92, 150, 42, 10, kAccentHex, LV_OPA_COVER,
                                  kAccentHex, LV_OPA_COVER, "F5 重新扫描", fonts.f13, kWhiteHex);
    lv_obj_add_event_cb(rescan, &Impl::settingsRescanCb, LV_EVENT_CLICKED, this);
    libStatsLabel = makeLabel(c, "", fonts.f12, kDimHex, 30, 150);
    lv_obj_set_width(libStatsLabel, px(kSettingsCardW - 60));
    lv_label_set_long_mode(libStatsLabel, LV_LABEL_LONG_MODE_DOTS);

    c = makeCard(lib, 284, 170, "扫描选项", nullptr);
    makeLabel(c, "自动扫描子目录", fonts.f14, kTextHex, 30, 92);
    toggleRecursive = makeToggle(c, 210, 88, config.recursiveScan);
    lv_obj_add_event_cb(toggleRecursive, &Impl::settingsToggleCb, LV_EVENT_CLICKED, this);
    makeLabel(c, "关闭时仅读取根目录", fonts.f12, kDimHex, 280, 96);
    makeLabel(c, "识别补丁 ROM", fonts.f14, kTextHex, 30, 132);
    togglePatches = makeToggle(c, 210, 128, config.detectPatches);
    lv_obj_add_event_cb(togglePatches, &Impl::settingsToggleCb, LV_EVENT_CLICKED, this);
    makeLabel(c, "自动检测 .ips / .bps 同名补丁并提示", fonts.f12, kDimHex, 280, 136);

    // ================= 1 显示 =================
    lv_obj_t* disp = settingsPanels[1];
    pageHead(disp, "显示", "窗口与画面");
    c = makeCard(disp, 64, 170, "窗口分辨率", "游戏窗口尺寸；游戏画面仍按整数倍缩放并保持宽高比。");
    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        const int px = 30 + i * (150 + 10);
        resPills[i] = makePill(c, px, 92, 150, 42, 10, resolutionPresets()[i].label, fonts.f13);
        resPillLbl[i] = lv_obj_get_child(resPills[i], 0);
        lv_obj_add_event_cb(resPills[i], &Impl::settingsSegCb, LV_EVENT_CLICKED, this);
    }

    c = makeCard(disp, 254, 290, "画面", nullptr);
    makeLabel(c, "像素滤镜", fonts.f14, kTextHex, 30, 92);
    static const char* filterNames[4] = { "CRT 扫描线", "点阵（最近邻）", "双线性平滑", "LCD 网格" };
    for (int i = 0; i < 4; ++i)
    {
        filterPills[i] = makePill(c, 30 + i * 174, 120, 164, 40, 10, filterNames[i], fonts.f12);
        filterPillLbl[i] = lv_obj_get_child(filterPills[i], 0);
        lv_obj_add_event_cb(filterPills[i], &Impl::settingsSegCb, LV_EVENT_CLICKED, this);
    }
    addToggleRow(c, 182, "保持 4:3 宽高比", toggleAspect, config.keepAspect);
    addToggleRow(c, 222, "强制整数倍缩放", toggleInteger, config.integerScale);
    addToggleRow(c, 262, "同步垂直刷新", toggleVsync, config.vsync);

    // ================= 2 输入 · 手柄 =================
    lv_obj_t* pad = settingsPanels[2];
    pageHead(pad, "输入 · 手柄", "手柄测试与按键绑定");

    c = makeCard(pad, 64, 520, "手柄测试", nullptr);
    for (int i = 0; i < 2; ++i)
    {
        playerPills[i] = makePill(c, 30 + i * 100, 88, 90, 36, 10,
                                  i == 0 ? "玩家 1" : "玩家 2", fonts.f13);
        playerPillLbl[i] = lv_obj_get_child(playerPills[i], 0);
        lv_obj_add_event_cb(playerPills[i], &Impl::settingsSegCb, LV_EVENT_CLICKED, this);
    }
    gpStatusLed = makeBox(c, 250, 96, 10, 10, 5, 0x565D68, LV_OPA_COVER);
    gpStatusLbl = makeLabel(c, "未检测到手柄", fonts.f13, kDimHex, 270, 92);
    lv_obj_set_width(gpStatusLbl, px(540));
    lv_label_set_long_mode(gpStatusLbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t* vibBtn = makeButton(c, 880, 86, 130, 40, 10, 0x21252F, LV_OPA_COVER,
                                  0xFFFFFF, 20, "测试震动", fonts.f13, kTextHex);
    lv_obj_add_event_cb(vibBtn, &Impl::gpVibCb, LV_EVENT_CLICKED, this);
    lv_obj_t* resetBtn = makeButton(c, 1020, 86, 150, 40, 10, 0x21252F, LV_OPA_COVER,
                                    0xFF5F56, 60, "恢复默认映射", fonts.f13, 0xFF5F56);
    lv_obj_add_event_cb(resetBtn, &Impl::gpResetCb, LV_EVENT_CLICKED, this);

    // Left: a live Xbox-style visualizer. Shapes are registered in padShapes
    // so pollGamepad() can light them up; stick nubs are moved by their axes.
    lv_obj_t* padBox = makeBox(c, 30, 150, 540, 300, 18, 0x171B22, LV_OPA_COVER);
    lv_obj_set_style_border_width(padBox, px(1), 0);
    lv_obj_set_style_border_color(padBox, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(padBox, 18, 0);

    const auto edge = [&](lv_obj_t* o, uint32_t color, lv_opa_t opa) {
        lv_obj_set_style_border_width(o, px(1), 0);
        lv_obj_set_style_border_color(o, col(color), 0);
        lv_obj_set_style_border_opa(o, opa, 0);
    };

    // Registers a shape with the poller. baseColor is the idle face and
    // activeColor the pressed face; labels switch to white when pressed.
    const auto bind = [&](lv_obj_t* o, int buttonIndex, uint32_t baseColor,
                          uint32_t activeColor, uint32_t labelColor,
                          lv_opa_t baseBorderOpa) {
        if (buttonIndex < 0)
            return;
        padShapes[buttonIndex].obj           = o;
        padShapes[buttonIndex].buttonIndex   = buttonIndex;
        padShapes[buttonIndex].baseColor     = baseColor;
        padShapes[buttonIndex].activeColor   = activeColor;
        padShapes[buttonIndex].labelColor    = labelColor;
        padShapes[buttonIndex].baseBorderOpa = baseBorderOpa;
    };

    const auto hit = [&](int x, int y, int w, int h, int radius, int buttonIndex,
                         uint32_t color) {
        lv_obj_t* o = makeBox(padBox, x, y, w, h, radius, color, LV_OPA_COVER);
        edge(o, 0xFFFFFF, 26);
        bind(o, buttonIndex, color, kAccentHex, kWhiteHex, 26);
        return o;
    };

    // Grips sit behind the body and give the silhouette its controller shape.
    lv_obj_t* gripL = makeBox(padBox, 108, 112, 88, 164, 44, 0x232833, LV_OPA_COVER);
    lv_obj_set_style_transform_pivot_x(gripL, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(gripL, lv_pct(50), 0);
    lv_obj_set_style_transform_rotation(gripL, 120, 0);
    lv_obj_t* gripR = makeBox(padBox, 344, 112, 88, 164, 44, 0x232833, LV_OPA_COVER);
    lv_obj_set_style_transform_pivot_x(gripR, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(gripR, lv_pct(50), 0);
    lv_obj_set_style_transform_rotation(gripR, -120, 0);

    // Body: soft vertical gradient, a thin top highlight and a drop shadow.
    lv_obj_t* body = makeBox(padBox, 70, 58, 400, 158, 58, 0x3C4558, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(body, col(0x232833), 0);
    lv_obj_set_style_bg_grad_dir(body, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(body, px(16), 0);
    lv_obj_set_style_shadow_color(body, col(0x000000), 0);
    lv_obj_set_style_shadow_opa(body, 70, 0);
    lv_obj_set_style_shadow_ofs_y(body, px(6), 0);
    makeBox(padBox, 110, 64, 320, 12, 6, 0xFFFFFF, 6);

    // LT/RT are axes, so they get their own shapes instead of padShapes keys.
    const auto trigger = [&](int x, const char* text, int index) {
        lv_obj_t* o = makeBox(padBox, x, 24, 92, 18, 9, 0x2A303C, LV_OPA_COVER);
        edge(o, 0xFFFFFF, 20);
        gpTriggerShapes[index] = o;
        makeCenteredLabel(o, text, fonts.f11, kMutedHex);
    };
    trigger(112, "LT", 0);
    trigger(336, "RT", 1);

    // LB/RB sit above the body, under the triggers.
    lv_obj_t* lb = hit(110, 44, 100, 26, 13, display::PadLeftShoulder, 0x3A4252);
    padShapes[display::PadLeftShoulder].label =
        makeCenteredLabel(lb, "LB", fonts.f11, kTextHex);
    lv_obj_t* rb = hit(330, 44, 100, 26, 13, display::PadRightShoulder, 0x3A4252);
    padShapes[display::PadRightShoulder].label =
        makeCenteredLabel(rb, "RB", fonts.f11, kTextHex);

    // D-pad: recessed cross base plus four raised direction blocks.
    makeBox(padBox, 133, 159, 78, 26, 8, 0x1A1F28, LV_OPA_COVER);
    makeBox(padBox, 159, 133, 26, 78, 8, 0x1A1F28, LV_OPA_COVER);
    hit(161, 135, 22, 22, 5, display::PadDpadUp, 0x4A5366);
    hit(161, 187, 22, 22, 5, display::PadDpadDown, 0x4A5366);
    hit(135, 161, 22, 22, 5, display::PadDpadLeft, 0x4A5366);
    hit(187, 161, 22, 22, 5, display::PadDpadRight, 0x4A5366);
    makeBox(padBox, 164, 164, 16, 16, 5, 0x3A4252, LV_OPA_COVER);

    // Recessed stick wells; the raised nubs are bound to L3/R3.
    const auto stick = [&](int cx, int cy, lv_obj_t** nub) {
        makeBox(padBox, cx - 30, cy - 30, 60, 60, 30, 0x10141A, LV_OPA_COVER);
        lv_obj_t* well = makeBox(padBox, cx - 27, cy - 27, 54, 54, 27, 0x2A303D,
                                 LV_OPA_COVER);
        edge(well, 0x000000, 100);
        lv_obj_t* n = makeBox(padBox, cx - 19, cy - 19, 38, 38, 19, 0x707C92,
                              LV_OPA_COVER);
        lv_obj_set_style_bg_grad_color(n, col(0x39404F), 0);
        lv_obj_set_style_bg_grad_dir(n, LV_GRAD_DIR_VER, 0);
        edge(n, 0x9AA5B8, 70);
        makeBox(n, 7, 5, 14, 14, 7, 0xFFFFFF, 22); // soft top highlight
        *nub = n;
    };
    stick(150, 100, &padStickNubL);
    stick(338, 182, &padStickNubR);
    bind(padStickNubL, display::PadLeftStick, 0x707C92, 0x8C99B0, kWhiteHex, 70);
    bind(padStickNubR, display::PadRightStick, 0x707C92, 0x8C99B0, kWhiteHex, 70);

    // ABXY diamond, coloured like the Xbox buttons.
    struct Abxy { const char* text; int cx; int cy; uint32_t color; int button; };
    const Abxy abxy[] = {
        { "Y", 398, 92, 0xE3B341, display::PadY },
        { "X", 362, 128, 0x5AA2F0, display::PadX },
        { "B", 434, 128, 0xE06C6C, display::PadB },
        { "A", 398, 164, 0x59C36A, display::PadA },
    };
    for (const Abxy& b : abxy)
    {
        lv_obj_t* o = makeBox(padBox, b.cx - 19, b.cy - 19, 38, 38, 19, 0x2A303C,
                              LV_OPA_COVER);
        edge(o, b.color, 140);
        lv_obj_set_style_border_width(o, px(2), 0);
        bind(o, b.button, 0x2A303C, b.color, b.color, 140);
        padShapes[b.button].label = makeCenteredLabel(o, b.text, fonts.f20, b.color);
    }

    // View / Menu: small recessed circles with tiny glyph shapes.
    lv_obj_t* view = hit(237, 108, 20, 20, 10, display::PadBack, 0x2A303C);
    makeBox(view, 5, 5, 6, 6, 1, 0xFFFFFF, 70);
    makeBox(view, 9, 9, 6, 6, 1, 0xFFFFFF, 40);

    lv_obj_t* menu = hit(283, 108, 20, 20, 10, display::PadStart, 0x2A303C);
    for (int i = 0; i < 3; ++i)
        makeBox(menu, 5, 5 + i * 4, 10, 2, 1, 0xFFFFFF, 70);

    // Guide sits between them, with a lit ring.
    lv_obj_t* guide = hit(256, 136, 28, 28, 14, display::PadGuide, 0x242A34);
    bind(guide, display::PadGuide, 0x242A34, 0x3FA34D, kWhiteHex, 40);
    lv_obj_t* ring = makeBox(guide, 8, 8, 12, 12, 6, 0xFFFFFF, 0);
    lv_obj_set_style_border_width(ring, px(2), 0);
    lv_obj_set_style_border_color(ring, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(ring, 150, 0);

    // Live D-pad readout: all four directions at a glance.
    gpDpadLbl = makeLabel(c, "十字键 —", fonts.f12, kMutedHex, 30, 458);
    lv_obj_set_width(gpDpadLbl, px(180));
    makeLabel(c, "LT/RT 扳机 · LB/RB 肩键 · 双摇杆可按下", fonts.f11, kDimHex, 220, 459);

    // Right: button chips + axis monitor + last input.
    for (int i = 0; i < display::PadButtonCount; ++i)
    {
        const int colX = 600 + (i % 5) * 112;
        const int rowY = 150 + (i / 5) * 38;
        gpChips[i].root = makeBox(c, colX, rowY, 106, 32, 8, 0xFFFFFF, 0);
        lv_obj_set_style_border_width(gpChips[i].root, px(1), 0);
        lv_obj_set_style_border_color(gpChips[i].root, col(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(gpChips[i].root, 30, 0);
        gpChips[i].lbl = makeCenteredLabel(gpChips[i].root, kPadButtonNames[i],
                                           fonts.f12, kDimHex);
    }
    for (int i = 0; i < display::PadAxisCount; ++i)
    {
        const int rowY = 280 + i * 36;
        makeLabel(c, kPadAxisNames[i], fonts.f12, kDimHex, 600, rowY);
        lv_obj_t* track = makeBox(c, 720, rowY + 5, 260, 8, 4, 0xFFFFFF, 16);
        gpAxes[i].fill = makeBox(track, 0, 0, 0, 8, 4, kAccentHex, LV_OPA_COVER);
        gpAxes[i].val = makeLabel(c, "0.00", fonts.f12, kMutedHex, 990, rowY);
        lv_obj_set_width(gpAxes[i].val, px(64));
        lv_obj_set_style_text_align(gpAxes[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    }
    gpLastInput = makeLabel(c, "最后输入：—", fonts.f12, kDimHex, 600, 500);
    lv_obj_set_width(gpLastInput, px(540));

    // ---- 按键绑定 / 高级：并排显示，整页无需滚动即可看全 ----
    const int halfCardW = (kSettingsCardW - 20) / 2;

    c = makeCard(pad, 0, 600, halfCardW, 280, "按键绑定",
                 "按键绑定由模拟器 keybindings.conf 管理（模拟器层）。");
    for (int i = 0; i < static_cast<int>(sizeof(kDefaultBinds) / sizeof(kDefaultBinds[0])); ++i)
    {
        const int colX = 30 + (i / 6) * 360;
        const int rowY = 92 + (i % 6) * 32;
        makeLabel(c, kDefaultBinds[i].label, fonts.f13, kTextHex, colX, rowY);
        makeLabel(c, kDefaultBinds[i].binding, fonts.f13, kMutedHex, colX + 140, rowY);
    }

    // ---- 高级 ----
    c = makeCard(pad, halfCardW + 20, 600, halfCardW, 280, "高级", nullptr);
    addToggleRow(c, 96, "摇杆模拟十字键", toggleStickDpad, config.stickToDpad, halfCardW);
    makeLabel(c, "摇杆死区", fonts.f14, kTextHex, 30, 144);
    sliderDeadzone = lv_slider_create(c);
    lv_obj_set_pos(sliderDeadzone, px(150), px(140));
    lv_obj_set_size(sliderDeadzone, px(300), px(10));
    lv_slider_set_range(sliderDeadzone, 0, 50);
    lv_slider_set_value(sliderDeadzone, static_cast<int>(config.deadzone * 100.0f), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sliderDeadzone, col(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sliderDeadzone, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(sliderDeadzone, &Impl::deadzoneSliderCb, LV_EVENT_VALUE_CHANGED, this);
    deadzoneVal = makeLabel(c, "0.15", fonts.f12, kMutedHex, 470, 138);
    makeLabel(c, "连发速度", fonts.f14, kTextHex, 30, 190);
    sliderTurbo = lv_slider_create(c);
    lv_obj_set_pos(sliderTurbo, px(150), px(186));
    lv_obj_set_size(sliderTurbo, px(300), px(10));
    lv_slider_set_range(sliderTurbo, 1, 30);
    lv_slider_set_value(sliderTurbo, config.turbo, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sliderTurbo, col(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sliderTurbo, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(sliderTurbo, &Impl::turboSliderCb, LV_EVENT_VALUE_CHANGED, this);
    turboVal = makeLabel(c, "12 / 帧", fonts.f12, kMutedHex, 470, 184);
    makeLabel(c, "设置保存在 launcher.conf，启动游戏时生效。", fonts.f11, kDimHex, 30, 232);

    // ================= 3 快捷键 =================
    lv_obj_t* keys = settingsPanels[3];
    pageHead(keys, "快捷键", "启动器与模拟器全局热键");
    c = makeCard(keys, 64, 340, "快捷键", nullptr);
    struct KeyHint { const char* k; const char* a; };
    const KeyHint hints[] = {
        { "↑ ↓ ← →", "选择游戏" }, { "Enter", "启动选中的游戏" },
        { "双击卡片", "直接启动" }, { "/", "聚焦搜索框" },
        { "F5", "重新扫描游戏库" }, { "F", "收藏 / 取消收藏" },
        { "[  ]", "切换窗口分辨率" }, { "F11", "启动器全屏" },
        { "Esc", "清除搜索 / 返回 / 退出" }, { "F1 · F2", "即时存档 / 读档" },
    };
    for (int i = 0; i < static_cast<int>(sizeof(hints) / sizeof(hints[0])); ++i)
    {
        const int colX = 30 + (i / 5) * 640;
        const int rowY = 88 + (i % 5) * 44;
        makeLabel(c, hints[i].k, fonts.f13, kAccentHex, colX, rowY);
        makeLabel(c, hints[i].a, fonts.f13, kDimHex, colX + 140, rowY);
    }

    // ================= 4 封面 =================
    lv_obj_t* cover = settingsPanels[4];
    pageHead(cover, "封面", "游戏库封面图");
    c = makeCard(cover, 64, 260, "封面目录", nullptr);
    makeLabel(c, "把任意 3:4 的 PNG / JPEG 命名为「ROM 文件名.png」，放到游戏库的 images/ 目录即可显示。",
              fonts.f12, kDimHex, 30, 54);
    coverDirLabel = makeLabel(c, "", fonts.f13, kAccentHex, 30, 92);
    lv_obj_set_width(coverDirLabel, px(kSettingsCardW - 60));
    lv_label_set_long_mode(coverDirLabel, LV_LABEL_LONG_MODE_DOTS);
    addToggleRow(c, 132, "显示封面（关闭后网格只显示标题）", toggleCovers, config.showCovers);
    makeLabel(c, "占位图风格", fonts.f14, kTextHex, 30, 172);
    static const char* styleNames[3] = { "像素马赛克", "纯色 + 首字母", "灰色占位" };
    for (int i = 0; i < 3; ++i)
    {
        coverStylePills[i] = makePill(c, 30 + i * 190, 196, 180, 40, 10, styleNames[i], fonts.f12);
        coverStyleLbl[i] = lv_obj_get_child(coverStylePills[i], 0);
        lv_obj_add_event_cb(coverStylePills[i], &Impl::settingsSegCb, LV_EVENT_CLICKED, this);
    }

    // ================= 5 关于 =================
    lv_obj_t* about = settingsPanels[5];
    pageHead(about, "关于", "");
    c = makeCard(about, 64, 190, "AllNES", nullptr);
    makeLabel(c, "版本 v0.3.0 · LVGL + FreeType", fonts.f14, kTextHex, 30, 62);
    makeLabel(c, "Windows / macOS / Linux", fonts.f13, kDimHex, 30, 90);
    makeLabel(c, "启动器可显示真实的 mapper、文件大小与运行状态。", fonts.f13, kDimHex, 30, 114);
    makeButton(c, 30, 142, 130, 40, 10, 0x21252F, LV_OPA_COVER, 0xFFFFFF, 20, "检查更新", fonts.f13, kTextHex);
    makeButton(c, 170, 142, 130, 40, 10, 0x21252F, LV_OPA_COVER, 0xFFFFFF, 20, "开源许可", fonts.f13, kTextHex);
    makeButton(c, 310, 142, 130, 40, 10, 0x21252F, LV_OPA_COVER, 0xFFFFFF, 20, "日志目录", fonts.f13, kTextHex);
}

void LvglLauncherApp::Impl::createRightPanel()
{
    // Detail drawer: closed by default, opened by clicking a game. An opaque
    // background is required because it now overlays the card grid instead of
    // occupying a dedicated column.
    rightPanel = makeBox(screen, kRightX, kRightY, kRightW, kRightH,
                         16, 0x14161D, LV_OPA_COVER);
    lv_obj_set_style_clip_corner(rightPanel, true, 0);
    lv_obj_set_clickable(rightPanel, true);
    lv_obj_set_style_shadow_width(rightPanel, px(40), 0);
    lv_obj_set_style_shadow_color(rightPanel, col(0x000000), 0);
    lv_obj_set_style_shadow_opa(rightPanel, 170, 0);
    lv_obj_set_style_shadow_offset_x(rightPanel, px(-10), 0);
    lv_obj_set_style_shadow_offset_y(rightPanel, px(6), 0);

    createArt(rightArt, rightPanel, 1, 1, kRightW - 2, 250, 15);
    lv_obj_set_style_bg_grad_color(rightArt.box, col(0x16171D), 0);
    lv_obj_set_style_bg_grad_dir(rightArt.box, LV_GRAD_DIR_VER, 0);

    rightAmbient = makeBox(rightPanel, 1, 1, kRightW - 2, 250,
                           0, 0x0A0B0F, 105);
    rightFade = makeBox(rightPanel, 1, 181, kRightW - 2, 70,
                        0, 0x0E0F13, LV_OPA_TRANSP);
    lv_obj_set_style_bg_grad_color(rightFade, col(0x111217), 0);
    lv_obj_set_style_bg_grad_opa(rightFade, 240, 0);
    lv_obj_set_style_bg_grad_dir(rightFade, LV_GRAD_DIR_VER, 0);

    lv_obj_t* border = makeBox(rightPanel, 0, 0, kRightW, kRightH, 16,
                               0x000000, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(border, px(1), 0);
    lv_obj_set_style_border_color(border, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(border, 22, 0);

    rightBadge = makeBox(rightPanel, 20, 204, LV_SIZE_CONTENT, 28, 8,
                         0x0E0F13, 210);
    lv_obj_set_style_pad_hor(rightBadge, px(12), 0);
    lv_obj_set_style_border_width(rightBadge, px(1), 0);
    lv_obj_set_style_border_color(rightBadge, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(rightBadge, 70, 0);
    lv_obj_set_flex_flow(rightBadge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rightBadge, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    rightBadgeLbl = lv_label_create(rightBadge);
    lv_obj_remove_style_all(rightBadgeLbl);
    lv_label_set_text(rightBadgeLbl, "");
    lv_obj_set_style_text_font(rightBadgeLbl, fonts.f12, 0);
    lv_obj_set_style_text_color(rightBadgeLbl, col(kDimHex), 0);

    rightTitle = makeLabel(rightPanel, "", fonts.f24, kWhiteHex, 22, 268);
    lv_obj_set_width(rightTitle, px(kRightBodyW));
    lv_label_set_long_mode(rightTitle, LV_LABEL_LONG_MODE_DOTS);

    rightFile = makeLabel(rightPanel, "", fonts.f13, kMutedHex, 22, 304);
    lv_obj_set_width(rightFile, px(kRightBodyW));
    lv_label_set_long_mode(rightFile, LV_LABEL_LONG_MODE_DOTS);

    rightTags = makeTagRow(rightPanel, 22, 330);
    for (Tag& tag : rightTag)
        tag = makeTag(rightTags);

    makeBox(rightPanel, 22, 372, kRightBodyW, 1, 0, 0xFFFFFF, 22);

    rightDesc = makeLabel(rightPanel, "", fonts.f13, kDimHex, 22, 386);
    lv_obj_set_width(rightDesc, px(kRightBodyW));
    lv_label_set_long_mode(rightDesc, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(rightDesc, px(4), 0);

    rightResLabel = makeLabel(rightPanel, "窗口分辨率", fonts.f12, kMutedHex, 22, 466);

    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        const int px = (kRightBodyX - kRightX) +
                       (i % 3) * (kRightPillW + kRightPillGap);
        const int py = (kRightPillY - kRightY) + (i / 3) * (kRightPillH + kRightPillGap);
        rightPills[i] = makePill(rightPanel, px, py, kRightPillW, kRightPillH,
                                 8, resolutionPresets()[i].label, fonts.f12);
        rightPillLbl[i] = lv_obj_get_child(rightPills[i], 0);
        lv_obj_add_event_cb(rightPills[i], &Impl::rightPillCb, LV_EVENT_CLICKED, this);
    }

    rightPlay = makeButton(rightPanel, 22, kRightPlayY - kRightY,
                           kRightBodyW, kRightPlayH, 12,
                           0xFFFFFF, LV_OPA_COVER, 0, 0,
                           "▶  启动游戏", fonts.f16, 0x0E0F13);
    rightPlayLbl = lv_obj_get_child(rightPlay, 0);
    lv_obj_add_event_cb(rightPlay, &Impl::rightPlayCb, LV_EVENT_CLICKED, this);

    rightHint = makeLabel(rightPanel, "", fonts.f12, kMutedHex, 22,
                          kRightPlayY - kRightY + kRightPlayH + 10);
    lv_obj_set_width(rightHint, px(kRightBodyW));
    lv_obj_set_style_text_align(rightHint, LV_TEXT_ALIGN_CENTER, 0);

    makeBox(rightPanel, 22, kRightH - 44, kRightBodyW, 1, 0, 0xFFFFFF, 22);
    rightFooter = makeLabel(rightPanel, "", fonts.f11, kFaintHex, 22, kRightH - 30);
    lv_obj_set_width(rightFooter, px(kRightBodyW));
    lv_label_set_long_mode(rightFooter, LV_LABEL_LONG_MODE_DOTS);

    // Close button floats over the cover so the drawer is dismissible with
    // both a pointer and Esc.
    rightClose = makeBox(rightPanel, kRightW - 52, 12, 36, 36, 18,
                         0x0E0F13, 220);
    lv_obj_set_clickable(rightClose, true);
    lv_obj_set_style_border_width(rightClose, px(1), 0);
    lv_obj_set_style_border_color(rightClose, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(rightClose, 70, 0);
    makeCenteredLabel(rightClose, "×", fonts.f18, kTextHex);
    lv_obj_add_event_cb(rightClose, &Impl::rightCloseCb, LV_EVENT_CLICKED, this);

    // Favorite toggle floats over the cover next to the close button.
    rightFav = makeBox(rightPanel, kRightW - 96, 12, 36, 36, 18,
                       0x0E0F13, 220);
    lv_obj_set_clickable(rightFav, true);
    lv_obj_set_style_border_width(rightFav, px(1), 0);
    lv_obj_set_style_border_color(rightFav, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(rightFav, 70, 0);
    rightFavLbl = makeCenteredLabel(rightFav, "☆", fonts.f18, kTextHex);
    lv_obj_add_event_cb(rightFav, &Impl::rightFavCb, LV_EVENT_CLICKED, this);

    lv_obj_set_hidden(rightPanel, true);
    rightOpen = false;
}

void LvglLauncherApp::Impl::createStatusBar()
{
    makeBox(screen, 0, kStatusY, kLogicalWidth, kStatusH, 0, 0x0C0D11, LV_OPA_COVER);
    makeBox(screen, 0, kStatusY, kLogicalWidth, 1, 0, 0xFFFFFF, 22);

    makeBox(screen, 49, kStatusY + 23, 6, 6, 3, kGreenHex, LV_OPA_COVER);

    lv_obj_t* left = makeBox(screen, 64, kStatusY + 18, LV_SIZE_CONTENT, 18,
                             0, 0x000000, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, px(18), 0);

    statusLib = makeLabel(left, "", fonts.f12, kMutedHex, 0, 0);
    makeLabel(left, "|", fonts.f12, 0x3A3F4A, 0, 0);
    statusSize = makeLabel(left, "", fonts.f12, kMutedHex, 0, 0);
    makeLabel(left, "|", fonts.f12, 0x3A3F4A, 0, 0);
    statusCovers = makeLabel(left, "", fonts.f12, kMutedHex, 0, 0);
    makeLabel(left, "|", fonts.f12, 0x3A3F4A, 0, 0);
    statusHint = makeLabel(left, "", fonts.f12, kFaintHex, 0, 0);

    statusRes = makeLabel(screen, "", fonts.f12, kMutedHex, 0, 0);
    lv_obj_align(statusRes, LV_ALIGN_TOP_RIGHT, px(-98), px(kStatusY + 18));
    statusSep = makeLabel(screen, "|", fonts.f12, 0x3A3F4A, 0, 0);
    lv_obj_align_to(statusSep, statusRes, LV_ALIGN_OUT_LEFT_MID, px(-18), 0);
    statusMessage = makeLabel(screen, "", fonts.f12, kMutedHex, 0, 0);
    lv_obj_set_width(statusMessage, px(520));
    lv_obj_set_style_text_align(statusMessage, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(statusMessage, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align_to(statusMessage, statusSep, LV_ALIGN_OUT_LEFT_MID, px(-18), 0);

    fullscreenBtn = makeBox(screen, kFullscreenX, kFullscreenY, kFullscreenW,
                            kFullscreenH, 8, 0xFFFFFF, 8);
    lv_obj_set_style_border_width(fullscreenBtn, px(1), 0);
    lv_obj_set_style_border_color(fullscreenBtn, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(fullscreenBtn, 22, 0);
    lv_obj_set_clickable(fullscreenBtn, true);
    lv_obj_add_event_cb(fullscreenBtn, &Impl::fullscreenCb, LV_EVENT_CLICKED, this);

    // Eight small arms form the classic fullscreen corner icon.
    const int inset = 10;
    const int arm   = 6;
    const int s     = kFullscreenW;
    makeBox(fullscreenBtn, inset, inset, arm, 1, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, inset, inset, 1, arm, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, s - inset - arm, inset, arm, 1, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, s - inset - 1, inset, 1, arm, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, inset, s - inset - 1, arm, 1, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, inset, s - inset - arm, 1, arm, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, s - inset - arm, s - inset - 1, arm, 1, 0, kDimHex, LV_OPA_COVER);
    makeBox(fullscreenBtn, s - inset - 1, s - inset - arm, 1, arm, 0, kDimHex, LV_OPA_COVER);
}

// --------------------------------------------------------------- refresh ---

void LvglLauncherApp::Impl::updateArt(Art& art, const RomEntry* rom, bool darkFallback)
{
    const std::string title = rom ? rom->title : std::string();
    const Palette& palette  = paletteFor(title.empty() ? "AllNES" : title);

    uint32_t top    = darkFallback ? 0x222632 : palette.top;
    uint32_t bottom = darkFallback ? 0x16171D : palette.bottom;
    if (!rom && !darkFallback)
    {
        top    = 0x222632;
        bottom = 0x101116;
    }

    lv_opa_t glyphOpa = 38;
    switch (config.coverStyle)
    {
    case 1: // solid + initial
        bottom = top;
        glyphOpa = 130;
        break;
    case 2: // gray placeholder
        top = bottom = 0x262A33;
        glyphOpa = 110;
        break;
    default: // pixel mosaic gradient
        break;
    }
    lv_obj_set_style_bg_color(art.box, col(top), 0);
    lv_obj_set_style_bg_grad_color(art.box, col(bottom), 0);

    const std::string glyph = firstGlyph(title);
    const char* current = lv_label_get_text(art.initial);
    if (!current || glyph != current)
        lv_label_set_text(art.initial, glyph.c_str());
    lv_obj_set_style_text_opa(art.initial, glyphOpa, 0);

    if (rom && !rom->coverPath.empty() && config.showCovers)
    {
        const std::string src = "A:" + rom->coverPath;
        bool usable = true;
        if (src != art.src)
        {
            lv_image_header_t header;
            usable = lv_image_decoder_get_info(src.c_str(), &header) == LV_RESULT_OK;
            if (usable)
            {
                lv_image_set_src(art.image, src.c_str());
                art.src = src;
            }
            else
            {
                art.src.clear();
            }
        }
        else
        {
            usable = !art.src.empty();
        }

        lv_obj_set_hidden(art.image, !usable);
        lv_obj_set_hidden(art.initial, usable);
    }
    else
    {
        art.src.clear();
        lv_obj_set_hidden(art.image, true);
        lv_obj_set_hidden(art.initial, false);
    }
}

void LvglLauncherApp::Impl::refreshPages()
{
    lv_obj_set_hidden(homePage, tab != 0);
    lv_obj_set_hidden(libraryPage, tab != 1);
    lv_obj_set_hidden(settingsPage, tab != 2);

    for (int i = 0; i < 3; ++i)
    {
        const bool active = i == tab;
        lv_obj_set_style_bg_opa(navButtons[i],
                                active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(navButtons[i], col(kNavActiveHex), 0);
        lv_obj_set_style_text_color(navLabels[i],
                                    col(active ? kWhiteHex : kDimHex), 0);
    }
}

void LvglLauncherApp::Impl::refreshHome()
{
    const std::size_t count = visible.size();
    const std::string countText = query.empty()
        ? "共 " + std::to_string(count) + " 款"
        : "搜索结果 " + std::to_string(count) + " 款";
    lv_label_set_text(homeCount, countText.c_str());

    refreshBanner();
    refreshShelf();
    refreshGrid(true);
}

void LvglLauncherApp::Impl::refreshBanner()
{
    const RomEntry* rom = selectedRom();

    updateArt(bannerArt, rom, false);

    if (rom)
    {
        setTag(bannerTag[0], mapperText(*rom).c_str(),
               rom->supported ? kAccentHex : kOrangeTextHex,
               rom->supported ? kAccentHex : kOrangeHex,
               rom->supported ? 38 : 38,
               rom->supported ? kAccentHex : kOrangeHex,
               rom->supported ? 100 : 110);
        const std::string size = formatSize(rom->sizeBytes);
        setTag(bannerTag[1], size.c_str(), kDimHex, 0x0E0F13, 180,
               0xFFFFFF, 70);
        setTag(bannerTag[2], rom->supported ? "可运行" : "不可运行",
               rom->supported ? 0x78DCA0 : kOrangeTextHex,
               0x0E0F13, 180,
               rom->supported ? kGreenHex : kOrangeHex,
               rom->supported ? 120 : 110);
        lv_obj_set_hidden(bannerTags, false);
    }
    else
    {
        lv_obj_set_hidden(bannerTags, true);
    }

    std::string title;
    if (rom)
        title = rom->title;
    else if (query.empty())
        title = "游戏库是空的";
    else
        title = "没有找到匹配的游戏";
    lv_label_set_text(bannerTitle, title.c_str());

    std::string meta;
    if (rom)
        meta = rom->name + "  ·  iNES  ·  " + formatSize(rom->sizeBytes);
    else
        meta = query.empty() ? "把 .nes 游戏放进游戏库目录，按 F5 重新扫描"
                             : "按 Esc 清除搜索条件";
    lv_label_set_text(bannerMeta, meta.c_str());

    const bool primary = rom && rom->supported;
    lv_obj_set_style_bg_color(bannerPlay, col(primary ? kWhiteHex : 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(bannerPlay, primary ? LV_OPA_COVER : 22, 0);
    lv_obj_set_style_border_width(bannerPlay, primary ? 0 : px(1), 0);
    lv_obj_set_style_border_color(bannerPlay, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(bannerPlay, 70, 0);
    lv_obj_set_style_text_color(bannerPlayLbl,
                                col(primary ? 0x0E0F13 : kTextHex), 0);

    const int dotCount = std::min<int>(5, static_cast<int>(recent.size()));
    int total = 0;
    for (int i = 0; i < dotCount; ++i)
    {
        total += (i == 0 ? 26 : 9);
        if (i + 1 < dotCount)
            total += 8;
    }
    int dotX = kLeftW - 36 - total;
    const int selectedIndex = selectedRomIndex();
    for (int i = 0; i < 5; ++i)
    {
        if (i >= dotCount)
        {
            lv_obj_set_hidden(dots[i].obj, true);
            continue;
        }
        const int w = (i == 0 ? 26 : 9);
        lv_obj_set_hidden(dots[i].obj, false);
        lv_obj_set_pos(dots[i].obj, px(dotX), px(293));
        lv_obj_set_size(dots[i].obj, px(w), px(9));
        lv_obj_set_style_radius(dots[i].obj, px(4), 0);
        const bool active = recent[static_cast<std::size_t>(i)] == selectedIndex;
        lv_obj_set_style_bg_opa(dots[i].obj,
                                active ? LV_OPA_COVER : 72, 0);
        lv_obj_set_style_bg_color(dots[i].obj, col(0xFFFFFF), 0);
        dotX += w + 8;
    }
}

void LvglLauncherApp::Impl::refreshShelf()
{
    const int count = std::min<int>(5, static_cast<int>(recent.size()));
    const bool hasRecent = count != 0;

    // With no play history the whole section collapses and the grid moves up.
    lv_obj_set_hidden(shelfHeader, !hasRecent);
    lv_obj_set_hidden(shelfSub, !hasRecent);
    lv_obj_set_hidden(shelfEmpty, !hasRecent);

    for (int i = 0; i < 5; ++i)
    {
        ShelfCard& card = shelfCards[i];
        if (i >= count)
        {
            lv_obj_set_hidden(card.root, true);
            card.romIndex = -1;
            continue;
        }

        const int romIndex = recent[static_cast<std::size_t>(i)];
        if (romIndex < 0 || romIndex >= static_cast<int>(roms.size()))
        {
            lv_obj_set_hidden(card.root, true);
            card.romIndex = -1;
            continue;
        }

        const RomEntry& rom = roms[static_cast<std::size_t>(romIndex)];
        if (card.romIndex != romIndex)
        {
            card.romIndex = romIndex;
            lv_label_set_text(card.title, rom.title.c_str());

            const std::string meta = mapperText(rom) + "  ·  " + formatSize(rom.sizeBytes);
            lv_label_set_text(card.meta, meta.c_str());
        }
        updateArt(card.art, &rom, false);
        lv_obj_set_hidden(card.root, false);
        refreshShelfHover(card);
    }

    const int gridHeaderY = hasRecent ? kHomeGridHeaderY : kHomeEmptyHeaderY;
    const int gridY       = hasRecent ? kHomeGridY       : kHomeEmptyGridY;
    const int gridH       = hasRecent ? kHomeGridH       : kHomeGridEmptyH;
    // Collapse the hero so two full card rows fit in the freed space.
    lv_obj_set_height(banner, px(hasRecent ? kBannerH : kBannerHEmpty));
    lv_obj_set_pos(homeGridHeader, px(kLeftX), px(gridHeaderY));
    lv_obj_set_pos(homeCount, px(kLeftX + kLeftW - 400), px(gridHeaderY + 8));
    lv_obj_set_pos(homeGrid, px(kLeftX), px(gridY));
    lv_obj_set_size(homeGrid, px(kLeftW), px(gridH));
}

void LvglLauncherApp::Impl::refreshShelfHover(ShelfCard& card)
{
    lv_obj_set_style_bg_opa(card.root, card.hovered ? 16 : 8, 0);
    lv_obj_set_style_border_opa(card.root, card.hovered ? 70 : 22, 0);
}

void LvglLauncherApp::Impl::refreshGrid(bool home)
{
    GridCard* cards = home ? homeCards : libraryCards;
    const int homeRows = recent.empty() ? 2 : 1;
    const int slots = home ? homeRows * kColumns : 27;
    const int rows  = home ? homeRows : 3;
    const int areaW = kLeftW;
    const int areaH = home ? (recent.empty() ? kHomeGridEmptyH : kHomeGridH) : kLibraryGridH;

    const int total = static_cast<int>(visible.size());
    const bool empty = total == 0;

    lv_obj_set_hidden(home ? homeEmptyMsg : libraryEmptyMsg, !empty);
    lv_obj_set_hidden(home ? homeEmptyHint : libraryEmptyHint, !empty);
    if (empty)
    {
        std::string message;
        std::string hint;
        if (!home && libraryFilter == 1)
        {
            message = "还没有收藏任何游戏";
            hint = "在游戏详情面板点 ☆，或选中游戏后按 F 收藏";
        }
        else if (query.empty())
        {
            message = "这个游戏库里还没有游戏";
            hint = "把 .nes 文件放进游戏库目录，然后按 F5 重新扫描";
        }
        else
        {
            message = "没有找到匹配的游戏";
            hint = "试试别的关键词，或按 Esc 清除搜索";
        }
        lv_label_set_text(home ? homeEmptyMsg : libraryEmptyMsg, message.c_str());
        lv_label_set_text(home ? homeEmptyHint : libraryEmptyHint, hint.c_str());
    }

    for (int i = 0; i < slots; ++i)
    {
        GridCard& card = cards[i];
        const int visualRow = i / kColumns;
        const int column    = i % kColumns;
        const int position  = (scrollRow + visualRow) * kColumns + column;
        const bool visibleSlot = !empty && visualRow < rows && position < total &&
                                 position < static_cast<int>(visible.size());

        if (!visibleSlot)
        {
            lv_obj_set_hidden(card.root, true);
            card.position = -1;
            continue;
        }

        const int romIndex = visible[static_cast<std::size_t>(position)];
        if (romIndex < 0 || romIndex >= static_cast<int>(roms.size()))
        {
            lv_obj_set_hidden(card.root, true);
            card.position = -1;
            continue;
        }

        const RomEntry& rom = roms[static_cast<std::size_t>(romIndex)];
        card.position = position;

        if (card.romIndex != romIndex)
        {
            card.romIndex = romIndex;
            lv_label_set_text(card.title, rom.title.c_str());

            const std::string mapper = rom.mapper < 0 ? "无效 ROM"
                                                       : "Mapper " + std::to_string(rom.mapper);
            const std::string meta = mapper + " · " + formatSize(rom.sizeBytes);
            lv_label_set_text(card.meta, meta.c_str());
            lv_obj_set_style_text_color(card.meta,
                                        col(rom.supported ? kMutedHex : kOrangeTextHex), 0);
            lv_obj_set_style_bg_color(card.dot,
                                      col(rom.supported ? kAccentHex : kOrangeHex), 0);
        }

        updateArt(card.art, &rom, false);
        lv_obj_set_hidden(card.root, false);
        lv_obj_set_pos(card.root, px(column * (kCardW + kCardGapX)),
                       px(visualRow * (kCardH + kCardGapY)));

        const bool selectedCard = position == selected;
        lv_obj_set_style_border_width(card.border, px(selectedCard ? 2 : 1), 0);
        lv_obj_set_style_border_color(card.border,
                                      col(selectedCard ? kAccentHex : 0xFFFFFF), 0);
        lv_obj_set_style_border_opa(card.border,
                                    selectedCard ? 255 : (card.hovered ? 70 : 22), 0);
        refreshCardHover(card);
    }

    // Scroll indicator. The library draws three rows but scrolls by two, just
    // like the SDL launcher: the third row is a preview of what comes next.
    lv_obj_t* track = home ? homeTrack : libraryTrack;
    lv_obj_t* thumb = home ? homeThumb : libraryThumb;
    const int fullRows = home ? homeRows : kLibraryVisibleRows;
    const int rowCount = (total + kColumns - 1) / kColumns;
    if (rowCount > fullRows && total > 0)
    {
        const int trackH = areaH - 8;
        const int thumbH = std::max<int>(24, trackH * fullRows / rowCount);
        const int maxScroll = std::max(1, rowCount - fullRows);
        const int thumbY = 4 + (trackH - thumbH) *
                           std::clamp(scrollRow, 0, maxScroll) / maxScroll;

        lv_obj_set_hidden(track, false);
        lv_obj_set_hidden(thumb, false);
        lv_obj_set_pos(track, px(areaW - 4), px(4));
        lv_obj_set_size(track, px(3), px(trackH));
        lv_obj_set_pos(thumb, px(areaW - 4), px(thumbY));
        lv_obj_set_size(thumb, px(3), px(thumbH));
    }
    else
    {
        lv_obj_set_hidden(track, true);
        lv_obj_set_hidden(thumb, true);
    }

}

void LvglLauncherApp::Impl::refreshCardHover(GridCard& card)
{
    lv_obj_set_hidden(card.hover, !card.hovered);
    if (!card.hovered)
        lv_obj_set_style_border_opa(card.border,
                                    card.position == selected ? 255 : 22, 0);
}

void LvglLauncherApp::Impl::setPillActive(lv_obj_t* pill, lv_obj_t* label, bool active)
{
    lv_obj_set_style_bg_color(pill, col(active ? kAccentHex : 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(pill, active ? 38 : 12, 0);
    lv_obj_set_style_border_color(pill, col(active ? kAccentHex : 0xFFFFFF), 0);
    lv_obj_set_style_border_opa(pill, active ? 130 : 22, 0);
    lv_obj_set_style_text_color(label, col(active ? kAccentHex : kDimHex), 0);
}

void LvglLauncherApp::Impl::refreshRight()
{
    const RomEntry* rom = selectedRom();

    updateArt(rightArt, rom, true);
    lv_obj_set_hidden(rightAmbient, false);
    lv_obj_set_hidden(rightFade, false);

    if (rom)
    {
        const std::string badge = mapperText(*rom) + " · " +
                                  (rom->supported ? "可运行" : "不可运行");
        lv_label_set_text(rightBadgeLbl, badge.c_str());
        lv_obj_set_style_text_color(rightBadgeLbl,
                                    col(rom->supported ? kDimHex : kOrangeTextHex), 0);
        lv_obj_set_hidden(rightBadge, false);
        lv_obj_set_hidden(rightTags, false);

        lv_label_set_text(rightTitle, rom->title.c_str());
        lv_label_set_text(rightFile, rom->name.c_str());

        const std::string size = formatSize(rom->sizeBytes);
        setTag(rightTag[0], size.c_str(), kDimHex, 0xFFFFFF, 12, 0xFFFFFF, 22);
        setTag(rightTag[1], "iNES", kDimHex, 0xFFFFFF, 12, 0xFFFFFF, 22);
        setTag(rightTag[2], "2P 按键", kDimHex, 0xFFFFFF, 12, 0xFFFFFF, 22);

        lv_label_set_text(rightDesc,
            "按 Enter 或点击下方按钮启动游戏，按 F 或点右上角 ☆ 收藏。"
            "游戏内按 Esc 返回启动器，F11 切换全屏，Ctrl+R 复位。");
        lv_label_set_text(rightHint, rom->supported ? "Enter 也可直接启动"
                                                    : "该 ROM 暂不支持");
        lv_obj_set_style_text_color(rightHint,
                                    col(rom->supported ? kMutedHex : kOrangeTextHex), 0);
    }
    else
    {
        lv_obj_set_hidden(rightBadge, true);
        lv_obj_set_hidden(rightTags, true);
        lv_label_set_text(rightTitle, "未选择游戏");
        lv_label_set_text(rightFile, "在游戏库中选择一款游戏");
        lv_label_set_text(rightDesc,
            "把 NES ROM 放进游戏库目录。启动器会自动扫描，并为 roms/images 里的封面图"
            "显示卡片。");
        lv_label_set_text(rightHint, "先选择一款游戏");
        lv_obj_set_style_text_color(rightHint, col(kOrangeTextHex), 0);
    }

    const bool fav = rom && isFavoritePath(rom->path);
    lv_label_set_text(rightFavLbl, fav ? "★" : "☆");
    lv_obj_set_style_text_color(rightFavLbl, col(fav ? kAccentHex : kTextHex), 0);

    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        setPillActive(rightPills[i], rightPillLbl[i],
                      i == config.resolutionIndex);
    }

    const bool primary = rom && rom->supported;
    lv_obj_set_style_bg_color(rightPlay, col(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(rightPlay, primary ? LV_OPA_COVER : 22, 0);
    lv_obj_set_style_border_width(rightPlay, primary ? 0 : px(1), 0);
    lv_obj_set_style_border_color(rightPlay, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(rightPlay, 70, 0);
    lv_obj_set_style_text_color(rightPlayLbl,
                                col(primary ? 0x0E0F13 : kTextHex), 0);

    lv_label_set_text(rightFooter, rom ? rom->path.c_str() : library.c_str());
}

void LvglLauncherApp::Impl::refreshSettings()
{
    if (!settingsSidebar)
        return;

    // Library.
    lv_label_set_text(libPathLabel, library.c_str());
    const int total = static_cast<int>(roms.size());
    long long totalBytes = 0;
    int covers = 0;
    for (const RomEntry& rom : roms)
    {
        totalBytes += rom.sizeBytes;
        if (!rom.coverPath.empty())
            ++covers;
    }
    lv_label_set_text(libStatsLabel,
        ("当前库：" + std::to_string(total) + " 个 ROM · 占用 " + formatSize(totalBytes) +
         " · 封面 " + std::to_string(covers) + "/" + std::to_string(total)).c_str());
    setToggle(toggleRecursive, config.recursiveScan);
    setToggle(togglePatches, config.detectPatches);

    // Display.
    for (int i = 0; i < resolutionPresetCount(); ++i)
        setPillActive(resPills[i], resPillLbl[i], i == config.resolutionIndex);
    for (int i = 0; i < 4; ++i)
        setPillActive(filterPills[i], filterPillLbl[i], i == config.pixelFilter);
    setToggle(toggleAspect, config.keepAspect);
    setToggle(toggleInteger, config.integerScale);
    setToggle(toggleVsync, config.vsync);

    // Cover.
    lv_label_set_text(coverDirLabel,
        (library.empty() ? std::string(".") : library + "/images").c_str());
    setToggle(toggleCovers, config.showCovers);
    for (int i = 0; i < 3; ++i)
        setPillActive(coverStylePills[i], coverStyleLbl[i], i == config.coverStyle);

    // Gamepad.
    for (int i = 0; i < 2; ++i)
        setPillActive(playerPills[i], playerPillLbl[i], i == gpPlayer);
    setToggle(toggleStickDpad, config.stickToDpad);

    for (int i = 0; i < 6; ++i)
    {
        lv_obj_set_hidden(settingsPanels[i], i != settingsSection);
        lv_obj_set_style_bg_opa(settingsNav[i],
                                i == settingsSection ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(settingsNavLbl[i],
                                    col(i == settingsSection ? kWhiteHex : kDimHex), 0);
    }
}

void LvglLauncherApp::Impl::setSettingsSection(int section)
{
    settingsSection = std::clamp(section, 0, 5);
    if (settingsContent)
        lv_obj_scroll_to_y(settingsContent, 0, LV_ANIM_OFF);
    refreshSettings();
}

void LvglLauncherApp::Impl::pollGamepad()
{
    if (!surface || tab != 2 || settingsSection != 2 || !gpChips[0].root)
        return;

    const int slot = gpPlayer;
    const bool connected = surface->padCount() > slot;

    lv_obj_set_style_bg_color(gpStatusLed, col(connected ? kGreenHex : 0x565D68), 0);
    if (connected)
    {
        const std::string name = surface->padName(slot);
        lv_label_set_text(gpStatusLbl,
            (name.empty() ? ("手柄 " + std::to_string(slot + 1)) : name).c_str());
    }
    else
    {
        lv_label_set_text(gpStatusLbl, "未检测到手柄 · 按任意键唤醒");
    }

    for (int i = 0; i < display::PadButtonCount; ++i)
    {
        const bool on = connected && surface->padButton(slot, i);
        lv_obj_set_style_bg_opa(gpChips[i].root, on ? 38 : 0, 0);
        lv_obj_set_style_border_color(gpChips[i].root, col(on ? kAccentHex : 0xFFFFFF), 0);
        lv_obj_set_style_border_opa(gpChips[i].root, on ? 255 : 30, 0);
        lv_obj_set_style_text_color(gpChips[i].lbl, col(on ? kWhiteHex : kDimHex), 0);

        if (padShapes[i].obj)
        {
            PadShape& shape = padShapes[i];
            lv_obj_set_style_bg_color(shape.obj,
                                      col(on ? shape.activeColor : shape.baseColor), 0);
            lv_obj_set_style_border_opa(shape.obj, on ? 255 : shape.baseBorderOpa, 0);
            if (shape.label)
                lv_obj_set_style_text_color(shape.label,
                                            col(on ? kWhiteHex : shape.labelColor), 0);
        }

        if (on && !gpPrevBtn[i])
            lv_label_set_text(gpLastInput, ("最后输入：" + std::string(kPadButtonNames[i])).c_str());
        gpPrevBtn[i] = on;
    }

    for (int i = 0; i < display::PadAxisCount; ++i)
    {
        const int raw = connected ? surface->padAxis(slot, i) : 0;
        const float v = i < 4 ? raw / 32768.0f : raw / 32767.0f;
        const int pct = i < 4 ? static_cast<int>((v + 1.0f) * 50.0f)
                              : static_cast<int>(v * 100.0f);
        lv_obj_set_width(gpAxes[i].fill, px(260 * std::clamp(pct, 0, 100) / 100));
        char buf[16];
        std::snprintf(buf, sizeof(buf), i < 4 ? "%+.2f" : "%.2f", v);
        lv_label_set_text(gpAxes[i].val, buf);
    }

    // Move the visualizer stick nubs to mirror the left/right sticks, and let
    // them spring back to centre when the pad disconnects.
    if (padStickNubL)
    {
        const float lx = connected ? surface->padAxis(slot, display::PadLeftX) / 32768.0f : 0.0f;
        const float ly = connected ? surface->padAxis(slot, display::PadLeftY) / 32768.0f : 0.0f;
        lv_obj_set_pos(padStickNubL, px(131 + static_cast<int>(lx * 9)),
                       px(81 + static_cast<int>(ly * 9)));
    }
    if (padStickNubR)
    {
        const float rx = connected ? surface->padAxis(slot, display::PadRightX) / 32768.0f : 0.0f;
        const float ry = connected ? surface->padAxis(slot, display::PadRightY) / 32768.0f : 0.0f;
        lv_obj_set_pos(padStickNubR, px(319 + static_cast<int>(rx * 9)),
                       px(163 + static_cast<int>(ry * 9)));
    }

    // Trigger bars are axis-driven.
    for (int t = 0; t < 2; ++t)
    {
        if (!gpTriggerShapes[t])
            continue;

        const int raw = connected
                          ? surface->padAxis(slot, t == 0 ? display::PadTriggerLeft
                                                          : display::PadTriggerRight)
                          : 0;
        const bool on = raw > 2000;
        lv_obj_set_style_bg_color(gpTriggerShapes[t], col(on ? kAccentHex : 0x2A303C), 0);
        lv_obj_set_style_border_opa(gpTriggerShapes[t], on ? 255 : 20, 0);
        if (lv_obj_t* lbl = lv_obj_get_child(gpTriggerShapes[t], 0))
            lv_obj_set_style_text_color(lbl, col(on ? kWhiteHex : kMutedHex), 0);

        if (on && !gpPrevTrigger[t])
            lv_label_set_text(gpLastInput, t == 0 ? "最后输入：LT" : "最后输入：RT");
        gpPrevTrigger[t] = on;
    }

    // Live D-pad readout, including the raw-hat fallback.
    if (gpDpadLbl)
    {
        std::string dirs;
        if (connected)
        {
            if (surface->padButton(slot, display::PadDpadUp))    dirs += "↑ ";
            if (surface->padButton(slot, display::PadDpadDown))  dirs += "↓ ";
            if (surface->padButton(slot, display::PadDpadLeft))  dirs += "← ";
            if (surface->padButton(slot, display::PadDpadRight)) dirs += "→ ";
        }
        lv_label_set_text(gpDpadLbl,
                          dirs.empty() ? "十字键 —" : ("十字键 " + dirs).c_str());
    }
}

void LvglLauncherApp::Impl::refreshStatus()
{
    const int total = static_cast<int>(roms.size());
    const int shown = static_cast<int>(visible.size());

    if (query.empty())
        lv_label_set_text(statusLib, ("ROM 库 " + std::to_string(total) + " 个").c_str());
    else
        lv_label_set_text(statusLib,
            ("筛选 " + std::to_string(shown) + " / " + std::to_string(total) + " 个").c_str());

    long long totalBytes = 0;
    int covers = 0;
    for (const RomEntry& rom : roms)
    {
        totalBytes += rom.sizeBytes;
        if (!rom.coverPath.empty())
            ++covers;
    }

    lv_label_set_text(statusSize, ("占用 " + formatSize(totalBytes)).c_str());
    lv_label_set_text(statusCovers,
        ("封面 " + std::to_string(covers) + " / " + std::to_string(total)).c_str());
    lv_label_set_text(statusHint, searchFocused ? "输入关键词筛选游戏"
                                                : "单击查看详情 · 双击启动");

    const std::string message = !error.empty()
        ? error
        : (status.empty() ? "就绪" : status);
    lv_label_set_text(statusMessage, message.c_str());
    lv_obj_set_style_text_color(statusMessage,
                                col(error.empty() ? kMutedHex : kOrangeHex), 0);

    lv_label_set_text(statusRes,
        ("分辨率 " + std::string(resolutionPresets()[std::clamp(
            config.resolutionIndex, 0, resolutionPresetCount() - 1)].label)).c_str());
    lv_obj_update_layout(statusRes);
    lv_obj_align(statusRes, LV_ALIGN_TOP_RIGHT, px(-98), px(kStatusY + 18));
    lv_obj_align_to(statusSep, statusRes, LV_ALIGN_OUT_LEFT_MID, px(-18), 0);
    lv_obj_align_to(statusMessage, statusSep, LV_ALIGN_OUT_LEFT_MID, px(-18), 0);
}

void LvglLauncherApp::Impl::refreshLibraryFilter()
{
    for (int i = 0; i < 2; ++i)
        setPillActive(libFilterPills[i], libFilterLbl[i], i == libraryFilter);
}

void LvglLauncherApp::Impl::refreshAll()
{
    if (!screen)
        return;
    refreshPages();
    refreshHome();
    refreshGrid(false);
    refreshLibraryFilter();
    refreshSettings();
    refreshRight();
    refreshStatus();
    updateSearchStyle();
}

// --------------------------------------------------------------- events ---

void LvglLauncherApp::Impl::navCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!app || !target)
        return;
    for (int i = 0; i < 3; ++i)
    {
        if (app->navButtons[i] == target)
        {
            app->setTab(i);
            return;
        }
    }
}

void LvglLauncherApp::Impl::searchCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    app->closeRight();
    app->focusSearch();
}

void LvglLauncherApp::Impl::fullscreenCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    app->closeRight();
    app->toggleFullscreen();
}

void LvglLauncherApp::Impl::bannerPlayCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->launchSelected();
}

void LvglLauncherApp::Impl::bannerRescanCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    app->rescan();
    app->status = "已重新扫描游戏库";
    app->refreshAll();
}

void LvglLauncherApp::Impl::dotCb(lv_event_t* event)
{
    Dot* dot = static_cast<Dot*>(lv_event_get_user_data(event));
    if (!dot || !dot->app)
        return;
    Impl* app = dot->app;
    if (dot->index >= 0 && dot->index < static_cast<int>(app->recent.size()))
    {
        const int romIndex = app->recent[static_cast<std::size_t>(dot->index)];
        const std::string path = romIndex >= 0 ? app->roms[static_cast<std::size_t>(romIndex)].path
                                               : std::string();
        const std::uint32_t now = app->surface ? app->surface->ticks() : 0;
        const bool dbl = app->isDoubleClick(path, now);
        app->selectRomIndex(romIndex);
        app->startDoubleClick(path, now);
        if (dbl)
            app->launchSelected();
        else
            app->openRight();
    }
}

void LvglLauncherApp::Impl::shelfCb(lv_event_t* event)
{
    ShelfCard* card = static_cast<ShelfCard*>(lv_event_get_user_data(event));
    if (!card || !card->app)
        return;
    Impl* app = card->app;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_HOVER_OVER || code == LV_EVENT_HOVER_LEAVE)
    {
        card->hovered = (code == LV_EVENT_HOVER_OVER);
        app->refreshShelfHover(*card);
        return;
    }
    if (code != LV_EVENT_CLICKED)
        return;
    if (card->romIndex < 0 || card->romIndex >= static_cast<int>(app->roms.size()))
        return;

    const std::string& path = app->roms[static_cast<std::size_t>(card->romIndex)].path;
    const std::uint32_t now = app->surface ? app->surface->ticks() : 0;
    const bool dbl = app->isDoubleClick(path, now);
    app->selectRomIndex(card->romIndex);
    app->startDoubleClick(path, now);
    if (dbl)
        app->launchSelected();
    else
        app->openRight();
}

void LvglLauncherApp::Impl::gridCb(lv_event_t* event)
{
    GridCard* card = static_cast<GridCard*>(lv_event_get_user_data(event));
    if (!card || !card->app)
        return;
    Impl* app = card->app;

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_HOVER_OVER || code == LV_EVENT_HOVER_LEAVE)
    {
        card->hovered = (code == LV_EVENT_HOVER_OVER);
        app->refreshCardHover(*card);
        return;
    }
    if (code != LV_EVENT_CLICKED)
        return;
    if (card->position < 0 || card->position >= static_cast<int>(app->visible.size()))
        return;

    const int romIndex = app->visible[static_cast<std::size_t>(card->position)];
    if (romIndex < 0 || romIndex >= static_cast<int>(app->roms.size()))
        return;

    const std::string& path = app->roms[static_cast<std::size_t>(romIndex)].path;
    const std::uint32_t now = app->surface ? app->surface->ticks() : 0;
    const bool dbl = app->isDoubleClick(path, now);

    app->selected = card->position;
    app->ensureSelectionVisible();
    app->error.clear();
    app->startDoubleClick(path, now);
    app->refreshAll();
    if (dbl)
        app->launchSelected();
    else
        app->openRight();
}

void LvglLauncherApp::Impl::rightPlayCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->launchSelected();
}

void LvglLauncherApp::Impl::rightPillCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!app || !target)
        return;
    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        if (app->rightPills[i] == target)
        {
            app->setResolution(i);
            return;
        }
    }
}

void LvglLauncherApp::Impl::rightCatcherCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->closeRight();
}

void LvglLauncherApp::Impl::rightCloseCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->closeRight();
}

void LvglLauncherApp::Impl::rightFavCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->toggleFavorite();
}

void LvglLauncherApp::Impl::libFilterCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!app || !target)
        return;
    for (int i = 0; i < 2; ++i)
    {
        if (app->libFilterPills[i] == target)
        {
            app->setLibraryFilter(i);
            return;
        }
    }
}

void LvglLauncherApp::Impl::settingsNavCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!app || !target)
        return;
    for (int i = 0; i < 6; ++i)
    {
        if (app->settingsNav[i] == target)
        {
            app->setSettingsSection(i);
            return;
        }
    }
}

void LvglLauncherApp::Impl::settingsToggleCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* t = lv_event_get_target_obj(event);
    if (!app || !t)
        return;

    if (t == app->toggleRecursive)
    {
        app->config.recursiveScan = !app->config.recursiveScan;
        app->saveConfig();
        app->rescan();
        app->refreshAll();
    }
    else if (t == app->togglePatches)
    {
        app->config.detectPatches = !app->config.detectPatches;
        app->saveConfig();
        app->refreshSettings();
    }
    else if (t == app->toggleAspect)
    {
        app->config.keepAspect = !app->config.keepAspect;
        app->saveConfig();
        app->refreshSettings();
    }
    else if (t == app->toggleInteger)
    {
        app->config.integerScale = !app->config.integerScale;
        app->saveConfig();
        app->refreshSettings();
    }
    else if (t == app->toggleVsync)
    {
        app->config.vsync = !app->config.vsync;
        app->saveConfig();
        app->refreshSettings();
    }
    else if (t == app->toggleCovers)
    {
        app->config.showCovers = !app->config.showCovers;
        app->saveConfig();
        app->refreshAll();
    }
    else if (t == app->toggleStickDpad)
    {
        app->config.stickToDpad = !app->config.stickToDpad;
        app->saveConfig();
        app->refreshSettings();
    }
}

void LvglLauncherApp::Impl::settingsSegCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* t = lv_event_get_target_obj(event);
    if (!app || !t)
        return;

    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        if (app->resPills[i] == t)
        {
            app->setResolution(i);
            return;
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        if (app->filterPills[i] == t)
        {
            app->config.pixelFilter = i;
            app->saveConfig();
            app->refreshSettings();
            return;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (app->coverStylePills[i] == t)
        {
            app->config.coverStyle = i;
            app->saveConfig();
            app->refreshAll(); // affects card art
            return;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        if (app->playerPills[i] == t)
        {
            app->gpPlayer = i;
            std::memset(app->gpPrevBtn, 0, sizeof(app->gpPrevBtn));
            std::memset(app->gpPrevTrigger, 0, sizeof(app->gpPrevTrigger));
            app->refreshSettings();
            return;
        }
    }
}

void LvglLauncherApp::Impl::settingsRescanCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    app->rescan();
    app->status = "已重新扫描游戏库";
    app->refreshAll();
}

void LvglLauncherApp::Impl::settingsBrowseCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app || !app->surface)
        return;

    // Reopen where the dialog was left last time (a cancelled dialog still
    // updates this), falling back to the current library.
    const std::string startDir = app->config.lastPickerDir.empty()
                                     ? app->library
                                     : app->config.lastPickerDir;

    std::string path;
    std::string error;
    std::string lastDir;
    const bool picked = app->surface->pickFolder(startDir, path, error, &lastDir);

    std::fprintf(stderr, "Launcher: folder picker %s start='%s' path='%s' dir='%s'\n",
                 picked ? "ok" : "cancelled", startDir.c_str(), path.c_str(),
                 lastDir.c_str());

    if (!lastDir.empty() && lastDir != app->config.lastPickerDir)
        app->config.lastPickerDir = lastDir;

    if (!picked)
    {
        if (!lastDir.empty())
            app->saveConfig();
        // Cancelling is not an error; only report an unavailable picker.
        if (!error.empty())
        {
            app->status = "无法打开文件夹选择：" + error;
            app->refreshStatus();
        }
        return;
    }

    app->library            = path;
    app->config.libraryPath = path;
    if (!app->saveConfig())
        std::fprintf(stderr, "Launcher: could not save %s\n", app->configPath.c_str());
    app->rescan();
    app->status = app->error.empty() ? "游戏库已切换到 " + path
                                     : app->error;
    app->refreshAll();
}

void LvglLauncherApp::Impl::deadzoneSliderCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    const int v = lv_slider_get_value(app->sliderDeadzone);
    app->config.deadzone = v / 100.0f;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", app->config.deadzone);
    lv_label_set_text(app->deadzoneVal, buf);
    app->saveConfig();
}

void LvglLauncherApp::Impl::turboSliderCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    const int v = lv_slider_get_value(app->sliderTurbo);
    app->config.turbo = v;
    lv_label_set_text(app->turboVal, (std::to_string(v) + " / 帧").c_str());
    app->saveConfig();
}

void LvglLauncherApp::Impl::gpVibCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    if (app->surface && app->surface->padRumble(app->gpPlayer))
        app->status = "已发送震动测试";
    else
        app->status = "当前手柄不支持震动或未连接";
    app->refreshStatus();
}

void LvglLauncherApp::Impl::gpResetCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (!app)
        return;
    app->config.stickToDpad = true;
    app->config.deadzone    = 0.15f;
    app->config.turbo       = 12;
    app->saveConfig();
    app->refreshSettings();
    app->status = "已恢复默认手柄设置";
    app->refreshStatus();
}

void LvglLauncherApp::Impl::padPollCb(lv_timer_t* timer)
{
    Impl* app = static_cast<Impl*>(lv_timer_get_user_data(timer));
    if (app)
        app->pollGamepad();
}

void LvglLauncherApp::Impl::pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data)
{
    Impl* app = static_cast<Impl*>(lv_indev_get_driver_data(indev));
    if (!app)
    {
        data->point.x = 0;
        data->point.y = 0;
        data->state   = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (!app->pointerQueue.empty())
    {
        const Impl::PointerSample sample = app->pointerQueue.front();
        app->pointerQueue.pop_front();
        data->point.x = sample.x;
        data->point.y = sample.y;
        data->state   = sample.down ? LV_INDEV_STATE_PRESSED
                                    : LV_INDEV_STATE_RELEASED;
        // Tell LVGL to call us again so a press and release that arrived
        // between two frames are both delivered instead of collapsing into
        // "still released" (which loses taps on touchscreens).
        data->continue_reading = !app->pointerQueue.empty();
        return;
    }

    data->point.x = app->mouseX;
    data->point.y = app->mouseY;
    data->state   = app->mouseDown ? LV_INDEV_STATE_PRESSED
                                   : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

// ------------------------------------------------------- flush/screenshot ---

void LvglLauncherApp::Impl::flush(const lv_area_t* area, uint8_t* pxMap)
{
    if (!surface || !area || !pxMap)
        return;

    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    if (w <= 0 || h <= 0)
        return;

    const std::uint32_t stride = lv_draw_buf_width_to_stride(
        static_cast<std::uint32_t>(w), LV_COLOR_FORMAT_ARGB8888);
    surface->updateRect(area->x1, area->y1, w, h, pxMap,
                        static_cast<int>(stride));

    if (lv_display_flush_is_last(display))
        needsPresent = true;
}

void LvglLauncherApp::Impl::flushCb(lv_display_t* disp, const lv_area_t* area,
                                    uint8_t* pxMap)
{
    Impl* app = static_cast<Impl*>(lv_display_get_user_data(disp));
    if (app)
        app->flush(area, pxMap);
    lv_display_flush_ready(disp);
}

bool LvglLauncherApp::Impl::saveScreenshot(const std::string& path)
{
    if (!surface || !screen)
        return false;

    // Make sure the backend holds a complete current frame. readPixels()
    // composes the frame without presenting it, so this also works before
    // the first present().
    lv_obj_invalidate(screen);
    lv_refr_now(display);

    std::vector<std::uint32_t> argb;
    int width  = 0;
    int height = 0;
    if (!surface->readPixels(argb, width, height) || width <= 0 || height <= 0)
        return false;

    // Backend pixels are 0xAARRGGBB; stb_image_write wants RGBA bytes.
    std::vector<unsigned char> rgba(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < argb.size(); ++i)
    {
        const std::uint32_t pixel = argb[i];
        rgba[i * 4u + 0u] = static_cast<unsigned char>((pixel >> 16) & 0xFFu);
        rgba[i * 4u + 1u] = static_cast<unsigned char>((pixel >> 8) & 0xFFu);
        rgba[i * 4u + 2u] = static_cast<unsigned char>(pixel & 0xFFu);
        rgba[i * 4u + 3u] = static_cast<unsigned char>((pixel >> 24) & 0xFFu);
    }

    return stbi_write_png(path.c_str(), width, height, 4, rgba.data(),
                          width * 4) != 0;
}

// ============================================================ public API ===

LvglLauncherApp::LvglLauncherApp()
  : m_impl(std::make_unique<Impl>())
{
}

LvglLauncherApp::~LvglLauncherApp() = default;

bool LvglLauncherApp::init(const std::string& libraryOverride, long menuFrames,
                           display::Backend backend)
{
    return m_impl->init(libraryOverride, menuFrames, backend);
}

bool LvglLauncherApp::run(LaunchRequest& out)
{
    return m_impl->run(out);
}

void LvglLauncherApp::hideWindow()
{
    m_impl->hideWindow();
}

void LvglLauncherApp::showWindow()
{
    m_impl->showWindow();
}

void LvglLauncherApp::setError(const std::string& message)
{
    m_impl->setError(message);
}

void LvglLauncherApp::rescan()
{
    m_impl->rescan();
}
} // namespace launcher
