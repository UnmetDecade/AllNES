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
constexpr int kCoverH   = 241;
constexpr int kCardH    = 295;
constexpr int kCardGapX = 20;
constexpr int kCardGapY = 20;

constexpr int kBannerY = kMainY;
constexpr int kBannerH = 320;

constexpr int kShelfHeaderY = 460;
constexpr int kShelfY       = 492;
constexpr int kShelfH       = 142;
constexpr int kShelfCardW   = 352;  // 5 cards fill the 1824px content width
constexpr int kShelfGap     = 16;

constexpr int kHomeGridHeaderY = 656;
constexpr int kHomeGridY       = 692;
constexpr int kHomeGridH       = 320;
constexpr int kLibraryGridY    = 158;
constexpr int kLibraryGridH    = 854;

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
constexpr int kBannerPlayY    = 350;
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
constexpr int kLibraryVisibleRows = 2;

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

int visibleRows(int tab)
{
    return tab == 0 ? kHomeVisibleRows : kLibraryVisibleRows;
}

int drawRows(int tab)
{
    return tab == 0 ? 1 : 3;
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

bool loadFonts(Fonts& fonts, int scale)
{
    if (scale < 1)
        scale = 1;

    for (const std::string& path : fontCandidates())
    {
        lv_font_t* probe = makeFont(path, 15 * scale);
        if (!probe)
            continue;
        lv_freetype_font_delete(probe);

        fonts.f11 = makeFont(path, 11 * scale);
        fonts.f12 = makeFont(path, 12 * scale);
        fonts.f13 = makeFont(path, 13 * scale);
        fonts.f14 = makeFont(path, 14 * scale);
        fonts.f15 = makeFont(path, 15 * scale);
        fonts.f16 = makeFont(path, 16 * scale);
        fonts.f17 = makeFont(path, 17 * scale);
        fonts.f18 = makeFont(path, 18 * scale);
        fonts.f20 = makeFont(path, 20 * scale);
        fonts.f21 = makeFont(path, 21 * scale);
        fonts.f22 = makeFont(path, 22 * scale);
        fonts.f24 = makeFont(path, 24 * scale);
        fonts.f52 = makeFont(path, 52 * scale);
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
    int           renderScale = 1;
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
    // widget factory converts them to canvas pixels through this.
    int px(int value) const
    {
        return value == LV_SIZE_CONTENT ? value : value * renderScale;
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

    // Grids.
    lv_obj_t*  homeGrid       = nullptr;
    GridCard   homeCards[9];
    lv_obj_t*  homeEmptyMsg   = nullptr;
    lv_obj_t*  homeEmptyHint  = nullptr;
    lv_obj_t*  homeTrack      = nullptr;
    lv_obj_t*  homeThumb      = nullptr;

    lv_obj_t*  libraryGrid    = nullptr;
    GridCard   libraryCards[27];
    lv_obj_t*  libraryCount   = nullptr;
    lv_obj_t*  libraryEmptyMsg  = nullptr;
    lv_obj_t*  libraryEmptyHint = nullptr;
    lv_obj_t*  libraryTrack   = nullptr;
    lv_obj_t*  libraryThumb   = nullptr;

    // Right panel.
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
    // The details panel is a drawer: hidden by default and shown when a game
    // is clicked. Keyboard navigation updates it without opening it.
    bool      rightOpen      = false;

    // Settings.
    lv_obj_t* settingsPath   = nullptr;
    lv_obj_t* settingsPills[6] = {};
    lv_obj_t* settingsPillLbl[6] = {};

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
    void handleMouseWheel(int wheelY);

    void focusSearch();
    void unfocusSearch();
    void updateSearchStyle();
    void syncQueryFromTextarea();
    void setQuery(const std::string& text);

    void setTab(int newTab);
    void rebuildVisible();
    void rebuildRecent();
    void ensureSelectionVisible();
    void selectRelative(int delta);
    void selectRomIndex(int romIndex);
    void changeResolution(int delta);
    void setResolution(int index);
    void launchSelected();
    void saveConfig();

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
    void createArt(Art& art, lv_obj_t* parent, int x, int y, int w, int h);
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
    static void rightCloseCb(lv_event_t* event);
    static void settingsRescanCb(lv_event_t* event);
    static void settingsPillCb(lv_event_t* event);
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

    renderScale = std::max(1, surface->renderScale());
    surface->setTextInput(true);
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
    // the render scale so it covers the same layout area at 1x and 2x.
    drawBufSize = static_cast<std::size_t>(canvasWidth) *
                  static_cast<std::size_t>(120u * static_cast<unsigned>(renderScale)) * 4u;
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

void LvglLauncherApp::Impl::saveConfig()
{
    saveLauncherConfig(configPath, config);
}

void LvglLauncherApp::Impl::rebuildVisible()
{
    visible.clear();
    for (std::size_t i = 0; i < roms.size(); ++i)
    {
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
            if (roms[i].path == path)
            {
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
    roms      = scanLibrary(library, scanError);
    error     = scanError;
    scrollRow = 0;
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

    const int rows        = visibleRows(tab);
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
    closeRight();
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
    lv_obj_set_hidden(rightPanel, false);
    lv_obj_move_foreground(rightPanel);
}

void LvglLauncherApp::Impl::closeRight()
{
    if (!rightOpen)
        return;
    rightOpen = false;
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
        selectRelative(-kColumns * visibleRows(tab));
        break;
    case display::Key::PageDown:
        selectRelative(kColumns * visibleRows(tab));
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

void LvglLauncherApp::Impl::handleMouseWheel(int wheelY)
{
    if (tab == 2 || wheelY == 0)
        return;
    selectRelative(-wheelY * kColumns);
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
                                      int x, int y, int w, int h)
{
    art.box = makeBox(parent, x, y, w, h, 0, 0x222632, LV_OPA_COVER);
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
        createArt(card.art, card.mask, 0, 0, kCardW, kCoverH);

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

        createArt(card.art, card.root, 1, 1, kShelfCardW - 2, 96);

        lv_obj_t* fade = makeBox(card.root, 1, 53, kShelfCardW - 2, 44,
                                 0, 0x0C0D11, LV_OPA_TRANSP);
        lv_obj_set_style_bg_grad_color(fade, col(0x14161C), 0);
        lv_obj_set_style_bg_grad_opa(fade, 220, 0);
        lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);

        card.title = makeLabel(card.root, "", fonts.f14, kTextHex, 12, 102);
        lv_obj_set_width(card.title, px(kShelfCardW - 24));
        lv_label_set_long_mode(card.title, LV_LABEL_LONG_MODE_DOTS);

        card.meta = makeLabel(card.root, "", fonts.f12, kMutedHex, 12, 122);
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

    createTopBar();
    createHomePage();
    createLibraryPage();
    createSettingsPage();
    createStatusBar();
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

    lv_obj_t* lens = makeBox(searchBox, 17, 15, 14, 14, 7,
                             0x000000, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(lens, px(1), 0);
    lv_obj_set_style_border_color(lens, col(kMutedHex), 0);
    lv_obj_set_style_border_opa(lens, LV_OPA_COVER, 0);
    lv_obj_t* handle = makeBox(searchBox, 27, 28, 2, 7, 1, kMutedHex, LV_OPA_COVER);
    lv_obj_set_style_transform_rotation(handle, 450, 0);

    searchArea = lv_textarea_create(searchBox);
    lv_obj_set_pos(searchArea, px(42), px(10));
    lv_obj_set_size(searchArea, px(246), px(24));
    lv_textarea_set_one_line(searchArea, true);
    lv_textarea_set_max_length(searchArea, 64);
    lv_textarea_set_placeholder_text(searchArea, "搜索游戏库…");
    lv_textarea_set_cursor_pos(searchArea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_scrollbar_mode(searchArea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(searchArea, false);
    lv_obj_set_style_bg_opa(searchArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(searchArea, 0, 0);
    lv_obj_set_style_pad_all(searchArea, 0, 0);
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

    createArt(bannerArt, banner, 0, 0, kLeftW, kBannerH);
    // The art fallback needs a vertical gradient, the banner art a horizontal
    // dark-to-transparent gradient plus a bottom fade.
    lv_obj_set_style_bg_grad_color(bannerArt.box, col(0x101116), 0);
    lv_obj_set_style_bg_grad_dir(bannerArt.box, LV_GRAD_DIR_VER, 0);

    lv_obj_t* leftShade = makeBox(banner, 0, 0, kLeftW, kBannerH,
                                  0, 0x0C0D11, 242);
    lv_obj_set_style_bg_grad_color(leftShade, col(0x0C0D11), 0);
    lv_obj_set_style_bg_grad_opa(leftShade, 30, 0);
    lv_obj_set_style_bg_grad_dir(leftShade, LV_GRAD_DIR_HOR, 0);

    lv_obj_t* bottomShade = makeBox(banner, 0, kBannerH / 2, kLeftW, kBannerH / 2,
                                    0, 0x0C0D11, LV_OPA_TRANSP);
    lv_obj_set_style_bg_grad_color(bottomShade, col(0x0C0D11), 0);
    lv_obj_set_style_bg_grad_opa(bottomShade, 210, 0);
    lv_obj_set_style_bg_grad_dir(bottomShade, LV_GRAD_DIR_VER, 0);

    bannerTags = makeTagRow(banner, 36, 232 - kBannerY);
    for (Tag& tag : bannerTag)
        tag = makeTag(bannerTags);

    bannerTitle = makeLabel(banner, "", fonts.f52, kWhiteHex, 36, 258 - kBannerY);
    lv_obj_set_width(bannerTitle, px(kLeftW - 240));
    lv_label_set_long_mode(bannerTitle, LV_LABEL_LONG_MODE_DOTS);

    bannerMeta = makeLabel(banner, "", fonts.f15, kDimHex, 36, 322 - kBannerY);
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

    makeLabel(homePage, "游戏库", fonts.f21, kTextHex, kLeftX, kHomeGridHeaderY);
    homeCount = makeLabel(homePage, "", fonts.f12, kMutedHex,
                          kLeftX + kLeftW - 400, kHomeGridHeaderY + 8);
    lv_obj_set_width(homeCount, px(400));
    lv_obj_set_style_text_align(homeCount, LV_TEXT_ALIGN_RIGHT, 0);

    homeGrid = makeBox(homePage, kLeftX, kHomeGridY, kLeftW, kHomeGridH,
                       0, 0x000000, LV_OPA_TRANSP);
    createGridCards(homeGrid, homeCards, 9);

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
    makeLabel(libraryPage, "游戏库", fonts.f21, kTextHex, kLeftX, kMainY);

    libraryCount = makeLabel(libraryPage, "", fonts.f12, kMutedHex,
                             kLeftX + kLeftW - 420, kMainY + 8);
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
    lv_obj_t* panel = makeBox(settingsPage, kLeftX, kMainY, kLeftW, 876,
                              16, 0xFFFFFF, 7);
    lv_obj_set_style_border_width(panel, px(1), 0);
    lv_obj_set_style_border_color(panel, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(panel, 22, 0);

    makeLabel(panel, "设置", fonts.f24, kWhiteHex, 36, 28);

    makeLabel(panel, "游戏库", fonts.f17, kTextHex, 36, 82);
    lv_obj_t* pathBox = makeBox(panel, 36, 112, 1040, 42, 10, 0xFFFFFF, 10);
    lv_obj_set_style_border_width(pathBox, px(1), 0);
    lv_obj_set_style_border_color(pathBox, col(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(pathBox, 22, 0);
    settingsPath = makeLabel(pathBox, "", fonts.f14, kAccentHex, 16, 12);
    lv_obj_set_width(settingsPath, px(1008));
    lv_label_set_long_mode(settingsPath, LV_LABEL_LONG_MODE_DOTS);

    makeLabel(panel,
              "启动器直接扫描该目录下的 .nes 文件（不递归），库路径可用 --library 指定。",
              fonts.f12, kMutedHex, 36, 162);

    lv_obj_t* rescan = makeButton(panel,
                                  kSettingsRescanX - kLeftX,
                                  kSettingsRescanY - kMainY,
                                  kSettingsRescanW, kSettingsRescanH, 10,
                                  0xFFFFFF, 18, 0xFFFFFF, 70,
                                  "F5 重新扫描", fonts.f13, kTextHex);
    lv_obj_add_event_cb(rescan, &Impl::settingsRescanCb, LV_EVENT_CLICKED, this);

    makeBox(panel, 36, 200, kLeftW - 72, 1, 0, 0xFFFFFF, 22);

    makeLabel(panel, "窗口分辨率", fonts.f17, kTextHex, 36, 222);
    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        const int px = kSettingsX - kLeftX + i * (kSettingsPillW + kSettingsPillGap);
        const int py = kSettingsPillY - kMainY;
        settingsPills[i] = makePill(panel, px, py, kSettingsPillW, kSettingsPillH,
                                    10, resolutionPresets()[i].label, fonts.f13);
        settingsPillLbl[i] = lv_obj_get_child(settingsPills[i], 0);
        lv_obj_add_event_cb(settingsPills[i], &Impl::settingsPillCb,
                            LV_EVENT_CLICKED, this);
    }
    makeLabel(panel, "游戏窗口尺寸；游戏画面仍按整数倍缩放并保持宽高比。",
              fonts.f12, kMutedHex, 36, 304);

    makeBox(panel, 36, 338, kLeftW - 72, 1, 0, 0xFFFFFF, 22);

    makeLabel(panel, "快捷键", fonts.f17, kTextHex, 36, 360);
    struct KeyHint { const char* key; const char* action; };
    const KeyHint hints[] = {
        { "↑ ↓ ← →", "选择游戏" },
        { "Enter", "启动选中的游戏" },
        { "双击卡片", "直接启动" },
        { "/", "聚焦搜索框" },
        { "F5", "重新扫描游戏库" },
        { "[  ]", "切换窗口分辨率" },
        { "F11", "启动器全屏" },
        { "Esc", "清除搜索 / 返回 / 退出" },
    };
    for (int i = 0; i < static_cast<int>(sizeof(hints) / sizeof(hints[0])); ++i)
    {
        const int columnX = 36 + (i / 4) * 620;
        const int rowY    = 396 + (i % 4) * 30;
        makeLabel(panel, hints[i].key, fonts.f13, kAccentHex, columnX, rowY);
        makeLabel(panel, hints[i].action, fonts.f13, kDimHex, columnX + 120, rowY);
    }

    makeBox(panel, 36, 530, kLeftW - 72, 1, 0, 0xFFFFFF, 22);

    makeLabel(panel, "封面", fonts.f17, kTextHex, 36, 552);
    lv_obj_t* coverHint = makeLabel(panel,
        "把任意 3:4 的 PNG/JPEG 命名为「ROM 文件名.png」，放到游戏库的 images/ "
        "目录即可显示；没有封面的游戏会自动生成像素风占位图。",
        fonts.f13, kDimHex, 36, 584);
    lv_obj_set_width(coverHint, px(1180));
    lv_label_set_long_mode(coverHint, LV_LABEL_LONG_MODE_WRAP);

    makeLabel(panel, "AllNES 启动器 · LVGL + FreeType · Windows / macOS / Linux",
              fonts.f12, kFaintHex, 36, 720);
    makeLabel(panel, "ROM 库中的游戏会显示真实的 mapper、文件大小与运行状态。",
              fonts.f12, kFaintHex, 36, 742);
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

    createArt(rightArt, rightPanel, 1, 1, kRightW - 2, 250);
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
    lv_obj_set_style_bg_color(art.box, col(top), 0);
    lv_obj_set_style_bg_grad_color(art.box, col(bottom), 0);

    const std::string glyph = firstGlyph(title);
    const char* current = lv_label_get_text(art.initial);
    if (!current || glyph != current)
        lv_label_set_text(art.initial, glyph.c_str());

    if (rom && !rom->coverPath.empty())
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
    lv_obj_set_hidden(shelfEmpty, count != 0);

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
}

void LvglLauncherApp::Impl::refreshShelfHover(ShelfCard& card)
{
    lv_obj_set_style_bg_opa(card.root, card.hovered ? 16 : 8, 0);
    lv_obj_set_style_border_opa(card.root, card.hovered ? 70 : 22, 0);
}

void LvglLauncherApp::Impl::refreshGrid(bool home)
{
    GridCard* cards = home ? homeCards : libraryCards;
    const int slots = home ? 9 : 27;
    const int rows  = home ? 1 : 3;
    const int areaW = kLeftW;
    const int areaH = home ? kHomeGridH : kLibraryGridH;

    const int total = static_cast<int>(visible.size());
    const bool empty = total == 0;

    lv_obj_set_hidden(home ? homeEmptyMsg : libraryEmptyMsg, !empty);
    lv_obj_set_hidden(home ? homeEmptyHint : libraryEmptyHint, !empty);
    if (empty)
    {
        const std::string message = query.empty() ? "这个游戏库里还没有游戏"
                                                  : "没有找到匹配的游戏";
        const std::string hint = query.empty()
            ? "把 .nes 文件放进游戏库目录，然后按 F5 重新扫描"
            : "试试别的关键词，或按 Esc 清除搜索";
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
    const int fullRows = visibleRows(home ? 0 : 1);
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
            "按 Enter 或点击下方按钮启动游戏。游戏内按 Esc 返回启动器，F11 切换全屏，"
            "Ctrl+R 复位。继续游戏会记住最近启动的顺序。");
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
    lv_label_set_text(settingsPath, library.c_str());
    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        setPillActive(settingsPills[i], settingsPillLbl[i],
                      i == config.resolutionIndex);
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

void LvglLauncherApp::Impl::refreshAll()
{
    if (!screen)
        return;
    refreshPages();
    refreshHome();
    refreshGrid(false);
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
    if (app)
        app->focusSearch();
}

void LvglLauncherApp::Impl::fullscreenCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
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

void LvglLauncherApp::Impl::rightCloseCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    if (app)
        app->closeRight();
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

void LvglLauncherApp::Impl::settingsPillCb(lv_event_t* event)
{
    Impl* app = static_cast<Impl*>(lv_event_get_user_data(event));
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!app || !target)
        return;
    for (int i = 0; i < resolutionPresetCount(); ++i)
    {
        if (app->settingsPills[i] == target)
        {
            app->setResolution(i);
            return;
        }
    }
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
