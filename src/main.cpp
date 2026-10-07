#include <emscripten.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <random>

struct Tile {
    int type; // 0 dirt, 1 rock, 2 copper, 3 crystal, 4 energy
    bool revealed;
};

struct Player {
    int x = 2;
    int y = 2;
    int energy = 100;
    int score = 0;
};

static constexpr int MAP_W = 48;
static constexpr int MAP_H = 34;
static constexpr int TILE = 24;

static std::vector<Tile> world;
static Player player;
static int bestScore = 0;
static bool running = true;
static bool gameOver = false;
static double lastTime = 0;
static double enemyTimer = 0;
static int level = 1;

struct Bug { double x, y, speed; };
static std::vector<Bug> bugs;

EM_JS(void, js_setup, (), {
    window.pm = window.pm || {};
    const c = document.getElementById('game');
    window.pm.canvas = c;
    window.pm.ctx = c.getContext('2d');
    window.pm.keys = {};
    window.pm.mouse = {x:0,y:0,down:false};
    window.addEventListener('keydown', e => {
        window.pm.keys[e.key.toLowerCase()] = true;
        if (["arrowup","arrowdown","arrowleft","arrowright","w","a","s","d"," "].includes(e.key.toLowerCase()))
            e.preventDefault();
    });
    window.addEventListener('keyup', e => window.pm.keys[e.key.toLowerCase()] = false);
    c.addEventListener('mousemove', e => {
        const r = c.getBoundingClientRect();
        window.pm.mouse.x = (e.clientX-r.left) * c.width/r.width;
        window.pm.mouse.y = (e.clientY-r.top) * c.height/r.height;
    });
    c.addEventListener('mousedown', () => window.pm.mouse.down = true);
    window.addEventListener('mouseup', () => window.pm.mouse.down = false);
    c.addEventListener('touchstart', e => {
        const t=e.touches[0], r=c.getBoundingClientRect();
        window.pm.mouse.x=(t.clientX-r.left)*c.width/r.width;
        window.pm.mouse.y=(t.clientY-r.top)*c.height/r.height;
        window.pm.mouse.down=true;
        e.preventDefault();
    }, {passive:false});
    c.addEventListener('touchmove', e => {
        const t=e.touches[0], r=c.getBoundingClientRect();
        window.pm.mouse.x=(t.clientX-r.left)*c.width/r.width;
        window.pm.mouse.y=(t.clientY-r.top)*c.height/r.height;
        e.preventDefault();
    }, {passive:false});
    c.addEventListener('touchend', () => window.pm.mouse.down=false);
});

EM_JS(int, js_key, (int code), {
    const k = ["w","s","a","d"][code];
    return window.pm?.keys?.[k] || false;
});

EM_JS(int, js_arrow, (int code), {
    const k = ["arrowup","arrowdown","arrowleft","arrowright"][code];
    return window.pm?.keys?.[k] || false;
});

EM_JS(int, js_mouse_down, (), { return !!window.pm?.mouse?.down; });

EM_JS(double, js_mouse_x, (), { return window.pm?.mouse?.x || 0; });
EM_JS(double, js_mouse_y, (), { return window.pm?.mouse?.y || 0; });

EM_JS(void, js_set_score, (int score, int energy, int level, int best), {
    document.getElementById("score").textContent = "ORE " + score;
    document.getElementById("energy").textContent = "ENERGY " + energy + "%";
    document.getElementById("level").textContent = "DEPTH " + level;
    document.getElementById("best").textContent = "BEST " + best;
});

EM_JS(void, js_game_state, (int over), {
    const panel=document.getElementById("gameover");
    panel.style.display=over ? "flex" : "none";
});

EM_JS(void, js_save_best, (int score), {
    localStorage.setItem("pixel_miner_best", String(score));
});

EM_JS(int, js_load_best, (), {
    return Number(localStorage.getItem("pixel_miner_best") || 0);
});

EM_JS(void, js_message, (const char* msg), {
    document.getElementById("message").textContent = UTF8ToString(msg);
});

static int idx(int x, int y) { return y * MAP_W + x; }

static bool inside(int x, int y) {
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H;
}

static int randomInt(int a, int b) {
    return a + std::rand() % (b - a + 1);
}

static void generateWorld() {
    world.assign(MAP_W * MAP_H, {0, false});

    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            Tile &t = world[idx(x,y)];
            if (x == 0 || y == 0 || x == MAP_W-1 || y == MAP_H-1) {
                t.type = 1;
            } else {
                int r = randomInt(0,99);
                if (r < 9) t.type = 1;
                else if (r < 17) t.type = 2;
                else if (r < 21) t.type = 3;
                else if (r < 23) t.type = 4;
                else t.type = 0;
            }
        }
    }

    // Starting cavern.
    for (int y = 1; y <= 5; ++y)
        for (int x = 1; x <= 6; ++x)
            world[idx(x,y)] = {0,true};

    player = {3,3,100,0};
    bugs.clear();
    level = 1;
    enemyTimer = 0;
    running = true;
    gameOver = false;
    js_game_state(0);
    js_message("Explore the mine. Break valuable tiles and watch your energy.");
}

static void revealAround(int cx, int cy) {
    for (int dy=-2; dy<=2; ++dy)
        for (int dx=-2; dx<=2; ++dx)
            if (inside(cx+dx,cy+dy) && std::abs(dx)+std::abs(dy) <= 3)
                world[idx(cx+dx,cy+dy)].revealed = true;
}

static void mine(int tx, int ty) {
    if (!inside(tx,ty)) return;
    Tile &t = world[idx(tx,ty)];
    if (!t.revealed || t.type == 1) {
        if (t.type == 1) {
            t.type = 0;
            player.energy -= 3;
            js_message("Rock cracked open. Keep digging!");
        }
        return;
    }

    if (t.type == 0) {
        player.energy -= 1;
        t.type = 0;
    } else if (t.type == 2) {
        player.score += 25;
        player.energy -= 2;
        t.type = 0;
        js_message("Copper vein collected! +25 ore");
    } else if (t.type == 3) {
        player.score += 80;
        player.energy -= 4;
        t.type = 0;
        js_message("Crystal found! +80 ore");
    } else if (t.type == 4) {
        player.energy = std::min(100, player.energy + 30);
        player.score += 10;
        t.type = 0;
        js_message("Energy cell! +30% energy");
    }
    revealAround(player.x, player.y);
}

static void tryMove(int dx, int dy) {
    int nx = player.x + dx;
    int ny = player.y + dy;
    if (!inside(nx,ny)) return;

    Tile &t = world[idx(nx,ny)];
    if (t.type == 1) {
        mine(nx,ny);
        return;
    }

    player.x = nx;
    player.y = ny;
    player.energy = std::max(0, player.energy - 1);
    revealAround(player.x, player.y);

    if (player.energy <= 0) {
        gameOver = true;
        running = false;
        js_game_state(1);
        js_message("Your energy is empty. Restart the expedition.");
    }
}

static void spawnBug() {
    Bug b;
    b.x = randomInt(5, MAP_W-6);
    b.y = randomInt(7, MAP_H-5);
    b.speed = 0.5 + level * 0.08;
    bugs.push_back(b);
}

static void updateBugs(double dt) {
    for (auto &b : bugs) {
        double dx = player.x - b.x;
        double dy = player.y - b.y;
        double d = std::max(0.001, std::sqrt(dx*dx+dy*dy));
        b.x += dx/d * b.speed * dt;
        b.y += dy/d * b.speed * dt;

        if (d < 0.65) {
            player.energy -= 12;
            b.x += -dx/d * 1.5;
            b.y += -dy/d * 1.5;
            js_message("Cave crawler attack! Find an energy cell.");
            if (player.energy <= 0) {
                gameOver = true;
                running = false;
                js_game_state(1);
            }
        }
    }
}

static void render() {
    EM_ASM({
        const ctx = window.pm.ctx, c = window.pm.canvas;
        ctx.fillStyle = "#061018";
        ctx.fillRect(0,0,c.width,c.height);

        const tile = 24;
        for (let y=0; y<$0; y++) {
            for (let x=0; x<$1; x++) {
                const t = HEAP32[($2 + (y*$1+x)*8)>>2];
                const revealed = HEAP32[($2 + (y*$1+x)*8+4)>>2];
                if (!revealed) {
                    ctx.fillStyle="#02070c";
                    ctx.fillRect(x*tile,y*tile,tile-1,tile-1);
                    continue;
                }
                const colors=["#182733","#33414b","#a66b3d","#9c7cff","#38e0a2"];
                ctx.fillStyle=colors[t] || colors[0];
                ctx.fillRect(x*tile,y*tile,tile-1,tile-1);
                if(t===2){ctx.fillStyle="#e9a66a";ctx.fillRect(x*tile+7,y*tile+7,10,10);}
                if(t===3){ctx.fillStyle="#e0d5ff";ctx.beginPath();ctx.moveTo(x*tile+12,y*tile+4);ctx.lineTo(x*tile+19,y*tile+12);ctx.lineTo(x*tile+12,y*tile+20);ctx.lineTo(x*tile+5,y*tile+12);ctx.fill();}
                if(t===4){ctx.fillStyle="#c7fff0";ctx.beginPath();ctx.arc(x*tile+12,y*tile+12,6,0,Math.PI*2);ctx.fill();}
            }
        }

        // Bugs are drawn from the JS-side C++ vector below via the second EM_ASM block.
    }, MAP_H, MAP_W, world.data());

    EM_ASM({
        const ctx=window.pm.ctx, tile=24;
        const px=$0, py=$1;
        ctx.fillStyle="#5ff6ff";
        ctx.beginPath();
        ctx.arc(px*tile+12,py*tile+12,9,0,Math.PI*2);
        ctx.fill();
        ctx.fillStyle="#ffffff";
        ctx.beginPath();
        ctx.arc(px*tile+9,py*tile+9,2,0,Math.PI*2);
        ctx.arc(px*tile+15,py*tile+9,2,0,Math.PI*2);
        ctx.fill();

        ctx.strokeStyle="rgba(95,246,255,.2)";
        ctx.strokeRect(px*tile+2,py*tile+2,20,20);
    }, player.x, player.y);

    for (const auto &b : bugs) {
        EM_ASM({
            const ctx=window.pm.ctx, tile=24;
            ctx.fillStyle="#ff547d";
            ctx.beginPath();
            ctx.arc($0*tile+12,$1*tile+12,8,0,Math.PI*2);
            ctx.fill();
            ctx.fillStyle="#fff";
            ctx.fillRect($0*tile+8,$1*tile+9,2,2);
            ctx.fillRect($0*tile+14,$1*tile+9,2,2);
        }, b.x, b.y);
    }
}

static void tick(double now) {
    double dt = (lastTime == 0) ? 0.016 : std::min(0.05, (now-lastTime)/1000.0);
    lastTime = now;
    if (!running) {
        render();
        return;
    }

    static double moveCooldown = 0;
    moveCooldown -= dt;
    enemyTimer += dt;

    if (moveCooldown <= 0) {
        int dx=0, dy=0;
        if (js_key(0) || js_arrow(0)) dy=-1;
        else if (js_key(1) || js_arrow(1)) dy=1;
        else if (js_key(2) || js_arrow(2)) dx=-1;
        else if (js_key(3) || js_arrow(3)) dx=1;

        if (dx || dy) {
            tryMove(dx,dy);
            moveCooldown=0.12;
        }

        // Mouse: click a nearby tile to mine/move.
        if (js_mouse_down()) {
            int mx=(int)(js_mouse_x()/TILE);
            int my=(int)(js_mouse_y()/TILE);
            if (std::abs(mx-player.x)<=1 && std::abs(my-player.y)<=1 && !(mx==player.x && my==player.y)) {
                tryMove(mx-player.x, my-player.y);
                moveCooldown=0.18;
            }
        }
    }

    if (enemyTimer > std::max(7.0, 15.0-level*0.8)) {
        spawnBug();
        enemyTimer=0;
        level=1+player.score/250;
    }

    updateBugs(dt);
    render();

    js_set_score(player.score, player.energy, level, bestScore);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void restart_game() {
    bestScore = js_load_best();
    generateWorld();
    lastTime=0;
}

EMSCRIPTEN_KEEPALIVE
int main() {
    std::srand((unsigned)std::time(nullptr));
    js_setup();
    bestScore=js_load_best();
    generateWorld();
    emscripten_set_main_loop_arg([](void*) {
        tick(emscripten_get_now());
    }, nullptr, 0, 1);
    return 0;
}

}
