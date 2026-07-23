// tests/test_platform_loop.cpp
//
// Unit tests for the Stage 7.1 platform abstraction + main loop.

#include "../engine/platform/platform.hpp"
#include "../engine/runtime/loop.hpp"

#include <atomic>
#include <cstdio>
#include <chrono>
#include <thread>

using namespace resf2::platform;
using namespace resf2::runtime;

static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",             \
                         __FILE__, __LINE__, #cond);                    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_tests;                                                      \
        if (!((a) == (b))) {                                            \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",      \
                         __FILE__, __LINE__, #a, #b);                   \
        }                                                               \
    } while (0)

static void test_null_platform_init() {
    NullPlatform p;
    WindowConfig cfg;
    cfg.title = "test";
    cfg.width = 640;
    cfg.height = 480;
    CHECK(p.init(cfg));
    CHECK_EQ(p.window_width(), 640);
    CHECK_EQ(p.window_height(), 480);
    p.shutdown();
}

static void test_null_platform_quit() {
    NullPlatform p;
    p.init({});
    CHECK(!p.should_quit());
    CHECK(p.poll_events());  // returns true when no quit requested
    p.inject_quit_request();
    CHECK(p.should_quit());
    CHECK(!p.poll_events());  // returns false when quit requested
    p.shutdown();
}

static void test_null_platform_input() {
    NullPlatform p;
    p.init({});

    // Poll first (clears any stale state), then inject
    p.poll_events();
    p.inject_key_down(Key::Escape);
    // Now check
    const auto& input = p.input();
    auto esc_idx = static_cast<std::size_t>(Key::Escape);
    CHECK(input.keys_down[esc_idx]);
    CHECK(input.keys_just_pressed[esc_idx]);

    // Poll again — just_pressed should be cleared, but keys_down stays
    p.poll_events();
    CHECK(input.keys_down[esc_idx]);
    CHECK(!input.keys_just_pressed[esc_idx]);

    // Release
    p.inject_key_up(Key::Escape);
    CHECK(!input.keys_down[esc_idx]);
    CHECK(input.keys_just_released[esc_idx]);

    p.shutdown();
}

static void test_null_platform_pointer() {
    NullPlatform p;
    p.init({});

    p.inject_pointer_down(0, 100.0f, 200.0f);
    const auto& input = p.input();
    bool found = false;
    for (const auto& ptr : input.pointers) {
        if (ptr.id == 0) {
            CHECK(ptr.pressed);
            CHECK(ptr.just_pressed);
            CHECK_EQ(ptr.x, 100.0f);
            CHECK_EQ(ptr.y, 200.0f);
            found = true;
            break;
        }
    }
    CHECK(found);

    p.inject_pointer_move(0, 150.0f, 250.0f);
    for (const auto& ptr : input.pointers) {
        if (ptr.id == 0) {
            CHECK_EQ(ptr.x, 150.0f);
            CHECK_EQ(ptr.y, 250.0f);
        }
    }

    p.inject_pointer_up(0);
    for (const auto& ptr : input.pointers) {
        if (ptr.id == 0) {
            CHECK(!ptr.pressed);
            CHECK(ptr.just_released);
        }
    }

    p.shutdown();
}

static void test_null_platform_pause_resume() {
    NullPlatform p;
    p.init({});

    std::atomic<int> pause_count{0};
    std::atomic<int> resume_count{0};

    p.set_pause_callback([&] { pause_count++; });
    p.set_resume_callback([&] { resume_count++; });

    CHECK(!p.is_paused());
    p.inject_pause();
    CHECK(p.is_paused());
    CHECK_EQ(pause_count.load(), 1);

    p.inject_resume();
    CHECK(!p.is_paused());
    CHECK_EQ(resume_count.load(), 1);

    p.shutdown();
}

static void test_null_platform_filesystem() {
    NullPlatform p;
    p.init({});

    // Write a file
    std::vector<std::byte> data = {std::byte{0x48}, std::byte{0x49}};
    auto tmp = std::string(std::string(p.save_dir() + "/resf2_test_file.bin"));
    CHECK(p.write_file(tmp, data));
    CHECK(p.file_exists(tmp));

    // Read it back
    auto read_back = p.read_file(tmp);
    CHECK_EQ(read_back.size(), 2u);
    CHECK_EQ(std::to_integer<int>(read_back[0]), 0x48);
    CHECK_EQ(std::to_integer<int>(read_back[1]), 0x49);

    // Cleanup
    std::remove(tmp.c_str());
    p.shutdown();
}

static void test_create_platform() {
    auto p = create_platform("null");
    CHECK(p != nullptr);
    CHECK(p->init({}));
    p->shutdown();
}

// ---------- Loop tests ----------

namespace {

// A minimal game that counts update/render calls and exits after N frames.
class CountingGame final : public IGame {
public:
    std::atomic<int> init_count{0};
    std::atomic<int> update_count{0};
    std::atomic<int> render_count{0};
    std::atomic<int> pause_count{0};
    std::atomic<int> resume_count{0};
    std::atomic<int> shutdown_count{0};
    int max_frames = 3;

    void on_init(Platform&) override { init_count++; }
    void on_update(Platform&, std::uint32_t) override {
        update_count++;
        if (update_count >= max_frames) {
            // Request exit via the platform (simulated)
            // We can't access the Loop directly, so we'll set a flag
            // and the test will call request_exit on the loop.
        }
    }
    void on_render(Platform&) override { render_count++; }
    void on_pause(Platform&) override { pause_count++; }
    void on_resume(Platform&) override { resume_count++; }
    void on_shutdown(Platform&) override { shutdown_count++; }
};

}  // namespace

static void test_loop_basic() {
    NullPlatform p;
    p.init({});
    Loop loop;
    CountingGame game;
    game.max_frames = 3;

    // Run loop in a thread; after 3 frames, request exit
    // (We can't easily inject quit from inside on_update without a
    // pointer to the loop. So we'll just inject quit from the test
    // thread after a brief sleep.)
    std::thread exit_thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        loop.request_exit();
    });

    int result = loop.run(p, game);
    exit_thread.join();

    CHECK_EQ(result, 0);
    CHECK_EQ(game.init_count.load(), 1);
    CHECK(game.update_count.load() >= 1);
    CHECK(game.render_count.load() >= 1);
    CHECK_EQ(game.shutdown_count.load(), 1);
    p.shutdown();
}

static void test_loop_quit_immediate() {
    NullPlatform p;
    p.init({});
    Loop loop;
    CountingGame game;

    // Request exit immediately
    loop.request_exit();

    int result = loop.run(p, game);
    CHECK_EQ(result, 0);
    CHECK_EQ(game.init_count.load(), 1);
    CHECK_EQ(game.update_count.load(), 0);
    CHECK_EQ(game.shutdown_count.load(), 1);
    p.shutdown();
}

static void test_loop_pause_resume() {
    NullPlatform p;
    p.init({});
    Loop loop;
    CountingGame game;

    // Run loop in a thread, control from main thread
    std::thread loop_thread([&] {
        loop.run(p, game);
    });

    // Inject pause, then resume, then quit.
    // Resume must fire well before the loop's 100ms pause-sleep ends,
    // otherwise the loop may re-enter sleep and hit the quit signal first.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.inject_pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // loop detects pause, starts 100ms sleep
    p.inject_resume();                                            // fires ~50ms before sleep ends
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // loop wakes, processes resume, runs
    p.inject_quit_request();

    loop_thread.join();

    CHECK(game.pause_count.load() >= 1);
    CHECK(game.resume_count.load() >= 1);
    p.shutdown();
}

int main() {
    test_null_platform_init();
    test_null_platform_quit();
    test_null_platform_input();
    test_null_platform_pointer();
    test_null_platform_pause_resume();
    test_null_platform_filesystem();
    test_create_platform();
    test_loop_basic();
    test_loop_quit_immediate();
    test_loop_pause_resume();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
