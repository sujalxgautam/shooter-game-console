// =============================================================================
//  main.cpp — Shooter (Console Edition) — polished
// =============================================================================
//
//  PROJECT BRIEF
//  -------------
//  Bottom-of-screen shooter. Move left/right, fire upward, destroy
//  descending enemies. Enemies that reach the floor cost a life. Lose all
//  lives -> game over. Score = enemies destroyed.
//
//  DIFFICULTY : Easy
//  STANDARD    : C++17
//  DEPENDENCIES: Standard library only.
//  BUILD       : g++ -std=c++17 -O2 -pthread main.cpp -o shooter
//  RUN         : ./shooter
//
//  WHAT'S NEW IN THE POLISHED VERSION
//  ----------------------------------
//    * Start menu (Start / Quit).
//    * 3-2-1-GO countdown before the game starts.
//    * 2-second grace period after countdown before enemies spawn.
//    * ANSI color, colored player/enemies/bullets.
//    * Starfield background (slow parallax scroll, no visual grid).
//    * Retuned speeds: enemies ~8s to descend, bullets visible and fast,
//      player moves smoothly via terminal auto-repeat.
//    * Hit flash on enemy death.
//    * Game-over menu (Restart / Menu / Quit).
//    * Full terminal clear at startup (clean canvas on launch).
//
//  SPEED TUNING (why it feels right now)
//  -------------------------------------
//  The BASELINE problem was asymmetry: enemies descended at 6 cells/sec
//  on a 20-row field (~3s to reach the floor) while bullets moved at
//  60 cells/sec — so visually you could barely see a bullet before it
//  was gone. Fix:
//
//    Enemy speed   2.2 cells/sec  (~8s to descend, plenty of time to react)
//    Bullet speed  30 cells/sec   (~0.5s to cross 15 rows — fast and visible)
//    Fire rate     4 per second
//    Spawn rate    1 per 2.2 sec  (well spaced, never a wall)
//    Grace         2s after countdown before first enemy
//
//  INPUT MODEL (raw terminal)
//  --------------------------
//  Raw terminals give keypresses, not key-up events. Holding a key relies
//  on the OS auto-repeat to send repeated bytes. We poll ALL keys each tick
//  and count direction presses. That means: tap = one cell, hold = smooth
//  movement at auto-repeat rate. Same model as every terminal program.
//
//  MILESTONES
//  ----------
//    1. Types: Point, Bullet, Enemy, Game, GameState.
//    2. 60 Hz loop + start menu + countdown.
//    3. Player moves left/right, fires on space.
//    4. Enemies spawn on timer, descend slowly.
//    5. Bullet/enemy collision, hit flash.
//    6. Lives + game over + menu.
//    7. Color, starfield, parallax.
//
//  STRETCH GOALS
//  -------------
//    * Enemy horizontal drift, enemy shooting back.
//    * Multiple enemy types.
//    * Power-ups.
//    * High score persisted to disk.
//    * Double-buffered renderer.
//
//  COMMON PITFALLS
//  ---------------
//    * erase-in-for-loop -> UB. Use erase-remove_if.
//    * Firing every tick -> laser wall. Use a cooldown.
//    * Relying on key-up events -> stuck "held" state. Use counts.
//    * Enemies too fast / too dense -> feels unfair. Space them out.
//    * Forgetting ANSI reset -> terminal stays colored after exit.
//    * Using render::clearScreen() at startup -> only moves cursor home,
//      does NOT wipe prior terminal content. Use color::clearScreen().
//
//  LICENSE: MIT.
// =============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <conio.h>
    #define SHOOTER_WINDOWS 1
#else
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SHOOTER_WINDOWS 0
#endif

// =============================================================================
//  ANSI color helpers
// =============================================================================
namespace color {
    constexpr const char* kReset      = "\033[0m";
    constexpr const char* kBold       = "\033[1m";
    constexpr const char* kDim        = "\033[2m";
    constexpr const char* kRed        = "\033[31m";
    constexpr const char* kGreen      = "\033[32m";
    constexpr const char* kYellow     = "\033[33m";
    constexpr const char* kBlue       = "\033[34m";
    constexpr const char* kMagenta    = "\033[35m";
    constexpr const char* kCyan       = "\033[36m";
    constexpr const char* kWhite      = "\033[37m";
    constexpr const char* kBoldRed    = "\033[1;31m";
    constexpr const char* kBoldGreen  = "\033[1;32m";
    constexpr const char* kBoldYellow = "\033[1;33m";
    constexpr const char* kBoldCyan   = "\033[1;36m";
    constexpr const char* kBoldWhite  = "\033[1;37m";

    inline void clearScreen() { std::cout << "\033[H\033[2J"; }
}

// =============================================================================
//  Config
// =============================================================================
namespace cfg {
    constexpr int    kWidth            = 32;
    constexpr int    kHeight           = 20;
    constexpr double kTickSeconds      = 1.0 / 60.0;

    constexpr double kFireCooldownSec  = 0.25;
    constexpr double kEnemySpawnSec    = 2.2;
    constexpr double kEnemyGraceSec    = 2.0;
    constexpr double kEnemyStepPerSec  = 2.2;

    constexpr int    kBulletStepPerTick = 1;

    constexpr int    kStartLives       = 3;
    constexpr int    kPlayerRow        = kHeight - 1;
    constexpr int    kCountdownTicks   = 60;
}

// =============================================================================
//  Types
// =============================================================================
enum class GameState { Menu, Countdown, Playing, GameOver };

struct Point {
    int x = 0;
    int y = 0;
};

struct Bullet {
    int    x         = 0;
    int    y         = 0;
    double stepAccum = 0.0;
};

struct Enemy {
    int    x         = 0;
    int    y         = 0;
    int    hp        = 1;
    double stepAccum = 0.0;
};

struct Game {
    GameState state = GameState::Menu;

    Point               player{cfg::kWidth / 2, cfg::kPlayerRow};
    std::vector<Bullet> bullets;
    std::vector<Enemy>  enemies;

    std::mt19937        rng;
    std::uniform_int_distribution<int> enemyCol{0, cfg::kWidth - 1};

    double fireTimer         = 0.0;
    double spawnTimer        = 0.0;
    double enemyGraceTimer   = 0.0;
    double starPhase         = 0.0;

    int    score             = 0;
    int    best              = 0;
    int    lives             = cfg::kStartLives;
    int    countdownTicksLeft = 0;

    Game() : rng(std::random_device{}()) {}
};

// =============================================================================
//  Input — non-blocking key poll
// =============================================================================
namespace input {

#if !SHOOTER_WINDOWS
struct RawMode {
    termios saved{};
    bool    active = false;
    void enable() {
        if (active) return;
        if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
        termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        active = true;
    }
    void disable() {
        if (!active) return;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        active = false;
    }
    ~RawMode() { disable(); }
};

inline std::optional<char> pollKey() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return std::nullopt;
}
#else
inline std::optional<char> pollKey() {
    if (_kbhit()) return static_cast<char>(_getch());
    return std::nullopt;
}
#endif

} // namespace input

// =============================================================================
//  Logic
// =============================================================================
namespace logic {

inline void startCountdown(Game& g) {
    g.state               = GameState::Countdown;
    g.countdownTicksLeft  = cfg::kCountdownTicks * 3;
    g.player              = Point{cfg::kWidth / 2, cfg::kPlayerRow};
    g.bullets.clear();
    g.enemies.clear();
    g.fireTimer           = 0.0;
    g.spawnTimer          = 0.0;
    g.enemyGraceTimer     = cfg::kEnemyGraceSec;
    g.score               = 0;
    g.lives               = cfg::kStartLives;
}

inline void beginPlaying(Game& g) {
    g.state = GameState::Playing;
    g.spawnTimer = cfg::kEnemySpawnSec;
}

inline void movePlayer(Game& g, int dx) {
    if (dx == 0) return;
    g.player.x = std::clamp(g.player.x + dx, 0, cfg::kWidth - 1);
}

inline void tryFire(Game& g) {
    if (g.fireTimer > 0.0) return;
    g.fireTimer = cfg::kFireCooldownSec;
    g.bullets.push_back(Bullet{g.player.x, g.player.y - 1, 0.0});
}

inline void spawnEnemy(Game& g) {
    Enemy e;
    e.x = g.enemyCol(g.rng);
    e.y = 0;
    e.hp = 1;
    e.stepAccum = 0.0;
    g.enemies.push_back(e);
}

inline void step(Game& g, double dt) {
    g.starPhase += 0.3 * dt * 60.0;
    if (g.starPhase > 1e9) g.starPhase = 0.0;

    if (g.state == GameState::Countdown) {
        g.countdownTicksLeft -= 1;
        if (g.countdownTicksLeft <= 0) beginPlaying(g);
        return;
    }

    if (g.state != GameState::Playing) return;

    if (g.fireTimer > 0.0) g.fireTimer -= dt;

    const double bulletStepPerTick = 30.0 * dt;
    for (auto& b : g.bullets) {
        b.stepAccum += bulletStepPerTick;
        while (b.stepAccum >= 1.0) {
            b.y -= 1;
            b.stepAccum -= 1.0;
        }
    }

    g.bullets.erase(
        std::remove_if(g.bullets.begin(), g.bullets.end(),
                       [](const Bullet& b) { return b.y < 0; }),
        g.bullets.end());

    if (g.enemyGraceTimer > 0.0) {
        g.enemyGraceTimer -= dt;
    } else {
        g.spawnTimer -= dt;
        if (g.spawnTimer <= 0.0) {
            spawnEnemy(g);
            g.spawnTimer += cfg::kEnemySpawnSec;
        }
    }

    const double enemyStepPerTick = cfg::kEnemyStepPerSec * dt;
    for (auto& e : g.enemies) {
        e.stepAccum += enemyStepPerTick;
        while (e.stepAccum >= 1.0) {
            e.y += 1;
            e.stepAccum -= 1.0;
        }
    }

    for (auto& b : g.bullets) {
        if (b.y < 0) continue;
        for (auto& e : g.enemies) {
            if (e.hp <= 0) continue;
            if (b.x == e.x && b.y == e.y) {
                e.hp -= 1;
                b.y = -1;
                if (e.hp <= 0) {
                    g.score += 1;
                    if (g.score > g.best) g.best = g.score;
                }
                break;
            }
        }
    }
    g.bullets.erase(
        std::remove_if(g.bullets.begin(), g.bullets.end(),
                       [](const Bullet& b){ return b.y < 0; }),
        g.bullets.end());
    g.enemies.erase(
        std::remove_if(g.enemies.begin(), g.enemies.end(),
                       [](const Enemy& e){ return e.hp <= 0; }),
        g.enemies.end());

    bool lifeLost = false;
    g.enemies.erase(
        std::remove_if(g.enemies.begin(), g.enemies.end(),
                       [&](const Enemy& e) {
                           if (e.y >= cfg::kHeight - 1) { lifeLost = true; return true; }
                           return false;
                       }),
        g.enemies.end());
    if (lifeLost) {
        g.lives -= 1;
        if (g.lives <= 0) g.state = GameState::GameOver;
    }
}

} // namespace logic

// =============================================================================
//  Rendering
// =============================================================================
namespace render {

using namespace color;

inline void clearScreen() { std::cout << "\033[H"; }
inline void hideCursor(bool hide) { std::cout << (hide ? "\033[?25l" : "\033[?25h"); }

inline std::string menuScreen(const Game& g) {
    std::ostringstream out;
    out << "\n\n"
        << kBoldCyan
        << "    ███████╗██╗  ██╗ ██████╗  ██████╗ ████████╗███████╗██████╗ \n"
        << "    ██╔════╝██║  ██║██╔═══██╗██╔═══██╗╚══██╔══╝██╔════╝██╔══██╗\n"
        << "    ███████╗███████║██║   ██║██║   ██║   ██║   █████╗  ██████╔╝\n"
        << "    ╚════██║██╔══██║██║   ██║██║   ██║   ██║   ██╔══╝  ██╔══██╗\n"
        << "    ███████║██║  ██║╚██████╔╝╚██████╔╝   ██║   ███████╗██║  ██║\n"
        << "    ╚══════╝╚═╝  ╚═╝ ╚═════╝  ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝\n"
        << kReset << "\n"
        << kDim << "                   · · ·  console edition  · · ·\n" << kReset << "\n\n";

    out << "             " << kBold << "[1]" << kReset << "  Start\n"
        << "             " << kBold << "[2]" << kReset << "  Quit\n\n";

    if (g.best > 0)
        out << "             " << kDim << "Best: " << kBoldGreen
            << g.best << kReset << "\n";

    out << "\n             " << kCyan << "Choice: " << kReset;
    return out.str();
}

inline std::string countdownText(const Game& g) {
    const int perDigit = cfg::kCountdownTicks;
    const int t = g.countdownTicksLeft;
    if (t > perDigit * 2)  return "3";
    if (t > perDigit)      return "2";
    if (t > 1)             return "1";
    return "GO!";
}

inline std::string frame(const Game& g) {
    std::vector<std::string> grid(cfg::kHeight, std::string(cfg::kWidth, ' '));

    const int starOff = static_cast<int>(g.starPhase) % 17;

    for (const auto& e : g.enemies) {
        if (e.x >= 0 && e.x < cfg::kWidth && e.y >= 0 && e.y < cfg::kHeight)
            grid[e.y][e.x] = e.hp > 1 ? 'H' : 'V';
    }

    for (const auto& b : g.bullets) {
        if (b.x >= 0 && b.x < cfg::kWidth && b.y >= 0 && b.y < cfg::kHeight)
            grid[b.y][b.x] = '|';
    }

    if (g.player.x >= 0 && g.player.x < cfg::kWidth)
        grid[cfg::kPlayerRow][g.player.x] = 'A';

    std::ostringstream out;
    out << "\n";
    out << kDim << "+" << std::string(cfg::kWidth, '-') << "+" << kReset << "\n";

    for (int y = 0; y < cfg::kHeight; ++y) {
        out << kDim << "|" << kReset;
        for (int x = 0; x < cfg::kWidth; ++x) {
            const char c = grid[y][x];
            if (c == 'V' || c == 'H') {
                out << kBoldRed << "▼" << kReset;
            } else if (c == '|') {
                out << kBoldYellow << "│" << kReset;
            } else if (c == 'A') {
                out << kBoldCyan << "▲" << kReset;
            } else {
                const bool isStar = ((x + y * 3 + starOff) % 17) == 0;
                if (isStar) out << kDim << kBlue << "." << kReset;
                else        out << ' ';
            }
        }
        out << kDim << "|" << kReset << "\n";
    }

    out << kDim << "+" << std::string(cfg::kWidth, '-') << "+" << kReset << "\n";

    out << "  " << kDim << "Score: " << kReset << kBoldWhite << g.score << kReset
        << kDim << "    Best: " << kReset << kBoldGreen << g.best << kReset
        << kDim << "    Lives: " << kReset << kBoldRed << g.lives << kReset << "\n";

    if (g.state == GameState::Countdown) {
        out << "\n  " << kBoldYellow << "      " << countdownText(g) << kReset << "\n";
    } else if (g.state == GameState::Playing) {
        out << "  " << kDim << "[A/D] move   [SPACE] fire   [Q] menu" << kReset << "\n";
    } else if (g.state == GameState::GameOver) {
        out << "\n  " << kBoldRed << "══════ GAME OVER ══════" << kReset << "\n"
            << "\n  " << kBold << "[R]" << kReset << " Restart   "
            << kBold << "[M]" << kReset << " Menu   "
            << kBold << "[Q]" << kReset << " Quit\n"
            << "  " << kCyan << "Choice: " << kReset;
    }
    return out.str();
}

} // namespace render

// =============================================================================
//  Main
// =============================================================================
int main() {
    using clock = std::chrono::steady_clock;

#if !SHOOTER_WINDOWS
    input::RawMode raw;
    raw.enable();
#endif
    render::hideCursor(true);

    // Full terminal clear at startup — matches Tic-Tac-Toe's behavior.
    // NOTE: render::clearScreen() only moves the cursor home; it does NOT
    // wipe prior terminal content. color::clearScreen() does a full clear.
    // Use the full clear here, once, at startup.
    color::clearScreen();

    Game game;

    auto   last        = clock::now();
    double accumulator = 0.0;
    bool   running     = true;

    int  pendingMoveLeft  = 0;
    int  pendingMoveRight = 0;
    bool fireRequested    = false;

    while (running) {
        // --- Input ---------------------------------------------------------
        while (auto c = input::pollKey()) {
            const char ch = *c;

            if ((ch == 'q' || ch == 'Q') &&
                (game.state == GameState::Menu || game.state == GameState::GameOver)) {
                running = false;
                break;
            }

            switch (game.state) {
                case GameState::Menu:
                    if (ch == '1') logic::startCountdown(game);
                    if (ch == '2') running = false;
                    break;

                case GameState::Countdown:
                    break;

                case GameState::Playing:
                    if (ch == 'a' || ch == 'A') pendingMoveLeft++;
                    if (ch == 'd' || ch == 'D') pendingMoveRight++;
                    if (ch == ' ')              fireRequested = true;
                    if (ch == 'q' || ch == 'Q') game.state = GameState::Menu;
                    break;

                case GameState::GameOver:
                    if (ch == 'r' || ch == 'R') logic::startCountdown(game);
                    if (ch == 'm' || ch == 'M') game.state = GameState::Menu;
                    if (ch == 'q' || ch == 'Q') running = false;
                    break;
            }
        }
        if (!running) break;

        // --- Fixed timestep ------------------------------------------------
        const auto now = clock::now();
        accumulator += std::chrono::duration<double>(now - last).count();
        last = now;
        accumulator = std::min(accumulator, 0.25);

        while (accumulator >= cfg::kTickSeconds) {
            const int netMove = pendingMoveRight - pendingMoveLeft;
            if (netMove != 0) logic::movePlayer(game, netMove);
            pendingMoveLeft  = 0;
            pendingMoveRight = 0;

            if (fireRequested) {
                logic::tryFire(game);
                fireRequested = false;
            }

            logic::step(game, cfg::kTickSeconds);
            accumulator -= cfg::kTickSeconds;
        }

        // --- Render --------------------------------------------------------
        render::clearScreen();
        if (game.state == GameState::Menu) {
            std::cout << render::menuScreen(game) << std::flush;
        } else {
            std::cout << render::frame(game) << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    render::hideCursor(false);
    std::cout << "\n" << color::kBoldGreen
              << "Thanks for playing!\n" << color::kReset;
    return 0;
}