EM_JS(void, js_render,
      (int mapW, int mapH, int tile, int worldPtr,
       int px, int py, int bugCount, int bugsPtr), {

    const ctx = window.pm.ctx;
    const c = window.pm.canvas;

    ctx.fillStyle = "#061018";
    ctx.fillRect(0, 0, c.width, c.height);

    const heap = HEAP32;
    const base = worldPtr >> 2;

    const colors = [
        "#182733",
        "#33414b",
        "#a66b3d",
        "#9c7cff",
        "#38e0a2"
    ];

    for (let y = 0; y < mapH; y++) {
        for (let x = 0; x < mapW; x++) {

            const offset = base + (y * mapW + x) * 2;

            const type = heap[offset];
            const revealed = heap[offset + 1];

            if (!revealed) {
                ctx.fillStyle = "#02070c";
                ctx.fillRect(
                    x * tile,
                    y * tile,
                    tile - 1,
                    tile - 1
                );
                continue;
            }

            ctx.fillStyle = colors[type] || colors[0];

            ctx.fillRect(
                x * tile,
                y * tile,
                tile - 1,
                tile - 1
            );

            if (type === 2) {
                ctx.fillStyle = "#e9a66a";
                ctx.fillRect(
                    x * tile + 7,
                    y * tile + 7,
                    10,
                    10
                );
            }

            if (type === 3) {
                ctx.fillStyle = "#e0d5ff";
                ctx.beginPath();
                ctx.moveTo(x * tile + 12, y * tile + 4);
                ctx.lineTo(x * tile + 19, y * tile + 12);
                ctx.lineTo(x * tile + 12, y * tile + 20);
                ctx.lineTo(x * tile + 5, y * tile + 12);
                ctx.fill();
            }

            if (type === 4) {
                ctx.fillStyle = "#c7fff0";
                ctx.beginPath();
                ctx.arc(
                    x * tile + 12,
                    y * tile + 12,
                    6,
                    0,
                    Math.PI * 2
                );
                ctx.fill();
            }
        }
    }

    // Cave crawlers
    if (bugsPtr && bugCount > 0) {

        const bugs = new Float64Array(HEAPF64.buffer);
        const base64 = bugsPtr >> 3;

        for (let i = 0; i < bugCount; i++) {

            const x = bugs[base64 + i * 3];
            const y = bugs[base64 + i * 3 + 1];

            ctx.fillStyle = "#ff547d";

            ctx.beginPath();
            ctx.arc(
                x * tile + 12,
                y * tile + 12,
                8,
                0,
                Math.PI * 2
            );
            ctx.fill();

            ctx.fillStyle = "#fff";

            ctx.fillRect(
                x * tile + 8,
                y * tile + 9,
                2,
                2
            );

            ctx.fillRect(
                x * tile + 14,
                y * tile + 9,
                2,
                2
            );
        }
    }

    // Player
    ctx.fillStyle = "#5ff6ff";

    ctx.beginPath();
    ctx.arc(
        px * tile + 12,
        py * tile + 12,
        9,
        0,
        Math.PI * 2
    );
    ctx.fill();

    ctx.fillStyle = "#ffffff";

    ctx.beginPath();

    ctx.arc(
        px * tile + 9,
        py * tile + 9,
        2,
        0,
        Math.PI * 2
    );

    ctx.arc(
        px * tile + 15,
        py * tile + 9,
        2,
        0,
        Math.PI * 2
    );

    ctx.fill();

    ctx.strokeStyle = "rgba(95,246,255,.2)";

    ctx.strokeRect(
        px * tile + 2,
        py * tile + 2,
        20,
        20
    );
});

static void render() {

    js_render(
        MAP_W,
        MAP_H,
        TILE,
        reinterpret_cast<int>(world.data()),
        player.x,
        player.y,
        static_cast<int>(bugs.size()),
        reinterpret_cast<int>(bugs.data())
    );
}
