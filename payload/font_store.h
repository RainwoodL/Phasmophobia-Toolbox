#pragma once

#include <cstddef>
#include <string_view>

struct ImFont;
struct ImGuiIO;

namespace font_store {

constexpr std::size_t kFontSizeCount = 10;

bool Initialize(ImGuiIO& io);
void RequestText(float fontSize, std::string_view utf8Text);
bool HasPendingRequests();
bool RebuildIfNeeded(ImGuiIO& io);
ImFont* Get(float fontSize);
void Shutdown();
void Clear();
const float* Sizes();
const char* const* Labels();
int IndexOf(float fontSize);

} // namespace font_store
