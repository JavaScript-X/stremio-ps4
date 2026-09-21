#include <cstdio>
#include <sstream>

#include <orbis/libkernel.h>

#include "graphics.h"
#include "log.h"

std::stringstream debugLogStream;

// OpenOrbis' shared Scene2D helper declares this destructor but v0.5.4 does
// not provide its definition. The application owns the scene for its entire
// process lifetime, so the default implementation is sufficient here.
Scene2D::~Scene2D() = default;

namespace {
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kPixelDepth = 4;
constexpr int kFrameBuffers = 2;
constexpr size_t kVideoMemory =
    static_cast<size_t>(kWidth) * kHeight * kPixelDepth * kFrameBuffers;

void drawHardwareProbe(Scene2D& scene) {
    const Color background = {18, 18, 24};
    const Color sidebar = {29, 29, 39};
    const Color stremioPurple = {123, 91, 214};
    const Color card = {45, 45, 58};
    const Color cardMuted = {35, 35, 46};

    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, 310, kHeight, sidebar);
    scene.DrawRectangle(48, 54, 214, 54, stremioPurple);
    scene.DrawRectangle(48, 170, 214, 18, card);
    scene.DrawRectangle(48, 224, 170, 18, cardMuted);
    scene.DrawRectangle(48, 278, 194, 18, cardMuted);

    scene.DrawRectangle(370, 76, 690, 48, stremioPurple);
    scene.DrawRectangle(370, 180, 310, 410, card);
    scene.DrawRectangle(718, 180, 310, 410, card);
    scene.DrawRectangle(1066, 180, 310, 410, card);
    scene.DrawRectangle(1414, 180, 310, 410, card);

    scene.DrawRectangle(370, 654, 1354, 28, cardMuted);
    scene.DrawRectangle(370, 720, 1030, 28, cardMuted);
    scene.DrawRectangle(370, 786, 1180, 28, cardMuted);
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DEBUGLOG << "Stremio PS4 M0 starting";

    Scene2D scene(kWidth, kHeight, kPixelDepth);
    if (!scene.Init(kVideoMemory, kFrameBuffers)) {
        DEBUGLOG << "Video initialization failed";
        for (;;) {
            sceKernelUsleep(1000000);
        }
    }

    DEBUGLOG << "Video initialized at 1920x1080; rendering hardware probe";
    int frameId = 1;
    scene.SetActiveFrameBuffer(0);

    for (;;) {
        drawHardwareProbe(scene);
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
    }
}
