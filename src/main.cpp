#include <emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <vector>

using namespace std;

// ============================================================
// CONFIG
// ============================================================

static constexpr int MAP_W = 48;
static constexpr int MAP_H = 34;
static constexpr int TILE  = 24;

static constexpr int START_ENERGY = 100;
static constexpr int MAX_BUGS = 8;

// ============================================================
// DATA TYPES
// ============================================================

struct Tile {
    int type;
    int revealed;
};

struct Player {
    int x;
    int y;
    int energy;
    int score;
};

struct Bug {
    double x;
    double y;
    double speed;
};

// Tile types
// 0 = dirt
// 1 = rock
// 2 = copper
// 3 = crystal
// 4 = energy

static vector<Tile> world;
static vector<Bug> bugs;

static Player player{MAP_W / 2, MAP_H / 2, START_ENERGY, 0};

static int bestScore = 0;
static bool gameOver = false;

static double lastMoveTime = 0.0;
static double lastBugSpawn = 0.0;

static mt19937 rng(
    static_cast<unsigned int>(time(nullptr))
);

// ============================================================
// BASIC RANDOM HELPERS
// ============================================================

static int randomInt(int minValue, int maxValue) {
    uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
}

static double randomDouble(double minValue, double maxValue) {
    uniform_real_distribution<double> dist(minValue, maxValue);
    return dist(rng);
}

// ============================================================
// JAVASCRIPT BRIDGE
// ============================================================

EM_JS(void, js_setup, (), {
    window.pm = window.pm || {};

    const canvas =
        document.getElementById("gameCanvas") ||
        document.querySelector("canvas");

    if (!canvas) {
        console.error("Pixel Miner: canvas not found.");
        return;
    }

    window.pm.canvas = canvas;
    window.pm.ctx = canvas.getContext("2d");

    window.pm.keys = {};
    window.pm.mouseDown = false;
    window.pm.mouseX = 0;
    window.pm.mouseY = 0;

    window.pm.canvas.addEventListener("keydown", function(e) {
        window.pm.keys[e.key] = true;

        if (
            e.key === "ArrowUp" ||
            e.key === "ArrowDown" ||
            e.key === "ArrowLeft" ||
            e.key === "ArrowRight" ||
            e.key === " "
        ) {
            e.preventDefault();
        }
    });

    window.pm.canvas.addEventListener("keyup", function(e) {
        window.pm.keys[e.key] = false;
    });

    window.pm.canvas.addEventListener("mousedown", function(e) {
        window.pm.mouseDown = true;

        const rect = window.pm.canvas.getBoundingClientRect();

        window.pm.mouseX =
            (e.clientX - rect.left) *
            (window.pm.canvas.width / rect.width);

        window.pm.mouseY =
            (e.clientY - rect.top) *
            (window.pm.canvas.height / rect.height);
    });

    window.pm.canvas.addEventListener("mousemove", function(e) {
        const rect = window.pm.canvas.getBoundingClientRect();

        window.pm.mouseX =
            (e.clientX - rect.left) *
            (window.pm.canvas.width / rect.width);

        window.pm.mouseY =
            (e.clientY - rect.top) *
            (window.pm.canvas.height / rect.height);
    });

    window.pm.canvas.addEventListener("mouseup", function() {
        window.pm.mouseDown = false;
    });

    window.pm.canvas.addEventListener("mouseleave", function() {
        window.pm.mouseDown = false;
    });

    window.pm.canvas.addEventListener(
        "touchstart",
        function(e) {
            const touch = e.touches[0];

            if (!touch) return;

            const rect =
                window.pm.canvas.getBoundingClientRect();

            window.pm.mouseX =
                (touch.clientX - rect.left) *
                (window.pm.canvas.width / rect.width);

            window.pm.mouseY =
                (touch.clientY - rect.top) *
                (window.pm.canvas.height / rect.height);

            window.pm.mouseDown = true;

            e.preventDefault();
        },
        { passive: false }
    );

    window.pm.canvas.addEventListener(
        "touchend",
        function(e) {
            window.pm.mouseDown = false;
            e.preventDefault();
        },
        { passive: false }
    );

    window.pm.canvas.addEventListener(
        "touchmove",
        function(e) {
            const touch = e.touches[0];

            if (!touch) return;

            const rect =
                window.pm.canvas.getBoundingClientRect();

            window.pm.mouseX =
                (touch.clientX - rect.left) *
                (window.pm.canvas.width / rect.width);

            window.pm.mouseY =
                (touch.clientY - rect.top) *
                (window.pm.canvas.height / rect.height);

            e.preventDefault();
        },
        { passive: false }
    );

    window.pm.canvas.setAttribute("tabindex", "0");
    window.pm.canvas.focus();
});

EM_JS(int, js_key, (int key), {
    if (!window.pm || !window.pm.keys) return 0;

    if (key === 0) return window.pm.keys["ArrowUp"] ? 1 : 0;
    if (key === 1) return window.pm.keys["ArrowDown"] ? 1 : 0;
    if (key === 2) return window.pm.keys["ArrowLeft"] ? 1 : 0;
    if (key === 3) return window.pm.keys["ArrowRight"] ? 1 : 0;
    if (key === 4) return window.pm.keys["w"] ? 1 : 0;
    if (key === 5) return window.pm.keys["s"] ? 1 : 0;
    if (key === 6) return window.pm.keys["a"] ? 1 : 0;
    if (key === 7) return window.pm.keys["d"] ? 1 : 0;

    return 0;
});

EM_JS(int, js_mouse_down, (), {
    return window.pm && window.pm.mouseDown ? 1 : 0;
});

EM_JS(int, js_mouse_x, (), {
    return window.pm ? window.pm.mouseX : 0;
});

EM_JS(int, js_mouse_y, (), {
    return window.pm ? window.pm.mouseY : 0;
});

EM_JS(void, js_set_score, (int score, int energy, int best), {
    const scoreEl = document.getElementById("score");
    const energyEl = document.getElementById("energy");
    const bestEl = document.getElementById("bestScore");

    if (scoreEl) {
        scoreEl.textContent = String(score);
    }

    if (energyEl) {
        energyEl.textContent = String(Math.max(0, energy));
    }

    if (bestEl) {
        bestEl.textContent = String(best);
    }
});

EM_JS(void, js_game_state, (int state), {
    const status =
        document.getElementById("gameStatus") ||
        document.getElementById("status");

    if (!status) return;

    if (state === 0) {
        status.textContent = "MINING";
    } else {
        status.textContent = "GAME OVER";
    }
});

EM_JS(void, js_save_best, (int score), {
    try {
        localStorage.setItem(
            "pixel_miner_best",
            String(score)
        );
    } catch (e) {
        console.warn("Could not save best score.");
    }
});

EM_JS(int, js_load_best, (), {
    try {
        const value =
            localStorage.getItem("pixel_miner_best");

        if (!value) return 0;

        const number = parseInt(value, 10);

        if (Number.isNaN(number)) return 0;

        return number;
    } catch (e) {
        return 0;
    }
});

EM_JS(void, js_message, (const char* message), {
    const text = UTF8ToString(message);

    const messageEl =
        document.getElementById("message");

    if (messageEl) {
        messageEl.textContent = text;
    }

    const status =
        document.getElementById("gameStatus") ||
        document.getElementById("status");

    if (status) {
        status.textContent = text;
    }
});

// ============================================================
// RENDERER
// IMPORTANT: This is EM_JS, not EM_ASM.
// ============================================================

EM_JS(
    void,
    js_render,
    (
        int mapW,
        int mapH,
        int tile,
        int worldPtr,
        int px,
        int py,
        int bugCount,
        int bugsPtr
    ),
    {
        if (!window.pm || !window.pm.ctx || !window.pm.canvas) {
            return;
        }

        const ctx = window.pm.ctx;
        const canvas = window.pm.canvas;

        ctx.fillStyle = "#061018";
        ctx.fillRect(
            0,
            0,
            canvas.width,
            canvas.height
        );

        const heap = HEAP32;
        const base = worldPtr >> 2;

        const colors = [
            "#182733",
            "#33414b",
            "#a66b3d",
            "#9c7cff",
            "#38e0a2"
        ];

        // ----------------------------------------------------
        // WORLD
        // ----------------------------------------------------

        for (let y = 0; y < mapH; y++) {
            for (let x = 0; x < mapW; x++) {

                const cell =
                    y * mapW + x;

                const offset =
                    base + cell * 2;

                const type =
                    heap[offset];

                const revealed =
                    heap[offset + 1];

                const screenX =
                    x * tile;

                const screenY =
                    y * tile;

                if (!revealed) {

                    ctx.fillStyle = "#02070c";

                    ctx.fillRect(
                        screenX,
                        screenY,
                        tile - 1,
                        tile - 1
                    );

                    continue;
                }

                ctx.fillStyle =
                    colors[type] || colors[0];

                ctx.fillRect(
                    screenX,
                    screenY,
                    tile - 1,
                    tile - 1
                );

                // Copper
                if (type === 2) {

                    ctx.fillStyle = "#e9a66a";

                    ctx.fillRect(
                        screenX + 7,
                        screenY + 7,
                        10,
                        10
                    );
                }

                // Crystal
                if (type === 3) {

                    ctx.fillStyle = "#e0d5ff";

                    ctx.beginPath();

                    ctx.moveTo(
                        screenX + 12,
                        screenY + 4
                    );

                    ctx.lineTo(
                        screenX + 19,
                        screenY + 12
                    );

                    ctx.lineTo(
                        screenX + 12,
                        screenY + 20
                    );

                    ctx.lineTo(
                        screenX + 5,
                        screenY + 12
                    );

                    ctx.closePath();

                    ctx.fill();
                }

                // Energy
                if (type === 4) {

                    ctx.fillStyle = "#c7fff0";

                    ctx.beginPath();

                    ctx.arc(
                        screenX + 12,
                        screenY + 12,
                        6,
                        0,
                        Math.PI * 2
                    );

                    ctx.fill();
                }
            }
        }

        // ----------------------------------------------------
        // CAVE BUGS
        // ----------------------------------------------------

        if (bugsPtr && bugCount > 0) {

            const bugsHeap =
                new Float64Array(
                    HEAPF64.buffer
                );

            const base64 =
                bugsPtr >> 3;

            for (
                let i = 0;
                i < bugCount;
                i++
            ) {

                const bx =
                    bugsHeap[base64 + i * 3];

                const by =
                    bugsHeap[
                        base64 + i * 3 + 1
                    ];

                const screenX =
                    bx * tile;

                const screenY =
                    by * tile;

                ctx.fillStyle = "#ff547d";

                ctx.beginPath();

                ctx.arc(
                    screenX + 12,
                    screenY + 12,
                    8,
                    0,
                    Math.PI * 2
                );

                ctx.fill();

                ctx.fillStyle = "#ffffff";

                ctx.fillRect(
                    screenX + 8,
                    screenY + 9,
                    2,
                    2
                );

                ctx.fillRect(
                    screenX + 14,
                    screenY + 9,
                    2,
                    2
                );
            }
        }

        // ----------------------------------------------------
        // PLAYER
        // ----------------------------------------------------

        const playerX =
            px * tile;

        const playerY =
            py * tile;

        ctx.fillStyle = "#5ff6ff";

        ctx.beginPath();

        ctx.arc(
            playerX + 12,
            playerY + 12,
            9,
            0,
            Math.PI * 2
        );

        ctx.fill();

        // Player eyes
        ctx.fillStyle = "#ffffff";

        ctx.beginPath();

        ctx.arc(
            playerX + 9,
            playerY + 9,
            2,
            0,
            Math.PI * 2
        );

        ctx.arc(
            playerX + 15,
            playerY + 9,
            2,
            0,
            Math.PI * 2
        );

        ctx.fill();

        // Player outline
        ctx.strokeStyle =
            "rgba(95,246,255,0.2)";

        ctx.strokeRect(
            playerX + 2,
            playerY + 2,
            20,
            20
        );
    }
);

// ============================================================
// WORLD GENERATION
// ============================================================

static int indexOf(int x, int y) {
    return y * MAP_W + x;
}

static bool insideMap(int x, int y) {
    return (
        x >= 0 &&
        x < MAP_W &&
        y >= 0 &&
        y < MAP_H
    );
}

static void revealAround(int cx, int cy) {

    for (int dy = -2; dy <= 2; dy++) {

        for (int dx = -2; dx <= 2; dx++) {

            const int x = cx + dx;
            const int y = cy + dy;

            if (!insideMap(x, y)) {
                continue;
            }

            if (
                abs(dx) + abs(dy) <= 2
            ) {
                world[indexOf(x, y)].revealed = 1;
            }
        }
    }
}

static void generateWorld() {

    world.clear();

    world.resize(
        MAP_W * MAP_H
    );

    for (int y = 0; y < MAP_H; y++) {

        for (int x = 0; x < MAP_W; x++) {

            Tile tile;

            tile.revealed = 0;

            const int roll =
                randomInt(0, 99);

            if (roll < 58) {
                tile.type = 0;
            }
            else if (roll < 82) {
                tile.type = 1;
            }
            else if (roll < 94) {
                tile.type = 2;
            }
            else if (roll < 98) {
                tile.type = 3;
            }
            else {
                tile.type = 4;
            }

            world[indexOf(x, y)] = tile;
        }
    }

    // Starting area
    const int sx = MAP_W / 2;
    const int sy = MAP_H / 2;

    for (int dy = -2; dy <= 2; dy++) {

        for (int dx = -2; dx <= 2; dx++) {

            const int x = sx + dx;
            const int y = sy + dy;

            if (!insideMap(x, y)) {
                continue;
            }

            Tile& tile =
                world[indexOf(x, y)];

            tile.type = 0;
            tile.revealed = 1;
        }
    }

    player.x = sx;
    player.y = sy;
}

// ============================================================
// MINING
// ============================================================

static void mine(int x, int y) {

    if (!insideMap(x, y)) {
        return;
    }

    Tile& tile =
        world[indexOf(x, y)];

    tile.revealed = 1;

    if (tile.type == 1) {

        tile.type = 0;

        player.score += 1;
        player.energy -= 2;
    }
    else if (tile.type == 2) {

        tile.type = 0;

        player.score += 10;
        player.energy -= 2;
    }
    else if (tile.type == 3) {

        tile.type = 0;

        player.score += 25;
        player.energy -= 2;
    }
    else if (tile.type == 4) {

        tile.type = 0;

        player.score += 15;

        player.energy += 15;

        if (player.energy > START_ENERGY) {
            player.energy = START_ENERGY;
        }
    }
}

// ============================================================
// PLAYER MOVEMENT
// ============================================================

static void tryMove(int dx, int dy) {

    if (gameOver) {
        return;
    }

    if (player.energy <= 0) {
        gameOver = true;
        js_game_state(1);
        js_message("OUT OF ENERGY");
        return;
    }

    const int nx =
        player.x + dx;

    const int ny =
        player.y + dy;

    if (!insideMap(nx, ny)) {
        return;
    }

    Tile& target =
        world[indexOf(nx, ny)];

    // Rock blocks movement until mined
    if (target.type == 1) {

        mine(nx, ny);

        revealAround(
            player.x,
            player.y
        );

        return;
    }

    // Move
    player.x = nx;
    player.y = ny;

    player.energy--;

    revealAround(
        player.x,
        player.y
    );

    // Collect resource
    if (
        target.type == 2 ||
        target.type == 3 ||
        target.type == 4
    ) {
        mine(nx, ny);
    }

    if (player.energy <= 0) {

        player.energy = 0;

        gameOver = true;

        js_game_state(1);
        js_message("OUT OF ENERGY");
    }
}

// ============================================================
// BUGS
// ============================================================

static void spawnBug() {

    if (
        static_cast<int>(bugs.size())
        >= MAX_BUGS
    ) {
        return;
    }

    Bug bug;

    bool valid = false;

    for (int attempt = 0; attempt < 50; attempt++) {

        bug.x =
            randomDouble(
                2.0,
                MAP_W - 3.0
            );

        bug.y =
            randomDouble(
                2.0,
                MAP_H - 3.0
            );

        const double distance =
            hypot(
                bug.x - player.x,
                bug.y - player.y
            );

        if (distance > 8.0) {
            valid = true;
            break;
        }
    }

    if (!valid) {
        return;
    }

    bug.speed =
        randomDouble(
            0.35,
            0.75
        );

    bugs.push_back(bug);
}

static void updateBugs(double dt) {

    if (gameOver) {
        return;
    }

    for (Bug& bug : bugs) {

        const double dx =
            static_cast<double>(player.x)
            - bug.x;

        const double dy =
            static_cast<double>(player.y)
            - bug.y;

        const double distance =
            sqrt(
                dx * dx +
                dy * dy
            );

        if (distance > 0.01) {

            bug.x +=
                (dx / distance)
                * bug.speed
                * dt;

            bug.y +=
                (dy / distance)
                * bug.speed
                * dt;
        }

        // Bug reached player
        if (distance < 0.7) {

            player.energy -= 8;

            bug.x =
                randomDouble(
                    2.0,
                    MAP_W - 3.0
                );

            bug.y =
                randomDouble(
                    2.0,
                    MAP_H - 3.0
                );

            if (player.energy <= 0) {

                player.energy = 0;

                gameOver = true;

                js_game_state(1);
                js_message("CAUGHT BY A CAVE CRAWLER");
            }
        }
    }
}

// ============================================================
// MOUSE CONTROL
// ============================================================

static void handleMouse() {

    if (!js_mouse_down()) {
        return;
    }

    const int mouseX =
        js_mouse_x();

    const int mouseY =
        js_mouse_y();

    const int targetX =
        mouseX / TILE;

    const int targetY =
        mouseY / TILE;

    if (!insideMap(targetX, targetY)) {
        return;
    }

    const int dx =
        targetX - player.x;

    const int dy =
        targetY - player.y;

    if (abs(dx) + abs(dy) == 1) {

        if (dx > 0) {
            tryMove(1, 0);
        }
        else if (dx < 0) {
            tryMove(-1, 0);
        }
        else if (dy > 0) {
            tryMove(0, 1);
        }
        else if (dy < 0) {
            tryMove(0, -1);
        }
    }
}

// ============================================================
// KEYBOARD CONTROL
// ============================================================

static void handleKeyboard() {

    if (js_key(0) || js_key(4)) {
        tryMove(0, -1);
    }
    else if (js_key(1) || js_key(5)) {
        tryMove(0, 1);
    }
    else if (js_key(2) || js_key(6)) {
        tryMove(-1, 0);
    }
    else if (js_key(3) || js_key(7)) {
        tryMove(1, 0);
    }
}

// ============================================================
// RENDER
// ============================================================

static void render() {

    if (world.empty()) {
        return;
    }

    js_render(
        MAP_W,
        MAP_H,
        TILE,
        static_cast<int>(
            reinterpret_cast<uintptr_t>(
                world.data()
            )
        ),
        player.x,
        player.y,
        static_cast<int>(
            bugs.size()
        ),
        bugs.empty()
            ? 0
            : static_cast<int>(
                reinterpret_cast<uintptr_t>(
                    bugs.data()
                )
            )
    );
}

// ============================================================
// GAME RESET
// ============================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
void restart_game() {

    player.x = MAP_W / 2;
    player.y = MAP_H / 2;
    player.energy = START_ENERGY;
    player.score = 0;

    gameOver = false;

    bugs.clear();

    lastMoveTime = 0.0;
    lastBugSpawn = 0.0;

    generateWorld();

    revealAround(
        player.x,
        player.y
    );

    js_game_state(0);
    js_message("MINING");

    js_set_score(
        player.score,
        player.energy,
        bestScore
    );

    render();
}

}

// ============================================================
// GAME TICK
// ============================================================

static void tick(void* arg) {

    static double previousTime = 0.0;

    const double currentTime =
        emscripten_get_now() / 1000.0;

    double dt =
        currentTime - previousTime;

    previousTime =
        currentTime;

    if (dt < 0.0) {
        dt = 0.0;
    }

    if (dt > 0.1) {
        dt = 0.1;
    }

    if (!gameOver) {

        // ----------------------------------------------------
        // Keyboard movement cooldown
        // ----------------------------------------------------

        if (
            currentTime - lastMoveTime
            >= 0.11
        ) {

            handleKeyboard();

            lastMoveTime =
                currentTime;
        }

        // ----------------------------------------------------
        // Mouse
        // ----------------------------------------------------

        if (
            currentTime - lastMoveTime
            >= 0.11
        ) {

            handleMouse();

            lastMoveTime =
                currentTime;
        }

        // ----------------------------------------------------
        // Spawn bugs
        // ----------------------------------------------------

        if (
            currentTime - lastBugSpawn
            >= 5.0
        ) {

            spawnBug();

            lastBugSpawn =
                currentTime;
        }

        // ----------------------------------------------------
        // Update bugs
        // ----------------------------------------------------

        updateBugs(dt);

        // ----------------------------------------------------
        // Best score
        // ----------------------------------------------------

        if (
            player.score > bestScore
        ) {

            bestScore =
                player.score;

            js_save_best(
                bestScore
            );
        }
    }

    js_set_score(
        player.score,
        player.energy,
        bestScore
    );

    render();
}

// ============================================================
// MAIN
// ============================================================

int main() {

    js_setup();

    bestScore =
        js_load_best();

    generateWorld();

    revealAround(
        player.x,
        player.y
    );

    js_game_state(0);

    js_message("MINING");

    js_set_score(
        player.score,
        player.energy,
        bestScore
    );

    render();

    emscripten_set_main_loop_arg(
        tick,
        nullptr,
        0,
        1
    );

    return 0;
}
