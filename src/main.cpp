#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include <emscripten.h>

static constexpr int MAP_W = 48;
static constexpr int MAP_H = 34;
static constexpr int TILE = 24;
static constexpr int START_HP = 100;
static constexpr int START_ENERGY = 100;
static constexpr int MAX_STAGE = 5;
static constexpr double STAGE_LENGTH_SECONDS = 60.0;
static constexpr int MAX_BUGS = 12;

enum Tile { DIRT, ROCK, COPPER, CRYSTAL, ENERGY, EXIT, HAZARD };

struct Player { int x=MAP_W/2,y=MAP_H/2,hp=100,energy=100,score=0,copper=0,crystals=0,depth=1; };
struct Bug { double x=0,y=0,speed=.5; };

static Player player;
static std::vector<int> world(MAP_W*MAP_H, DIRT);
static std::vector<unsigned char> revealed(MAP_W*MAP_H, 0);
static std::vector<Bug> bugs;
static std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));
static bool paused=false, gameOver=false;
static int currentStage=1;
static double runStartTime=0,lastFrameTime=0,bugTimer=0;

static int idx(int x,int y){return y*MAP_W+x;}
static bool inside(int x,int y){return x>=0&&x<MAP_W&&y>=0&&y<MAP_H;}
static int ri(int a,int b){return std::uniform_int_distribution<int>(a,b)(rng);}
static double rd(double a,double b){return std::uniform_real_distribution<double>(a,b)(rng);}
static double elapsed(){return std::max(0.0,emscripten_get_now()/1000.0-runStartTime);}
static int stageFor(double t){return std::min(MAX_STAGE,1+static_cast<int>(t/STAGE_LENGTH_SECONDS));}

extern "C" EMSCRIPTEN_KEEPALIVE void move_player(int dx,int dy);
extern "C" EMSCRIPTEN_KEEPALIVE void toggle_pause();
extern "C" EMSCRIPTEN_KEEPALIVE void restart_game();
extern "C" EMSCRIPTEN_KEEPALIVE void js_key(int k);

EM_JS(void, js_setup, (), {
    const c=document.getElementById("gameCanvas");
    if(!c)return;
    window.pixelMiner=window.pixelMiner||{};
    function key(k){if(typeof Module!=="undefined"&&Module._js_key)Module._js_key(k.charCodeAt(0));}
    document.addEventListener("keydown",e=>{
        let k="";
        if(e.key==="ArrowUp"||e.key==="w"||e.key==="W")k="w";
        else if(e.key==="ArrowDown"||e.key==="s"||e.key==="S")k="s";
        else if(e.key==="ArrowLeft"||e.key==="a"||e.key==="A")k="a";
        else if(e.key==="ArrowRight"||e.key==="d"||e.key==="D")k="d";
        else if(e.key==="p"||e.key==="P")k="p";
        if(k){e.preventDefault();key(k);}
    });
    c.addEventListener("click",e=>{
        if(typeof Module==="undefined"||!Module._move_player)return;
        const r=c.getBoundingClientRect(),x=e.clientX-r.left-r.width/2,y=e.clientY-r.top-r.height/2;
        if(Math.abs(x)<12&&Math.abs(y)<12)return;
        if(Math.abs(x)>Math.abs(y))Module._move_player(x>0?1:-1,0);
        else Module._move_player(0,y>0?1:-1);
    });
    window.pixelMiner.restart=()=>{if(typeof Module!=="undefined"&&Module._restart_game)Module._restart_game();else location.reload();};
    window.pixelMiner.pause=()=>{if(typeof Module!=="undefined"&&Module._toggle_pause)Module._toggle_pause();};
});

EM_JS(void, js_update_hud,(int hp,int energy,int score,int copper,int crystals,int depth,int stage,double time),{
    const set=(id,v)=>{const e=document.getElementById(id);if(e)e.textContent=v;};
    set("hpValue",hp);set("energyValue",energy);set("scoreValue",score);
    set("copperValue",copper);set("crystalValue",crystals);set("depthValue",depth);set("stageValue",stage);
    const s=Math.max(0,Math.floor(time));set("timeValue",String(Math.floor(s/60)).padStart(2,"0")+":"+String(s%60).padStart(2,"0"));
    const h=document.getElementById("hpBar"),en=document.getElementById("energyBar");
    if(h)h.style.width=Math.max(0,Math.min(100,hp))+"%";
    if(en)en.style.width=Math.max(0,Math.min(100,energy))+"%";
});

EM_JS(void, js_message,(const char* t),{
    const e=document.getElementById("message");if(!e)return;
    e.textContent=UTF8ToString(t);e.classList.add("show");
    clearTimeout(window.__pm);window.__pm=setTimeout(()=>e.classList.remove("show"),1600);
});

EM_JS(void, js_game_over,(const char* reason,int score),{
    let o=document.getElementById("gameOverOverlay");
    if(!o){
        o=document.createElement("div");o.id="gameOverOverlay";
        o.innerHTML='<div class="game-over-card"><div class="game-over-title">GAME OVER</div><div id="gameOverReason" class="game-over-reason"></div><div class="game-over-score">SCORE: <span id="gameOverScore">0</span></div><button id="gameOverRestart">RESTART</button></div>';
        document.body.appendChild(o);
        document.getElementById("gameOverRestart").onclick=()=>window.pixelMiner.restart();
    }
    document.getElementById("gameOverReason").textContent=UTF8ToString(reason);
    document.getElementById("gameOverScore").textContent=score;
    o.classList.add("visible");
});

EM_JS(void, js_hide_game_over, (), {
    const e=document.getElementById("gameOverOverlay");if(e)e.classList.remove("visible");
});

static void revealAround();
static void mine(int x,int y);
static void over(const char* r);

static void hud(){js_update_hud(player.hp,player.energy,player.score,player.copper,player.crystals,player.depth,currentStage,elapsed());}
static void over(const char* r){if(gameOver)return;gameOver=true;js_game_over(r,player.score);}
static int tile(int x,int y){return inside(x,y)?world[idx(x,y)]:ROCK;}
static void setTile(int x,int y,int t){if(inside(x,y))world[idx(x,y)]=t;}

static bool freeSpecial(int x,int y){
    return inside(x,y)&&!(std::abs(x-player.x)<=3&&std::abs(y-player.y)<=3)&&tile(x,y)==DIRT;
}
static void place(int type,int n){
    for(int p=0;p<n;){
        int x=ri(1,MAP_W-2),y=ri(1,MAP_H-2);
        if(freeSpecial(x,y)){setTile(x,y,type);++p;}
    }
}

static int energyCells(){return 7+currentStage*2;}
static int hazards(){return std::min(20,2+currentStage*2+player.depth/2);}

static void generateWorld(){
    world.assign(MAP_W*MAP_H,DIRT);revealed.assign(MAP_W*MAP_H,0);bugs.clear();
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++){
        if(x==0||y==0||x==MAP_W-1||y==MAP_H-1){setTile(x,y,ROCK);continue;}
        int r=ri(0,99);setTile(x,y,r<22?ROCK:r<31?COPPER:r<35?CRYSTAL:DIRT);
    }
    for(int y=player.y-3;y<=player.y+3;y++)for(int x=player.x-3;x<=player.x+3;x++)setTile(x,y,DIRT);
    place(EXIT,1);place(ENERGY,energyCells());place(HAZARD,hazards());revealAround();
}

static void revealAround(){
    for(int y=player.y-4;y<=player.y+4;y++)for(int x=player.x-4;x<=player.x+4;x++)
        if(inside(x,y)){int dx=x-player.x,dy=y-player.y;if(dx*dx+dy*dy<=16)revealed[idx(x,y)]=1;}
}

static void mine(int x,int y){
    int t=tile(x,y);
    if(t==ROCK){player.energy=std::max(0,player.energy-2);player.score+=2;setTile(x,y,DIRT);}
    else if(t==COPPER){player.energy=std::max(0,player.energy-2);player.copper++;player.score+=10;setTile(x,y,DIRT);}
    else if(t==CRYSTAL){player.energy=std::max(0,player.energy-2);player.crystals++;player.score+=25;setTile(x,y,DIRT);}
    else if(t==ENERGY){player.energy=std::min(START_ENERGY,player.energy+25);player.score+=5;setTile(x,y,DIRT);js_message("ENERGY CELL +25");}
    else if(t==HAZARD){player.hp=std::max(0,player.hp-(8+currentStage*2));player.energy=std::max(0,player.energy-1);setTile(x,y,DIRT);}
}

static void move(int dx,int dy){
    if(paused||gameOver)return;
    if(player.energy<=0){over("ENERGY DEPLETED");return;}
    if(std::abs(dx)+std::abs(dy)!=1)return;
    int nx=player.x+dx,ny=player.y+dy;if(!inside(nx,ny))return;
    int t=tile(nx,ny);
    if(t==ROCK){mine(nx,ny);if(player.energy<=0)over("ENERGY DEPLETED");return;}
    player.energy=std::max(0,player.energy-1);player.x=nx;player.y=ny;revealAround();mine(nx,ny);
    if(player.hp<=0){over("HP DEPLETED");return;}
    if(t==EXIT){player.depth++;player.score+=50*currentStage;player.hp=std::min(START_HP,player.hp+10);generateWorld();return;}
    if(player.energy<=0)over("ENERGY DEPLETED");
}

static int maxBugs(){return std::min(MAX_BUGS,1+currentStage+player.depth/2);}
static double spawnInterval(){return std::max(1.2,6.0-currentStage*.8-player.depth*.08);}

static void spawnBug(){
    if((int)bugs.size()>=maxBugs())return;
    for(int a=0;a<100;a++){int x=ri(2,MAP_W-3),y=ri(2,MAP_H-3);if(std::hypot(x-player.x,y-player.y)<8)continue;bugs.push_back({x+.5,y+.5,rd(.28+currentStage*.08,.55+currentStage*.12)});return;}
}

static void updateBugs(double dt){
    for(auto &b:bugs){
        double dx=player.x+.5-b.x,dy=player.y+.5-b.y,d=std::hypot(dx,dy);
        if(d>.001){double m=std::min(d,b.speed*dt);int nx=(int)std::floor(b.x+dx/d*m),ny=(int)std::floor(b.y+dy/d*m);if(tile(nx,ny)!=ROCK){b.x+=dx/d*m;b.y+=dy/d*m;}}
        if(std::hypot(b.x-(player.x+.5),b.y-(player.y+.5))<.75){player.hp=std::max(0,player.hp-(3+currentStage));if(player.hp<=0){over("HP DEPLETED");return;}}
    }
}

static void render(){
    EM_ASM({
        const c=document.getElementById("gameCanvas"),ctx=c&&c.getContext("2d");if(!ctx)return;
        const W=$0,H=$1,T=$2,cx=$3,cy=$4,px=$5,py=$6;
        ctx.fillStyle="#07090c";ctx.fillRect(0,0,W,H);
        const wp=$7,rp=$8;
        for(let y=0;y<$1/T;y++)for(let x=0;x<$0/T;x++){
            const i=y*(W/T)+x;if(!HEAPU8[rp+i]){ctx.fillStyle="#030405";ctx.fillRect(x*T-cx,y*T-cy,T,T);continue;}
            const t=HEAP32[wp/4+i],colors=["#171a20","#343b46","#a56a2a","#8b5cf6","#22c55e","#38bdf8","#ef4444"];
            ctx.fillStyle=colors[t]||colors[0];ctx.fillRect(x*T-cx,y*T-cy,T,T);
        }
        ctx.fillStyle="#fff";ctx.beginPath();ctx.arc(px*T-cx+T/2,py*T-cy+T/2,8,0,Math.PI*2);ctx.fill();
    },MAP_W*T,MAP_H*T,T,player.x*T-MAP_W*T/2+T/2,player.y*T-MAP_H*T/2+T/2,player.x,player.y,world.data(),revealed.data());
}

extern "C" EMSCRIPTEN_KEEPALIVE void js_key(int k){if(k=='w')move(0,-1);else if(k=='s')move(0,1);else if(k=='a')move(-1,0);else if(k=='d')move(1,0);else if(k=='p')toggle_pause();}
extern "C" EMSCRIPTEN_KEEPALIVE void move_player(int dx,int dy){move(dx,dy);}
extern "C" EMSCRIPTEN_KEEPALIVE void toggle_pause(){if(!gameOver){paused=!paused;js_message(paused?"PAUSED":"RESUMED");}}

extern "C" EMSCRIPTEN_KEEPALIVE void restart_game(){
    player=Player();paused=false;gameOver=false;currentStage=1;bugTimer=0;
    runStartTime=emscripten_get_now()/1000.0;lastFrameTime=runStartTime;js_hide_game_over();generateWorld();js_message("STAGE 1");hud();render();
}

static void loop(){
    double now=emscripten_get_now()/1000.0;if(!lastFrameTime)lastFrameTime=now;
    double dt=std::min(.1,std::max(0.0,now-lastFrameTime));lastFrameTime=now;
    if(!paused&&!gameOver){
        int s=stageFor(now-runStartTime);if(s!=currentStage){currentStage=s;js_message((std::string("STAGE ")+std::to_string(s)).c_str());}
        bugTimer+=dt;if(bugTimer>=spawnInterval()){bugTimer=0;spawnBug();}updateBugs(dt);
        if(player.energy<=0)over("ENERGY DEPLETED");else if(player.hp<=0)over("HP DEPLETED");
    }
    hud();render();
}

int main(){js_setup();restart_game();emscripten_set_main_loop(loop,0,1);return 0;}
