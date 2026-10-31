#pragma once

// UI automation tests built on the official Dear ImGui Test Engine.
// Only compiled into test builds (CMake -DBUILD_UI_TESTS=ON).

#ifdef HAVE_UI_TESTS

struct ImGuiTestEngine;

namespace ap::airplay { struct LiveSettings; }
namespace ap::video {

class VideoRenderer;

namespace ui_tests {

struct Environment {
    VideoRenderer* renderer = nullptr;
    ap::airplay::LiveSettings* live = nullptr;
    float ui_scale = 1.0f;
};

// Registers the UI test cases with the engine. Called on the render thread
// after the ImGui context exists, before the first test is queued.
void register_tests(ImGuiTestEngine* engine, const Environment& env);

} // namespace ui_tests
} // namespace ap::video

#endif // HAVE_UI_TESTS
