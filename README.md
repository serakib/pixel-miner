# ⛏️ Pixel Miner — C++ + WebAssembly

**Pixel Miner** is an original browser mining/exploration game written in **C++17** and compiled to **WebAssembly with Emscripten**.

Explore a generated underground map, collect valuable ore, manage your energy, and survive increasingly dangerous cave creatures.

## Features

- C++17 game logic
- WebAssembly via Emscripten
- Procedurally generated mine
- Dirt, rock, copper, crystal and energy cells
- Limited energy system
- Cave crawler enemies
- Increasing depth/difficulty
- Score and local browser high score
- Keyboard controls
- Mouse/touch controls
- Responsive GitHub Pages shell
- No backend
- No database
- No API

## Controls

- **W / A / S / D** — move
- **Arrow keys** — move
- Walk into adjacent rock/ore to mine it
- **Mouse/touch** — tap an adjacent tile
- Collect green energy cells to restore energy

## Requirements

For local development:

- CMake 3.16+
- Emscripten SDK
- A modern browser

Install Emscripten from the official Emscripten SDK documentation.

## Build locally

Activate Emscripten, then:

```bash
emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The generated browser files will be in `build/`.

Serve them with a local HTTP server rather than opening the HTML file directly.

Example:

```bash
python -m http.server 8080 -d build
```

Then open:

```text
http://localhost:8080/pixel_miner.html
```

## GitHub Pages

The repository includes:

```text
.github/workflows/deploy-pages.yml
```

The workflow:

1. Installs Emscripten.
2. Configures CMake.
3. Compiles the C++ game to WebAssembly.
4. Creates a Pages-ready `site/` folder.
5. Deploys it using GitHub Pages.

### Setup

1. Create a GitHub repository.
2. Upload the project.
3. Open **Settings → Pages**.
4. Select **GitHub Actions**.
5. Push to `main`.
6. Wait for the workflow under **Actions** to finish.
7. Open the generated GitHub Pages URL.

## Project structure

```text
pixel-miner-cpp-wasm/
├── .github/
│   └── workflows/
│       └── deploy-pages.yml
├── src/
│   └── main.cpp
├── web/
│   ├── shell.html
│   └── style.css
├── CMakeLists.txt
└── README.md
```

## Saving

The browser stores the best score using `localStorage`.

No user account, server, database, or online leaderboard is required.

## License

MIT
