// Take main() back from SDL2main: it also supplies module_info, which would
// otherwise name this module "SDL App". This game registers its own exit
// callback below, so SDL's callback thread is redundant either way.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>
#include <pspctrl.h>
#include <pspkernel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Without this the module inherits SDL2's fallback and reports itself as
// "SDL App" to the firmware, CFW tools and crash dumps.
PSP_MODULE_INFO("Monk Tower", 0, 1, 0);
/* VFPU access is required: SDL's PSP renderer uses VFPU instructions on every
   texture draw (MathAbs in SDL_render_psp.c), and a thread without this flag
   takes a CPU exception on real hardware -- the PSP goes black and powers off.
   PPSSPP does not enforce it. */
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

#define SCREEN_W 480
#define SCREEN_H 272
#define BOARD_SIZE 8
#define TILE 32
#define BOARD_X 8
#define BOARD_Y 8
#define MAX_ENEMIES 16
#define VIEW_RANGE 5
#define WEAPON_SLOTS 4
#define CONSUMABLE_SLOTS 4
#define MUSIC_RATE 22050
#define SAVE_VERSION 3
#define SAVE_DIR "ms0:/PSP/SAVEDATA/MONKTOWER"
#define SAVE_PATH SAVE_DIR "/RUN.DAT"
#define SETTINGS_PATH SAVE_DIR "/SETTINGS.DAT"

typedef enum { MODE_TITLE, MODE_GAME, MODE_DEAD, MODE_SETTINGS } GameMode;
typedef enum { CELL_FLOOR, CELL_WALL, CELL_PILLAR, CELL_DOOR, CELL_OPEN_DOOR } Cell;
typedef enum {
    WEAPON_NONE,
    WEAPON_SMALL_SWORD,
    WEAPON_MEDIUM_SWORD,
    WEAPON_DAGGER,
    WEAPON_AXE,
    WEAPON_SPEAR,
    WEAPON_STUN_WAND,
    WEAPON_DISPLACEMENT,
    WEAPON_WARHAMMER,
    WEAPON_GOLDEN_SWORD,
    WEAPON_GREEN_HAMMER
} WeaponKind;
typedef enum {
    ITEM_NONE,
    ITEM_STAIR,
    ITEM_SMALL_SWORD,
    ITEM_MEDIUM_SWORD,
    ITEM_DAGGER,
    ITEM_AXE,
    ITEM_SPEAR,
    ITEM_STUN_WAND,
    ITEM_DISPLACEMENT,
    ITEM_WARHAMMER,
    ITEM_GOLDEN_SWORD,
    ITEM_GREEN_HAMMER,
    ITEM_POTION_HEAL,
    ITEM_POTION_REGEN,
    ITEM_POTION_IMMUNITY,
    ITEM_POTION_POISON,
    ITEM_POTION_ANTIDOTE,
    ITEM_POTION_TELEPORT,
    ITEM_GOLD,
    ITEM_SMALL_VASE,
    ITEM_LARGE_VASE,
    ITEM_FORGE,
    ITEM_HERBALIST,
    ITEM_SPIKES_HIDDEN,
    ITEM_SPIKES_HALF,
    ITEM_SPIKES,
    ITEM_BOOK
} ItemKind;
typedef enum {
    ENEMY_RAT, ENEMY_SNAKE, ENEMY_VIPER, ENEMY_BASILISK, ENEMY_GOLDEN_SNAKE,
    ENEMY_NOVICE, ENEMY_MONK, ENEMY_LIBRARIAN, ENEMY_PIXIE, ENEMY_GARGOYLE,
    ENEMY_GOLEM, ENEMY_GHOST, ENEMY_EXORCIST
} EnemyKind;
typedef enum {
    SFX_NONE, SFX_PICK, SFX_HIT, SFX_USE, SFX_ASCEND, SFX_UPGRADE,
    SFX_TELEPORT, SFX_WIN, SFX_DEFEAT
} SfxKind;

typedef struct {
    WeaponKind kind;
    int durability;
} Weapon;

typedef struct {
    unsigned int lead_phase;
    unsigned int bass_phase;
    unsigned int sample_cursor;
    unsigned int sfx_phase;
    int sfx_frequency;
    int sfx_remaining;
    int volume;
} MusicState;

typedef struct {
    int x;
    int y;
    int hp;
    int alive;
    int sprite;
    int damage;
    int poison_damage;
    int range;
    int stunned;
    int poisoned;
    int cooldown;
    EnemyKind kind;
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Enemy;

typedef struct {
    int x;
    int y;
    int hp;
    int max_hp;
    Weapon weapons[WEAPON_SLOTS];
    int active_weapon;
    ItemKind consumables[CONSUMABLE_SLOTS];
    int active_consumable;
    int poisoned;
    int regenerating;
    int immune;
    int stunned;
    unsigned int discovered_potions;
    int gold;
    int kills;
} Player;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *units;
    SDL_Texture *tiles;
    SDL_Texture *items;
    SDL_Texture *splash;
    Cell board[BOARD_SIZE][BOARD_SIZE];
    ItemKind board_items[BOARD_SIZE][BOARD_SIZE];
    unsigned char discovered[BOARD_SIZE][BOARD_SIZE];
    unsigned char visible[BOARD_SIZE][BOARD_SIZE];
    Enemy enemies[MAX_ENEMIES];
    int enemy_count;
    Player player;
    GameMode mode;
    int floor;
    int turn;
    char message[64];
    unsigned int previous_buttons;
    int input_lock;
    GameMode settings_return_mode;
    int settings_selection;
    int music_level;
    int exit_requested;
    int won;
    int save_available;
    int help_visible;
    SDL_AudioDeviceID audio_device;
    MusicState music;
    const char *missing_asset;
} Game;

typedef struct {
    unsigned int version;
    Cell board[BOARD_SIZE][BOARD_SIZE];
    ItemKind board_items[BOARD_SIZE][BOARD_SIZE];
    unsigned char discovered[BOARD_SIZE][BOARD_SIZE];
    Enemy enemies[MAX_ENEMIES];
    int enemy_count;
    Player player;
    int floor;
    int turn;
} SaveState;

typedef struct {
    char c;
    unsigned char row[7];
} Glyph;

static const Glyph glyphs[] = {
    {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {':',{0,4,4,0,4,4,0}},
    {'/',{1,2,4,8,16,0,0}}, {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}}, {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}}, {'A',{14,17,17,31,17,17,17}},
    {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,14}},
    {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}},
    {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}},
    {'Z',{31,1,2,4,8,16,31}}
};

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    int callback_id;
    (void)args; (void)argp;
    callback_id = sceKernelCreateCallback("exit_callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(callback_id);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int id = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0xFA0,
                                   PSP_THREAD_ATTR_USER, NULL);
    if (id >= 0) sceKernelStartThread(id, 0, NULL);
}

static void music_callback(void *userdata, Uint8 *stream, int length) {
    static const unsigned short lead_notes[32] = {
        440, 0, 523, 0, 659, 0, 523, 0,
        392, 0, 494, 0, 587, 0, 494, 0,
        349, 0, 440, 0, 523, 0, 440, 0,
        330, 0, 392, 0, 494, 440, 392, 0
    };
    static const unsigned short bass_notes[32] = {
        110,110,110,110,131,131,131,131,
        98,98,98,98,123,123,123,123,
        87,87,87,87,110,110,110,110,
        82,82,98,98,110,110,98,98
    };
    MusicState *music = userdata;
    Sint16 *samples = (Sint16 *)stream;
    int count = length / (int)sizeof(Sint16);
    int i;
    for (i = 0; i < count; ++i) {
        unsigned int step_samples = MUSIC_RATE / 4;
        unsigned int step = (music->sample_cursor / step_samples) % 32;
        unsigned int within = music->sample_cursor % step_samples;
        unsigned int lead_inc = (unsigned int)lead_notes[step] * 194783U;
        unsigned int bass_inc = (unsigned int)bass_notes[step] * 194783U;
        int envelope = within < step_samples / 12 ? (int)(within * 12U * 256U / step_samples)
                                                  : (int)((step_samples - within) * 256U / step_samples);
        int lead = 0;
        int bass;
        int mixed;
        music->lead_phase += lead_inc;
        music->bass_phase += bass_inc;
        if (lead_notes[step]) lead = (music->lead_phase & 0x80000000U) ? envelope : -envelope;
        bass = (music->bass_phase & 0x80000000U) ? 128 : -128;
        mixed = (lead * 3 + bass * 2) * music->volume / 1024;
        if (music->sfx_remaining > 0) {
            int sfx_amp = music->sfx_remaining * 7000 / (MUSIC_RATE / 8);
            music->sfx_phase += (unsigned int)music->sfx_frequency * 194783U;
            mixed += (music->sfx_phase & 0x80000000U) ? sfx_amp : -sfx_amp;
            music->sfx_remaining--;
        }
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        samples[i] = (Sint16)mixed;
        music->sample_cursor++;
    }
}

static int music_volume_for_level(int level) {
    static const int volumes[] = {0, 3200, 5600, 8200};
    if (level < 0 || level > 3) return volumes[2];
    return volumes[level];
}

static const char *music_level_name(int level) {
    static const char *const names[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
    if (level < 0 || level > 3) return names[2];
    return names[level];
}

static void trigger_sfx(Game *g, SfxKind kind) {
    static const int frequencies[] = {0, 880, 180, 660, 990, 1200, 520, 1500, 110};
    if (!g->audio_device || kind == SFX_NONE) return;
    SDL_LockAudioDevice(g->audio_device);
    g->music.sfx_frequency = frequencies[kind];
    g->music.sfx_remaining = MUSIC_RATE / 8;
    g->music.sfx_phase = 0;
    SDL_UnlockAudioDevice(g->audio_device);
}

static const char *weapon_name(WeaponKind kind) {
    switch (kind) {
        case WEAPON_SMALL_SWORD: return "SMALL SWORD";
        case WEAPON_MEDIUM_SWORD: return "MEDIUM SWORD";
        case WEAPON_DAGGER: return "POISON DAGGER";
        case WEAPON_AXE: return "AXE";
        case WEAPON_SPEAR: return "SPEAR";
        case WEAPON_STUN_WAND: return "STUN WAND";
        case WEAPON_DISPLACEMENT: return "SWAP WAND";
        case WEAPON_WARHAMMER: return "WARHAMMER";
        case WEAPON_GOLDEN_SWORD: return "GOLDEN SWORD";
        case WEAPON_GREEN_HAMMER: return "GREEN HAMMER";
        default: return "BARE HANDS";
    }
}

static int weapon_sprite(WeaponKind kind) {
    switch (kind) {
        case WEAPON_DAGGER: return 3;
        case WEAPON_AXE: return 4;
        case WEAPON_SPEAR: return 2;
        case WEAPON_STUN_WAND:
        case WEAPON_DISPLACEMENT: return 1;
        case WEAPON_WARHAMMER: return 5;
        case WEAPON_GREEN_HAMMER: return 5;
        default: return 0;
    }
}

static WeaponKind item_weapon(ItemKind item) {
    if (item >= ITEM_SMALL_SWORD && item <= ITEM_GREEN_HAMMER)
        return (WeaponKind)(item - ITEM_SMALL_SWORD + WEAPON_SMALL_SWORD);
    return WEAPON_NONE;
}

static ItemKind weapon_item(WeaponKind weapon) {
    if (weapon >= WEAPON_SMALL_SWORD && weapon <= WEAPON_GREEN_HAMMER)
        return (ItemKind)(weapon - WEAPON_SMALL_SWORD + ITEM_SMALL_SWORD);
    return ITEM_NONE;
}

static void weapon_damage(WeaponKind kind, int *minimum, int *maximum) {
    *minimum = 1;
    *maximum = 1;
    switch (kind) {
        case WEAPON_SMALL_SWORD: *minimum = 2; *maximum = 3; break;
        case WEAPON_MEDIUM_SWORD: *minimum = 4; *maximum = 5; break;
        case WEAPON_DAGGER: *minimum = 1; *maximum = 2; break;
        case WEAPON_AXE: *minimum = 4; *maximum = 5; break;
        case WEAPON_SPEAR: *minimum = 4; *maximum = 5; break;
        case WEAPON_STUN_WAND: *minimum = 0; *maximum = 0; break;
        case WEAPON_DISPLACEMENT: *minimum = 2; *maximum = 2; break;
        case WEAPON_WARHAMMER: *minimum = 1; *maximum = 1; break;
        case WEAPON_GOLDEN_SWORD: *minimum = 6; *maximum = 10; break;
        case WEAPON_GREEN_HAMMER: *minimum = 2; *maximum = 3; break;
        default: break;
    }
}

static int random_range(int min, int max) {
    return min + rand() % (max - min + 1);
}

static Weapon *active_weapon(Game *g) {
    return &g->player.weapons[g->player.active_weapon];
}

static const char *enemy_name(EnemyKind kind) {
    switch (kind) {
        case ENEMY_SNAKE: return "SNAKE";
        case ENEMY_VIPER: return "VIPER";
        case ENEMY_BASILISK: return "BASILISK";
        case ENEMY_GOLDEN_SNAKE: return "GOLDEN SNAKE";
        case ENEMY_NOVICE: return "NOVICE";
        case ENEMY_MONK: return "MONK";
        case ENEMY_LIBRARIAN: return "LIBRARIAN";
        case ENEMY_PIXIE: return "PIXIE";
        case ENEMY_GARGOYLE: return "GARGOYLE";
        case ENEMY_GOLEM: return "STONE GOLEM";
        case ENEMY_GHOST: return "GHOST";
        case ENEMY_EXORCIST: return "EXORCIST";
        default: return "RAT";
    }
}

static void configure_enemy(Enemy *e, int floor) {
    EnemyKind pool[13];
    int count = 0;
    int x = e->x;
    int y = e->y;
    EnemyKind kind;
    pool[count++] = ENEMY_RAT;
    if (floor >= 3) pool[count++] = ENEMY_SNAKE;
    if (floor >= 5) pool[count++] = ENEMY_NOVICE;
    if (floor >= 7) { pool[count++] = ENEMY_VIPER; pool[count++] = ENEMY_MONK; }
    if (floor >= 8) pool[count++] = ENEMY_LIBRARIAN;
    if (floor >= 9) pool[count++] = ENEMY_BASILISK;
    if (floor >= 10) { pool[count++] = ENEMY_GOLDEN_SNAKE; pool[count++] = ENEMY_PIXIE; }
    if (floor >= 12) { pool[count++] = ENEMY_GARGOYLE; pool[count++] = ENEMY_GHOST; }
    if (floor >= 14) pool[count++] = ENEMY_GOLEM;
    if (floor >= 16) pool[count++] = ENEMY_EXORCIST;
    kind = pool[random_range(0, count - 1)];
    memset(e, 0, sizeof(*e));
    e->x = x;
    e->y = y;
    e->kind = kind;
    e->alive = 1;
    e->sprite = 14;
    e->hp = 2;
    e->damage = 1;
    e->r = 255; e->g = 126; e->b = 102;
    if (kind == ENEMY_SNAKE) {
        e->sprite = 4; e->poison_damage = 2;
        e->r = 145; e->g = 200; e->b = 185;
    } else if (kind == ENEMY_VIPER) {
        e->sprite = 4; e->hp = 4; e->poison_damage = 4;
        e->r = 89; e->g = 116; e->b = 166;
    } else if (kind == ENEMY_BASILISK) {
        e->sprite = 4; e->hp = 3; e->damage = 2;
        e->r = 189; e->g = 200; e->b = 220;
    } else if (kind == ENEMY_GOLDEN_SNAKE) {
        e->sprite = 4; e->hp = 6; e->damage = 0; e->poison_damage = 4; e->range = 4;
        e->r = 255; e->g = 191; e->b = 102;
    } else if (kind == ENEMY_NOVICE) {
        e->sprite = 6; e->hp = 4;
        e->r = 189; e->g = 200; e->b = 220;
    } else if (kind == ENEMY_MONK) {
        e->sprite = 6; e->hp = 6; e->damage = 3;
        e->r = 89; e->g = 116; e->b = 166;
    } else if (kind == ENEMY_LIBRARIAN) {
        e->sprite = 6; e->hp = 4; e->damage = 2; e->range = 4;
        e->r = 255; e->g = 126; e->b = 102;
    } else if (kind == ENEMY_PIXIE) {
        e->sprite = 16; e->hp = 5; e->poison_damage = 2;
        e->r = 145; e->g = 200; e->b = 185;
    } else if (kind == ENEMY_GARGOYLE) {
        e->sprite = 8; e->hp = 10; e->damage = 2;
        e->r = 126; e->g = 104; e->b = 104;
    } else if (kind == ENEMY_GOLEM) {
        e->sprite = 10; e->hp = 12; e->damage = 4; e->cooldown = 4;
        e->r = 255; e->g = 126; e->b = 102;
    } else if (kind == ENEMY_GHOST) {
        e->sprite = 12; e->hp = 6; e->damage = 2;
        e->r = 195; e->g = 234; e->b = 254;
    } else if (kind == ENEMY_EXORCIST) {
        e->sprite = 6; e->hp = 10; e->damage = 2; e->cooldown = 6;
        e->r = 77; e->g = 57; e->b = 65;
    }
}

static void pick_up_weapon(Game *g, WeaponKind kind) {
    int slot;
    Weapon *weapon;
    for (slot = 0; slot < WEAPON_SLOTS; ++slot) {
        if (g->player.weapons[slot].kind == WEAPON_NONE) break;
    }
    if (slot == WEAPON_SLOTS) slot = g->player.active_weapon;
    g->player.active_weapon = slot;
    weapon = &g->player.weapons[slot];
    weapon->kind = kind;
    if (kind == WEAPON_WARHAMMER) weapon->durability = random_range(10, 18);
    else if (kind == WEAPON_GOLDEN_SWORD) weapon->durability = random_range(6, 9);
    else if (kind == WEAPON_GREEN_HAMMER) weapon->durability = random_range(8, 15);
    else weapon->durability = random_range(5, 15);
    snprintf(g->message, sizeof(g->message), "%s EQUIPPED", weapon_name(kind));
    trigger_sfx(g, SFX_PICK);
}

static void use_durability(Game *g) {
    Weapon *weapon = active_weapon(g);
    if (weapon->kind == WEAPON_NONE) return;
    weapon->durability--;
    if (weapon->durability <= 0) {
        weapon->kind = WEAPON_NONE;
        weapon->durability = 0;
        snprintf(g->message, sizeof(g->message), "YOUR WEAPON BREAKS");
    }
}

static int is_potion(ItemKind item) {
    return item >= ITEM_POTION_HEAL && item <= ITEM_POTION_TELEPORT;
}

static unsigned int potion_bit(ItemKind item) {
    return is_potion(item) ? 1U << (unsigned int)(item - ITEM_POTION_HEAL) : 0;
}

static const char *potion_name(ItemKind item) {
    switch (item) {
        case ITEM_POTION_HEAL: return "HEALING";
        case ITEM_POTION_REGEN: return "REGEN";
        case ITEM_POTION_IMMUNITY: return "IMMUNITY";
        case ITEM_POTION_POISON: return "POISON";
        case ITEM_POTION_ANTIDOTE: return "ANTIDOTE";
        case ITEM_POTION_TELEPORT: return "TELEPORT";
        default: return "EMPTY";
    }
}

static const char *shown_potion_name(const Player *player, ItemKind item) {
    if (!is_potion(item)) return "EMPTY";
    if (!(player->discovered_potions & potion_bit(item))) return "MYSTERY";
    return potion_name(item);
}

static ItemKind random_potion_for_floor(int floor) {
    ItemKind pool[6];
    int count = 0;
    pool[count++] = ITEM_POTION_HEAL;
    pool[count++] = ITEM_POTION_POISON;
    pool[count++] = ITEM_POTION_ANTIDOTE;
    if (floor >= 4) { pool[count++] = ITEM_POTION_REGEN; pool[count++] = ITEM_POTION_IMMUNITY; }
    if (floor >= 5) pool[count++] = ITEM_POTION_TELEPORT;
    return pool[random_range(0, count - 1)];
}

static void pick_up_consumable(Game *g, ItemKind item) {
    int slot;
    for (slot = 0; slot < CONSUMABLE_SLOTS; ++slot) {
        if (g->player.consumables[slot] == ITEM_NONE) break;
    }
    if (slot == CONSUMABLE_SLOTS) slot = g->player.active_consumable;
    g->player.active_consumable = slot;
    g->player.consumables[slot] = item;
    snprintf(g->message, sizeof(g->message), "%s POTION FOUND",
             shown_potion_name(&g->player, item));
    trigger_sfx(g, SFX_PICK);
}

static void set_color(SDL_Renderer *r, unsigned char red, unsigned char green,
                      unsigned char blue, unsigned char alpha) {
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
}

static const unsigned char *glyph_rows(char c) {
    size_t i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].c == c) return glyphs[i].row;
    }
    return glyphs[0].row;
}

static void draw_text(Game *g, const char *text, int x, int y, int scale,
                      unsigned char r, unsigned char green, unsigned char b) {
    int i;
    set_color(g->renderer, r, green, b, 255);
    for (i = 0; text[i]; ++i) {
        const unsigned char *rows = glyph_rows(text[i]);
        int row;
        int col;
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 5; ++col) {
                if (rows[row] & (1 << (4 - col))) {
                    SDL_Rect pixel = {x + i * 6 * scale + col * scale,
                                      y + row * scale, scale, scale};
                    SDL_RenderFillRect(g->renderer, &pixel);
                }
            }
        }
    }
}

static SDL_Texture *load_texture(Game *g, const char *path) {
    SDL_Surface *surface = IMG_Load(path);
    SDL_Texture *texture;
    if (!surface) {
        if (!g->missing_asset) g->missing_asset = path;
        return NULL;
    }
    texture = SDL_CreateTextureFromSurface(g->renderer, surface);
    SDL_FreeSurface(surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

static void draw_text_centered(Game *g, const char *text, int y, int scale,
                              unsigned char r, unsigned char green,
                              unsigned char b) {
    int width = (int)strlen(text) * 6 * scale;
    draw_text(g, text, (SCREEN_W - width) / 2, y, scale, r, green, b);
}

/* The glyph table only covers A-Z, 0-9 and " -:/", so fold anything else
   (asset paths are lower case and contain dots) into what it can draw. */
static void to_glyph_text(const char *src, char *dst, size_t size) {
    size_t i;
    for (i = 0; src[i] && i + 1 < size; ++i) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == ' ' || c == '-' || c == ':' || c == '/')) {
            c = ' ';
        }
        dst[i] = c;
    }
    dst[i] = '\0';
}

/* Without this a missing assets folder just bounces straight back to the XMB,
   which is indistinguishable from a crash. The font is built in, so this still
   draws when no asset loaded at all. */
static void show_missing_assets_screen(Game *g) {
    char detail[64];
    SceCtrlData pad;
    int frames;

    if (!g->renderer || !g->missing_asset) return;
    to_glyph_text(g->missing_asset, detail, sizeof(detail));

    for (frames = 0; frames < 60 * 60; ++frames) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) return;
        }

        set_color(g->renderer, 12, 10, 20, 255);
        SDL_RenderClear(g->renderer);
        draw_text_centered(g, "MONK TOWER", 44, 2, 255, 191, 102);
        draw_text_centered(g, "GAME FILES ARE MISSING", 86, 1, 255, 126, 102);
        draw_text_centered(g, detail, 104, 1, 189, 200, 220);
        draw_text_centered(g, "COPY THE ASSETS FOLDER NEXT TO", 140, 1,
                           189, 200, 220);
        draw_text_centered(g, "EBOOT PBP IN PSP/GAME/MONKTOWER", 156, 1,
                           189, 200, 220);
        draw_text_centered(g, "PRESS X TO QUIT", 200, 1, 145, 200, 185);
        SDL_RenderPresent(g->renderer);

        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS) return;
        SDL_Delay(16);
    }
}

static void draw_atlas(Game *g, SDL_Texture *texture, int index, int x, int y,
                       int size, unsigned char r, unsigned char green,
                       unsigned char b, unsigned char alpha) {
    SDL_Rect src = {(index % 8) * 16, (index / 8) * 16, 16, 16};
    SDL_Rect dst = {x, y, size, size};
    SDL_SetTextureColorMod(texture, r, green, b);
    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_RenderCopy(g->renderer, texture, &src, &dst);
}

static int enemy_at(const Game *g, int x, int y) {
    int i;
    for (i = 0; i < g->enemy_count; ++i) {
        if (g->enemies[i].alive && g->enemies[i].x == x && g->enemies[i].y == y) return i;
    }
    return -1;
}

static int blocks_move(const Game *g, int x, int y) {
    if (x < 0 || y < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) return 1;
    return g->board[y][x] == CELL_WALL || g->board[y][x] == CELL_PILLAR ||
           g->board[y][x] == CELL_DOOR;
}

static int occupied(const Game *g, int x, int y) {
    return (g->player.x == x && g->player.y == y) || enemy_at(g, x, y) >= 0;
}

static int clear_tile(const Game *g, int x, int y) {
    return !blocks_move(g, x, y) && !occupied(g, x, y) &&
           g->board_items[y][x] == ITEM_NONE;
}

static void mark_reachable(const Game *g, unsigned char reachable[BOARD_SIZE][BOARD_SIZE]) {
    int queue_x[BOARD_SIZE * BOARD_SIZE];
    int queue_y[BOARD_SIZE * BOARD_SIZE];
    int head = 0;
    int tail = 0;
    static const int offsets[4][2] = {{0,-1}, {0,1}, {-1,0}, {1,0}};
    memset(reachable, 0, BOARD_SIZE * BOARD_SIZE * sizeof(unsigned char));
    reachable[g->player.y][g->player.x] = 1;
    queue_x[tail] = g->player.x;
    queue_y[tail++] = g->player.y;
    while (head < tail) {
        int x = queue_x[head];
        int y = queue_y[head++];
        int i;
        for (i = 0; i < 4; ++i) {
            int nx = x + offsets[i][0];
            int ny = y + offsets[i][1];
            if (nx < 0 || ny < 0 || nx >= BOARD_SIZE || ny >= BOARD_SIZE) continue;
            if (reachable[ny][nx] || g->board[ny][nx] == CELL_WALL ||
                g->board[ny][nx] == CELL_PILLAR) continue;
            reachable[ny][nx] = 1;
            queue_x[tail] = nx;
            queue_y[tail++] = ny;
        }
    }
}

static int approach_count(const Game *g, int x, int y) {
    int count = 0;
    if (x > 0 && g->board[y][x - 1] != CELL_WALL && g->board[y][x - 1] != CELL_PILLAR) count++;
    if (x + 1 < BOARD_SIZE && g->board[y][x + 1] != CELL_WALL && g->board[y][x + 1] != CELL_PILLAR) count++;
    if (y > 0 && g->board[y - 1][x] != CELL_WALL && g->board[y - 1][x] != CELL_PILLAR) count++;
    if (y + 1 < BOARD_SIZE && g->board[y + 1][x] != CELL_WALL && g->board[y + 1][x] != CELL_PILLAR) count++;
    return count;
}

static int random_reachable_clear_tile(const Game *g,
                                       const unsigned char reachable[BOARD_SIZE][BOARD_SIZE],
                                       int require_open_approach, int *x, int *y) {
    int tries;
    for (tries = 0; tries < 1000; ++tries) {
        int tx = random_range(0, BOARD_SIZE - 1);
        int ty = random_range(0, BOARD_SIZE - 1);
        if (reachable[ty][tx] && clear_tile(g, tx, ty) &&
            (!require_open_approach || approach_count(g, tx, ty) >= 2)) {
            *x = tx;
            *y = ty;
            return 1;
        }
    }
    for (*y = 0; *y < BOARD_SIZE; ++*y) {
        for (*x = 0; *x < BOARD_SIZE; ++*x) {
            if (reachable[*y][*x] && clear_tile(g, *x, *y) &&
                (!require_open_approach || approach_count(g, *x, *y) >= 2)) return 1;
        }
    }
    return 0;
}

static void add_door(Game *g, int x, int y) {
    if (x >= 0 && y >= 0 && x < BOARD_SIZE && y < BOARD_SIZE) g->board[y][x] = CELL_DOOR;
}

static void generate_layout(Game *g) {
    int x;
    int y;
    int split_x = random_range(3, 4);
    int split_y_left = random_range(3, 4);
    int split_y_right = random_range(3, 4);

    for (y = 0; y < BOARD_SIZE; ++y) {
        for (x = 0; x < BOARD_SIZE; ++x) {
            g->board[y][x] = CELL_FLOOR;
            g->board_items[y][x] = ITEM_NONE;
            g->discovered[y][x] = 0;
            g->visible[y][x] = 0;
        }
    }
    for (y = 0; y < BOARD_SIZE; ++y) g->board[y][split_x] = CELL_WALL;
    add_door(g, split_x, random_range(1, BOARD_SIZE - 2));
    if (rand() & 1) add_door(g, split_x, random_range(1, BOARD_SIZE - 2));

    for (x = 0; x < split_x; ++x) g->board[split_y_left][x] = CELL_WALL;
    add_door(g, random_range(0, split_x - 1), split_y_left);
    for (x = split_x + 1; x < BOARD_SIZE; ++x) g->board[split_y_right][x] = CELL_WALL;
    add_door(g, random_range(split_x + 1, BOARD_SIZE - 1), split_y_right);
    for (x = 0; x < BOARD_SIZE; ++x) {
        for (y = 0; y < BOARD_SIZE; ++y) {
            if (g->board[y][x] == CELL_WALL && random_range(0, 99) < 10)
                g->board[y][x] = CELL_PILLAR;
        }
    }
}

static void reveal(Game *g) {
    int x;
    int y;
    memset(g->visible, 0, sizeof(g->visible));
    for (y = 0; y < BOARD_SIZE; ++y) {
        for (x = 0; x < BOARD_SIZE; ++x) {
            int distance = abs(x - g->player.x) + abs(y - g->player.y);
            if (distance <= VIEW_RANGE) {
                g->visible[y][x] = 1;
                g->discovered[y][x] = 1;
            }
        }
    }
}

static void refresh_save_available(Game *g) {
    FILE *file = fopen(SAVE_PATH, "rb");
    g->save_available = file != NULL;
    if (file) fclose(file);
}

static void delete_save(Game *g) {
    remove(SAVE_PATH);
    g->save_available = 0;
}

static void save_game(Game *g) {
    SaveState save;
    FILE *file;
    if (g->mode != MODE_GAME) return;
    memset(&save, 0, sizeof(save));
    save.version = SAVE_VERSION;
    memcpy(save.board, g->board, sizeof(save.board));
    memcpy(save.board_items, g->board_items, sizeof(save.board_items));
    memcpy(save.discovered, g->discovered, sizeof(save.discovered));
    memcpy(save.enemies, g->enemies, sizeof(save.enemies));
    save.enemy_count = g->enemy_count;
    save.player = g->player;
    save.floor = g->floor;
    save.turn = g->turn;
    file = fopen(SAVE_PATH, "wb");
    if (!file) return;
    if (fwrite(&save, sizeof(save), 1, file) == 1) g->save_available = 1;
    fclose(file);
}

static int load_game(Game *g) {
    SaveState save;
    FILE *file = fopen(SAVE_PATH, "rb");
    if (!file) return 0;
    if (fread(&save, sizeof(save), 1, file) != 1 || save.version != SAVE_VERSION) {
        fclose(file);
        return 0;
    }
    fclose(file);
    memcpy(g->board, save.board, sizeof(g->board));
    memcpy(g->board_items, save.board_items, sizeof(g->board_items));
    memcpy(g->discovered, save.discovered, sizeof(g->discovered));
    memcpy(g->enemies, save.enemies, sizeof(g->enemies));
    g->enemy_count = save.enemy_count;
    g->player = save.player;
    g->floor = save.floor;
    g->turn = save.turn;
    g->won = 0;
    g->mode = MODE_GAME;
    snprintf(g->message, sizeof(g->message), "RUN CONTINUED");
    reveal(g);
    return 1;
}

static void save_settings(Game *g) {
    FILE *file = fopen(SETTINGS_PATH, "wb");
    if (!file) return;
    fwrite(&g->music_level, sizeof(g->music_level), 1, file);
    fclose(file);
}

static void load_settings(Game *g) {
    FILE *file = fopen(SETTINGS_PATH, "rb");
    int level;
    if (!file) return;
    if (fread(&level, sizeof(level), 1, file) == 1 && level >= 0 && level <= 3)
        g->music_level = level;
    fclose(file);
}

static void start_floor(Game *g) {
    int x;
    int y;
    int i;
    int objective_found;
    unsigned char reachable[BOARD_SIZE][BOARD_SIZE];
    generate_layout(g);
    g->enemy_count = 2 + g->floor / 2;
    if (g->enemy_count > 12) g->enemy_count = 12;
    memset(g->enemies, 0, sizeof(g->enemies));

    g->player.x = random_range(0, 1);
    g->player.y = random_range(0, 2);
    if (blocks_move(g, g->player.x, g->player.y)) g->board[g->player.y][g->player.x] = CELL_FLOOR;
    mark_reachable(g, reachable);

    objective_found = random_reachable_clear_tile(g, reachable, 1, &x, &y);
    if (!objective_found)
        objective_found = random_reachable_clear_tile(g, reachable, 0, &x, &y);
    if (objective_found)
        g->board_items[y][x] = g->floor == 20 ? ITEM_BOOK : ITEM_STAIR;
    if (g->floor == 1 && g->player.weapons[0].kind == WEAPON_NONE) {
        if (random_reachable_clear_tile(g, reachable, 0, &x, &y))
            g->board_items[y][x] = ITEM_SMALL_SWORD;
    } else if ((g->floor % 2) == 0) {
        WeaponKind kind = WEAPON_SMALL_SWORD;
        int roll = random_range(0, 99);
        if (g->floor >= 14 && roll < 10) kind = WEAPON_GOLDEN_SWORD;
        else if (g->floor >= 10 && roll < 20) kind = WEAPON_GREEN_HAMMER;
        else if (g->floor >= 5 && roll < 32) kind = WEAPON_WARHAMMER;
        else if (g->floor >= 5 && roll < 44) kind = WEAPON_STUN_WAND;
        else if (g->floor >= 5 && roll < 54) kind = WEAPON_DISPLACEMENT;
        else if (g->floor >= 4 && roll < 69) kind = WEAPON_DAGGER;
        else if (g->floor >= 4 && roll < 84) kind = WEAPON_MEDIUM_SWORD;
        else if (g->floor >= 7 && roll < 93) kind = WEAPON_AXE;
        else if (g->floor >= 7) kind = WEAPON_SPEAR;
        if (random_reachable_clear_tile(g, reachable, 0, &x, &y))
            g->board_items[y][x] = weapon_item(kind);
    }

    if (g->floor >= 2 && random_reachable_clear_tile(g, reachable, 0, &x, &y))
        g->board_items[y][x] = random_potion_for_floor(g->floor);
    if (random_reachable_clear_tile(g, reachable, 0, &x, &y))
        g->board_items[y][x] = ITEM_GOLD;
    if (g->floor >= 6 && random_reachable_clear_tile(g, reachable, 0, &x, &y))
        g->board_items[y][x] = g->floor >= 12 && (rand() & 1) ? ITEM_LARGE_VASE : ITEM_SMALL_VASE;
    if ((g->floor % 5) == 0 && random_reachable_clear_tile(g, reachable, 0, &x, &y))
        g->board_items[y][x] = (g->floor % 10) == 0 ? ITEM_HERBALIST : ITEM_FORGE;
    if (g->floor >= 6 && random_reachable_clear_tile(g, reachable, 0, &x, &y))
        g->board_items[y][x] = ITEM_SPIKES_HIDDEN;

    for (i = 0; i < g->enemy_count; ++i) {
        if (!random_reachable_clear_tile(g, reachable, 0, &x, &y)) {
            g->enemy_count = i;
            break;
        }
        g->enemies[i].x = x;
        g->enemies[i].y = y;
        configure_enemy(&g->enemies[i], g->floor);
    }
    snprintf(g->message, sizeof(g->message), "FLOOR %d", g->floor);
    reveal(g);
}

static void start_game(Game *g) {
    delete_save(g);
    g->floor = 1;
    g->turn = 0;
    memset(&g->player, 0, sizeof(g->player));
    g->player.hp = 8;
    g->player.max_hp = 8;
    g->won = 0;
    g->help_visible = 0;
    g->mode = MODE_GAME;
    start_floor(g);
}

static int clear_shot(const Game *g, const Enemy *e) {
    int x = e->x;
    int y = e->y;
    int dx = (g->player.x > x) - (g->player.x < x);
    int dy = (g->player.y > y) - (g->player.y < y);
    if (dx && dy) return 0;
    x += dx;
    y += dy;
    while (x != g->player.x || y != g->player.y) {
        if (blocks_move(g, x, y) || enemy_at(g, x, y) >= 0) return 0;
        x += dx;
        y += dy;
    }
    return 1;
}

static void drop_enemy_loot(Game *g, Enemy *e) {
    ItemKind drop = ITEM_NONE;
    g->player.kills++;
    if (random_range(0, 99) < 45) {
        if (e->kind == ENEMY_VIPER) drop = ITEM_GREEN_HAMMER;
        else if (e->kind == ENEMY_GOLDEN_SNAKE) drop = ITEM_GOLDEN_SWORD;
        else if (e->kind == ENEMY_NOVICE) drop = ITEM_MEDIUM_SWORD;
        else if (e->kind == ENEMY_MONK) drop = (rand() & 1) ? ITEM_AXE : ITEM_SPEAR;
        else if (e->kind == ENEMY_LIBRARIAN) drop = ITEM_POTION_REGEN;
        else if (e->kind == ENEMY_PIXIE) drop = ITEM_DISPLACEMENT;
        else if (e->kind >= ENEMY_GARGOYLE) drop = random_potion_for_floor(g->floor);
        else drop = ITEM_GOLD;
    }
    if (drop != ITEM_NONE && g->board_items[e->y][e->x] == ITEM_NONE)
        g->board_items[e->y][e->x] = drop;
    else
        g->player.gold += random_range(0, 2);
    e->alive = 0;
}

static int spawn_minion(Game *g, const Enemy *source, EnemyKind kind) {
    static const int offsets[4][2] = {{0,-1}, {0,1}, {-1,0}, {1,0}};
    int slot;
    int i;
    if (g->enemy_count >= MAX_ENEMIES) return 0;
    slot = g->enemy_count;
    for (i = 0; i < 4; ++i) {
        int x = source->x + offsets[i][0];
        int y = source->y + offsets[i][1];
        Enemy *e;
        if (blocks_move(g, x, y) || occupied(g, x, y)) continue;
        e = &g->enemies[slot];
        memset(e, 0, sizeof(*e));
        e->x = x; e->y = y; e->alive = 1; e->kind = kind;
        if (kind == ENEMY_GHOST) {
            e->sprite = 12; e->hp = 6; e->damage = 2;
            e->r = 195; e->g = 234; e->b = 254;
        } else {
            e->sprite = 14; e->hp = 2; e->damage = 1;
            e->r = 255; e->g = 126; e->b = 102;
        }
        g->enemy_count++;
        return 1;
    }
    return 0;
}

static void enemy_turn(Game *g) {
    int i;
    int acting_count = g->enemy_count;
    for (i = 0; i < acting_count; ++i) {
        Enemy *e = &g->enemies[i];
        int dx;
        int dy;
        int nx;
        int ny;
        if (!e->alive) continue;
        if (e->poisoned > 0) {
            e->poisoned--;
            e->hp--;
            if (e->hp <= 0) {
                drop_enemy_loot(g, e);
                continue;
            }
        }
        if (e->stunned > 0) {
            e->stunned--;
            continue;
        }
        dx = g->player.x - e->x;
        dy = g->player.y - e->y;
        if ((e->kind == ENEMY_GOLEM || e->kind == ENEMY_EXORCIST) && --e->cooldown <= 0) {
            spawn_minion(g, e, e->kind == ENEMY_EXORCIST ? ENEMY_GHOST : ENEMY_RAT);
            e->cooldown = e->kind == ENEMY_EXORCIST ? 6 : 4;
        }
        if (abs(dx) + abs(dy) == 1) {
            g->player.hp -= e->damage;
            if (g->player.immune == 0 && e->poison_damage > g->player.poisoned)
                g->player.poisoned = e->poison_damage;
            if (e->kind == ENEMY_PIXIE) {
                int px = g->player.x;
                int py = g->player.y;
                g->player.x = e->x; g->player.y = e->y;
                e->x = px; e->y = py;
            }
            snprintf(g->message, sizeof(g->message), "%s HITS FOR %d",
                     enemy_name(e->kind), e->damage);
            continue;
        }
        if (e->range > 0 && abs(dx) + abs(dy) <= e->range && clear_shot(g, e)) {
            g->player.hp -= e->damage;
            if (g->player.immune == 0 && e->poison_damage > g->player.poisoned)
                g->player.poisoned = e->poison_damage;
            snprintf(g->message, sizeof(g->message), "%s BLASTS FOR %d",
                     enemy_name(e->kind), e->damage);
            continue;
        }
        if (abs(dx) > abs(dy)) {
            dx = dx < 0 ? -1 : 1;
            dy = 0;
        } else {
            dy = dy < 0 ? -1 : 1;
            dx = 0;
        }
        nx = e->x + dx;
        ny = e->y + dy;
        if ((e->kind == ENEMY_GHOST || !blocks_move(g, nx, ny)) &&
            nx >= 0 && ny >= 0 && nx < BOARD_SIZE && ny < BOARD_SIZE && !occupied(g, nx, ny)) {
            e->x = nx;
            e->y = ny;
        } else {
            int tx = e->x + (dy != 0 ? (rand() & 1 ? 1 : -1) : 0);
            int ty = e->y + (dx != 0 ? (rand() & 1 ? 1 : -1) : 0);
            if (!blocks_move(g, tx, ty) && !occupied(g, tx, ty)) {
                e->x = tx;
                e->y = ty;
            }
        }
    }
    if (g->player.hp <= 0) {
        g->player.hp = 0;
        g->mode = MODE_DEAD;
        delete_save(g);
        trigger_sfx(g, SFX_DEFEAT);
        snprintf(g->message, sizeof(g->message), "THE TOWER CLAIMS YOU");
    }
}

static void advance_traps(Game *g) {
    int x;
    int y;
    for (y = 0; y < BOARD_SIZE; ++y) {
        for (x = 0; x < BOARD_SIZE; ++x) {
            if (g->board_items[y][x] == ITEM_SPIKES_HIDDEN)
                g->board_items[y][x] = ITEM_SPIKES_HALF;
            else if (g->board_items[y][x] == ITEM_SPIKES_HALF)
                g->board_items[y][x] = ITEM_SPIKES;
            else if (g->board_items[y][x] == ITEM_SPIKES)
                g->board_items[y][x] = ITEM_SPIKES_HIDDEN;
        }
    }
    if (g->board_items[g->player.y][g->player.x] == ITEM_SPIKES) {
        g->player.hp--;
        snprintf(g->message, sizeof(g->message), "SPIKES HIT FOR 1");
        trigger_sfx(g, SFX_HIT);
    }
}

static void finish_turn(Game *g) {
    g->turn++;
    advance_traps(g);
    enemy_turn(g);
    if (g->mode == MODE_GAME && g->player.poisoned > 0) {
        g->player.poisoned--;
        g->player.hp--;
        if (g->player.hp <= 0) {
            g->player.hp = 0;
            g->mode = MODE_DEAD;
            delete_save(g);
            trigger_sfx(g, SFX_DEFEAT);
            snprintf(g->message, sizeof(g->message), "POISON CLAIMS YOU");
        }
    }
    if (g->mode == MODE_GAME && g->player.regenerating > 0) {
        g->player.regenerating--;
        if (g->player.hp < g->player.max_hp) g->player.hp++;
    }
    if (g->player.immune > 0) g->player.immune--;
    reveal(g);
    save_game(g);
}

static void attack_enemy(Game *g, int enemy, int dx, int dy) {
    Enemy *target = &g->enemies[enemy];
    Weapon *weapon = active_weapon(g);
    WeaponKind kind = weapon->kind;
    int minimum;
    int maximum;
    int damage;
    weapon_damage(kind, &minimum, &maximum);
    damage = random_range(minimum, maximum);

    if (kind == WEAPON_STUN_WAND) {
        target->stunned += random_range(3, 6);
        snprintf(g->message, sizeof(g->message), "%s STUNNED", enemy_name(target->kind));
    } else {
        target->hp -= damage;
        if (kind == WEAPON_DAGGER) target->poisoned += random_range(4, 6);
        if (kind == WEAPON_GREEN_HAMMER) target->poisoned += random_range(2, 3);
        snprintf(g->message, sizeof(g->message), "YOU STRIKE FOR %d", damage);
    }

    if (target->kind == ENEMY_BASILISK && target->hp > 0) {
        g->player.stunned += 2;
        snprintf(g->message, sizeof(g->message), "BASILISK STUNS YOU");
    }

    if (kind == WEAPON_DISPLACEMENT && target->hp > 0) {
        int px = g->player.x;
        int py = g->player.y;
        g->player.x = target->x;
        g->player.y = target->y;
        target->x = px;
        target->y = py;
        target->stunned++;
    } else if ((kind == WEAPON_WARHAMMER || kind == WEAPON_GREEN_HAMMER) && target->hp > 0) {
        int step;
        for (step = 0; step < 2; ++step) {
            int px = target->x + dx;
            int py = target->y + dy;
            if (blocks_move(g, px, py) || occupied(g, px, py)) break;
            target->x = px;
            target->y = py;
        }
    }

    if (kind == WEAPON_AXE) {
        int i;
        for (i = 0; i < g->enemy_count; ++i) {
            Enemy *other = &g->enemies[i];
            if (i != enemy && other->alive &&
                abs(other->x - g->player.x) + abs(other->y - g->player.y) == 1)
                other->hp -= damage;
        }
    } else if (kind == WEAPON_SPEAR) {
        int second = enemy_at(g, g->player.x + dx * 2, g->player.y + dy * 2);
        if (second >= 0) g->enemies[second].hp -= damage;
    }

    {
        int i;
        for (i = 0; i < g->enemy_count; ++i) {
            if (g->enemies[i].alive && g->enemies[i].hp <= 0) {
                drop_enemy_loot(g, &g->enemies[i]);
            }
        }
    }
    trigger_sfx(g, SFX_HIT);
    use_durability(g);
}

static void use_consumable(Game *g) {
    ItemKind item = g->player.consumables[g->player.active_consumable];
    unsigned char reachable[BOARD_SIZE][BOARD_SIZE];
    int x;
    int y;
    if (!is_potion(item)) {
        snprintf(g->message, sizeof(g->message), "NO POTION IN SLOT");
        return;
    }
    g->player.discovered_potions |= potion_bit(item);
    g->player.consumables[g->player.active_consumable] = ITEM_NONE;
    if (item == ITEM_POTION_HEAL) {
        g->player.hp += random_range(1, 3);
        if (g->player.hp > g->player.max_hp) g->player.hp = g->player.max_hp;
    } else if (item == ITEM_POTION_REGEN) {
        g->player.regenerating += random_range(3, 6);
    } else if (item == ITEM_POTION_IMMUNITY) {
        g->player.immune += random_range(4, 6);
        g->player.poisoned = 0;
    } else if (item == ITEM_POTION_POISON) {
        if (g->player.immune == 0) g->player.poisoned += random_range(2, 4);
    } else if (item == ITEM_POTION_ANTIDOTE) {
        g->player.poisoned = 0;
    } else if (item == ITEM_POTION_TELEPORT) {
        mark_reachable(g, reachable);
        if (random_reachable_clear_tile(g, reachable, 0, &x, &y)) {
            g->player.x = x;
            g->player.y = y;
        }
    }
    snprintf(g->message, sizeof(g->message), "%s POTION USED", potion_name(item));
    trigger_sfx(g, item == ITEM_POTION_TELEPORT ? SFX_TELEPORT : SFX_USE);
    finish_turn(g);
}

static void break_vase(Game *g, int x, int y, ItemKind vase) {
    int roll = random_range(0, 99);
    if (roll < 35) g->board_items[y][x] = random_potion_for_floor(g->floor);
    else if (roll < 55 && vase == ITEM_LARGE_VASE)
        g->board_items[y][x] = weapon_item(g->floor >= 14 ? WEAPON_GOLDEN_SWORD : WEAPON_MEDIUM_SWORD);
    else if (roll < 80) g->board_items[y][x] = ITEM_GOLD;
    else g->board_items[y][x] = ITEM_NONE;
    snprintf(g->message, sizeof(g->message), "THE VASE SHATTERS");
    trigger_sfx(g, SFX_HIT);
    finish_turn(g);
}

static void use_fixture(Game *g, ItemKind fixture) {
    Weapon *weapon = active_weapon(g);
    if (g->player.gold < 5) {
        snprintf(g->message, sizeof(g->message), "NEED 5 GOLD");
        finish_turn(g);
        return;
    }
    if (fixture == ITEM_FORGE && weapon->kind == WEAPON_NONE) {
        snprintf(g->message, sizeof(g->message), "EQUIP A WEAPON FIRST");
        finish_turn(g);
        return;
    }
    g->player.gold -= 5;
    if (fixture == ITEM_FORGE) {
        weapon->durability += random_range(5, 10);
        snprintf(g->message, sizeof(g->message), "WEAPON REPAIRED");
    } else {
        int gain = random_range(1, 3);
        g->player.max_hp += gain;
        g->player.hp += gain;
        snprintf(g->message, sizeof(g->message), "MAX HP INCREASED");
    }
    trigger_sfx(g, SFX_UPGRADE);
    finish_turn(g);
}

static void move_player(Game *g, int dx, int dy) {
    int nx = g->player.x + dx;
    int ny = g->player.y + dy;
    int enemy;
    if (nx < 0 || ny < 0 || nx >= BOARD_SIZE || ny >= BOARD_SIZE) return;
    if (g->board_items[ny][nx] == ITEM_SMALL_VASE ||
        g->board_items[ny][nx] == ITEM_LARGE_VASE) {
        break_vase(g, nx, ny, g->board_items[ny][nx]);
        return;
    }
    if (g->board[ny][nx] == CELL_WALL || g->board[ny][nx] == CELL_PILLAR) {
        snprintf(g->message, sizeof(g->message), "A COLD STONE WALL");
        return;
    }
    if (g->board[ny][nx] == CELL_DOOR) {
        g->board[ny][nx] = CELL_OPEN_DOOR;
        snprintf(g->message, sizeof(g->message), "DOOR OPENED");
        finish_turn(g);
        return;
    }
    enemy = enemy_at(g, nx, ny);
    if (enemy >= 0) {
        attack_enemy(g, enemy, dx, dy);
        finish_turn(g);
        return;
    }
    g->player.x = nx;
    g->player.y = ny;
    if (g->board_items[ny][nx] == ITEM_FORGE ||
        g->board_items[ny][nx] == ITEM_HERBALIST) {
        use_fixture(g, g->board_items[ny][nx]);
        return;
    } else if (item_weapon(g->board_items[ny][nx]) != WEAPON_NONE) {
        WeaponKind kind = item_weapon(g->board_items[ny][nx]);
        g->board_items[ny][nx] = ITEM_NONE;
        pick_up_weapon(g, kind);
    } else if (is_potion(g->board_items[ny][nx])) {
        ItemKind potion = g->board_items[ny][nx];
        g->board_items[ny][nx] = ITEM_NONE;
        pick_up_consumable(g, potion);
    } else if (g->board_items[ny][nx] == ITEM_GOLD) {
        int amount = random_range(1, 3);
        g->board_items[ny][nx] = ITEM_NONE;
        g->player.gold += amount;
        snprintf(g->message, sizeof(g->message), "FOUND %d GOLD", amount);
        trigger_sfx(g, SFX_PICK);
    } else if (g->board_items[ny][nx] == ITEM_BOOK) {
        g->board_items[ny][nx] = ITEM_NONE;
        g->won = 1;
        g->mode = MODE_DEAD;
        delete_save(g);
        snprintf(g->message, sizeof(g->message), "THE LOST SCROLL IS FOUND");
        trigger_sfx(g, SFX_WIN);
        return;
    } else if (g->board_items[ny][nx] == ITEM_STAIR) {
        g->floor++;
        start_floor(g);
        trigger_sfx(g, SFX_ASCEND);
        save_game(g);
        return;
    } else {
        snprintf(g->message, sizeof(g->message), "TURN %d", g->turn + 1);
    }
    finish_turn(g);
}

static void sync_music_volume(Game *g) {
    int volume = g->mode == MODE_SETTINGS ? 0 : music_volume_for_level(g->music_level);
    if (g->audio_device) SDL_LockAudioDevice(g->audio_device);
    g->music.volume = volume;
    if (g->audio_device) SDL_UnlockAudioDevice(g->audio_device);
}

static void change_music(Game *g, int direction) {
    g->music_level = (g->music_level + direction + 4) % 4;
    sync_music_volume(g);
    save_settings(g);
}

static void open_settings(Game *g) {
    g->settings_return_mode = g->mode;
    g->settings_selection = 0;
    g->mode = MODE_SETTINGS;
    if (g->audio_device) SDL_LockAudioDevice(g->audio_device);
    g->music.sfx_remaining = 0;
    if (g->audio_device) SDL_UnlockAudioDevice(g->audio_device);
    sync_music_volume(g);
}

static void close_settings(Game *g) {
    g->mode = g->settings_return_mode;
    sync_music_volume(g);
}

static void activate_setting(Game *g) {
    switch (g->settings_selection) {
        case 0: close_settings(g); break;
        case 1: change_music(g, 1); break;
        case 2: start_game(g); sync_music_volume(g); break;
        case 3: g->mode = MODE_TITLE; sync_music_volume(g); break;
        case 4: g->exit_requested = 1; break;
        default: break;
    }
}

static void handle_input(Game *g, const SceCtrlData *pad) {
    unsigned int pressed = pad->Buttons & ~g->previous_buttons;
    int analog_x = (int)pad->Lx - 128;
    int analog_y = (int)pad->Ly - 128;
    if (g->mode == MODE_GAME && (pressed & PSP_CTRL_SELECT)) {
        g->help_visible = !g->help_visible;
    } else if (g->help_visible) {
        if (pressed & PSP_CTRL_CIRCLE) g->help_visible = 0;
    } else if (pressed & PSP_CTRL_START) {
        if (g->mode == MODE_SETTINGS) close_settings(g);
        else open_settings(g);
    } else if (g->mode == MODE_SETTINGS) {
        if (pressed & PSP_CTRL_UP)
            g->settings_selection = (g->settings_selection + 4) % 5;
        else if (pressed & PSP_CTRL_DOWN)
            g->settings_selection = (g->settings_selection + 1) % 5;
        else if (g->settings_selection == 1 && (pressed & PSP_CTRL_LEFT))
            change_music(g, -1);
        else if (g->settings_selection == 1 && (pressed & PSP_CTRL_RIGHT))
            change_music(g, 1);
        else if (pressed & PSP_CTRL_CROSS)
            activate_setting(g);
        else if (pressed & PSP_CTRL_CIRCLE)
            close_settings(g);
    }
    else if (g->mode == MODE_TITLE) {
        if (pressed & PSP_CTRL_CROSS)
            start_game(g);
        else if ((pressed & PSP_CTRL_TRIANGLE) && g->save_available)
            load_game(g);
    } else if (g->mode == MODE_DEAD) {
        if (pressed & PSP_CTRL_CROSS)
            start_game(g);
        else if (pressed & PSP_CTRL_CIRCLE) {
            g->mode = MODE_TITLE;
            refresh_save_available(g);
        }
    } else {
        unsigned int turn_actions = PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE |
                                    PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT;
        int analog_action = !g->input_lock && (abs(analog_x) > 55 || abs(analog_y) > 55);
        if (g->player.stunned > 0 && ((pressed & turn_actions) || analog_action)) {
            g->player.stunned--;
            snprintf(g->message, sizeof(g->message), "YOU ARE STUNNED");
            if (analog_action) g->input_lock = 1;
            finish_turn(g);
        } else if (pressed & PSP_CTRL_LTRIGGER) {
            g->player.active_weapon = (g->player.active_weapon + WEAPON_SLOTS - 1) % WEAPON_SLOTS;
            save_game(g);
        } else if (pressed & PSP_CTRL_RTRIGGER) {
            g->player.active_weapon = (g->player.active_weapon + 1) % WEAPON_SLOTS;
            save_game(g);
        } else if (pressed & PSP_CTRL_CIRCLE) {
            g->player.active_consumable = (g->player.active_consumable + 1) % CONSUMABLE_SLOTS;
            save_game(g);
        } else if (pressed & PSP_CTRL_SQUARE) use_consumable(g);
        else if (pressed & PSP_CTRL_TRIANGLE) finish_turn(g);
        else if (pressed & PSP_CTRL_UP) move_player(g, 0, -1);
        else if (pressed & PSP_CTRL_DOWN) move_player(g, 0, 1);
        else if (pressed & PSP_CTRL_LEFT) move_player(g, -1, 0);
        else if (pressed & PSP_CTRL_RIGHT) move_player(g, 1, 0);
        else if (!g->input_lock && abs(analog_x) > 55) {
            move_player(g, analog_x < 0 ? -1 : 1, 0);
            g->input_lock = 1;
        } else if (!g->input_lock && abs(analog_y) > 55) {
            move_player(g, 0, analog_y < 0 ? -1 : 1);
            g->input_lock = 1;
        }
    }
    if (abs(analog_x) < 30 && abs(analog_y) < 30) g->input_lock = 0;
    g->previous_buttons = pad->Buttons;
}

static void draw_board(Game *g) {
    int x;
    int y;
    for (y = 0; y < BOARD_SIZE; ++y) {
        for (x = 0; x < BOARD_SIZE; ++x) {
            int px = BOARD_X + x * TILE;
            int py = BOARD_Y + y * TILE;
            int tile_index = 0;
            if (!g->discovered[y][x]) {
                set_color(g->renderer, 20, 10, 20, 255);
                SDL_Rect unknown = {px, py, TILE, TILE};
                SDL_RenderFillRect(g->renderer, &unknown);
                continue;
            }
            draw_atlas(g, g->tiles, 0, px, py, TILE, 72, 54, 61, 255);
            if (g->board[y][x] == CELL_WALL) tile_index = 4;
            if (g->board[y][x] == CELL_PILLAR) tile_index = 13;
            if (g->board[y][x] == CELL_DOOR) tile_index = 3;
            if (g->board[y][x] == CELL_OPEN_DOOR) tile_index = 1;
            if (tile_index) draw_atlas(g, g->tiles, tile_index, px, py, TILE, 255, 255, 255, 255);
            if (g->board_items[y][x] == ITEM_STAIR)
                draw_atlas(g, g->tiles, 2, px, py, TILE, 255, 255, 255, 255);
            if (g->board_items[y][x] == ITEM_FORGE)
                draw_atlas(g, g->tiles, 10, px, py, TILE, 255, 191, 102, 255);
            if (g->board_items[y][x] == ITEM_HERBALIST)
                draw_atlas(g, g->tiles, 8, px, py, TILE, 145, 200, 185, 255);
            if (g->board_items[y][x] == ITEM_SPIKES_HIDDEN)
                draw_atlas(g, g->tiles, 16, px, py, TILE, 126, 104, 104, 255);
            if (g->board_items[y][x] == ITEM_SPIKES_HALF)
                draw_atlas(g, g->tiles, 17, px, py, TILE, 189, 200, 220, 255);
            if (g->board_items[y][x] == ITEM_SPIKES)
                draw_atlas(g, g->tiles, 18, px, py, TILE, 255, 126, 102, 255);
            if (g->board_items[y][x] == ITEM_BOOK)
                draw_atlas(g, g->items, 18, px, py, TILE, 255, 255, 255, 255);
            if (g->board_items[y][x] == ITEM_GOLD)
                draw_atlas(g, g->items, 19, px, py, TILE, 255, 191, 102, 255);
            if (is_potion(g->board_items[y][x]))
                draw_atlas(g, g->items, 16, px, py, TILE, 195, 234, 254, 255);
            if (g->board_items[y][x] == ITEM_SMALL_VASE)
                draw_atlas(g, g->items, 25, px, py, TILE, 189, 200, 220, 255);
            if (g->board_items[y][x] == ITEM_LARGE_VASE)
                draw_atlas(g, g->items, 26, px, py, TILE, 255, 191, 102, 255);
            if (item_weapon(g->board_items[y][x]) != WEAPON_NONE) {
                WeaponKind kind = item_weapon(g->board_items[y][x]);
                unsigned char red = 189, green = 200, blue = 220;
                if (kind == WEAPON_DAGGER) { red = 145; green = 200; blue = 185; }
                if (kind == WEAPON_STUN_WAND) { red = 134; green = 241; blue = 249; }
                if (kind == WEAPON_DISPLACEMENT) { red = 255; green = 102; blue = 145; }
                if (kind == WEAPON_WARHAMMER) { red = 255; green = 126; blue = 102; }
                if (kind == WEAPON_GOLDEN_SWORD) { red = 255; green = 191; blue = 102; }
                if (kind == WEAPON_GREEN_HAMMER) { red = 145; green = 200; blue = 185; }
                draw_atlas(g, g->items, weapon_sprite(kind), px, py, TILE,
                           red, green, blue, 255);
            }
            if (!g->visible[y][x]) {
                set_color(g->renderer, 18, 9, 20, 180);
                SDL_Rect fog = {px, py, TILE, TILE};
                SDL_RenderFillRect(g->renderer, &fog);
            }
        }
    }
}

static void draw_actors(Game *g) {
    int i;
    int frame = (SDL_GetTicks() / 500) & 1;
    for (i = 0; i < g->enemy_count; ++i) {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || !g->visible[e->y][e->x]) continue;
        draw_atlas(g, g->units, e->sprite + frame,
                   BOARD_X + e->x * TILE, BOARD_Y + e->y * TILE, TILE,
                   e->r, e->g, e->b, 255);
    }
    draw_atlas(g, g->units, (active_weapon(g)->kind != WEAPON_NONE ? 2 : 0) + frame,
               BOARD_X + g->player.x * TILE, BOARD_Y + g->player.y * TILE,
               TILE, 255, 255, 255, 255);
}

static void draw_hud(Game *g) {
    char line[48];
    SDL_Rect panel = {272, 8, 200, 256};
    set_color(g->renderer, 48, 32, 36, 255);
    SDL_RenderFillRect(g->renderer, &panel);
    draw_text(g, "MONK TOWER", 288, 18, 2, 255, 191, 102);
    snprintf(line, sizeof(line), "FLOOR %d/20", g->floor);
    draw_text(g, line, 288, 48, 2, 189, 200, 220);
    snprintf(line, sizeof(line), "HP %d/%d", g->player.hp, g->player.max_hp);
    draw_text(g, line, 288, 72, 2, 255, 126, 102);
    snprintf(line, sizeof(line), "GOLD %d", g->player.gold);
    draw_text(g, line, 288, 96, 2, 255, 191, 102);
    if (g->player.poisoned > 0) {
        snprintf(line, sizeof(line), "POISON %d", g->player.poisoned);
        draw_text(g, line, 384, 96, 1, 145, 200, 185);
    }
    {
        int i;
        for (i = 0; i < WEAPON_SLOTS; ++i) {
            Weapon *w = &g->player.weapons[i];
            snprintf(line, sizeof(line), "%c%d %s %d",
                     i == g->player.active_weapon ? 'X' : ' ', i + 1,
                     weapon_name(w->kind), w->durability);
            draw_text(g, line, 282, 114 + i * 13, 1,
                      i == g->player.active_weapon ? 255 : 189,
                      i == g->player.active_weapon ? 191 : 200,
                      i == g->player.active_weapon ? 102 : 220);
        }
    }
    snprintf(line, sizeof(line), "P%d %s", g->player.active_consumable + 1,
             shown_potion_name(&g->player,
                               g->player.consumables[g->player.active_consumable]));
    draw_text(g, line, 282, 168, 1, 195, 234, 254);
    snprintf(line, sizeof(line), "PSN %d REG %d IMM %d STN %d",
             g->player.poisoned, g->player.regenerating,
             g->player.immune, g->player.stunned);
    draw_text(g, line, 282, 181, 1, 145, 200, 185);
    draw_text(g, g->message, 280, 198, 1, 255, 191, 102);
    draw_text(g, "L/R WEAPON  O ITEM", 280, 224, 1, 150, 128, 128);
    draw_text(g, "SQ USE TRI WAIT SEL", 280, 239, 1, 150, 128, 128);
}

static void draw_help(Game *g) {
    SDL_Rect shade = {0, 0, SCREEN_W, SCREEN_H};
    SDL_Rect panel = {54, 18, 372, 236};
    set_color(g->renderer, 10, 5, 12, 190);
    SDL_RenderFillRect(g->renderer, &shade);
    set_color(g->renderer, 48, 32, 36, 250);
    SDL_RenderFillRect(g->renderer, &panel);
    set_color(g->renderer, 145, 200, 185, 255);
    SDL_RenderDrawRect(g->renderer, &panel);
    draw_text(g, "TOWER GUIDE", 150, 32, 2, 255, 191, 102);
    draw_text(g, "D PAD OR STICK  MOVE AND ATTACK", 76, 70, 1, 189, 200, 220);
    draw_text(g, "L R  CHANGE WEAPON", 76, 88, 1, 189, 200, 220);
    draw_text(g, "O  CHANGE POTION   SQUARE  USE", 76, 106, 1, 189, 200, 220);
    draw_text(g, "TRIANGLE  WAIT     START  PAUSE", 76, 124, 1, 189, 200, 220);
    draw_text(g, "BUMP VASES TO BREAK THEM", 76, 151, 1, 145, 200, 185);
    draw_text(g, "FORGE AND HERBALIST COST 5 GOLD", 76, 169, 1, 145, 200, 185);
    draw_text(g, "FIND THE LOST SCROLL ON FLOOR 20", 76, 187, 1, 255, 191, 102);
    draw_text(g, "SELECT OR O  CLOSE", 150, 226, 1, 150, 128, 128);
}

static void draw_settings(Game *g) {
    static const char *const entries[] = {
        "RESUME", "MUSIC", "RESTART RUN", "RETURN TO TITLE", "EXIT TO XMB"
    };
    SDL_Rect shade = {0, 0, SCREEN_W, SCREEN_H};
    SDL_Rect panel = {80, 22, 320, 228};
    int i;
    char line[40];
    set_color(g->renderer, 10, 5, 12, 185);
    SDL_RenderFillRect(g->renderer, &shade);
    set_color(g->renderer, 48, 32, 36, 250);
    SDL_RenderFillRect(g->renderer, &panel);
    set_color(g->renderer, 145, 200, 185, 255);
    SDL_RenderDrawRect(g->renderer, &panel);
    draw_text(g, "PAUSE AND SETTINGS", 116, 38, 2, 255, 191, 102);
    for (i = 0; i < 5; ++i) {
        unsigned char r = i == g->settings_selection ? 255 : 189;
        unsigned char green = i == g->settings_selection ? 191 : 200;
        unsigned char b = i == g->settings_selection ? 102 : 220;
        snprintf(line, sizeof(line), "%c %s", i == g->settings_selection ? 'X' : ' ', entries[i]);
        draw_text(g, line, 116, 82 + i * 27, 1, r, green, b);
        if (i == 1) {
            snprintf(line, sizeof(line), "- %s -", music_level_name(g->music_level));
            draw_text(g, line, 276, 82 + i * 27, 1, r, green, b);
        }
    }
    draw_text(g, "X SELECT  O BACK  START CLOSE", 102, 224, 1, 150, 128, 128);
}

static void render(Game *g) {
    GameMode scene_mode = g->mode == MODE_SETTINGS ? g->settings_return_mode : g->mode;
    set_color(g->renderer, 38, 18, 34, 255);
    SDL_RenderClear(g->renderer);
    if (scene_mode == MODE_TITLE) {
        SDL_Rect dst = {160, 24, 160, 240};
        SDL_RenderCopy(g->renderer, g->splash, NULL, &dst);
        draw_text(g, "X NEW RUN", 186, 224, 1, 255, 191, 102);
        if (g->save_available)
            draw_text(g, "TRI CONTINUE", 174, 242, 1, 145, 200, 185);
    } else {
        draw_board(g);
        draw_actors(g);
        draw_hud(g);
        if (scene_mode == MODE_DEAD) {
            char stats[48];
            SDL_Rect overlay = {40, 82, 400, 116};
            set_color(g->renderer, 20, 10, 20, 235);
            SDL_RenderFillRect(g->renderer, &overlay);
            draw_text(g, g->message, 72, 101, 2,
                      g->won ? 255 : 255, g->won ? 191 : 126, 102);
            snprintf(stats, sizeof(stats), "FLOOR %d  TURNS %d  KILLS %d",
                     g->floor, g->turn, g->player.kills);
            draw_text(g, stats, 104, 137, 1, 145, 200, 185);
            draw_text(g, "X RESTART  O TITLE", 126, 166, 1, 189, 200, 220);
        }
    }
    if (g->mode == MODE_SETTINGS) draw_settings(g);
    if (g->help_visible) draw_help(g);
    SDL_RenderPresent(g->renderer);
}

static int init_game(Game *g) {
    memset(g, 0, sizeof(*g));
    SDL_AudioSpec desired;
    mkdir(SAVE_DIR, 0777);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) return 0;
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return 0;
    g->window = SDL_CreateWindow("Monk Tower PSP", SDL_WINDOWPOS_UNDEFINED,
                                 SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    if (!g->window) return 0;
    g->renderer = SDL_CreateRenderer(g->window, -1, SDL_RENDERER_ACCELERATED);
    if (!g->renderer) g->renderer = SDL_CreateRenderer(g->window, -1, SDL_RENDERER_SOFTWARE);
    if (!g->renderer) return 0;
    SDL_RenderSetLogicalSize(g->renderer, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    g->units = load_texture(g, "assets/sprites/units.png");
    g->tiles = load_texture(g, "assets/sprites/tiles.png");
    g->items = load_texture(g, "assets/sprites/items.png");
    g->splash = load_texture(g, "assets/ui/splash.png");
    if (!g->units || !g->tiles || !g->items || !g->splash) return 0;
    memset(&desired, 0, sizeof(desired));
    desired.freq = MUSIC_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = music_callback;
    desired.userdata = &g->music;
    g->music_level = 2;
    load_settings(g);
    g->music.volume = music_volume_for_level(g->music_level);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        g->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
        if (g->audio_device) SDL_PauseAudioDevice(g->audio_device, 0);
    }
    g->mode = MODE_TITLE;
    refresh_save_available(g);
    return 1;
}

static void shutdown_game(Game *g) {
    if (g->audio_device) SDL_CloseAudioDevice(g->audio_device);
    SDL_DestroyTexture(g->units);
    SDL_DestroyTexture(g->tiles);
    SDL_DestroyTexture(g->items);
    SDL_DestroyTexture(g->splash);
    SDL_DestroyRenderer(g->renderer);
    SDL_DestroyWindow(g->window);
    IMG_Quit();
    SDL_Quit();
}

int main(int argc, char **argv) {
    Game game;
    SceCtrlData pad;
    int running = 1;
    (void)argc; (void)argv;
    SDL_SetMainReady();
    setup_callbacks();
    srand((unsigned int)time(NULL));
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    if (!init_game(&game)) {
        show_missing_assets_screen(&game);
        shutdown_game(&game);
        return 1;
    }
#ifdef QA_AUTOSTART
    start_game(&game);
#endif
#ifdef QA_SETTINGS
    start_game(&game);
    open_settings(&game);
#endif
#ifdef QA_CONTENT
    start_game(&game);
    game.floor = 12;
    game.player.gold = 12;
    game.player.consumables[0] = ITEM_POTION_HEAL;
    game.player.consumables[1] = ITEM_POTION_TELEPORT;
    game.player.discovered_potions = potion_bit(ITEM_POTION_HEAL);
    pick_up_weapon(&game, WEAPON_GREEN_HAMMER);
    start_floor(&game);
#endif
#ifdef QA_SAVE
    start_game(&game);
    game.player.gold = 77;
    save_game(&game);
    game.player.gold = 0;
    if (load_game(&game) && game.player.gold == 77)
        snprintf(game.message, sizeof(game.message), "SAVE TEST PASSED");
    else
        snprintf(game.message, sizeof(game.message), "SAVE TEST FAILED");
    delete_save(&game);
#endif
#ifdef QA_WIN
    {
        int qx;
        int qy;
        start_game(&game);
        game.floor = 20;
        start_floor(&game);
        for (qy = 0; qy < BOARD_SIZE; ++qy) {
            for (qx = 0; qx < BOARD_SIZE; ++qx) {
                if (game.board_items[qy][qx] == ITEM_BOOK) {
                    game.player.x = qx;
                    game.player.y = qy;
                    move_player(&game, 0, 0);
                }
            }
        }
    }
#endif
    while (running && !game.exit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
        }
        sceCtrlPeekBufferPositive(&pad, 1);
        handle_input(&game, &pad);
        render(&game);
        SDL_Delay(16);
    }
    shutdown_game(&game);
    return 0;
}
