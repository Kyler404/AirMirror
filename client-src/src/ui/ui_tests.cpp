// UI regression tests using the official Dear ImGui Test Engine.
//
// These run against the real renderer window and the real widgets (no mock
// UI): the engine injects clicks/keystrokes through ImGui's input queue and
// reads back item rectangles from ImGui's item hooks, so checks are numeric
// (sizes/positions) rather than screenshot comparisons.
//
// Run:  airplay-windows.exe --ui-tests --demo
// Out:  ui-tests-results.xml (JUnit) next to the executable; exit 0/1.

#include "ui/ui_tests.h"

#ifdef HAVE_UI_TESTS

#include "video/video_renderer.h"
#include "airplay/live_settings.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <string>

namespace ap::video::ui_tests {

namespace {

Environment g_env;
std::string g_original_name;

// Nav ids in the sidebar (order matches app_ui ViewState).
const char* kNavIds[] = {
    "##au_nav_overview", "##au_nav_screen", "##au_nav_music",
    "##au_nav_recording", "##au_nav_settings", "##au_nav_system",
};

void wait_for_demo(ImGuiTestContext* ctx) {
    for (int i = 0; i < 240; ++i) {
        VideoRenderer::DeviceInfo dev;
        if (g_env.renderer && g_env.renderer->active_device_snapshot(dev) &&
            dev.kind == "Mirror")
            return;
        ctx->Yield();
    }
}

void go_page(ImGuiTestContext* ctx, const char* nav_id) {
    ctx->SetRef("##au_side");
    ctx->ItemClick(nav_id);
    ctx->Yield(8);
}

ImGuiTestItemInfo item(ImGuiTestContext* ctx, const char* window, const char* path) {
    ctx->SetRef(window);
    return ctx->ItemInfo(path);
}

// Resolve a window, retrying a few frames: child windows can register a
// frame later than their parent.
ImGuiTestItemInfo window_info(ImGuiTestContext* ctx, const char* name) {
    for (int i = 0; i < 60; ++i) {
        auto w = ctx->WindowInfo(name, ImGuiTestOpFlags_NoError);
        if (w.ID != 0 && w.Window != nullptr && w.Window->ContentSize.x > 1.0f)
            return w;
        ctx->Yield();
    }
    return ctx->WindowInfo(name);
}

// The settings page hosts the name field; its scrollable body is a
// top-level detail window that may register a frame late.
ImGuiTestItemInfo settings_body(ImGuiTestContext* ctx) {
    go_page(ctx, "##au_nav_settings");
    return window_info(ctx, "//##au_detail");
}

// ---------------------------------------------------------------------------

void register_dpi_consistency(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "dpi_consistency");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        const float s = g_env.ui_scale;

        // Sidebar chrome scales with DPI.
        const ImGuiTestItemInfo side = ctx->WindowInfo("//##au_side");
        IM_CHECK(side.ID != 0);
        IM_CHECK_FLOAT_NEAR(side.RectFull.GetWidth(), 190.0f * s, 3.0f);

        // Top bar height scales with DPI.
        const ImGuiTestItemInfo top = ctx->WindowInfo("//##au_top");
        IM_CHECK(top.ID != 0);
        IM_CHECK_FLOAT_NEAR(top.RectFull.GetHeight(), 54.0f * s, 3.0f);

        // Nav rows share one height everywhere.
        const ImGuiTestItemInfo n0 = item(ctx, "##au_side", kNavIds[0]);
        const ImGuiTestItemInfo n2 = item(ctx, "##au_side", kNavIds[2]);
        IM_CHECK(n0.ID != 0);
        IM_CHECK(n2.ID != 0);
        IM_CHECK_FLOAT_NEAR(n0.RectFull.GetHeight(), 37.0f * s, 2.5f);
        IM_CHECK_FLOAT_NEAR(n2.RectFull.GetHeight(), 37.0f * s, 2.5f);
    };
}

void register_nav_pages(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "nav_pages");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        wait_for_demo(ctx);

        // All six nav rows exist, share a left edge and a height.
        ImGuiTestItemInfo rows[6];
        for (int i = 0; i < 6; ++i) {
            rows[i] = item(ctx, "##au_side", kNavIds[i]);
            IM_CHECK(rows[i].ID != 0);
            if (i > 0) {
                IM_CHECK_FLOAT_NEAR(rows[i].RectFull.Min.x, rows[0].RectFull.Min.x, 1.0f);
                IM_CHECK_FLOAT_NEAR(rows[i].RectFull.GetHeight(),
                                    rows[0].RectFull.GetHeight(), 1.0f);
            }
        }

        // Each page must switch without leaving stale windows behind.
        for (int i = 1; i < 6; ++i) {
            go_page(ctx, kNavIds[i]);
            const ImGuiTestItemInfo main = window_info(ctx, "//##au_main");
            IM_CHECK(main.ID != 0);
            IM_CHECK(main.Window != nullptr);
            IM_CHECK_LE(main.Window->ContentSize.x, main.Window->Size.x + 1.0f);
        }
        go_page(ctx, kNavIds[0]);
    };
}

void register_overview_stage(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "overview_stage");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        wait_for_demo(ctx);
        go_page(ctx, "##au_nav_overview");

        // Overview action buttons live in the main window.
        const ImGuiTestItemInfo full = item(ctx, "##au_main", "全屏##au_f11");
        const ImGuiTestItemInfo hide = item(ctx, "##au_main", "纯画面##au_pure");
        IM_CHECK(full.ID != 0);
        IM_CHECK(hide.ID != 0);
        IM_CHECK_LE(full.RectFull.Max.x, hide.RectFull.Min.x + 1.0f);

        // Right rail cards exist side by side with the main panel.
        const ImGuiTestItemInfo rail = ctx->WindowInfo("//##au_rail");
        IM_CHECK(rail.ID != 0);
        IM_CHECK(rail.Window != nullptr);
        IM_CHECK_FLOAT_NEAR(rail.RectFull.GetWidth(), 292.0f * g_env.ui_scale, 3.0f);
        IM_CHECK_GT(rail.RectFull.Min.x, ctx->WindowInfo("//##au_main").RectFull.Max.x - 1.0f);

        // Rail hosts the transport controls.
        IM_CHECK(item(ctx, "##au_rail", "##au_rplay").ID != 0);
        IM_CHECK(item(ctx, "##au_rail", "##au_rnext").ID != 0);
    };
}

void register_settings_controls(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "settings_controls");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ImGuiTestItemInfo body = settings_body(ctx);
        IM_CHECK(body.ID != 0);
        IM_CHECK(body.Window != nullptr);
        IM_CHECK_LE(body.Window->ContentSize.x, body.Window->Size.x + 1.0f);
        ctx->SetRef(body.Window);

        // Name field immediately available.
        IM_CHECK(ctx->ItemInfo("##au_name").ID != 0);

        // Live toggle round-trip on the GPU switch.
        if (g_env.live) {
            ctx->ScrollToItem("启用 GPU 硬件解码", ImGuiAxis_Y);
            const bool before = g_env.live->mirror_hwaccel.load();
            ctx->ItemClick("启用 GPU 硬件解码");
            ctx->Yield(3);
            IM_CHECK_EQ(g_env.live->mirror_hwaccel.load(), !before);
            ctx->ItemClick("启用 GPU 硬件解码");
            ctx->Yield(3);
            IM_CHECK_EQ(g_env.live->mirror_hwaccel.load(), before);
        }
    };
}

void register_responsive_home(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "responsive_home");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        wait_for_demo(ctx);
        go_page(ctx, "##au_nav_overview");

        // 800x600: smallest practical window; nothing may overflow sideways.
        g_env.renderer->test_resize_window(800, 600);
        ctx->Yield(10);
        ImGuiTestItemInfo content;
        content = window_info(ctx, "//##au_main");
        IM_CHECK_GT_NO_RET(content.ID, 0u);
        IM_CHECK_NE_NO_RET(content.Window, nullptr);
        IM_CHECK_LE_NO_RET(content.Window->ContentSize.x, content.Window->Size.x + 1.0f);

        // Every page survives the narrow window without sideways overflow.
        for (int i = 1; i < 6; ++i) {
            go_page(ctx, kNavIds[i]);
            const ImGuiTestItemInfo page = window_info(ctx, "//##au_main");
            IM_CHECK_LE_NO_RET(page.Window->ContentSize.x, page.Window->Size.x + 1.0f);
        }

        // Wide window: same invariant.
        g_env.renderer->test_resize_window(1400, 900);
        ctx->Yield(10);
        for (int i = 0; i < 6; ++i) {
            go_page(ctx, kNavIds[i]);
            const ImGuiTestItemInfo page = window_info(ctx, "//##au_main");
            IM_CHECK_LE_NO_RET(page.Window->ContentSize.x, page.Window->Size.x + 1.0f);
        }

        g_env.renderer->test_resize_window(1220, 800);
        ctx->Yield(10);
    };
}

void register_music_layout(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "music_layout");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        wait_for_demo(ctx);
        // Swap the fake device to an audio sender, verify, then restore.
        g_env.renderer->set_active_device({"192.168.1.100", "audio-test",
                                           "音频", "Demo iPhone", "iPhone18,2"});
        go_page(ctx, "##au_nav_music");

        const ImGuiTestItemInfo main = window_info(ctx, "//##au_main");
        if (main.ID == 0 || main.Window == nullptr) {
            g_env.renderer->set_active_device({"192.168.1.100", "demo-session",
                                               "Mirror", "Demo iPhone", "iPhone18,2"});
            IM_ERRORF("music main window missing");
            return;
        }
        IM_CHECK_LE_NO_RET(main.Window->ContentSize.x, main.Window->Size.x + 1.0f);

        // Narrow music page: content scrolls vertically, never sideways.
        g_env.renderer->test_resize_window(800, 600);
        ctx->Yield(8);
        const ImGuiTestItemInfo narrow = window_info(ctx, "//##au_main");
        IM_CHECK_GT_NO_RET(narrow.ID, 0u);
        IM_CHECK_NE_NO_RET(narrow.Window, nullptr);
        IM_CHECK_LE_NO_RET(narrow.Window->ContentSize.x, narrow.Window->Size.x + 1.0f);
        g_env.renderer->test_resize_window(1220, 800);
        ctx->Yield(8);

        g_env.renderer->set_active_device({"192.168.1.100", "demo-session",
                                           "Mirror", "Demo iPhone", "iPhone18,2"});
        go_page(ctx, "##au_nav_overview");
    };
}

void register_receiver_name_autosave(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "receiver_name_autosave");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ImGuiTestItemInfo body = settings_body(ctx);
        IM_CHECK(body.ID != 0);
        ctx->SetRef(body.Window);
        IM_CHECK(ctx->ItemInfo("##au_name").ID != 0);

        // Type a Chinese name and deactivate the field without pressing
        // the button: the hint promises "修改后自动保存", so deactivation
        // must apply it (IsItemDeactivatedAfterEdit path).
        ctx->ItemInput("##au_name");
        ctx->KeyCharsReplace("自动保存测试");
        ctx->Yield(2);
        // Nav click (sidebar ref!) deactivates the field: the page switch
        // is deferred to end-of-frame, so the click frame still submits
        // the settings page and IsItemDeactivatedAfterEdit fires.
        ctx->SetRef("##au_side");
        ctx->ItemClick("##au_nav_screen");
        ctx->Yield(3);
        IM_CHECK_STR_EQ_NO_RET(g_env.live->receiver_name().c_str(),
                               "自动保存测试");

        // Restore directly: simulated text input is fragile around spaces
        // and this process exits right after the suite anyway.
        g_env.live->set_receiver_name(g_original_name);
        IM_CHECK_STR_EQ(g_env.live->receiver_name().c_str(),
                        g_original_name.c_str());
    };
}

void register_receiver_name_length_guard(ImGuiTestEngine* e) {
    ImGuiTest* t = IM_REGISTER_TEST(e, "app_ui", "receiver_name_length_guard");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        std::string too_long;
        for (int i = 0; i < 20; ++i)
            too_long += "长";   // 20 * 3 = 60 UTF-8 bytes > 50 limit

        ImGuiTestItemInfo body = settings_body(ctx);
        IM_CHECK(body.ID != 0);
        ctx->SetRef(body.Window);
        ctx->ItemInput("##au_name");
        ctx->KeyCharsReplace(too_long.c_str());
        ctx->Yield(2);
        ctx->SetRef("##au_side");
        ctx->ItemClick("##au_nav_screen");   // deactivate without applying
        ctx->Yield(4);

        // Too long => normalize_receiver_name() returns empty => no change.
        IM_CHECK_STR_EQ(g_env.live->receiver_name().c_str(),
                        g_original_name.c_str());
        g_env.live->set_receiver_name(g_original_name);
    };
}

} // namespace

void register_tests(ImGuiTestEngine* engine, const Environment& env) {
    g_env = env;
    g_original_name = env.live ? env.live->receiver_name() : std::string();
    if (g_env.live && g_original_name.empty())
        g_original_name = "Windows 投屏";

    register_dpi_consistency(engine);
    register_nav_pages(engine);
    register_overview_stage(engine);
    register_settings_controls(engine);
    register_responsive_home(engine);
    register_music_layout(engine);
    register_receiver_name_autosave(engine);
    register_receiver_name_length_guard(engine);
}

} // namespace ap::video::ui_tests

#endif // HAVE_UI_TESTS
