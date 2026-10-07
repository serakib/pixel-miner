#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include <emscripten.h>
#include <emscripten/html5.h>

using std::max;
using std::min;

// ============================================================
// CONFIG
// ============================================================

static constexpr int MAP_W = 48;
static constexpr int MAP_H = 34;
static constexpr int TILE = 24;

static constexpr int START_HP = 100;
static constexpr int START_ENERGY = 100;

static constexpr int MAX_STAGE = 5;
static constexpr double STAGE_LENGTH_SECONDS = 60.0;

static constexpr int MAX_BUGS = 12;

static constexpr int TILE_DIRT = 0;
static constexpr int TILE_ROCK = 1;
static constexpr int TILE_COPPER = 2;
static constexpr int TILE_CRYSTAL = 3;
static constexpr int TILE_ENERGY = 4;
static constexpr int TILE_EXIT = 5;
static constexpr int TILE_HAZARD = 6;

// ============================================================
// EM_JS HELPERS
// ============================================================

EM_JS(void, js_setup, (), {
    const canvas = document.getElementById("gameCanvas");

    if (!canvas) {
        console.error("gameCanvas not found");
        return;
    }

    window.pixelMiner = window.pixelMiner || {};

    function sendKey(key) {
        if (typeof Module !== "undefined" && Module._js_key) {
            Module._js_key(key.charCodeAt(0));
        }
    }

    document.addEventListener("keydown", function (e) {
        let key = "";

        if (e.key === "ArrowUp") key = "w";
        else if (e.key === "ArrowDown") key = "s";
        else if (e.key === "ArrowLeft") key = "a";
        else if (e.key === "ArrowRight") key = "d";
        else if (e.key === "w" || e.key === "W") key = "w";
        else if (e.key === "s" || e.key === "S") key = "s";
        else if (e.key === "a" || e.key === "A") key = "a";
        else if (e.key === "d" || e.key === "D") key = "d";
        else if (e.key === "p" || e.key === "P") key = "p";

        if (key) {
            e.preventDefault();
            sendKey(key);
        }
    });

    canvas.addEventListener("click", function (e) {
        if (
            typeof Module === "undefined" ||
            !Module._move_player
        ) {
            return;
        }

        const rect = canvas.getBoundingClientRect();

        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;

        const cx = rect.width / 2;
        const cy = rect.height / 2;

        const dx = x - cx;
        const dy = y - cy;

        if (Math.abs(dx) < 12 && Math.abs(dy) < 12) {
            return;
        }

        if (Math.abs(dx) > Math.abs(dy)) {
            Module._move_player(dx > 0 ? 1 : -1, 0);
        } else {
            Module._move_player(0, dy > 0 ? 1 : -1);
        }
    });

    let touchStartX = 0;
    let touchStartY = 0;

    canvas.addEventListener("touchstart", function (e) {
        if (!e.touches.length) return;

        touchStartX = e.touches[0].clientX;
        touchStartY = e.touches[0].clientY;
    }, { passive: true });

    canvas.addEventListener("touchend", function (e) {
        if (!e.changedTouches.length) return;

        const touch = e.changedTouches[0];

        const dx = touch.clientX - touchStartX;
        const dy = touch.clientY - touchStartY;

        if (Math.abs(dx) < 18 && Math.abs(dy) < 18) {
            return;
        }

        if (
            typeof Module === "undefined" ||
            !Module._move_player
        ) {
            return;
        }

        if (Math.abs(dx) > Math.abs(dy)) {
            Module._move_player(dx > 0 ? 1 : -1, 0);
        } else {
            Module._move_player(0, dy > 0 ? 1 : -1);
        }
    }, { passive: true });

    window.pixelMiner.restart = function () {
        if (
            typeof Module !== "undefined" &&
            Module._restart_game
        ) {
            Module._restart_game();
        } else {
            window.location.reload();
        }
    };

    window.pixelMiner.pause = function () {
        if (
            typeof Module !== "undefined" &&
            Module._toggle_pause
        ) {
            Module._toggle_pause();
        }
    };
});

EM_JS(void, js_update_hud,
      (int hp,
       int energy,
       int score,
       int copper,
       int crystals,
       int depth,
       int stage,
       double elapsed),
{
    function setText(id, value) {
        const el = document.getElementById(id);
        if (el) el.textContent = value;
    }

    setText("hpValue", hp);
    setText("energyValue", energy);
    setText("scoreValue", score);
    setText("copperValue", copper);
    setText("crystalValue", crystals);
    setText("depthValue", depth);
    setText("stageValue", stage);

    const timer = document.getElementById("timeValue");

    if (timer) {
        const total = Math.max(0, Math.floor(elapsed));

        const minutes = Math.floor(total / 60);
        const seconds = total % 60;

        timer.textContent =
            String(minutes).padStart(2, "0") +
            ":" +
            String(seconds).padStart(2, "0");
    }

    const hpBar = document.getElementById("hpBar");
    const energyBar = document.getElementById("energyBar");

    if (hpBar) {
        hpBar.style.width = Math.max(0, Math.min(100, hp)) + "%";
    }

    if (energyBar) {
        energyBar.style.width =
            Math.max(0, Math.min(100, energy)) + "%";
    }
});

EM_JS(void, js_message, (const char* text), {
    const message = UTF8ToString(text);

    const el = document.getElementById("message");

    if (el) {
        el.textContent = message;

        el.classList.remove("show");

        void el.offsetWidth;

        el.classList.add("show");

        clearTimeout(window.__pixelMinerMessageTimer);

        window.__pixelMinerMessageTimer =
            setTimeout(function () {
                el.classList.remove("show");
            }, 1800);
    }
});

EM_JS(void, js_show_game_over, (const char* reason, int score), {
    let overlay = document.getElementById("gameOverOverlay");

    if (!overlay) {
        overlay = document.createElement("div");

        overlay.id = "gameOverOverlay";

        overlay.innerHTML = `
            <div class="game-over-card">
                <div class="game-over-title">GAME OVER</div>
                <div id="gameOverReason" class="game-over-reason"></div>
                <div class="game-over-score">
                    SCORE: <span id="gameOverScore">0</span>
                </div>
                <button id="gameOverRestart">RESTART</button>
            </div>
        `;

        document.body.appendChild(overlay);

        const button =
            document.getElementById("gameOverRestart");

        if (button) {
            button.addEventListener("click", function () {
                if (
                    typeof Module !== "undefined" &&
                    Module._restart_game
                ) {
                    Module._restart_game();
                } else {
                    window.location.reload();
                }
            });
        }
    }

    const reasonEl =
        document.getElementById("gameOverReason");

    const scoreEl =
        document.getElementById("gameOverScore");

    if (reasonEl) {
        reasonEl.textContent = UTF8ToString(reason);
    }

    if (scoreEl) {
        scoreEl.textContent = String(score);
    }

    overlay.classList.add("visible");
});

EM_JS(void, js_hide_game_over, (), {
    const overlay =
        document.getElementById("gameOverOverlay");

    if (overlay) {
        overlay.classList.remove("visible");
    }
});

EM_JS(void, js_save_best, (int score), {
    try {
        const oldBest =
            Number(localStorage.getItem("pixelMinerBest") || 0);

        if (score > oldBest) {
            localStorage.setItem(
                "pixelMinerBest",
                String(score)
            );
        }
    } catch (e) {
        console.warn("Could not save best score", e);
    }
});

EM_JS(int, js_load_best, (), {
    try {
        return Number(
            localStorage.getItem("pixelMinerBest") || 0
        );
    } catch (e) {
        return 0;
    }
});

// ============================================================
// DATA
// ============================================================

struct Player {
    int x = MAP_W / 2;
    int y = MAP_H / 2;

    int hp = START_HP;
    int energy = START_ENERGY;

    int score = 0;
    int copper = 0;
    int crystals = 0;

    int depth = 1;
};

struct Bug {
    double x = 0.0;
    double y = 0.0;
    double speed = 0.5;
};

// ============================================================
// GLOBAL STATE
// ============================================================

static Player player;

static std::vector<int> world(
    MAP_W * MAP_H,
    TILE_DIRT
);

static std::vector<unsigned char> revealed(
    MAP_W * MAP_H,
    0
);

static std::vector<Bug> bugs;

static std::mt19937 rng(
    static_cast<unsigned int>(
        std::time(nullptr)
    )
);

static bool running = true;
static bool paused = false;
static bool gameOver = false;

static int currentStage = 1;

static double runStartTime = 0.0;
static double lastFrameTime = 0.0;
static double bugSpawnTimer = 0.0;
static double stageMessageTimer = 0.0;

// ============================================================
// UTILS
// ============================================================

static int indexOf(int x, int y) {
    return y * MAP_W + x;
}

static bool insideMap(int x, int y) {
    return
        x >= 0 &&
        x < MAP_W &&
        y >= 0 &&
        y < MAP_H;
}

static int randomInt(int minValue, int maxValue) {
    std::uniform_int_distribution<int> dist(
        minValue,
        maxValue
    );

    return dist(rng);
}

static double randomDouble(
    double minValue,
    double maxValue
) {
    std::uniform_real_distribution<double> dist(
        minValue,
        maxValue
    );

    return dist(rng);
}

static double runElapsed() {
    const double now =
        emscripten_get_now() / 1000.0;

    return max(0.0, now - runStartTime);
}

// ============================================================
// STAGE / DIFFICULTY
// ============================================================

static int calculateStage(double elapsed) {
    const int stage =
        1 +
        static_cast<int>(
            elapsed / STAGE_LENGTH_SECONDS
        );

    return min(MAX_STAGE, stage);
}

static int stageHazards() {
    return min(
        20,
        2 + currentStage * 2 + player.depth / 2
    );
}

static int stageEnergyCells() {
    return 7 + currentStage * 2;
}

static double bugSpawnInterval() {
    const double value =
        6.0 -
        currentStage * 0.8 -
        player.depth * 0.08;

    return max(1.2, value);
}

static double bugSpeedMin() {
    return
        0.28 +
        currentStage * 0.08 +
        player.depth * 0.015;
}

static double bugSpeedMax() {
    return
        0.55 +
        currentStage * 0.12 +
        player.depth * 0.025;
}

static int maximumBugs() {
    return min(
        MAX_BUGS,
        1 +
        currentStage +
        player.depth / 2
    );
}

// ============================================================
// GAME OVER
// ============================================================

static void gameOverNow(
    const char* reason
) {
    if (gameOver) {
        return;
    }

    gameOver = true;
    paused = false;

    js_save_best(player.score);

    js_show_game_over(
        reason,
        player.score
    );
}

// ============================================================
// WORLD
// ============================================================

static void setTile(
    int x,
    int y,
    int type
) {
    if (insideMap(x, y)) {
        world[indexOf(x, y)] = type;
    }
}

static int getTile(
    int x,
    int y
) {
    if (!insideMap(x, y)) {
        return TILE_ROCK;
    }

    return world[indexOf(x, y)];
}

static void clearSpawnArea() {
    const int radius = 3;

    for (int y = player.y - radius;
         y <= player.y + radius;
         ++y) {

        for (int x = player.x - radius;
             x <= player.x + radius;
             ++x) {

            if (insideMap(x, y)) {
                setTile(
                    x,
                    y,
                    TILE_DIRT
                );
            }
        }
    }
}

static bool isFreeForSpecial(
    int x,
    int y
) {
    if (!insideMap(x, y)) {
        return false;
    }

    if (x == player.x &&
        y == player.y) {
        return false;
    }

    if (
        std::abs(x - player.x) <= 3 &&
        std::abs(y - player.y) <= 3
    ) {
        return false;
    }

    return getTile(x, y) == TILE_DIRT;
}

static void placeSpecialTiles(
    int type,
    int amount
) {
    int placed = 0;

    int attempts = amount * 100;

    while (
        placed < amount &&
        attempts-- > 0
    ) {
        const int x =
            randomInt(1, MAP_W - 2);

        const int y =
            randomInt(1, MAP_H - 2);

        if (!isFreeForSpecial(x, y)) {
            continue;
        }

        setTile(x, y, type);
        ++placed;
    }
}

static void generateWorld() {
    world.assign(
        MAP_W * MAP_H,
        TILE_DIRT
    );

    revealed.assign(
        MAP_W * MAP_H,
        0
    );

    bugs.clear();

    // --------------------------------------------------------
    // Base terrain
    // --------------------------------------------------------

    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {

            if (
                x == 0 ||
                y == 0 ||
                x == MAP_W - 1 ||
                y == MAP_H - 1
            ) {
                setTile(
                    x,
                    y,
                    TILE_ROCK
                );

                continue;
            }

            const int roll =
                randomInt(0, 99);

            if (roll < 22) {
                setTile(
                    x,
                    y,
                    TILE_ROCK
                );
            }
            else if (roll < 31) {
                setTile(
                    x,
                    y,
                    TILE_COPPER
                );
            }
            else if (roll < 35) {
                setTile(
                    x,
                    y,
                    TILE_CRYSTAL
                );
            }
            else {
                setTile(
                    x,
                    y,
                    TILE_DIRT
                );
            }
        }
    }

    clearSpawnArea();

    // --------------------------------------------------------
    // Exit
    // --------------------------------------------------------

    placeSpecialTiles(
        TILE_EXIT,
        1
    );

    // --------------------------------------------------------
    // Energy cells
    // --------------------------------------------------------

    placeSpecialTiles(
        TILE_ENERGY,
        stageEnergyCells()
    );

    // --------------------------------------------------------
    // Hazards
    // --------------------------------------------------------

    placeSpecialTiles(
        TILE_HAZARD,
        stageHazards()
    );

    // Player area always safe.
    clearSpawnArea();

    revealAround();

    // Initial bugs according to difficulty.
    const int initialBugs =
        min(
            currentStage,
            maximumBugs()
        );

    for (int i = 0;
         i < initialBugs;
         ++i) {
        // Spawn bugs away from player.
        for (int attempt = 0;
             attempt < 100;
             ++attempt) {

            const int x =
                randomInt(2, MAP_W - 3);

            const int y =
                randomInt(2, MAP_H - 3);

            const double distance =
                std::hypot(
                    static_cast<double>(
                        x - player.x
                    ),
                    static_cast<double>(
                        y - player.y
                    )
                );

            if (distance < 7.0) {
                continue;
            }

            Bug bug;

            bug.x = x + 0.5;
            bug.y = y + 0.5;

            bug.speed =
                randomDouble(
                    bugSpeedMin(),
                    bugSpeedMax()
                );

            bugs.push_back(bug);
            break;
        }
    }
}

// ============================================================
// REVEAL
// ============================================================

static void revealAround() {
    const int radius = 4;

    for (int y = player.y - radius;
         y <= player.y + radius;
         ++y) {

        for (int x = player.x - radius;
             x <= player.x + radius;
             ++x) {

            if (!insideMap(x, y)) {
                continue;
            }

            const int dx =
                x - player.x;

            const int dy =
                y - player.y;

            if (
                dx * dx +
                dy * dy <=
                radius * radius
            ) {
                revealed[indexOf(x, y)] = 1;
            }
        }
    }
}

// ============================================================
// TILE INTERACTION
// ============================================================

static void mineTile(
    int x,
    int y
) {
    if (!insideMap(x, y)) {
        return;
    }

    const int tile =
        getTile(x, y);

    if (tile == TILE_ROCK) {

        const int cost = 2;

        player.energy =
            max(
                0,
                player.energy - cost
            );

        setTile(
            x,
            y,
            TILE_DIRT
        );

        player.score += 2;
    }

    else if (tile == TILE_COPPER) {

        player.energy =
            max(
                0,
                player.energy - 2
            );

        player.copper += 1;
        player.score += 10;

        setTile(
            x,
            y,
            TILE_DIRT
        );
    }

    else if (tile == TILE_CRYSTAL) {

        player.energy =
            max(
                0,
                player.energy - 2
            );

        player.crystals += 1;
        player.score += 25;

        setTile(
            x,
            y,
            TILE_DIRT
        );
    }

    else if (tile == TILE_ENERGY) {

        // ONLY source of energy regeneration.
        player.energy =
            min(
                START_ENERGY,
                player.energy + 25
            );

        player.score += 5;

        setTile(
            x,
            y,
            TILE_DIRT
        );

        js_message(
            "ENERGY CELL +25"
        );
    }

    else if (tile == TILE_HAZARD) {

        const int damage =
            8 + currentStage * 2;

        player.hp =
            max(
                0,
                player.hp - damage
            );

        player.energy =
            max(
                0,
                player.energy - 1
            );

        setTile(
            x,
            y,
            TILE_DIRT
        );

        if (player.hp <= 0) {
            gameOverNow(
                "HP DEPLETED"
            );

            return;
        }
    }
}

// ============================================================
// PLAYER MOVEMENT
// ============================================================

static void tryMove(
    int dx,
    int dy
) {
    if (!running ||
        paused ||
        gameOver) {
        return;
    }

    // Energy zero means the run is already over.
    if (player.energy <= 0) {
        gameOverNow(
            "ENERGY DEPLETED"
        );

        return;
    }

    if (
        std::abs(dx) +
        std::abs(dy) != 1
    ) {
        return;
    }

    const int nx =
        player.x + dx;

    const int ny =
        player.y + dy;

    if (!insideMap(nx, ny)) {
        return;
    }

    const int target =
        getTile(nx, ny);

    // --------------------------------------------------------
    // Rock = mine instead of move
    // --------------------------------------------------------

    if (target == TILE_ROCK) {

        mineTile(nx, ny);

        if (player.energy <= 0 &&
            !gameOver) {

            gameOverNow(
                "ENERGY DEPLETED"
            );
        }

        return;
    }

    // --------------------------------------------------------
    // Every movement consumes energy
    // --------------------------------------------------------

    player.energy =
        max(
            0,
            player.energy - 1
        );

    player.x = nx;
    player.y = ny;

    revealAround();

    // --------------------------------------------------------
    // Tile interaction
    // --------------------------------------------------------

    mineTile(
        player.x,
        player.y
    );

    if (gameOver) {
        return;
    }

    // --------------------------------------------------------
    // Exit
    // --------------------------------------------------------

    if (
        target == TILE_EXIT
    ) {
        player.depth++;

        player.score +=
            50 * currentStage;

        // Small transition reward.
        player.hp =
            min(
                START_HP,
                player.hp + 10
            );

        // IMPORTANT:
        // No automatic energy refill.
        // Energy remains unchanged here.

        js_message(
            "DEPTH COMPLETE"
        );

        generateWorld();

        return;
    }

    // --------------------------------------------------------
    // Energy zero = immediate game over
    // --------------------------------------------------------

    if (player.energy <= 0) {
        gameOverNow(
            "ENERGY DEPLETED"
        );

        return;
    }

    // --------------------------------------------------------
    // HP zero
    // --------------------------------------------------------

    if (player.hp <= 0) {
        gameOverNow(
            "HP DEPLETED"
        );
    }
}

// ============================================================
// BUG SYSTEM
// ============================================================

static void spawnBug() {
    if (
        static_cast<int>(bugs.size()) >=
        maximumBugs()
    ) {
        return;
    }

    for (int attempt = 0;
         attempt < 100;
         ++attempt) {

        const int x =
            randomInt(2, MAP_W - 3);

        const int y =
            randomInt(2, MAP_H - 3);

        const double distance =
            std::hypot(
                static_cast<double>(
                    x - player.x
                ),
                static_cast<double>(
                    y - player.y
                )
            );

        if (distance < 8.0) {
            continue;
        }

        Bug bug;

        bug.x =
            x + 0.5;

        bug.y =
            y + 0.5;

        bug.speed =
            randomDouble(
                bugSpeedMin(),
                bugSpeedMax()
            );

        bugs.push_back(bug);

        return;
    }
}

static void updateBugs(
    double dt
) {
    if (gameOver ||
        paused) {
        return;
    }

    for (auto& bug : bugs) {

        const double dx =
            (player.x + 0.5) -
            bug.x;

        const double dy =
            (player.y + 0.5) -
            bug.y;

        const double distance =
            std::hypot(dx, dy);

        if (distance > 0.001) {

            const double vx =
                dx / distance;

            const double vy =
                dy / distance;

            double moveAmount =
                bug.speed * dt;

            if (moveAmount > distance) {
                moveAmount = distance;
            }

            const double nx =
                bug.x +
                vx * moveAmount;

            const double ny =
                bug.y +
                vy * moveAmount;

            const int tx =
                static_cast<int>(
                    std::floor(nx)
                );

            const int ty =
                static_cast<int>(
                    std::floor(ny)
                );

            if (
                insideMap(tx, ty) &&
                getTile(tx, ty) != TILE_ROCK
            ) {
                bug.x = nx;
                bug.y = ny;
            }
        }

        const double hitDistance =
            std::hypot(
                bug.x -
                    (player.x + 0.5),
                bug.y -
                    (player.y + 0.5)
            );

        if (hitDistance < 0.75) {

            const int damage =
                3 + currentStage;

            player.hp =
                max(
                    0,
                    player.hp - damage
                );

            // Push bug away.
            const double pushX =
                player.x + 0.5 -
                bug.x;

            const double pushY =
                player.y + 0.5 -
                bug.y;

            const double pushDistance =
                std::max(
                    0.001,
                    std::hypot(
                        pushX,
                        pushY
                    )
                );

            bug.x -=
                (pushX / pushDistance) *
                0.8;

            bug.y -=
                (pushY / pushDistance) *
                0.8;

            if (player.hp <= 0) {

                gameOverNow(
                    "HP DEPLETED"
                );

                return;
            }
        }
    }
}

// ============================================================
// RENDER
// ============================================================

static void render() {
    const int width =
        MAP_W * TILE;

    const int height =
        MAP_H * TILE;

    EM_ASM({
        const canvas =
            document.getElementById("gameCanvas");

        if (!canvas) return;

        const ctx =
            canvas.getContext("2d");

        const w = $0;
        const h = $1;

        if (
            canvas.width !== w ||
            canvas.height !== h
        ) {
            canvas.width = w;
            canvas.height = h;
        }

        ctx.clearRect(
            0,
            0,
            w,
            h
        );
    }, width, height);

    // Camera centered on player.
    const int cameraX =
        player.x * TILE -
        width / 2 +
        TILE / 2;

    const int cameraY =
        player.y * TILE -
        height / 2 +
        TILE / 2;

    EM_ASM({
        const canvas =
            document.getElementById("gameCanvas");

        if (!canvas) return;

        const ctx =
            canvas.getContext("2d");

        const mapW = $0;
        const mapH = $1;
        const tileSize = $2;

        const cameraX = $3;
        const cameraY = $4;

        const playerX = $5;
        const playerY = $6;

        const stage = $7;

        const HEAP = HEAP32;

        function tileColor(type) {
            if (type === 0) return "#171a20";
            if (type === 1) return "#343b46";
            if (type === 2) return "#a56a2a";
            if (type === 3) return "#8b5cf6";
            if (type === 4) return "#22c55e";
            if (type === 5) return "#38bdf8";
            if (type === 6) return "#ef4444";

            return "#171a20";
        }

        const worldPtr = $8;
        const revealPtr = $9;

        for (let y = 0; y < mapH; ++y) {
            for (let x = 0; x < mapW; ++x) {

                const idx =
                    y * mapW + x;

                const visible =
                    HEAP[revealPtr / 4 + idx];

                const px =
                    x * tileSize -
                    cameraX;

                const py =
                    y * tileSize -
                    cameraY;

                if (!visible) {
                    ctx.fillStyle = "#07090c";

                    ctx.fillRect(
                        px,
                        py,
                        tileSize,
                        tileSize
                    );

                    continue;
                }

                const type =
                    HEAP[
                        worldPtr / 4 +
                        idx
                    ];

                ctx.fillStyle =
                    tileColor(type);

                ctx.fillRect(
                    px,
                    py,
                    tileSize,
                    tileSize
                );

                ctx.strokeStyle =
                    "rgba(255,255,255,0.035)";

                ctx.strokeRect(
                    px,
                    py,
                    tileSize,
                    tileSize
                );

                const cx =
                    px + tileSize / 2;

                const cy =
                    py + tileSize / 2;

                if (type === 2) {
                    ctx.fillStyle =
                        "#d28b35";

                    ctx.beginPath();

                    ctx.arc(
                        cx,
                        cy,
                        5,
                        0,
                        Math.PI * 2
                    );

                    ctx.fill();
                }

                if (type === 3) {
                    ctx.fillStyle =
                        "#c084fc";

                    ctx.beginPath();

                    ctx.moveTo(
                        cx,
                        py + 5
                    );

                    ctx.lineTo(
                        px + tileSize - 5,
                        cy
                    );

                    ctx.lineTo(
                        cx,
                        py + tileSize - 5
                    );

                    ctx.lineTo(
                        px + 5,
                        cy
                    );

                    ctx.closePath();
                    ctx.fill();
                }

                if (type === 4) {
                    ctx.fillStyle =
                        "#86efac";

                    ctx.beginPath();

                    ctx.arc(
                        cx,
                        cy,
                        7,
                        0,
                        Math.PI * 2
                    );

                    ctx.fill();

                    ctx.fillStyle =
                        "#052e16";

                    ctx.font =
                        "bold 11px Arial";

                    ctx.textAlign =
                        "center";

                    ctx.textBaseline =
                        "middle";

                    ctx.fillText(
                        "E",
                        cx,
                        cy
                    );
                }

                if (type === 5) {
                    ctx.strokeStyle =
                        "#7dd3fc";

                    ctx.lineWidth = 3;

                    ctx.strokeRect(
                        px + 5,
                        py + 5,
                        tileSize - 10,
                        tileSize - 10
                    );

                    ctx.lineWidth = 1;
                }

                if (type === 6) {
                    ctx.fillStyle =
                        "#fca5a5";

                    ctx.beginPath();

                    ctx.moveTo(
                        cx,
                        py + 5
                    );

                    ctx.lineTo(
                        px + tileSize - 5,
                        py + tileSize - 5
                    );

                    ctx.lineTo(
                        px + 5,
                        py + tileSize - 5
                    );

                    ctx.closePath();

                    ctx.fill();
                }
            }
        }

        // Bugs
        const bugPtr = $10;
        const bugCount = $11;

        for (let i = 0; i < bugCount; ++i) {

            const offset =
                bugPtr / 8 +
                i * 3;

            const bx =
                HEAPF64[offset];

            const by =
                HEAPF64[offset + 1];

            const px =
                bx * tileSize -
                cameraX;

            const py =
                by * tileSize -
                cameraY;

            ctx.fillStyle =
                "#f97316";

            ctx.beginPath();

            ctx.arc(
                px,
                py,
                8,
                0,
                Math.PI * 2
            );

            ctx.fill();

            ctx.fillStyle =
                "#111827";

            ctx.beginPath();

            ctx.arc(
                px - 3,
                py - 2,
                1.5,
                0,
                Math.PI * 2
            );

            ctx.arc(
                px + 3,
                py - 2,
                1.5,
                0,
                Math.PI * 2
            );

            ctx.fill();
        }

        // Player
        const playerPX =
            playerX * tileSize -
            cameraX +
            tileSize / 2;

        const playerPY =
            playerY * tileSize -
            cameraY +
            tileSize / 2;

        ctx.fillStyle =
            "#f8fafc";

        ctx.beginPath();

        ctx.arc(
            playerPX,
            playerPY,
            8,
            0,
            Math.PI * 2
        );

        ctx.fill();

        ctx.strokeStyle =
            "#38bdf8";

        ctx.lineWidth = 3;

        ctx.stroke();

        ctx.lineWidth = 1;

        // Stage indicator
        ctx.fillStyle =
            "rgba(0,0,0,0.55)";

        ctx.fillRect(
            12,
            12,
            105,
            30
        );

        ctx.fillStyle =
            "#ffffff";

        ctx.font =
            "bold 14px Arial";

        ctx.textAlign =
            "left";

        ctx.textBaseline =
            "middle";

        ctx.fillText(
            "STAGE " + stage,
            24,
            27
        );
    },
    MAP_W,
    MAP_H,
    TILE,
    cameraX,
    cameraY,
    player.x,
    player.y,
    currentStage,
    world.data(),
    revealed.data(),
    bugs.empty()
        ? nullptr
        : &bugs[0],
    static_cast<int>(bugs.size())
    );
}

// ============================================================
// HUD
// ============================================================

static void updateHUD() {
    js_update_hud(
        player.hp,
        player.energy,
        player.score,
        player.copper,
        player.crystals,
        player.depth,
        currentStage,
        runElapsed()
    );
}

// ============================================================
// KEY INPUT
// ============================================================

extern "C"
EMSCRIPTEN_KEEPALIVE
void js_key(int key) {

    if (key == 'w') {
        tryMove(0, -1);
    }
    else if (key == 's') {
        tryMove(0, 1);
    }
    else if (key == 'a') {
        tryMove(-1, 0);
    }
    else if (key == 'd') {
        tryMove(1, 0);
    }
    else if (key == 'p') {
        if (!gameOver) {
            paused = !paused;

            js_message(
                paused
                    ? "PAUSED"
                    : "RESUMED"
            );
        }
    }
}

// ============================================================
// EXPORTED CONTROLS
// ============================================================

extern "C"
EMSCRIPTEN_KEEPALIVE
void move_player(
    int dx,
    int dy
) {
    tryMove(dx, dy);
}

extern "C"
EMSCRIPTEN_KEEPALIVE
void toggle_pause() {
    if (gameOver) {
        return;
    }

    paused = !paused;

    js_message(
        paused
            ? "PAUSED"
            : "RESUMED"
    );
}

// ============================================================
// RESTART
// ============================================================

extern "C"
EMSCRIPTEN_KEEPALIVE
void restart_game() {

    player = Player();

    running = true;
    paused = false;
    gameOver = false;

    currentStage = 1;

    bugSpawnTimer = 0.0;
    stageMessageTimer = 0.0;

    runStartTime =
        emscripten_get_now() / 1000.0;

    lastFrameTime =
        runStartTime;

    js_hide_game_over();

    generateWorld();

    js_message(
        "STAGE 1"
    );

    updateHUD();
    render();
}

// ============================================================
// GAME LOOP
// ============================================================

static void gameLoop() {

    if (!running) {
        return;
    }

    const double now =
        emscripten_get_now() / 1000.0;

    if (lastFrameTime <= 0.0) {
        lastFrameTime = now;
    }

    double dt =
        now - lastFrameTime;

    lastFrameTime = now;

    dt = max(
        0.0,
        min(0.1, dt)
    );

    if (!gameOver &&
        !paused) {

        const double elapsed =
            now - runStartTime;

        const int newStage =
            calculateStage(elapsed);

        if (newStage != currentStage) {

            currentStage = newStage;

            js_message(
                (
                    std::string("STAGE ") +
                    std::to_string(currentStage)
                ).c_str()
            );
        }

        // ----------------------------------------------------
        // Bug spawning becomes faster at higher stages.
        // ----------------------------------------------------

        bugSpawnTimer += dt;

        if (
            bugSpawnTimer >=
            bugSpawnInterval()
        ) {
            bugSpawnTimer = 0.0;

            spawnBug();
        }

        updateBugs(dt);

        // ----------------------------------------------------
        // Absolute rule:
        // Energy 0 = Game Over.
        // ----------------------------------------------------

        if (
            player.energy <= 0 &&
            !gameOver
        ) {
            gameOverNow(
                "ENERGY DEPLETED"
            );
        }

        if (
            player.hp <= 0 &&
            !gameOver
        ) {
            gameOverNow(
                "HP DEPLETED"
            );
        }
    }

    updateHUD();
    render();
}

// ============================================================
// MAIN
// ============================================================

int main() {

    js_setup();

    restart_game();

    emscripten_set_main_loop(
        gameLoop,
        0,
        1
    );

    return 0;
}
