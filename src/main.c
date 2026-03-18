#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/metasprites.h>
#include <stdint.h>
#include "ship.h"
#include "ship2.h"
#include "flag_spr.h"
#include "font.h"
#include "hUGEDriver.h"

extern const hUGESong_t game_music;

/* Safe VBL wrapper — skip dosound if bank 2 is active */
void safe_dosound(void) {
    if (_current_bank == 1) hUGE_dosound();
}

/* RAM buffer for copying bank 2 data before passing to GBDK functions.
   GBDK functions (set_bkg_data etc.) are in bank 1, so they CANNOT be
   called while bank 2 is active. Must: switch to bank 2, memcpy to RAM,
   switch back to bank 1, then call GBDK functions from RAM buffer. */
uint8_t vram_buf[256]; /* reusable copy buffer */
uint8_t font_map_copy[83];

/* Copy N bytes from bank 2 ROM to RAM buffer, then load into VRAM */
void load_tiles_from_bank2(const uint8_t *src, uint16_t count,
                           uint8_t vram_idx, uint8_t is_sprite) {
    uint16_t offset = 0;
    while (offset < count) {
        uint16_t chunk = count - offset;
        uint16_t i;
        if (chunk > 256) chunk = 256;
        /* Copy chunk from bank 2 to RAM */
        SWITCH_ROM(2);
        for (i = 0; i < chunk; i++) vram_buf[i] = src[offset + i];
        SWITCH_ROM(1);
        /* Now call GBDK function from bank 1 */
        if (is_sprite)
            set_sprite_data(vram_idx + (uint8_t)(offset >> 4),
                           (uint8_t)(chunk >> 4), vram_buf);
        else
            set_bkg_data(vram_idx + (uint8_t)(offset >> 4),
                        (uint8_t)(chunk >> 4), vram_buf);
        offset += chunk;
    }
}

void load_font_from_bank2(void) {
    /* font_tiles: font_TILE_COUNT tiles at font_TILE_ORIGIN */
    load_tiles_from_bank2(font_tiles, font_TILE_COUNT * 16,
                          font_TILE_ORIGIN, 0);
}

void copy_font_map(void) {
    uint8_t i;
    SWITCH_ROM(2);
    for (i = 0; i < 83; i++) font_map_copy[i] = font_map[i];
    SWITCH_ROM(1);
}

/* ============ SFX via direct register writes ============ */
/* Uses CH1 (pulse+sweep) and CH4 (noise). Music channels muted during SFX,
   restored after sfx_frames expires. No banking needed — just register writes. */

uint8_t sfx_frames;      /* frames remaining for current SFX */
uint8_t sfx_ch1_active;  /* CH1 used by current SFX */
uint8_t sfx_ch4_active;  /* CH4 used by current SFX */

void sfx_end(void) {
    if (sfx_ch1_active) { hUGE_mute_channel(HT_CH1, HT_CH_PLAY); sfx_ch1_active = 0; }
    if (sfx_ch4_active) { hUGE_mute_channel(HT_CH4, HT_CH_PLAY); sfx_ch4_active = 0; }
    sfx_frames = 0;
}

void sfx_tick(void) {
    if (sfx_frames > 0) {
        sfx_frames--;
        if (sfx_frames == 0) sfx_end();
    }
}

/* --- Individual SFX --- */

void sfx_laser(void) {  /* weapon fire — descending zap */
    sfx_end();
    hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
    sfx_ch1_active = 1;
    NR10_REG = 0x79; /* sweep: time=7, descending, shift=1 */
    NR11_REG = 0x80; /* duty 50% */
    NR12_REG = 0xF3; /* vol 15, decrease step 3 */
    NR13_REG = 0x80; /* freq mid */
    NR14_REG = 0x86; /* trigger, freq high bits=6 */
    sfx_frames = 8;
}

void sfx_hit(void) {  /* ship takes damage — punchy noise burst */
    sfx_end();
    hUGE_mute_channel(HT_CH4, HT_CH_MUTE);
    sfx_ch4_active = 1;
    NR41_REG = 0x00; /* length unused */
    NR42_REG = 0xF1; /* vol 15, decrease step 1 (fast) */
    NR43_REG = 0x51; /* clock shift=5, 7-bit mode, div=1 — crunchy */
    NR44_REG = 0xC0; /* trigger + length enable */
    sfx_frames = 10;
}

void sfx_explode(void) {  /* ship destroyed — long rumble */
    sfx_end();
    hUGE_mute_channel(HT_CH4, HT_CH_MUTE);
    sfx_ch4_active = 1;
    NR41_REG = 0x00;
    NR42_REG = 0xF2; /* vol 15, decrease step 2 */
    NR43_REG = 0x40; /* clock shift=4, 15-bit, div=0 — deep boom */
    NR44_REG = 0x80; /* trigger */
    sfx_frames = 20;
}

void sfx_grab(void) {  /* flag grabbed — quick 3-note ascending */
    sfx_end();
    hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
    sfx_ch1_active = 1;
    NR10_REG = 0x12; /* sweep: time=1, ascending, shift=2 */
    NR11_REG = 0x40; /* duty 25% — brighter tone */
    NR12_REG = 0xF0; /* vol 15, NO decay */
    NR13_REG = 0xC0; /* freq mid-high */
    NR14_REG = 0x85; /* trigger, freq high bits */
    sfx_frames = 15;
}

void sfx_capture(void) {  /* flag captured — triumphant slow rise */
    sfx_end();
    hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
    sfx_ch1_active = 1;
    NR10_REG = 0x21; /* sweep: time=2, ascending, shift=1 (slow rise) */
    NR11_REG = 0x80; /* duty 50% */
    NR12_REG = 0xF0; /* vol 15, NO decay */
    NR13_REG = 0x00; /* start freq low */
    NR14_REG = 0x84; /* trigger, freq high=4 */
    sfx_frames = 40;
}

void sfx_drop(void) {  /* flag dropped — sad descending tone */
    sfx_end();
    hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
    sfx_ch1_active = 1;
    NR10_REG = 0x6A; /* sweep: time=6, descending, shift=2 */
    NR11_REG = 0xC0; /* duty 75% — hollow tone */
    NR12_REG = 0xF0; /* vol 15, NO decay */
    NR13_REG = 0x00; /* freq */
    NR14_REG = 0x87; /* trigger, start high */
    sfx_frames = 25;
}

void sfx_ui(void) {  /* menu select — short high blip */
    sfx_end();
    hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
    sfx_ch1_active = 1;
    NR10_REG = 0x00; /* no sweep */
    NR11_REG = 0x80; /* duty 50% */
    NR12_REG = 0xA1; /* vol 10, decrease step 1 */
    NR13_REG = 0x80; /* freq */
    NR14_REG = 0x87; /* trigger, high freq */
    sfx_frames = 4;
}

/* Forward declarations */
void fire_bullet(uint16_t px, uint16_t py, uint8_t dir, uint8_t weapon, uint8_t team);
void handle_weapon(uint8_t *clip, uint8_t *fire_t, uint8_t *reload_t, uint8_t weapon);
void draw_win_text(uint8_t x, uint8_t y, const char *str);
void set_win_text_attrs(uint8_t x, uint8_t y, uint8_t len);
void update_hud(void);
void unstick(uint32_t *x, uint32_t *y, int16_t *vx, int16_t *vy);

/* ============ Physics constants (8.8 fixed point, tuned for 20fps) ============ */
#define ACCEL       36
#define DIAG_ACCEL  24
#define MAX_SPEED   400

/* ============ Map constants ============ */
#define MAP_TILES   80
#define MAP_PX      (MAP_TILES * 8)

#define HIT_MIN     (-2)
#define HIT_MAX     9
#define NUM_WALL_PALS 6

/* ============ Game states ============ */
#define STATE_MENU    0
#define STATE_GAME    1
#define STATE_CREDITS 2
#define STATE_VICTORY 3

#define CAPS_TO_WIN   5

/* ============ CTF / Flag constants ============ */
/* Base positions in pixels (converted from Spacedout: team0=(400,400), team1=(-400,-400)) */
#define BASE0_X  492
#define BASE0_Y  492
#define BASE1_X  156
#define BASE1_Y  156

#define FLAG_HOME    0
#define FLAG_CARRIED 1
#define FLAG_DROPPED 2

#define FLAG_GRAB_DIST    12   /* pixels: overlap distance for grab/capture */
#define FLAG_RETURN_TIME  400  /* 20 seconds at 20fps */

/* Collision categories (matching Spacedout CollisionFlags.java) */
#define CAT_SOLID    0x0001
#define CAT_T1FLAG   0x0002
#define CAT_T2FLAG   0x0004
#define CAT_T1PLAYER 0x0008
#define CAT_T2PLAYER 0x0010
#define CAT_BULLET   0x0020
#define CAT_PICKUP   0x0040

/* Sprite/tile allocation
   Flags at OAM 0-3 (2 sprites each, 8x16 metasprite) so they render ON TOP of ship.
   Ship metasprite starts at OAM 4. */
#define FLAG_TILE_BASE   32  /* sprite VRAM index for flag tiles (2 tiles) */
#define FLAG_SPR_0       0   /* OAM base for team 0 flag metasprite */
#define FLAG_SPR_1       2   /* OAM base for team 1 flag metasprite */
#define SHIP_SPR_BASE    4   /* OAM base for ship metasprite */

/* ============ Crossbox shape data ============ */
#define TRI_COUNT 13
const int8_t tri_dx[] = { 2, 2, 1, 1, 0, 0,-1,-2,-3,-2,-1, 0, 1};
const int8_t tri_dy[] = {-2,-1, 0, 1, 2, 3, 2, 1, 0, 0,-1,-1,-2};

/* ============ TILE DATA ============ */

const uint8_t bg_tiles[] = {
    /* Tile 0: Empty space (dark with star dots) */
    0x80,0x00, 0x00,0x00, 0x02,0x00, 0x00,0x00,
    0x10,0x00, 0x00,0x00, 0x00,0x00, 0x01,0x00,
    /* Tile 1: Wall block (color 1 border, color 3 fill) */
    0xFF,0x00, 0xFF,0x7E, 0xFF,0x7E, 0xFF,0x7E,
    0xFF,0x7E, 0xFF,0x7E, 0xFF,0x7E, 0xFF,0x00
};

/* ============ COLOR PALETTES ============ */

const uint16_t bg_palettes[] = {
    /* Palette 0: Space */
    RGB(1, 1, 3),   RGB(8, 8, 16),  RGB(0, 0, 0),   RGB(0, 0, 0),
    /* Palette 1: Red */
    RGB(2, 0, 0),   RGB(20, 4, 4),  RGB(0, 0, 0),   RGB(31, 8, 8),
    /* Palette 2: Green */
    RGB(0, 2, 0),   RGB(4, 20, 4),  RGB(0, 0, 0),   RGB(8, 31, 8),
    /* Palette 3: Blue */
    RGB(0, 0, 2),   RGB(4, 4, 20),  RGB(0, 0, 0),   RGB(8, 8, 31),
    /* Palette 4: Yellow */
    RGB(2, 2, 0),   RGB(20, 20, 4), RGB(0, 0, 0),   RGB(31, 31, 8),
    /* Palette 5: Magenta */
    RGB(2, 0, 2),   RGB(20, 4, 20), RGB(0, 0, 0),   RGB(31, 8, 31),
    /* Palette 6: Orange */
    RGB(2, 1, 0),   RGB(20, 12, 4), RGB(0, 0, 0),   RGB(31, 20, 4)
};

/* Font BG palette (palette 7): matches font_palettes color order */
const uint16_t font_bg_pal[] = {
    RGB(1, 1, 3),     /* 0: dark bg (mapped from white bg in source) */
    RGB(31, 31, 31),  /* 1: white text */
    RGB(22, 22, 20),  /* 2: mid gray */
    RGB(0, 0, 0)      /* 3: black outline */
};

/* Direction -> metasprite frame */
const uint8_t dir_to_frame[] = { 0, 5, 3, 7, 1, 6, 2, 4 };

/* ============ GAME STATE ============ */

uint32_t ship_x, ship_y;
int16_t  ship_vx, ship_vy;
uint8_t  ship_dir;
uint16_t cam_x, cam_y;
uint8_t  last_cam_tx, last_cam_ty;
uint8_t  bounce_pal;
uint8_t  game_state;
uint8_t  gamma_caps;   /* Team Gamma (player team) flag captures */
uint8_t  delta_caps;   /* Team Delta flag captures */

/* Flag state */
typedef struct {
    uint16_t x, y;
    uint16_t home_x, home_y;
    uint16_t drop_timer;
    uint8_t  state;
    uint8_t  team;
    int8_t   carrier_type;  /* -1=none, 0=player, 1=bot */
    int8_t   carrier_idx;
} Flag;

Flag flags[2];

/* ============ BOT / NAV CONSTANTS ============ */
#define NUM_BOTS        5
#define BOT_OAM_START   8

/* Flow field: coarse grid BFS from each base, precomputed at startup.
   Each cell stores direction (0-7) toward target, or 0xFF if blocked. */
#define FLOW_CELL  4     /* tiles per coarse cell */
#define FLOW_PX    32    /* pixels per cell (FLOW_CELL * 8) */
#define FLOW_W     20    /* map_tiles / FLOW_CELL */
#define FLOW_H     20
#define FLOW_AT    8     /* marker: cell IS the target */

uint8_t flow_to_base0[FLOW_W * FLOW_H];
uint8_t flow_to_base1[FLOW_W * FLOW_H];
uint16_t bfs_q[FLOW_W * FLOW_H]; /* BFS queue, only used at startup */

/* Bot actions */
#define ACT_GRAB_FLAG   0
#define ACT_CAPTURE     1
#define ACT_DEFEND      2
#define ACT_RETURN_FLAG 3
#define ACT_ROAM        4

typedef struct {
    uint32_t x, y;
    int16_t  vx, vy;
    uint8_t  dir;
    uint8_t  team;
    uint8_t  action;
    int8_t   carrying_flag;
    uint8_t  decide_timer;
    uint16_t target_x, target_y;
    /* Navigation */
    uint16_t best_dist;    /* closest distance to target seen recently */
    uint8_t  stuck_count;  /* frames without progress toward target */
    uint8_t  wander_dir;   /* random direction when stuck (0-7) */
    uint8_t  wander_timer; /* frames to wander before re-evaluating */
    /* Combat */
    uint8_t  hp;
    uint8_t  weapon;
    uint8_t  clip_ammo;
    uint8_t  fire_timer;
    uint8_t  reload_timer;
    uint8_t  alive;
    uint16_t respawn_timer;
} Bot;

Bot bots[NUM_BOTS];

/* ============ WEAPONS & BULLETS ============ */
#define WPN_POLYGUN 0
#define WPN_ZOOMBA  1
#define WPN_PEWPEW  2
#define NUM_WEAPONS 3
#define MAX_HP      65
#define RESPAWN_FRAMES 100  /* 5 seconds at 20fps */
#define MAX_BULLETS 8
#define BULLET_OAM_START 28
#define BULLET_TILE 34     /* sprite VRAM index */
#define BULLET_LIFE 30     /* frames before despawn (20fps) */

/* Weapon stats tuned for 20fps: fire_rate, clip, reload, damage, speed(px/f) */
const uint8_t wpn_rate[]   = { 3,  9,  7};
const uint8_t wpn_clip[]   = {16,  5, 10};
const uint8_t wpn_reload[] = {60, 30, 48};
const uint8_t wpn_dmg[]    = { 5,  8,  6};
const uint8_t wpn_spd[]    = { 9,  6, 12};
const uint8_t wpn_pal[]    = { 2,  3,  4};  /* sprite palette index */

/* Direction unit vectors for bullet movement */
const int8_t dir_vx[] = {0, 1, 1, 1, 0,-1,-1,-1};
const int8_t dir_vy[] = {-1,-1, 0, 1, 1, 1, 0,-1};

/* Weapon name strings for HUD (max 7 chars) */
const char * const wpn_names[] = {"POLYGUN", "ZOOMBA ", "PEW PEW"};

/* Bullet dot tile (2x2 px centered, color 1) */
const uint8_t bullet_tile[] = {
    0x00,0x00, 0x00,0x00, 0x00,0x00, 0x18,0x00,
    0x18,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};

/* Bullet sprite palettes (palettes 2-4: polygun blue, zoomba yellow, pewpew red) */
const uint16_t bullet_pals[] = {
    RGB(0,0,0), RGB(8,4,27),  RGB(0,0,0), RGB(0,0,0),   /* 2: Polygun blue */
    RGB(0,0,0), RGB(31,31,20),RGB(0,0,0), RGB(0,0,0),   /* 3: Zoomba yellow */
    RGB(0,0,0), RGB(29,5,5),  RGB(0,0,0), RGB(0,0,0)    /* 4: Pew Pew red */
};

typedef struct {
    uint16_t x, y;
    int8_t   dx, dy;
    uint8_t  active;
    uint8_t  team;
    uint8_t  weapon;
    uint8_t  life;
} Bullet;

Bullet bullets[MAX_BULLETS];

/* Player combat state */
uint8_t  p_hp, p_weapon, p_clip, p_fire_timer, p_reload_timer;
uint8_t  p_alive;
uint16_t p_respawn_timer;
uint8_t  p_deaths;


/* ============ WALL BITMAP CACHE ============ */
/* 80x80 tile map cached as a bitmap: 800 bytes.
   Turns every wall check from 15+ comparisons into 1 bit lookup. */
uint8_t wall_bitmap[800]; /* 80 rows × 10 bytes (80 bits) per row */

uint8_t is_wall_fast(uint8_t tx, uint8_t ty) {
    if (tx >= MAP_TILES || ty >= MAP_TILES) return 0;
    return (wall_bitmap[(uint16_t)ty * 10 + (tx >> 3)] >> (tx & 7)) & 1;
}

/* ============ PROCEDURAL MAP (used only at startup to build bitmap) ============ */

uint8_t is_wall_tile(uint8_t tx, uint8_t ty) {
    int8_t dx, dy;
    uint8_t i;

    if (tx >= MAP_TILES || ty >= MAP_TILES) return 0;
    if (tx == 0 || tx == MAP_TILES - 1 ||
        ty == 0 || ty == MAP_TILES - 1) return 1;

    dx = (int8_t)tx - 40; dy = (int8_t)ty - 40;
    for (i = 0; i < TRI_COUNT; i++) {
        if (dx == tri_dx[i] && dy == tri_dy[i]) return 1;
    }

    dx = (int8_t)tx - 29; dy = (int8_t)ty - 29;
    if ((dy == 0 && dx >= -2 && dx <= 2) || (dx == 0 && dy >= -2 && dy <= 2)) return 1;
    dx = (int8_t)tx - 51; dy = (int8_t)ty - 29;
    if ((dy == 0 && dx >= -2 && dx <= 2) || (dx == 0 && dy >= -2 && dy <= 2)) return 1;
    dx = (int8_t)tx - 29; dy = (int8_t)ty - 51;
    if ((dy == 0 && dx >= -2 && dx <= 2) || (dx == 0 && dy >= -2 && dy <= 2)) return 1;
    dx = (int8_t)tx - 51; dy = (int8_t)ty - 51;
    if ((dy == 0 && dx >= -2 && dx <= 2) || (dx == 0 && dy >= -2 && dy <= 2)) return 1;

    dx = (int8_t)tx - 40; dy = (int8_t)ty - 19;
    if ((dx == dy || dx == -dy) && dx >= -2 && dx <= 2) return 1;
    dx = (int8_t)tx - 40; dy = (int8_t)ty - 61;
    if ((dx == dy || dx == -dy) && dx >= -2 && dx <= 2) return 1;
    dx = (int8_t)tx - 61; dy = (int8_t)ty - 40;
    if ((dx == dy || dx == -dy) && dx >= -2 && dx <= 2) return 1;
    dx = (int8_t)tx - 19; dy = (int8_t)ty - 40;
    if ((dx == dy || dx == -dy) && dx >= -2 && dx <= 2) return 1;

    dx = (int8_t)tx - 61; dy = (int8_t)ty - 19;
    if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2 &&
        (dx == -2 || dx == 2 || dy == -2 || dy == 2)) return 1;
    dx = (int8_t)tx - 19; dy = (int8_t)ty - 61;
    if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2 &&
        (dx == -2 || dx == 2 || dy == -2 || dy == 2)) return 1;

    return 0;
}

uint8_t get_wall_color(uint8_t tx, uint8_t ty) {
    return ((tx + ty) % NUM_WALL_PALS) + 1;
}

/* ============ SCROLLING BACKGROUND ============ */

void fill_column(uint8_t map_col) {
    uint8_t buf[32];
    uint8_t i, map_row, bg_row;
    uint8_t bg_col = map_col & 31;
    for (i = 0; i < 32; i++) {
        map_row = last_cam_ty + i;
        bg_row = map_row & 31;
        buf[bg_row] = is_wall_fast(map_col, map_row) ? 1 : 0;
    }
    set_bkg_tiles(bg_col, 0, 1, 32, buf);
    for (i = 0; i < 32; i++) {
        map_row = last_cam_ty + i;
        bg_row = map_row & 31;
        buf[bg_row] = buf[bg_row] ? get_wall_color(map_col, map_row) : 0;
    }
    VBK_REG = VBK_ATTRIBUTES;
    set_bkg_tiles(bg_col, 0, 1, 32, buf);
    VBK_REG = VBK_TILES;
}

void fill_row(uint8_t map_row) {
    uint8_t buf[32];
    uint8_t i, map_col, bg_col;
    uint8_t bg_row = map_row & 31;
    for (i = 0; i < 32; i++) {
        map_col = last_cam_tx + i;
        bg_col = map_col & 31;
        buf[bg_col] = is_wall_fast(map_col, map_row) ? 1 : 0;
    }
    set_bkg_tiles(0, bg_row, 32, 1, buf);
    for (i = 0; i < 32; i++) {
        map_col = last_cam_tx + i;
        bg_col = map_col & 31;
        buf[bg_col] = buf[bg_col] ? get_wall_color(map_col, map_row) : 0;
    }
    VBK_REG = VBK_ATTRIBUTES;
    set_bkg_tiles(0, bg_row, 32, 1, buf);
    VBK_REG = VBK_TILES;
}

void init_bg(void) {
    uint8_t buf[32];
    uint8_t i, j, map_tx, map_ty;
    uint8_t start_tx = (uint8_t)(cam_x >> 3);
    uint8_t start_ty = (uint8_t)(cam_y >> 3);

    /* Fill using ring buffer mapping: bg position = map_tile & 31.
       This ensures update_scroll's incremental updates stay consistent. */
    for (j = 0; j < 32; j++) {
        map_ty = start_ty + j;
        for (i = 0; i < 32; i++) {
            map_tx = start_tx + i;
            buf[map_tx & 31] = is_wall_fast(map_tx, map_ty) ? 1 : 0;
        }
        set_bkg_tiles(0, map_ty & 31, 32, 1, buf);
    }
    VBK_REG = VBK_ATTRIBUTES;
    for (j = 0; j < 32; j++) {
        map_ty = start_ty + j;
        for (i = 0; i < 32; i++) {
            map_tx = start_tx + i;
            buf[map_tx & 31] = is_wall_fast(map_tx, map_ty)
                ? get_wall_color(map_tx, map_ty) : 0;
        }
        set_bkg_tiles(0, map_ty & 31, 32, 1, buf);
    }
    VBK_REG = VBK_TILES;
    last_cam_tx = start_tx;
    last_cam_ty = start_ty;
}

/* Cached player pixel position — used by BOTH camera and sprite rendering
   to guarantee they always agree (prevents rounding jitter) */
uint16_t ship_px, ship_py;

void update_camera(void) {
    int16_t cx, cy;
    ship_px = (uint16_t)((ship_x + 128) >> 8);  /* round instead of truncate */
    ship_py = (uint16_t)((ship_y + 128) >> 8);
    cx = (int16_t)ship_px - 80;
    cy = (int16_t)ship_py - 68;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > MAP_PX - 160) cx = MAP_PX - 160;
    if (cy > MAP_PX - 136) cy = MAP_PX - 136;
    cam_x = (uint16_t)cx;
    cam_y = (uint16_t)cy;
}

void update_scroll(void) {
    uint8_t cam_tx = (uint8_t)(cam_x >> 3);
    uint8_t cam_ty = (uint8_t)(cam_y >> 3);
    if (cam_tx > last_cam_tx) {
        fill_column(last_cam_tx + 32);
        last_cam_tx++;
    } else if (cam_tx < last_cam_tx) {
        last_cam_tx--;
        fill_column(last_cam_tx);
    }
    if (cam_ty > last_cam_ty) {
        fill_row(last_cam_ty + 32);
        last_cam_ty++;
    } else if (cam_ty < last_cam_ty) {
        last_cam_ty--;
        fill_row(last_cam_ty);
    }
}

/* ============ COLLISION ============ */

uint8_t check_collision(uint16_t px, uint16_t py) {
    /* 8-point check: 4 corners + 4 edge midpoints.
       Catches diagonal wall clips that 4-corner check misses. */
    uint8_t lx = (uint8_t)((px + HIT_MIN) >> 3);
    uint8_t rx = (uint8_t)((px + HIT_MAX) >> 3);
    uint8_t ty = (uint8_t)((py + HIT_MIN) >> 3);
    uint8_t by = (uint8_t)((py + HIT_MAX) >> 3);
    uint8_t mx = (uint8_t)((px + (HIT_MIN + HIT_MAX) / 2) >> 3);
    uint8_t my = (uint8_t)((py + (HIT_MIN + HIT_MAX) / 2) >> 3);
    return is_wall_fast(lx, ty) || is_wall_fast(rx, ty)     /* top corners */
        || is_wall_fast(lx, by) || is_wall_fast(rx, by)     /* bottom corners */
        || is_wall_fast(mx, ty) || is_wall_fast(mx, by)     /* top/bottom center */
        || is_wall_fast(lx, my) || is_wall_fast(rx, my);    /* left/right center */
}

void bounce_at(uint8_t map_tx, uint8_t map_ty) {
    if (!is_wall_fast(map_tx, map_ty)) return;
    bounce_pal++;
    if (bounce_pal > NUM_WALL_PALS) bounce_pal = 1;
    VBK_REG = VBK_ATTRIBUTES;
    set_bkg_tile_xy(map_tx & 31, map_ty & 31, bounce_pal);
    VBK_REG = VBK_TILES;
}

void do_bounce(uint16_t px, uint16_t py) {
    uint8_t l = (uint8_t)((px + HIT_MIN) >> 3);
    uint8_t r = (uint8_t)((px + HIT_MAX) >> 3);
    uint8_t t = (uint8_t)((py + HIT_MIN) >> 3);
    uint8_t b = (uint8_t)((py + HIT_MAX) >> 3);
    bounce_at(l, t);
    if (r != l) bounce_at(r, t);
    if (b != t) {
        bounce_at(l, b);
        if (r != l) bounce_at(r, b);
    }
}

/* ============ NAV GRAPH & A* ============ */

/* Check if tile-line from (x0,y0) to (x1,y1) is wall-free (Bresenham) */
/* ============ FLOW FIELD (precomputed BFS) ============ */

void build_flow_field(uint8_t *field, uint8_t target_tile_x, uint8_t target_tile_y) {
    uint16_t ci, ni, head, tail;
    uint8_t cx, cy, d;
    int8_t nx_s, ny_s;
    uint8_t nx, ny;
    uint8_t tcx = target_tile_x / FLOW_CELL;
    uint8_t tcy = target_tile_y / FLOW_CELL;

    /* Init all cells as unvisited */
    for (ci = 0; ci < FLOW_W * FLOW_H; ci++) field[ci] = 0xFF;

    /* Seed BFS from target cell */
    field[tcy * FLOW_W + tcx] = FLOW_AT;
    bfs_q[0] = tcy * FLOW_W + tcx;
    head = 0; tail = 1;

    while (head < tail) {
        ci = bfs_q[head++];
        cx = (uint8_t)(ci % FLOW_W);
        cy = (uint8_t)(ci / FLOW_W);

        /* Expand to 8 neighbors */
        for (d = 0; d < 8; d++) {
            nx_s = (int8_t)cx + dir_vx[d];
            ny_s = (int8_t)cy + dir_vy[d];
            if (nx_s < 0 || ny_s < 0 || nx_s >= FLOW_W || ny_s >= FLOW_H) continue;
            nx = (uint8_t)nx_s;
            ny = (uint8_t)ny_s;
            ni = (uint16_t)ny * FLOW_W + nx;
            if (field[ni] != 0xFF) continue; /* already visited */

            /* Check walkability: center tile of the coarse cell */
            if (is_wall_tile(nx * FLOW_CELL + FLOW_CELL / 2,
                             ny * FLOW_CELL + FLOW_CELL / 2)) continue;

            /* Direction from neighbor TOWARD current cell (toward target) */
            field[ni] = (d + 4) & 7; /* opposite of expansion direction */
            bfs_q[tail++] = ni;
        }
    }
}

void build_wall_bitmap(void) {
    uint8_t tx, ty;
    uint16_t ri;
    for (ri = 0; ri < 800; ri++) wall_bitmap[ri] = 0;
    for (ty = 0; ty < MAP_TILES; ty++) {
        for (tx = 0; tx < MAP_TILES; tx++) {
            if (is_wall_tile(tx, ty)) {
                wall_bitmap[(uint16_t)ty * 10 + (tx >> 3)] |= (1 << (tx & 7));
            }
        }
    }
}

void build_flow_fields(void) {
    /* Base 0 (gamma) at tile ~61,61. Base 1 (delta) at tile ~19,19. */
    build_flow_field(flow_to_base0, BASE0_X / 8, BASE0_Y / 8);
    build_flow_field(flow_to_base1, BASE1_X / 8, BASE1_Y / 8);
}

/* ============ BOT AI ============ */

/* Simple direct-to-target decision */
void bot_decide(uint8_t bi) {
    Bot *b = &bots[bi];
    uint8_t ef = (b->team == 0) ? 1 : 0;  /* enemy flag */
    uint8_t of = b->team;                   /* own flag */

    /* Priority 1: carrying enemy flag → go capture */
    if (b->carrying_flag >= 0) {
        b->action = ACT_CAPTURE;
        b->target_x = flags[of].home_x;
        b->target_y = flags[of].home_y;
        return;
    }

    /* Priority 2: own flag is NOT at home → intercept the carrier or recover it.
       This is the key anti-stalemate behavior: chase whoever has your flag. */
    if (flags[of].state != FLAG_HOME) {
        /* Target the flag's current position (follows the carrier) */
        b->action = ACT_RETURN_FLAG;
        b->target_x = flags[of].x;
        b->target_y = flags[of].y;
        return;
    }

    /* Priority 3: enemy flag available → go grab it */
    if (flags[ef].state != FLAG_CARRIED) {
        b->action = ACT_GRAB_FLAG;
        b->target_x = flags[ef].x;
        b->target_y = flags[ef].y;
        return;
    }

    /* Priority 4: enemy flag is being carried by a teammate → escort them.
       Go toward own base where the carrier is heading. */
    if (flags[ef].state == FLAG_CARRIED && flags[ef].carrier_type == 1 &&
        bots[flags[ef].carrier_idx].team == b->team) {
        b->action = ACT_DEFEND;
        b->target_x = flags[of].home_x;
        b->target_y = flags[of].home_y;
        return;
    }

    /* Default: go after enemy flag carrier (intercept) */
    b->action = ACT_DEFEND;
    b->target_x = flags[ef].x;
    b->target_y = flags[ef].y;
}

void bot_flag_logic(uint8_t bi) {
    Bot *b = &bots[bi];
    uint16_t bpx = (uint16_t)(b->x >> 8) + 4;
    uint16_t bpy = (uint16_t)(b->y >> 8) + 4;
    uint8_t ef = (b->team == 0) ? 1 : 0;
    uint8_t of = b->team;
    int16_t dx, dy;

    /* Grab enemy flag */
    if (flags[ef].state != FLAG_CARRIED && b->carrying_flag < 0) {
        dx = (int16_t)bpx - (int16_t)flags[ef].x;
        dy = (int16_t)bpy - (int16_t)flags[ef].y;
        if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
            dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
            flags[ef].state = FLAG_CARRIED;
            flags[ef].carrier_type = 1;
            flags[ef].carrier_idx = bi;
            b->carrying_flag = ef;
            b->decide_timer = 0;
            sfx_grab();
        }
    }

    /* Return own flag if dropped */
    if (flags[of].state == FLAG_DROPPED) {
        dx = (int16_t)bpx - (int16_t)flags[of].x;
        dy = (int16_t)bpy - (int16_t)flags[of].y;
        if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
            dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
            flags[of].state = FLAG_HOME;
            flags[of].x = flags[of].home_x;
            flags[of].y = flags[of].home_y;
            flags[of].drop_timer = 0;
        }
    }

    /* Capture: at own base with enemy flag, own flag home */
    if (b->carrying_flag >= 0 && flags[of].state == FLAG_HOME) {
        dx = (int16_t)bpx - (int16_t)flags[of].home_x;
        dy = (int16_t)bpy - (int16_t)flags[of].home_y;
        if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
            dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
            flags[b->carrying_flag].state = FLAG_HOME;
            flags[b->carrying_flag].x = flags[b->carrying_flag].home_x;
            flags[b->carrying_flag].y = flags[b->carrying_flag].home_y;
            b->carrying_flag = -1;
            /* Score the capture for the bot's team */
            if (b->team == 0) { gamma_caps++; sfx_capture(); }
            else { delta_caps++; sfx_capture(); }
        }
    }
}

void update_bot(uint8_t bi) {
    Bot *b = &bots[bi];
    uint16_t bpx, bpy;
    int16_t dx, dy, ax, ay;
    int16_t thrust;
    uint32_t save;
    uint8_t diag;

    /* Respawn handling */
    if (!b->alive) {
        if (b->respawn_timer > 0) {
            b->respawn_timer--;
        } else {
            b->alive = 1;
            b->hp = MAX_HP;
            b->clip_ammo = wpn_clip[b->weapon];
            b->x = (uint32_t)((b->team == 0) ? BASE0_X : BASE1_X) << 8;
            b->y = (uint32_t)((b->team == 0) ? BASE0_Y : BASE1_Y) << 8;
            b->vx = 0; b->vy = 0;
        }
        return;
    }

    /* Push out of wall if stuck inside one (check every other update) */
    if (b->decide_timer & 1) unstick(&b->x, &b->y, &b->vx, &b->vy);

    /* Weapon tick */
    handle_weapon(&b->clip_ammo, &b->fire_timer, &b->reload_timer, b->weapon);

    /* Decision every ~1 second (20fps) */
    if (b->decide_timer == 0) {
        b->decide_timer = 20;
        bot_decide(bi);
    }
    b->decide_timer--;

    /* ---- Flow field steering + greedy fallback + rotational probe ---- */
    bpx = (uint16_t)(b->x >> 8);
    bpy = (uint16_t)(b->y >> 8);
    dx = (int16_t)b->target_x - (int16_t)bpx;
    dy = (int16_t)b->target_y - (int16_t)bpy;

    {
        uint8_t ideal_dir = 0;
        uint8_t chosen_dir = 0;
        uint8_t found = 0;
        uint8_t use_flow = 0;
        uint8_t *field = NULL;
        static const int8_t rot[] = {0, 1, -1, 2, -2, 3, -3, 4};

        /* 1) Try flow field for base-targeted actions */
        if (b->action == ACT_CAPTURE || b->action == ACT_DEFEND) {
            field = (b->team == 0) ? flow_to_base0 : flow_to_base1;
        } else if (b->action == ACT_GRAB_FLAG) {
            uint8_t ef = (b->team == 0) ? 1 : 0;
            if (flags[ef].state == FLAG_HOME)
                field = (ef == 0) ? flow_to_base0 : flow_to_base1;
        }

        if (field) {
            uint8_t fcx = bpx / FLOW_PX;
            uint8_t fcy = bpy / FLOW_PX;
            if (fcx < FLOW_W && fcy < FLOW_H) {
                uint8_t fd = field[fcy * FLOW_W + fcx];
                if (fd < 8) { ideal_dir = fd; use_flow = 1; }
            }
        }

        /* 2) If no flow field, compute greedy ideal direction */
        if (!use_flow) {
            if (dx > 8 && dy < -8)       ideal_dir = 1;
            else if (dx > 8 && dy > 8)   ideal_dir = 3;
            else if (dx < -8 && dy < -8) ideal_dir = 7;
            else if (dx < -8 && dy > 8)  ideal_dir = 5;
            else if (dx > 8)             ideal_dir = 2;
            else if (dx < -8)            ideal_dir = 6;
            else if (dy < -8)            ideal_dir = 0;
            else if (dy > 8)             ideal_dir = 4;
            else                         ideal_dir = b->dir;
        }

        /* 3) Stuck detection — works in both flow and greedy modes.
              Wall hits (counted in collision) accelerate the counter. */
        {
            uint16_t cur_dist = (uint16_t)((dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy));
            if (cur_dist + 8 < b->best_dist) {
                b->best_dist = cur_dist;
                b->stuck_count = 0;
                b->wander_timer = 0;
            }
            /* stuck_count also incremented by wall hits in collision code */
            if (b->stuck_count > 10) {
                b->stuck_count = 0;
                b->best_dist = 0xFFFF;
                b->wander_dir = (ideal_dir + 4) & 7;
                b->wander_timer = 15;
            }
            if (b->wander_timer > 0) {
                ideal_dir = b->wander_dir;
                b->wander_timer--;
                if (b->wander_timer == 10) {
                    b->wander_dir = (b->wander_dir + 2) & 7;
                }
            }
        }

        /* 4) 5-tile lookahead: score directions by clear distance ahead.
              Check ideal and 4 adjacent directions at 1-5 tile distances.
              Pick the closest-to-ideal with the deepest clearance. */
        {
            uint8_t tx8 = (uint8_t)((bpx + 4) >> 3);
            uint8_t ty8 = (uint8_t)((bpy + 4) >> 3);
            uint8_t best_score = 0;
            uint8_t best_dir2 = ideal_dir;
            uint8_t di, dist;
            static const int8_t try_offsets[] = {0, 1, -1, 2, -2};

            for (di = 0; di < 5; di++) {
                uint8_t try_dir = (ideal_dir + try_offsets[di]) & 7;
                uint8_t score = 0;
                for (dist = 1; dist <= 5; dist++) {
                    uint8_t cx = (uint8_t)((int8_t)tx8 + dir_vx[try_dir] * dist);
                    uint8_t cy = (uint8_t)((int8_t)ty8 + dir_vy[try_dir] * dist);
                    if (!is_wall_fast(cx, cy))
                        score++;
                    else
                        break;
                }
                if (score > best_score) {
                    best_score = score;
                    best_dir2 = try_dir;
                    if (score == 5) break; /* full clearance, take it */
                }
            }
            chosen_dir = best_dir2;
            found = (best_score > 0);
        }

        if (!found) chosen_dir = (ideal_dir + 4) & 7;

        ax = dir_vx[chosen_dir];
        ay = dir_vy[chosen_dir];
        b->dir = chosen_dir;
    }

    diag = (ax != 0 && ay != 0) ? 1 : 0;
    thrust = diag ? DIAG_ACCEL : ACCEL;
    ax *= thrust; ay *= thrust;

    /* Physics */
    b->vx += ax; b->vy += ay;
    b->vx -= b->vx >> 6; b->vy -= b->vy >> 6;
    if (b->vx > MAX_SPEED) b->vx = MAX_SPEED;
    if (b->vx < -MAX_SPEED) b->vx = -MAX_SPEED;
    if (b->vy > MAX_SPEED) b->vy = MAX_SPEED;
    if (b->vy < -MAX_SPEED) b->vy = -MAX_SPEED;

    /* Bot collision: SLIDE along walls */
    save = b->x; b->x += b->vx;
    bpx = (uint16_t)(b->x >> 8); bpy = (uint16_t)(b->y >> 8);
    if (check_collision(bpx, bpy)) {
        b->x = save; b->vx = 0;
        if (b->wander_timer == 0) b->stuck_count += 3;
    }
    save = b->y; b->y += b->vy;
    bpx = (uint16_t)(b->x >> 8); bpy = (uint16_t)(b->y >> 8);
    if (check_collision(bpx, bpy)) {
        b->y = save; b->vy = 0;
        if (b->wander_timer == 0) b->stuck_count += 3;
    }

    /* Flag interaction */
    bot_flag_logic(bi);

    /* Update carried flag position */
    if (b->carrying_flag >= 0) {
        flags[b->carrying_flag].x = (uint16_t)(b->x >> 8) + 4;
        flags[b->carrying_flag].y = (uint16_t)(b->y >> 8) + 4;
    }

    /* Bot firing: shoot at nearest visible enemy within 100px */
    if (b->fire_timer == 0 && b->clip_ammo > 0) {
        uint8_t ei;
        int16_t best_d = 100;
        int16_t edx, edy;
        uint8_t has_target = 0;
        bpx = (uint16_t)(b->x >> 8) + 4;
        bpy = (uint16_t)(b->y >> 8) + 4;

        /* Check player as target */
        if (b->team != 0 && p_alive) {
            edx = (int16_t)((uint16_t)(ship_x >> 8) + 4) - (int16_t)bpx;
            edy = (int16_t)((uint16_t)(ship_y >> 8) + 4) - (int16_t)bpy;
            dx = edx > 0 ? edx : -edx;
            dy = edy > 0 ? edy : -edy;
            if (dx + dy < best_d) { best_d = dx + dy; has_target = 1; }
        }
        /* Check enemy bots */
        for (ei = 0; ei < NUM_BOTS; ei++) {
            if (ei == bi || bots[ei].team == b->team || !bots[ei].alive) continue;
            edx = (int16_t)((uint16_t)(bots[ei].x >> 8) + 4) - (int16_t)bpx;
            edy = (int16_t)((uint16_t)(bots[ei].y >> 8) + 4) - (int16_t)bpy;
            dx = edx > 0 ? edx : -edx;
            dy = edy > 0 ? edy : -edy;
            if (dx + dy < best_d) { best_d = dx + dy; has_target = 1; }
        }
        if (has_target) {
            fire_bullet(bpx, bpy, b->dir, b->weapon, b->team);
            b->clip_ammo--;
            b->fire_timer = wpn_rate[b->weapon];
            if (b->clip_ammo == 0) b->reload_timer = wpn_reload[b->weapon];
        }
    }
}

/* ============ SHIP-TO-SHIP COLLISION ============ */

#define SHIP_COLLIDE_DIST 10  /* pixels: overlap threshold for ship bodies */
#define PUSH_STRENGTH 64      /* 8.8 fixed-point push per frame */

/* Push two ships apart if overlapping. px/py are centers.
   Modifies positions and velocities of both entities. */
/* Push entity out of wall if stuck inside one */
void unstick(uint32_t *x, uint32_t *y, int16_t *vx, int16_t *vy) {
    uint16_t px = (uint16_t)(*x >> 8);
    uint16_t py = (uint16_t)(*y >> 8);
    uint8_t d, dist;
    if (!check_collision(px, py)) return; /* not stuck */
    /* Try pushing outward in each direction at increasing distances */
    for (dist = 8; dist <= 40; dist += 8) {
        for (d = 0; d < 8; d++) {
            uint16_t tx = (uint16_t)((int16_t)px + dir_vx[d] * dist);
            uint16_t ty = (uint16_t)((int16_t)py + dir_vy[d] * dist);
            if (!check_collision(tx, ty)) {
                *x = (uint32_t)tx << 8;
                *y = (uint32_t)ty << 8;
                *vx = 0; *vy = 0; /* kill momentum */
                return;
            }
        }
    }
}

void push_apart(uint32_t *x1, uint32_t *y1, int16_t *vx1, int16_t *vy1,
                uint32_t *x2, uint32_t *y2, int16_t *vx2, int16_t *vy2) {
    int16_t cx1 = (int16_t)(*x1 >> 8) + 4;
    int16_t cy1 = (int16_t)(*y1 >> 8) + 4;
    int16_t cx2 = (int16_t)(*x2 >> 8) + 4;
    int16_t cy2 = (int16_t)(*y2 >> 8) + 4;
    int16_t dx = cx1 - cx2;
    int16_t dy = cy1 - cy2;
    int16_t adx = dx > 0 ? dx : -dx;
    int16_t ady = dy > 0 ? dy : -dy;

    if (adx < SHIP_COLLIDE_DIST && ady < SHIP_COLLIDE_DIST) {
        /* Velocity push only — no direct position modification */
        if (adx >= ady) {
            int16_t push = (dx > 0) ? PUSH_STRENGTH : -PUSH_STRENGTH;
            *vx1 += push; *vx2 -= push;
        } else {
            int16_t push = (dy > 0) ? PUSH_STRENGTH : -PUSH_STRENGTH;
            *vy1 += push; *vy2 -= push;
        }
    }
}

void resolve_ship_collisions(void) {
    uint8_t i, j;

    if (!p_alive) goto bots_only;

    /* Player vs all bots */
    for (i = 0; i < NUM_BOTS; i++) {
        if (!bots[i].alive) continue;
        push_apart(&ship_x, &ship_y, &ship_vx, &ship_vy,
                   &bots[i].x, &bots[i].y, &bots[i].vx, &bots[i].vy);
    }

bots_only:
    /* Bot vs bot */
    for (i = 0; i < NUM_BOTS - 1; i++) {
        if (!bots[i].alive) continue;
        for (j = i + 1; j < NUM_BOTS; j++) {
            if (!bots[j].alive) continue;
            push_apart(&bots[i].x, &bots[i].y, &bots[i].vx, &bots[i].vy,
                       &bots[j].x, &bots[j].y, &bots[j].vx, &bots[j].vy);
        }
    }
}

/* ============ COMBAT SYSTEM ============ */

void fire_bullet(uint16_t px, uint16_t py, uint8_t dir, uint8_t weapon, uint8_t team) {
    uint8_t i, oam;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x = px;
            bullets[i].y = py;
            bullets[i].dx = dir_vx[dir] * (int8_t)wpn_spd[weapon];
            bullets[i].dy = dir_vy[dir] * (int8_t)wpn_spd[weapon];
            bullets[i].active = 1;
            bullets[i].team = team;
            bullets[i].weapon = weapon;
            bullets[i].life = BULLET_LIFE;
            /* Set tile + palette once at spawn */
            oam = BULLET_OAM_START + i;
            set_sprite_tile(oam, BULLET_TILE);
            set_sprite_prop(oam, S_PAL(wpn_pal[weapon]));
            return;
        }
    }
}

void update_bullets(void) {
    uint8_t i, bi;
    uint16_t bx, by;
    int16_t dx, dy;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;

        bullets[i].x += bullets[i].dx;
        bullets[i].y += bullets[i].dy;
        bullets[i].life--;

        bx = bullets[i].x;
        by = bullets[i].y;

        /* Despawn if expired or hit wall */
        if (bullets[i].life == 0) {
            bullets[i].active = 0;
            continue;
        }
        {
            uint8_t wtx = (uint8_t)(bx >> 3);
            uint8_t wty = (uint8_t)(by >> 3);
            if (is_wall_fast(wtx, wty)) {
                /* Change wall color */
                bounce_pal++;
                if (bounce_pal > NUM_WALL_PALS) bounce_pal = 1;
                VBK_REG = VBK_ATTRIBUTES;
                set_bkg_tile_xy(wtx & 31, wty & 31, bounce_pal);
                VBK_REG = VBK_TILES;
                bullets[i].active = 0;
                continue;
            }
        }

        /* Hit player? (bullets from other team) */
        if (bullets[i].team != 0 && p_alive) {
            uint16_t pcx = (uint16_t)(ship_x >> 8) + 4;
            uint16_t pcy = (uint16_t)(ship_y >> 8) + 4;
            dx = (int16_t)bx - (int16_t)pcx;
            dy = (int16_t)by - (int16_t)pcy;
            if (dx > -8 && dx < 8 && dy > -8 && dy < 8) {
                uint8_t dmg = wpn_dmg[bullets[i].weapon];
                if (p_hp <= dmg) {
                    p_hp = 0; p_alive = 0;
                    p_respawn_timer = RESPAWN_FRAMES;
                    sfx_explode();
                    p_deaths++;
                    /* Drop carried flag */
                    if (flags[1].carrier_type == 0) {
                        flags[1].state = FLAG_DROPPED;
                        flags[1].carrier_type = -1;
                        flags[1].drop_timer = 0;
                        sfx_drop();
                    }
                } else {
                    p_hp -= dmg;
                    sfx_hit();
                }
                bullets[i].active = 0;
                continue;
            }
        }

        /* Hit bots? (coarse pre-filter: skip if >32px away on either axis) */
        for (bi = 0; bi < NUM_BOTS; bi++) {
            if (!bots[bi].alive || bots[bi].team == bullets[i].team) continue;
            dx = (int16_t)bx - (int16_t)(bots[bi].x >> 8) - 4;
            if (dx > 32 || dx < -32) continue;
            dy = (int16_t)by - (int16_t)(bots[bi].y >> 8) - 4;
            if (dx > -8 && dx < 8 && dy > -8 && dy < 8) {
                uint8_t dmg = wpn_dmg[bullets[i].weapon];
                if (bots[bi].hp <= dmg) {
                    bots[bi].hp = 0;
                    bots[bi].alive = 0;
                    sfx_explode();
                    bots[bi].respawn_timer = RESPAWN_FRAMES;
                    bots[bi].vx = 0; bots[bi].vy = 0;
                    /* Drop carried flag */
                    if (bots[bi].carrying_flag >= 0) {
                        uint8_t fi = bots[bi].carrying_flag;
                        flags[fi].state = FLAG_DROPPED;
                        flags[fi].carrier_type = -1;
                        flags[fi].drop_timer = 0;
                        bots[bi].carrying_flag = -1;
                        sfx_drop();
                    }
                    /* Kill — no score, only captures count */
                } else {
                    bots[bi].hp -= dmg;
                    /* SFX if hit bot is on screen */
                    {
                        int16_t sx = (int16_t)(bots[bi].x >> 8) - (int16_t)cam_x;
                        int16_t sy = (int16_t)(bots[bi].y >> 8) - (int16_t)cam_y;
                        (void)sx; (void)sy;
                    }
                }
                bullets[i].active = 0;
                break;
            }
        }
    }
}

void render_bullets(void) {
    uint8_t i;
    int16_t sx, sy;
    for (i = 0; i < MAX_BULLETS; i++) {
        uint8_t oam = BULLET_OAM_START + i;
        if (!bullets[i].active) { move_sprite(oam, 0, 0); continue; }
        sx = (int16_t)bullets[i].x - (int16_t)cam_x;
        sy = (int16_t)bullets[i].y - (int16_t)cam_y;
        if (sx >= -4 && sx < 164 && sy >= -4 && sy < 140)
            move_sprite(oam, (uint8_t)(sx + 8), (uint8_t)(sy + 16));
        else
            move_sprite(oam, 0, 0);
    }
}

void handle_weapon(uint8_t *clip, uint8_t *fire_t, uint8_t *reload_t,
                   uint8_t weapon) {
    /* Reload logic */
    if (*clip == 0) {
        if (*reload_t > 0) {
            (*reload_t)--;
        } else {
            *clip = wpn_clip[weapon];
        }
    }
    /* Fire cooldown */
    if (*fire_t > 0) (*fire_t)--;
}

/* Update HUD bottom bar: "WEAPON AM G:X D:X" or "RELOAD    G:X D:X" */
/* HUD dirty tracking — only redraw when values change */
uint8_t hud_prev_state; /* packed: weapon<<4 | alive<<3 | clip high bits */
uint8_t hud_prev_clip;
uint8_t hud_prev_scores; /* packed: gamma_caps<<4 | delta_caps */
uint8_t hud_prev_respawn;

void update_hud(void) {
    uint8_t state = (p_weapon << 4) | (p_alive << 3) | (p_clip == 0 ? 4 : 0);
    uint8_t scores = ((gamma_caps > 9 ? 9 : gamma_caps) << 4) | (delta_caps > 9 ? 9 : delta_caps);
    uint8_t respawn_s = 0;
    uint8_t need = 0;

    if (!p_alive) {
        respawn_s = (uint8_t)(p_respawn_timer / 20) + 1;
        if (respawn_s > 5) respawn_s = 5;
        if (respawn_s != hud_prev_respawn) need = 1;
    }
    if (state != hud_prev_state || p_clip != hud_prev_clip || scores != hud_prev_scores)
        need = 1;
    if (!need) return;

    hud_prev_state = state;
    hud_prev_clip = p_clip;
    hud_prev_scores = scores;
    hud_prev_respawn = respawn_s;

    /* Clear left side (weapon area, 12 chars) */
    {
        uint8_t i;
        for (i = 0; i < 12; i++)
            set_win_tile_xy(i, 0, font_map_copy[0]);
    }

    if (!p_alive) {
        draw_win_text(4, 0, "RESPAWN ");
        set_win_tile_xy(12, 0, font_map_copy[16 + respawn_s]);
    } else if (p_clip == 0) {
        draw_win_text(0, 0, "RELOAD");
    } else {
        draw_win_text(0, 0, wpn_names[p_weapon]);
        if (p_clip >= 10)
            set_win_tile_xy(8, 0, font_map_copy[16 + p_clip / 10]);
        set_win_tile_xy(9, 0, font_map_copy[16 + p_clip % 10]);
    }

    /* Scores (right side) — only update digits */
    draw_win_text(13, 0, "G:");
    set_win_tile_xy(15, 0, font_map_copy[16 + (scores >> 4)]);
    draw_win_text(17, 0, "D:");
    set_win_tile_xy(19, 0, font_map_copy[16 + (scores & 0x0F)]);
}

/* ============ TEXT RENDERING ============ */

/* Custom font character order (NOT standard ASCII):
   0=space, 1=!, 2=", 3=(c), 4=_, 5=%, 6=heart, 7=', 8=(, 9=), 10=*,
   11=+, 12=comma, 13=-, 14=., 15=/, 16-25=0-9, 26=:, 27=leftarrow,
   28=<, 29==, 30=>, 31=?, 32=@, 33-58=A-Z, 59+=accented (unused) */
uint8_t char_to_font(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;  /* uppercase only font */
    if (c >= 'A' && c <= 'Z') return 33 + (c - 'A');
    if (c >= '0' && c <= '9') return 16 + (c - '0');
    switch (c) {
        case ' ': return 0;
        case '!': return 1;
        case '"': return 2;
        case '_': return 4;
        case '%': return 5;
        case '\'': return 7;
        case '(': return 8;
        case ')': return 9;
        case '*': return 10;
        case '+': return 11;
        case ',': return 12;
        case '-': return 13;
        case '.': return 14;
        case '/': return 15;
        case ':': return 26;
        case '<': return 28;
        case '=': return 29;
        case '>': return 30;
        case '?': return 31;
        case '@': return 32;
        default:  return 0;
    }
}

/* Draw text on BG tilemap. Spaces restore star tile (tile 0, palette 0).
   Non-space chars use font tile with palette 7. Handles attrs inline. */
void draw_text(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        if (*str == ' ') {
            set_bkg_tile_xy(x, y, 0);
            VBK_REG = VBK_ATTRIBUTES;
            set_bkg_tile_xy(x, y, 0);
            VBK_REG = VBK_TILES;
        } else {
            set_bkg_tile_xy(x, y, font_map_copy[char_to_font(*str)]);
            VBK_REG = VBK_ATTRIBUTES;
            set_bkg_tile_xy(x, y, 7);
            VBK_REG = VBK_TILES;
        }
        x++;
        str++;
    }
}

/* Draw text on Window tilemap */
void draw_win_text(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        set_win_tile_xy(x, y, font_map_copy[char_to_font(*str)]);
        x++;
        str++;
    }
}

void set_win_text_attrs(uint8_t x, uint8_t y, uint8_t len) {
    uint8_t i;
    VBK_REG = VBK_ATTRIBUTES;
    for (i = 0; i < len; i++)
        set_win_tile_xy(x + i, y, 7);
    VBK_REG = VBK_TILES;
}

void clear_bg(void) {
    uint8_t buf[32];
    uint8_t j;
    for (j = 0; j < 32; j++) buf[j] = 0;
    for (j = 0; j < 32; j++) set_bkg_tiles(0, j, 32, 1, buf);
    VBK_REG = VBK_ATTRIBUTES;
    for (j = 0; j < 32; j++) set_bkg_tiles(0, j, 32, 1, buf);
    VBK_REG = VBK_TILES;
    SCX_REG = 0;
    SCY_REG = 0;
}

void hide_all_sprites(void) {
    uint8_t i;
    for (i = 0; i < 40; i++) move_sprite(i, 0, 0);
}

/* ============ MENU ============ */

uint8_t menu_music_playing = 0; /* global: tracks if menu music is active */

void run_menu(void) {
    uint8_t selected = 0;
    uint8_t keys, old_keys = 0xFF;

    NR51_REG = 0x00; /* stop VBL bank switching during data load */
    DISPLAY_OFF;

    set_bkg_data(0, 2, bg_tiles);
    load_font_from_bank2();
    set_bkg_palette(0, 7, bg_palettes);
    set_bkg_palette(7, 1, font_bg_pal);

    clear_bg();
    hide_all_sprites();

    draw_text(7, 4, "VOIDGUN");
    draw_text(9, 6, "GBC");
    draw_text(5, 10, "> NEW GAME");
    draw_text(5, 12, "  CREDITS");

    SHOW_BKG;
    HIDE_SPRITES;
    DISPLAY_ON;
    NR51_REG = 0xFF; /* unmute */
    if (!menu_music_playing) {
        hUGE_init(&game_music);
        menu_music_playing = 1;
    }

    while (1) {
        vsync();
        keys = joypad();

        if ((keys & J_DOWN) && !(old_keys & J_DOWN) && selected == 0) {
            selected = 1;
            draw_text(5, 10, "  NEW GAME");
            draw_text(5, 12, "> CREDITS");
        }
        if ((keys & J_UP) && !(old_keys & J_UP) && selected == 1) {
            selected = 0;
            draw_text(5, 10, "> NEW GAME");
            draw_text(5, 12, "  CREDITS");
        }
        if ((keys & (J_A | J_START)) && !(old_keys & (J_A | J_START))) {
            sfx_ui();
            if (!selected) menu_music_playing = 0;
            game_state = selected ? STATE_CREDITS : STATE_GAME;
            return;
        }
        old_keys = keys;
    }
}

/* ============ CREDITS ============ */

void run_credits(void) {
    uint8_t keys, old_keys = 0xFF;

    NR51_REG = 0x00; /* stop VBL bank switching during data load */
    DISPLAY_OFF;

    set_bkg_data(0, 2, bg_tiles);
    load_font_from_bank2();
    set_bkg_palette(0, 7, bg_palettes);
    set_bkg_palette(7, 1, font_bg_pal);

    clear_bg();
    hide_all_sprites();

    draw_text(3, 2, "PROGRAMMING BY");
    draw_text(2, 3, "MICHAEL WHITLOCK");
    draw_text(5, 5, "GRAPHICS BY");
    draw_text(2, 6, "MICHAEL WHITLOCK");
    draw_text(6, 8, "MUSIC BY");
    draw_text(5, 9, "BEATSCRIBE");
    draw_text(7, 11, "SFX BY");
    draw_text(4, 12, "TIPTOPTOMCAT");

    SHOW_BKG;
    HIDE_SPRITES;
    DISPLAY_ON;
    NR51_REG = 0xFF;

    while (1) {
        vsync();
        keys = joypad();
        if ((keys & J_B) && !(old_keys & J_B)) {
            sfx_ui();
            game_state = STATE_MENU;
            return;
        }
        old_keys = keys;
    }
}

/* ============ VICTORY ============ */

uint8_t winning_team; /* set before entering STATE_VICTORY */

void run_victory(void) {
    uint8_t keys, old_keys = 0xFF;

    NR51_REG = 0x00;
    DISPLAY_OFF;

    set_bkg_data(0, 2, bg_tiles);
    load_font_from_bank2();
    set_bkg_palette(0, 7, bg_palettes);
    set_bkg_palette(7, 1, font_bg_pal);

    clear_bg();
    hide_all_sprites();

    if (winning_team == 0) {
        draw_text(6, 6, "VICTORY!");
    } else {
        draw_text(6, 6, "DEFEAT!");
    }

    draw_text(5, 11, "G:");
    {
        uint8_t gc = gamma_caps > 9 ? 9 : gamma_caps;
        uint8_t dc = delta_caps > 9 ? 9 : delta_caps;
        set_bkg_tile_xy(7, 11, font_map_copy[16 + gc]);
        VBK_REG = VBK_ATTRIBUTES;
        set_bkg_tile_xy(7, 11, 7);
        VBK_REG = VBK_TILES;
        draw_text(10, 11, "D:");
        set_bkg_tile_xy(12, 11, font_map_copy[16 + dc]);
        VBK_REG = VBK_ATTRIBUTES;
        set_bkg_tile_xy(12, 11, 7);
        VBK_REG = VBK_TILES;
    }

    draw_text(3, 15, "PRESS A TO PLAY");

    SHOW_BKG;
    HIDE_SPRITES;
    DISPLAY_ON;
    NR51_REG = 0xFF;
    hUGE_init(&game_music);

    while (1) {
        vsync();
        keys = joypad();
        if ((keys & (J_A | J_START)) && !(old_keys & (J_A | J_START))) {
            sfx_ui();
            menu_music_playing = 0;
            game_state = STATE_MENU;
            return;
        }
        old_keys = keys;
    }
}

/* ============ GAME ============ */

void init_flags(void) {
    uint8_t i;
    for (i = 0; i < 2; i++) {
        flags[i].home_x = (i == 0) ? BASE0_X : BASE1_X;
        flags[i].home_y = (i == 0) ? BASE0_Y : BASE1_Y;
        flags[i].x = flags[i].home_x;
        flags[i].y = flags[i].home_y;
        flags[i].state = FLAG_HOME;
        flags[i].team = i;
        flags[i].drop_timer = 0;
        flags[i].carrier_type = -1;
        flags[i].carrier_idx = -1;
    }
}

void init_bots(void) {
    uint8_t i;
    /* 2 gamma (team 0) bots, 3 delta (team 1) bots */
    const uint8_t bot_teams[] = {0, 0, 1, 1, 1};
    /* Spawn positions: gamma near their base, delta near theirs */
    const uint16_t spawn_x[] = {460, 440, 180, 160, 170};
    const uint16_t spawn_y[] = {460, 440, 180, 160, 170};

    for (i = 0; i < NUM_BOTS; i++) {
        bots[i].x = (uint32_t)spawn_x[i] << 8;
        bots[i].y = (uint32_t)spawn_y[i] << 8;
        bots[i].vx = 0;
        bots[i].vy = 0;
        bots[i].dir = 0;
        bots[i].team = bot_teams[i];
        bots[i].action = ACT_ROAM;
        bots[i].carrying_flag = -1;
        bots[i].decide_timer = i * 10;
        bots[i].best_dist = 0xFFFF;
        bots[i].stuck_count = 0;
        bots[i].wander_dir = i;  /* varied initial wander */
        bots[i].wander_timer = 0;
        bots[i].target_x = flags[bots[i].team].home_x;
        bots[i].target_y = flags[bots[i].team].home_y;
        bots[i].hp = MAX_HP;
        bots[i].weapon = i % NUM_WEAPONS;  /* distribute weapons */
        bots[i].clip_ammo = wpn_clip[bots[i].weapon];
        bots[i].fire_timer = 0;
        bots[i].reload_timer = 0;
        bots[i].alive = 1;
        bots[i].respawn_timer = 0;
    }
}

void run_game(void) {
    uint8_t keys, old_keys, input, diag, fi;
    uint16_t px, py, screen_x, screen_y, pcx, pcy;
    int16_t ax, ay, a, dx, dy, fx, fy;
    uint32_t save;

    /* Stop music VBL handler to prevent bank conflicts during load */
    NR51_REG = 0x00;

    DISPLAY_OFF;

    /* Show loading screen first (font tiles + text only) */
    load_font_from_bank2();
    set_bkg_palette(7, 1, font_bg_pal);
    set_bkg_palette(0, 1, bg_palettes); /* palette 0 for dark bg */
    clear_bg();
    hide_all_sprites();
    draw_text(6, 8, "LOADING..");
    SHOW_BKG;
    HIDE_SPRITES;
    HIDE_WIN;
    DISPLAY_ON;

    /* Now do the heavy loading with the screen visible */
    set_bkg_data(0, 2, bg_tiles);
    set_bkg_palette(0, 7, bg_palettes);

    /* Load sprite data */
    set_sprite_data(0, ship_TILE_COUNT, ship_tiles);
    set_sprite_data(ship_TILE_COUNT, ship2_TILE_COUNT, ship2_tiles);
    set_sprite_data(FLAG_TILE_BASE, flag_spr_TILE_COUNT, flag_spr_tiles);
    set_sprite_data(BULLET_TILE, 1, bullet_tile);
    set_sprite_palette(1, 1, ship2_palettes);
    set_sprite_palette(2, 3, bullet_pals);

    /* Initialize ship + player combat */
    ship_x = (uint32_t)320 << 8;
    ship_y = (uint32_t)280 << 8;
    ship_vx = 0; ship_vy = 0;
    ship_dir = 0; bounce_pal = 1;
    gamma_caps = 0; delta_caps = 0; p_deaths = 0;
    p_hp = MAX_HP; p_weapon = WPN_POLYGUN;
    p_clip = wpn_clip[0]; p_fire_timer = 0; p_reload_timer = 0;
    p_alive = 1; p_respawn_timer = 0;

    /* Clear bullets */
    for (fi = 0; fi < MAX_BULLETS; fi++) bullets[fi].active = 0;

    /* Build wall bitmap + flow fields only once (map never changes) */
    {
        static uint8_t map_built = 0;
        if (!map_built) {
            build_wall_bitmap();
            build_flow_fields();
            map_built = 1;
        }
    }

    /* Initialize flags and bots */
    init_flags();
    init_bots();

    /* Done loading — set up the game screen */
    DISPLAY_OFF;

    update_camera();
    init_bg();
    hide_all_sprites();
    move_metasprite(ship_metasprites[dir_to_frame[0]], 0, SHIP_SPR_BASE, 0, 0);

    /* Window HUD */
    {
        uint8_t buf[20];
        for (fi = 0; fi < 20; fi++) buf[fi] = font_map_copy[0];
        set_win_tiles(0, 0, 20, 1, buf);
    }
    set_win_text_attrs(0, 0, 20);
    update_hud();
    WX_REG = 7;
    WY_REG = 136;

    SHOW_BKG;
    SHOW_SPRITES;
    SHOW_WIN;

    /* Restore audio and restart music */
    NR51_REG = 0xFF;
    hUGE_init(&game_music);

    DISPLAY_ON;

    old_keys = 0xFF;

    while (1) {
        keys = joypad();

        /* ---- Player respawn ---- */
        if (!p_alive) {
            if (p_respawn_timer > 0) {
                p_respawn_timer--;
                if (p_respawn_timer == 0) {
                    p_alive = 1;
                    p_hp = MAX_HP;
                    p_clip = wpn_clip[p_weapon];
                    p_reload_timer = 0;
                    ship_x = (uint32_t)BASE0_X << 8;
                    ship_y = (uint32_t)BASE0_Y << 8;
                    ship_vx = 0; ship_vy = 0;
                }
            }
            /* Hide player ship while dead */
            move_sprite(SHIP_SPR_BASE, 0, 0);
            move_sprite(SHIP_SPR_BASE+1, 0, 0);
            move_sprite(SHIP_SPR_BASE+2, 0, 0);
            move_sprite(SHIP_SPR_BASE+3, 0, 0);
            goto skip_player;
        }

        /* ---- B = cycle weapon ---- */
        if ((keys & J_B) && !(old_keys & J_B)) {
            p_weapon = (p_weapon + 1) % NUM_WEAPONS;
            p_clip = wpn_clip[p_weapon];
            p_reload_timer = 0;
            p_fire_timer = 0;
        }

        /* ---- Player weapon tick ---- */
        handle_weapon(&p_clip, &p_fire_timer, &p_reload_timer, p_weapon);

        /* ---- A = fire ---- */
        if ((keys & J_A) && p_fire_timer == 0 && p_clip > 0) {
            pcx = (uint16_t)(ship_x >> 8) + 4;
            pcy = (uint16_t)(ship_y >> 8) + 4;
            {
                /* Bullet spawn offset per direction */
                int8_t box = 0, boy = 0;
                switch (ship_dir) {
                    case 0: box = -3; break;           /* UP */
                    case 1: box = -3; boy = 1; break;  /* UP-RIGHT */
                    case 2: boy = -2; break;           /* RIGHT */
                    case 3: boy = -1; break;           /* DOWN-RIGHT */
                    case 4: box = -3; break;           /* DOWN */
                    case 5: box = -2; break;           /* DOWN-LEFT */
                    case 6: boy = -3; break;           /* LEFT */
                    case 7: break;                     /* UP-LEFT */
                }
                fire_bullet(pcx + box, pcy + boy, ship_dir, p_weapon, 0);
            }
            sfx_laser();
            p_clip--;
            p_fire_timer = wpn_rate[p_weapon];
            if (p_clip == 0) p_reload_timer = wpn_reload[p_weapon];
        }

        /* ---- Input ---- */
        diag = 0;
        if ((keys & J_UP) && (keys & (J_LEFT | J_RIGHT))) diag = 1;
        if ((keys & J_DOWN) && (keys & (J_LEFT | J_RIGHT))) diag = 1;
        a = diag ? DIAG_ACCEL : ACCEL;

        ax = 0; ay = 0; input = 0;
        if (keys & J_UP)    { ay = -a; input = 1; }
        if (keys & J_DOWN)  { ay =  a; input = 1; }
        if (keys & J_LEFT)  { ax = -a; input = 1; }
        if (keys & J_RIGHT) { ax =  a; input = 1; }

        if (input) {
            if      (ax == 0 && ay < 0)  ship_dir = 0;
            else if (ax > 0  && ay < 0)  ship_dir = 1;
            else if (ax > 0  && ay == 0) ship_dir = 2;
            else if (ax > 0  && ay > 0)  ship_dir = 3;
            else if (ax == 0 && ay > 0)  ship_dir = 4;
            else if (ax < 0  && ay > 0)  ship_dir = 5;
            else if (ax < 0  && ay == 0) ship_dir = 6;
            else                         ship_dir = 7;
        }

        /* ---- Physics ---- */
        ship_vx += ax; ship_vy += ay;
        ship_vx -= ship_vx >> 6; ship_vy -= ship_vy >> 6;
        if (ship_vx > MAX_SPEED)  ship_vx = MAX_SPEED;
        if (ship_vx < -MAX_SPEED) ship_vx = -MAX_SPEED;
        if (ship_vy > MAX_SPEED)  ship_vy = MAX_SPEED;
        if (ship_vy < -MAX_SPEED) ship_vy = -MAX_SPEED;

        save = ship_x; ship_x += ship_vx;
        px = (uint16_t)(ship_x >> 8); py = (uint16_t)(ship_y >> 8);
        if (check_collision(px, py)) { do_bounce(px, py); ship_x = save; ship_vx = -ship_vx; }

        save = ship_y; ship_y += ship_vy;
        px = (uint16_t)(ship_x >> 8); py = (uint16_t)(ship_y >> 8);
        if (check_collision(px, py)) { do_bounce(px, py); ship_y = save; ship_vy = -ship_vy; }

        /* ---- CTF Flag Logic (Player = team 0) ---- */
        /* Use rounded ship_px/ship_py so flag matches camera (no jitter) */
        pcx = ship_px + 4;
        pcy = ship_py + 4;

        if (flags[1].state != FLAG_CARRIED) {
            dx = (int16_t)pcx - (int16_t)flags[1].x;
            dy = (int16_t)pcy - (int16_t)flags[1].y;
            if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
                dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
                flags[1].state = FLAG_CARRIED;
                flags[1].carrier_type = 0;
                flags[1].carrier_idx = 0;
                sfx_grab();
            }
        }

        if (flags[0].state == FLAG_DROPPED) {
            dx = (int16_t)pcx - (int16_t)flags[0].x;
            dy = (int16_t)pcy - (int16_t)flags[0].y;
            if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
                dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
                flags[0].state = FLAG_HOME;
                flags[0].x = flags[0].home_x;
                flags[0].y = flags[0].home_y;
                flags[0].drop_timer = 0;
            }
        }

        if (flags[1].state == FLAG_CARRIED && flags[1].carrier_type == 0 &&
            flags[0].state == FLAG_HOME) {
            dx = (int16_t)pcx - (int16_t)BASE0_X;
            dy = (int16_t)pcy - (int16_t)BASE0_Y;
            if (dx > -FLAG_GRAB_DIST && dx < FLAG_GRAB_DIST &&
                dy > -FLAG_GRAB_DIST && dy < FLAG_GRAB_DIST) {
                flags[1].state = FLAG_HOME;
                flags[1].x = flags[1].home_x;
                flags[1].y = flags[1].home_y;
                flags[1].carrier_type = -1;
                gamma_caps++;
                sfx_capture();
            }
        }

        if (flags[1].state == FLAG_CARRIED && flags[1].carrier_type == 0) {
            int8_t flag_ox = 0, flag_oy = 0;
            switch (ship_dir) {
                case 0: flag_ox = 4; flag_oy = -3; break;
                case 1: flag_ox = 4; flag_oy = -2; break;
                case 2: flag_ox = 2; flag_oy = -3; break;
                case 3: flag_ox = 4; flag_oy = -2; break;
                case 4: flag_ox = 4; flag_oy = -6; break;
                case 5: flag_ox = 4; flag_oy = -2; break;
                case 6: flag_ox = 6; flag_oy = -4; break;
                case 7: flag_ox = 3; flag_oy = -2; break;
            }
            flags[1].x = pcx + flag_ox;
            flags[1].y = pcy + flag_oy;
        }

        /* ---- Render player ship ---- */
        screen_x = ship_px - cam_x + 12;
        screen_y = ship_py - cam_y + 20;
        move_metasprite(ship_metasprites[dir_to_frame[ship_dir]],
                        0, SHIP_SPR_BASE, (uint8_t)screen_x, (uint8_t)screen_y);

skip_player:

        /* ---- Victory check ---- */
        if (gamma_caps >= CAPS_TO_WIN) {
            winning_team = 0;
            game_state = STATE_VICTORY;
            HIDE_WIN;
            return;
        }
        if (delta_caps >= CAPS_TO_WIN) {
            winning_team = 1;
            game_state = STATE_VICTORY;
            HIDE_WIN;
            return;
        }

        /* ---- Update all bots every frame ---- */
        for (fi = 0; fi < NUM_BOTS; fi++)
            update_bot(fi);

        /* ---- Stagger expensive checks: ship collision on even frames,
              full bullet-entity checks every frame (bullets are fast) ---- */
        {
            static uint8_t frame_tick = 0;
            if ((frame_tick & 1) == 0) {
                resolve_ship_collisions();
                if (p_alive) unstick(&ship_x, &ship_y, &ship_vx, &ship_vy);
            }
            frame_tick++;
        }
        update_bullets();

        /* Dropped flag auto-return */
        for (fi = 0; fi < 2; fi++) {
            if (flags[fi].state == FLAG_DROPPED) {
                flags[fi].drop_timer++;
                if (flags[fi].drop_timer >= FLAG_RETURN_TIME) {
                    flags[fi].state = FLAG_HOME;
                    flags[fi].x = flags[fi].home_x;
                    flags[fi].y = flags[fi].home_y;
                    flags[fi].drop_timer = 0;
                    flags[fi].carrier_type = -1;
                }
            }
        }

        /* ---- Camera (compute position, tiles loaded after vsync) ---- */
        update_camera();

        /* ---- Render bots ---- */
        for (fi = 0; fi < NUM_BOTS; fi++) {
            uint8_t bot_oam = BOT_OAM_START + fi * 4;
            if (!bots[fi].alive) {
                move_sprite(bot_oam, 0, 0); move_sprite(bot_oam+1, 0, 0);
                move_sprite(bot_oam+2, 0, 0); move_sprite(bot_oam+3, 0, 0);
                continue;
            }
            fx = (int16_t)(bots[fi].x >> 8) - (int16_t)cam_x + 12;
            fy = (int16_t)(bots[fi].y >> 8) - (int16_t)cam_y + 20;
            if (fx >= -8 && fx < 168 && fy >= -8 && fy < 160) {
                if (bots[fi].team == 0) {
                    move_metasprite(ship_metasprites[dir_to_frame[bots[fi].dir]],
                                    0, bot_oam, (uint8_t)fx, (uint8_t)fy);
                } else {
                    move_metasprite(ship2_metasprites[dir_to_frame[bots[fi].dir]],
                                    ship_TILE_COUNT, bot_oam, (uint8_t)fx, (uint8_t)fy);
                    /* OR palette bit into props to preserve flip flags */
                    shadow_OAM[bot_oam].prop   |= S_PAL(1);
                    shadow_OAM[bot_oam+1].prop |= S_PAL(1);
                    shadow_OAM[bot_oam+2].prop |= S_PAL(1);
                    shadow_OAM[bot_oam+3].prop |= S_PAL(1);
                }
            } else {
                move_sprite(bot_oam, 0, 0); move_sprite(bot_oam+1, 0, 0);
                move_sprite(bot_oam+2, 0, 0); move_sprite(bot_oam+3, 0, 0);
            }
        }

        /* ---- Render flags ---- */
        for (fi = 0; fi < 2; fi++) {
            uint8_t f_oam = (fi == 0) ? FLAG_SPR_0 : FLAG_SPR_1;
            fx = (int16_t)flags[fi].x - (int16_t)cam_x;
            fy = (int16_t)flags[fi].y - (int16_t)cam_y;
            if (fx >= -8 && fx < 168 && fy >= -16 && fy < 144) {
                move_metasprite(flag_spr_metasprites[0],
                                FLAG_TILE_BASE, f_oam,
                                (uint8_t)(fx + 8), (uint8_t)(fy + 16));
                if (fi == 1) {
                    set_sprite_prop(f_oam, S_PAL(1));
                    set_sprite_prop(f_oam + 1, S_PAL(1));
                }
            } else {
                move_sprite(f_oam, 0, 0);
                move_sprite(f_oam + 1, 0, 0);
            }
        }

        /* ---- Render bullets ---- */
        render_bullets();

        /* ---- SFX timer: restart music when SFX jingle finishes ---- */

        /* ---- SFX tick + Update HUD ---- */
        sfx_tick();
        update_hud();

        old_keys = keys;
        vsync();
        /* During vblank: set scroll + load new tiles (VRAM fully accessible) */
        SCX_REG = (uint8_t)cam_x;
        SCY_REG = (uint8_t)cam_y;
        update_scroll();
        vsync(); vsync(); /* 3 total = 20fps */
    }
}

/* ============ MAIN ============ */

void main(void) {
    if (_cpu == CGB_TYPE) {
        cpu_fast();
    }

    set_sprite_palette(0, 1, ship_palettes);

    /* Initialize audio hardware and start music */
    NR52_REG = 0x80;  /* enable audio */
    NR50_REG = 0x77;  /* max volume both sides */
    NR51_REG = 0xFF;  /* all channels to both outputs */

    /* Simple music — VBL handler, no banking */
    __critical {
        add_VBL(safe_dosound);
    }

    copy_font_map();

    game_state = STATE_MENU;

    while (1) {
        switch (game_state) {
            case STATE_MENU:    run_menu();    break;
            case STATE_GAME:    run_game();    break;
            case STATE_CREDITS: run_credits(); break;
            case STATE_VICTORY: run_victory(); break;
        }
    }
}
