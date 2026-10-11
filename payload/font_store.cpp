#include "font_store.h"

#include "timer_logic.h"

#include "imgui.h"

#include <windows.h>

#include <array>
#include <mutex>
#include <set>

namespace font_store {
namespace {

constexpr std::array<float, kFontSizeCount> kSizes = {16.0f, 20.0f, 24.0f, 26.0f, 28.0f, 32.0f, 36.0f, 42.0f, 48.0f, 56.0f};
constexpr std::array<const char*, kFontSizeCount> kLabels = {"16 px", "20 px", "24 px", "26 px", "28 px", "32 px", "36 px", "42 px", "48 px", "56 px"};
constexpr int kD3D11MaxTextureDimension = 16384;
std::array<ImFont*, kFontSizeCount> g_fonts{};
ImFont* g_fallback = nullptr;
std::array<std::set<std::uint16_t>, kFontSizeCount> g_requested;
std::array<std::set<std::uint16_t>, kFontSizeCount> g_pending;
std::array<ImVector<ImWchar>, kFontSizeCount> g_ranges;
std::mutex g_mutex;
const char* g_fontPath = nullptr;

bool IsAtlasUsable(ImGuiIO& io, bool built) {
    return built && io.Fonts->TexWidth > 0 && io.Fonts->TexHeight > 0 &&
           io.Fonts->TexWidth <= kD3D11MaxTextureDimension && io.Fonts->TexHeight <= kD3D11MaxTextureDimension;
}

void FindFontPath() {
    constexpr const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf", "C:\\Windows\\Fonts\\simsun.ttc"
    };
    g_fontPath = nullptr;
    for (const char* candidate : candidates) {
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) { g_fontPath = candidate; break; }
    }
}

bool BuildAtlas(ImGuiIO& io, const std::array<std::set<std::uint16_t>, kFontSizeCount>& requested) {
    io.Fonts->Clear();
    g_fonts.fill(nullptr);
    g_fallback = nullptr;
    for (auto& range : g_ranges) range.clear();

    if (g_fontPath) {
        for (std::size_t i = 0; i < kSizes.size(); ++i) {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            builder.AddText(timer_logic::GetFixedUiText());
            for (std::uint16_t glyph : requested[i]) builder.AddChar(static_cast<ImWchar>(glyph));
            builder.BuildRanges(&g_ranges[i]);
            g_fonts[i] = io.Fonts->AddFontFromFileTTF(g_fontPath, kSizes[i], nullptr, g_ranges[i].Data);
        }
    }
    g_fallback = io.Fonts->AddFontDefault();
    io.FontDefault = g_fonts[IndexOf(20.0f)] ? g_fonts[IndexOf(20.0f)] : g_fallback;
    return IsAtlasUsable(io, io.Fonts->Build());
}

} // namespace

bool Initialize(ImGuiIO& io) {
    Clear();
    std::lock_guard<std::mutex> lock(g_mutex);
    FindFontPath();
    if (BuildAtlas(io, g_requested)) return true;
    g_fontPath = nullptr;
    return BuildAtlas(io, g_requested);
}

void RequestText(float fontSize, std::string_view utf8Text) {
    const int index = IndexOf(fontSize);
    const auto glyphs = timer_logic::ExtractSupportedGlyphs(utf8Text);
    std::lock_guard<std::mutex> lock(g_mutex);
    for (std::uint16_t glyph : glyphs) {
        if (g_requested[index].find(glyph) == g_requested[index].end()) g_pending[index].insert(glyph);
    }
}

bool HasPendingRequests() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& pending : g_pending) if (!pending.empty()) return true;
    return false;
}

bool RebuildIfNeeded(ImGuiIO& io) {
    std::lock_guard<std::mutex> lock(g_mutex);
    bool pending = false;
    for (const auto& glyphs : g_pending) pending |= !glyphs.empty();
    if (!pending) return true;

    const auto previous = g_requested;
    for (std::size_t i = 0; i < kFontSizeCount; ++i) {
        g_requested[i].insert(g_pending[i].begin(), g_pending[i].end());
        g_pending[i].clear();
    }
    if (BuildAtlas(io, g_requested)) return true;

    g_requested = previous;
    if (BuildAtlas(io, g_requested)) return false;
    g_fontPath = nullptr;
    BuildAtlas(io, g_requested);
    return false;
}

ImFont* Get(float fontSize) {
    const int index = IndexOf(fontSize);
    return g_fonts[index] ? g_fonts[index] : g_fallback;
}

void Shutdown() { Clear(); }

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fonts.fill(nullptr);
    g_fallback = nullptr;
    for (auto& glyphs : g_requested) glyphs.clear();
    for (auto& glyphs : g_pending) glyphs.clear();
    for (auto& range : g_ranges) range.clear();
    g_fontPath = nullptr;
}

const float* Sizes() { return kSizes.data(); }
const char* const* Labels() { return kLabels.data(); }

int IndexOf(float fontSize) {
    const float snapped = timer_logic::SnapFontSize(fontSize);
    for (std::size_t i = 0; i < kSizes.size(); ++i) if (kSizes[i] == snapped) return static_cast<int>(i);
    return 3;
}

} // namespace font_store
