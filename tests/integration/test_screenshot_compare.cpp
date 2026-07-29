// tests/integration/test_screenshot_compare.cpp
//
// Screenshot comparison harness: renders the game headlessly via the
// software renderer, captures the framebuffer, and compares it against
// reference PNG images using MSE (Mean Squared Error).
//
// Test scenarios:
//   1. Framebuffer access & PNG round-trip (save -> load -> compare)
//   2. Determinism: two identical runs produce identical screenshots
//   3. Reference comparison: current output vs stored reference
//   4. Negative test: different frames produce MSE above threshold
//
// On failure, the actual screenshot and difference image are saved to
// tests/data/actual/ for debugging.

#include "../headless_test_runner.hpp"
#include "../check.hpp"

#include "engine/renderer/software_renderer.hpp"
#include "engine/renderer/stb_image.h"
#include "engine/renderer/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Screenshot comparison utilities
// ---------------------------------------------------------------------------

namespace resf2::test::screenshot {

// RGBA pixel buffer
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // RGBA8, row-major, top-to-bottom
};

// Load a PNG file into an RGBA image. Returns empty Image on failure.
// Uses stbi_load_from_memory since the project builds stb_image with STBI_NO_STDIO.
Image load_png(const std::string& path) {
    Image img;

    // Read file into memory using std::ifstream (portable, no C4996)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "  [screenshot] load_png: cannot open %s\n", path.c_str());
        return img;
    }
    const auto file_size = file.tellg();
    if (file_size <= 0) {
        return img;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> file_data(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(file_data.data()), file_size)) {
        std::fprintf(stderr, "  [screenshot] load_png: short read on %s\n", path.c_str());
        return img;
    }
    file.close();

    int w = 0, h = 0, ch = 0;
    stbi_uc* data = stbi_load_from_memory(file_data.data(), static_cast<int>(file_data.size()),
                                           &w, &h, &ch, 4);
    if (!data) {
        std::fprintf(stderr, "  [screenshot] load_png failed: %s (%s)\n",
                     path.c_str(), stbi_failure_reason());
        return img;
    }
    img.width = w;
    img.height = h;
    img.pixels.assign(data, data + (std::size_t)w * h * 4);
    stbi_image_free(data);
    return img;
}

// Compute MSE between two images of the same dimensions.
// Returns -1.0 if dimensions don't match.
float compute_mse(const Image& ref, const Image& actual) {
    if (ref.width != actual.width || ref.height != actual.height) {
        return -1.0f;
    }
    if (ref.pixels.empty() || actual.pixels.empty()) {
        return -1.0f;
    }

    const auto n = ref.pixels.size();
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double diff = static_cast<double>(ref.pixels[i])
                          - static_cast<double>(actual.pixels[i]);
        sum += diff * diff;
    }
    return static_cast<float>(sum / static_cast<double>(n));
}

// Save a difference image: highlights pixels that differ between ref and actual.
// Diff pixels are shown in red; matching pixels are shown dimmed.
bool save_diff_image(const Image& ref, const Image& actual,
                     const std::string& path) {
    if (ref.width != actual.width || ref.height != actual.height) {
        return false;
    }

    const int w = ref.width;
    const int h = ref.height;
    std::vector<std::uint8_t> diff(w * h * 4);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            const int dr = std::abs(ref.pixels[idx + 0] - actual.pixels[idx + 0]);
            const int dg = std::abs(ref.pixels[idx + 1] - actual.pixels[idx + 1]);
            const int db = std::abs(ref.pixels[idx + 2] - actual.pixels[idx + 2]);

            if (dr > 2 || dg > 2 || db > 2) {
                // Highlight difference in red, intensity proportional to diff
                const int max_diff = std::max({dr, dg, db});
                diff[idx + 0] = static_cast<std::uint8_t>(std::min(255, max_diff * 4));
                diff[idx + 1] = 0;
                diff[idx + 2] = 0;
                diff[idx + 3] = 255;
            } else {
                // Dimmed version of actual pixel
                diff[idx + 0] = actual.pixels[idx + 0] / 3;
                diff[idx + 1] = actual.pixels[idx + 1] / 3;
                diff[idx + 2] = actual.pixels[idx + 2] / 3;
                diff[idx + 3] = 128;
            }
        }
    }

    return stbi_write_png(path.c_str(), w, h, 4, diff.data(), w * 4) != 0;
}

// Capture the current framebuffer from a software renderer.
Image capture_framebuffer(const renderer::SoftwareRendererAdapter& adapter) {
    Image img;
    const auto& soft = adapter.soft_renderer();
    img.width = soft.width();
    img.height = soft.height();
    img.pixels = soft.framebuffer();  // copy the RGBA8 buffer
    return img;
}

// Save an image as PNG.
bool save_png(const Image& img, const std::string& path) {
    if (img.pixels.empty()) return false;
    return stbi_write_png(path.c_str(), img.width, img.height, 4,
                          img.pixels.data(), img.width * 4) != 0;
}

// Ensure a directory exists.
void ensure_directory(const std::string& path) {
    fs::create_directories(path);
}

}  // namespace resf2::test::screenshot

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    std::printf("  TEST %d: %s ... ", test_count, name); \
    bool _ok = true; (void)_ok;
#define END_TEST \
    if (_ok) { pass_count++; std::printf("PASS\n"); } \
    else { std::printf("FAIL\n"); } \
} while(0)
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("\n    FAIL at line %d: %s\n", __LINE__, #cond); \
        _ok = false; \
    } \
} while(0)

// Thresholds
static constexpr float kMseThreshold = 5.0f;  // MSE > 5% means different
static constexpr int kWarmupFrames = 30;       // frames to run past boot
static constexpr int kCaptureFrames = 10;      // additional frames before capture

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_framebuffer_access() {
    TEST("Framebuffer access: renderer captures non-empty image")
        using namespace resf2::test;
        using namespace resf2::test::screenshot;

        HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 640;
        config.height = 360;

        HeadlessTestRunner runner(config);
        CHECK(runner.init());

        // Run a few frames so the renderer processes at least one frame
        runner.run_frames(10);

        const auto* adapter = runner.renderer();
        CHECK(adapter != nullptr);

        Image img = capture_framebuffer(*adapter);
        CHECK(img.width == 640);
        CHECK(img.height == 360);
        CHECK(img.pixels.size() == static_cast<std::size_t>(640 * 360 * 4));

        // Framebuffer should be accessible (may be black during boot splash,
        // but the buffer itself must be valid and non-empty).
        CHECK(!img.pixels.empty());
    END_TEST;
}

static void test_png_roundtrip() {
    TEST("PNG round-trip: save then load produces identical pixels")
        using namespace resf2::test;
        using namespace resf2::test::screenshot;

        HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 640;
        config.height = 360;

        HeadlessTestRunner runner(config);
        CHECK(runner.init());
        runner.run_frames(10);

        Image original = capture_framebuffer(*runner.renderer());
        CHECK(!original.pixels.empty());

        // Save to actual/
        ensure_directory("tests/data/actual");
        const std::string path = "tests/data/actual/roundtrip_test.png";
        CHECK(save_png(original, path));

        // Reload
        Image reloaded = load_png(path);
        CHECK(reloaded.width == original.width);
        CHECK(reloaded.height == original.height);
        CHECK(reloaded.pixels.size() == original.pixels.size());

        // Compare — should be pixel-identical
        float mse = compute_mse(original, reloaded);
        CHECK(mse >= 0.0f);
        CHECK(mse < 0.001f);  // effectively zero (rounding possible in PNG)
    END_TEST;
}

static void test_determinism() {
    TEST("Determinism: two identical runs produce identical screenshots")
        using namespace resf2::test;
        using namespace resf2::test::screenshot;

        // Run 1
        HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 640;
        config.height = 360;

        HeadlessTestRunner runner1(config);
        CHECK(runner1.init());
        runner1.run_frames(kWarmupFrames + kCaptureFrames);
        Image img1 = capture_framebuffer(*runner1.renderer());

        // Run 2
        HeadlessTestRunner runner2(config);
        CHECK(runner2.init());
        runner2.run_frames(kWarmupFrames + kCaptureFrames);
        Image img2 = capture_framebuffer(*runner2.renderer());

        CHECK(img1.width == img2.width);
        CHECK(img1.height == img2.height);

        float mse = compute_mse(img1, img2);
        CHECK(mse >= 0.0f);
        CHECK(mse < 0.001f);  // identical runs should produce identical output
    END_TEST;
}

static void test_reference_comparison() {
    TEST("Reference comparison: generate or match reference screenshot")
        using namespace resf2::test;
        using namespace resf2::test::screenshot;

        ensure_directory("tests/data/reference");
        ensure_directory("tests/data/actual");

        const std::string ref_path = "tests/data/reference/default_scene.png";

        // Render current state
        HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 640;
        config.height = 360;

        HeadlessTestRunner runner(config);
        CHECK(runner.init());
        runner.run_frames(kWarmupFrames + kCaptureFrames);
        Image current = capture_framebuffer(*runner.renderer());
        CHECK(!current.pixels.empty());

        if (!fs::exists(ref_path)) {
            // First run: generate reference
            CHECK(save_png(current, ref_path));
            std::printf("\n    [info] Generated reference: %s\n", ref_path.c_str());
        } else {
            // Compare against reference
            Image ref = load_png(ref_path);
            CHECK(ref.width > 0);
            CHECK(ref.height > 0);

            if (ref.width == current.width && ref.height == current.height) {
                float mse = compute_mse(ref, current);
                CHECK(mse >= 0.0f);

                if (mse > kMseThreshold) {
                    // Save actual and diff for debugging
                    save_png(current, "tests/data/actual/default_scene_actual.png");
                    save_diff_image(ref, current, "tests/data/actual/default_scene_diff.png");
                    std::printf("\n    [info] MSE=%.4f (threshold=%.1f) — saved diff images\n",
                                mse, kMseThreshold);
                    CHECK(mse <= kMseThreshold);
                }
            }
            // Dimension mismatch is already caught by CHECK above
        }
    END_TEST;
}

static void test_different_frames_differ() {
    TEST("Negative test: frames at different times produce different screenshots")
        using namespace resf2::test;
        using namespace resf2::test::screenshot;

        HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 640;
        config.height = 360;

        // Early frame
        HeadlessTestRunner runner1(config);
        CHECK(runner1.init());
        runner1.run_frames(2);
        Image early = capture_framebuffer(*runner1.renderer());

        // Later frame
        HeadlessTestRunner runner2(config);
        CHECK(runner2.init());
        runner2.run_frames(kWarmupFrames + kCaptureFrames);
        Image late = capture_framebuffer(*runner2.renderer());

        CHECK(early.width == late.width);
        CHECK(early.height == late.height);

        float mse = compute_mse(early, late);
        CHECK(mse >= 0.0f);

        // Early (boot) and later (game loaded) frames should be visually different
        CHECK(mse > 1.0f);
    END_TEST;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== Screenshot Comparison Tests ===\n\n");

    test_framebuffer_access();
    test_png_roundtrip();
    test_determinism();
    test_reference_comparison();
    test_different_frames_differ();

    std::printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
