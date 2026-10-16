#include <gui/gui.h>
#include <gui/style.h>
#include <EGL/egl.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

static void frame(float horizontal, float vertical, bool pressed) {
    auto& input = ImGui::GetIO();
    input.AddMousePosEvent(horizontal, vertical);
    input.AddMouseButtonEvent(ImGuiMouseButton_Left, pressed);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(40, 24));
    ImGui::SetNextWindowSize(ImVec2(input.DisplaySize.x - 40, input.DisplaySize.y - 24));
    ImGui::Begin("Waterfall resize test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    gui::waterfall.draw();
    ImGui::End();
    ImGui::Render();
}

static void drag(float offset, float distance, bool releaseOnly = false) {
    float horizontal = gui::waterfall.wfMax.x - 30 * style::uiScale;
    float vertical = gui::waterfall.wfMin.y + offset;
    frame(horizontal, vertical, false);
    frame(horizontal, vertical, true);
    if (!releaseOnly) { frame(horizontal, vertical + distance, true); }
    frame(horizontal, vertical + distance, false);
}

static void checkLayout() {
    auto& waterfall = gui::waterfall;
    require(std::abs(waterfall.wfMin.y - waterfall.fftAreaMin.y + 9 * style::uiScale - waterfall.getFFTHeight()) < 1.1f,
        "Visible divider does not match the saved FFT height");
    require(waterfall.fftAreaMax.y - waterfall.fftAreaMin.y >= 100 * style::uiScale,
        "Minimum FFT height ignores the UI scale or tuning controls");
    require(waterfall.wfMax.y - waterfall.wfMin.y >= 49 * style::uiScale,
        "Resizing left no room for the waterfall");
    require(waterfall.wfMax.y <= ImGui::GetIO().DisplaySize.y,
        "Waterfall extends below the window");
}

int main() {
    try {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "Cannot initialize EGL");
        EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
        EGLConfig config;
        EGLint count = 0;
        require(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1, "Cannot select EGL config");
        EGLint surfaceAttributes[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
        EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
        EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        require(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT && eglMakeCurrent(display, surface, surface, context),
            "Cannot create offscreen OpenGL context");

        ImGui::CreateContext();
        auto& input = ImGui::GetIO();
        input.IniFilename = nullptr;
        input.DeltaTime = 1.0f / 60;
        input.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int atlasWidth;
        int atlasHeight;
        input.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
        gui::mainWindow.lockWaterfallControls = false;
        auto& waterfall = gui::waterfall;
        waterfall.init();
        waterfall.setFullWaterfallUpdate(false);
        waterfall.setRawFFTSize(1024);
        waterfall.setBandwidth(1000000);
        waterfall.setCenterFrequency(100000000);
        ImGui::WaterfallVFO channel{};
        channel.minBandwidth = 10000;
        channel.maxBandwidth = 1000000;
        channel.snapInterval = 1000;
        channel.setBandwidth(200000);
        channel.setOffset(0);
        waterfall.vfos["Test channel"] = &channel;
        waterfall.selectedVFO = "Test channel";

        for (float scale : {2.0f, 1.0f, 1.5f, 3.0f}) {
            for (ImVec2 size : {ImVec2(2400, 1080), ImVec2(1080, 1920)}) {
                style::uiScale = scale;
                input.DisplaySize = size;
                frame(-100, -100, false);
                waterfall.setFFTHeight(150);
                frame(-100, -100, false);
                int original = waterfall.getFFTHeight();
                drag(0, 130);
                require(waterfall.getFFTHeight() == original + 130, "Dragging the visible divider did not resize the FFT");
                checkLayout();
                drag(0, -70);
                require(waterfall.getFFTHeight() == original + 60, "Upward divider drag failed");
                drag(7 * scale, 40);
                require(waterfall.getFFTHeight() == original + 100, "Lower touch hit target failed or divider jumped");
                drag(-7 * scale, 40);
                require(waterfall.getFFTHeight() == original + 140, "Upper touch hit target failed or divider jumped");
                drag(0, 35, true);
                require(waterfall.getFFTHeight() == original + 175, "Resize ignored the final release position");

                float horizontal = waterfall.wfMax.x - 30 * scale;
                float vertical = waterfall.wfMin.y;
                frame(horizontal, vertical, false);
                frame(horizontal, vertical, true);
                frame(horizontal, (waterfall.tuningAreaMin.y + waterfall.tuningAreaMax.y) / 2, true);
                frame(horizontal, vertical + 25, true);
                frame(horizontal, vertical + 25, false);
                require(waterfall.getFFTHeight() == original + 200, "Tuning controls interrupted divider capture");

                drag(0, size.y * 2);
                checkLayout();
                int maximum = waterfall.getFFTHeight();
                drag(0, 100);
                require(waterfall.getFFTHeight() == maximum, "Maximum resize height is unstable");
                drag(0, -size.y * 2);
                checkLayout();
                require(waterfall.getFFTHeight() == original, "Minimum resize height is inconsistent with loaded settings");
                drag(20 * scale, 30);
                require(waterfall.getFFTHeight() == original, "Dragging the waterfall body resized the FFT");
                require(waterfall.getCenterFrequency() == 100000000 && channel.generalOffset == 0 && channel.bandwidth == 200000,
                    "Vertical resize changed channel frequency or bandwidth");
                std::cout << "PASS: scale " << scale << ", " << size.x << "x" << size.y << '\n';
            }
        }

        style::uiScale = 2;
        input.DisplaySize = ImVec2(800, 340);
        frame(-100, -100, false);
        waterfall.setFFTHeight(150);
        drag(0, -1000);
        require(waterfall.getFFTHeight() > 0 && waterfall.wfMax.y > waterfall.wfMin.y, "Small window resize has invalid bounds");

        input.DisplaySize = ImVec2(2400, 1080);
        frame(-100, -100, false);
        gui::mainWindow.lockWaterfallControls = true;
        int locked = waterfall.getFFTHeight();
        drag(0, 100);
        require(waterfall.getFFTHeight() == locked, "Locked controls allowed a resize");
        gui::mainWindow.lockWaterfallControls = false;
        waterfall.hideWaterfall();
        frame(-100, -100, false);
        float horizontal = waterfall.fftAreaMax.x - 30;
        float vertical = waterfall.fftAreaMin.y - 9 * style::uiScale + waterfall.getFFTHeight();
        frame(horizontal, vertical, false);
        frame(horizontal, vertical, true);
        frame(horizontal, vertical + 80, true);
        frame(horizontal, vertical + 80, false);
        require(!waterfall.mouseInFFTResize && waterfall.getFFTHeight() == locked, "Hidden waterfall left an active resize hit target");
        waterfall.vfos.clear();
        waterfall.selectedVFO.clear();
        ImGui::DestroyContext();
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        std::cout << "PASS: waterfall divider resize\n";
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
