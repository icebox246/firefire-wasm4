#include "wasm4.h"
//
#include "art.h"
#include "bg2.h"
#include "bg3.h"
#include "bg4.h"
#include "fireball.h"
#include "mage.h"

#define MAX_PLAYER_COUNT 4
#define MAX_FIRE_COUNT 4
#define TILE_SIZE 6
#define PLAYER_SPEED 1
#define FIRE_SPEED 3
#define FIRE_SPEED_DIAG 2
#define FIRE_SIZE 5
#define FIRE_BOUNCE_TIME 10
#define FIRE_COMEBACK_TIME (60 * 4)
#define STARTUP_COUNTDOWN 5
#define MAP_WIDTH 26
#define MAP_HEIGHT 20
#define WIN_THRESHOLD 7

uint8_t frame = 0;
uint8_t prev_gamepads[MAX_PLAYER_COUNT] = {0};
uint8_t scores[MAX_PLAYER_COUNT] = {0};

uint16_t random_st = 0xbe;

uint16_t random() {
    random_st =
        (random_st >> 1) | (uint16_t)((((random_st >> 0) ^ (random_st >> 2) ^
                                        (random_st >> 3) ^ (random_st >> 5)) &
                                       1)
                                      << 15);
    return random_st;
}

enum State : uint8_t {
    MENU,
    GAMEPLAY,
    GAMEOVER,
} current_state = MENU;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t flip;
    uint8_t lhx;
    uint8_t lhy;
} Player;

typedef struct {
    uint16_t x;
    uint16_t y;
    int8_t dx;
    int8_t dy;
    uint8_t holder;
    enum : uint8_t {
        F_IDLE,
        F_HELD,
        F_FLYING,
        F_BOUNCING,
    } state;
    uint8_t bounce_timer;
} Fire;

const uint16_t music[] = {
    80, 120, 80, 120, 80, 120, 160, 200,
};
#define music_length (sizeof(music) / sizeof(music[0]))
uint16_t music_current_tone = 0;
const uint16_t tone_length = 15;

void play_music() {
    music_current_tone %= music_length;
    tone(music[music_current_tone], tone_length, 8, TONE_TRIANGLE);
}

Player players[MAX_PLAYER_COUNT];
Fire fires[MAX_FIRE_COUNT];

uint8_t selected_player_count_option = 0;
uint8_t selected_fire_count_option = 0;
uint16_t game_countdown = 0;

uint8_t last_winner = 0;

const uint32_t maps[3][MAP_HEIGHT] = {
    {
        // 2P
        0b00000000000111100000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000111100000000000,  //
        0b00000000001000010000000000,  //
        0b00000000001000010000000000,  //
        0b00000000001000010000000000,  //
        0b00000000001000010000000000,  //
        0b00000000000111100000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000111100000000000,  //
    },
    {
        // 3P
        0b00000000000111100000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000111100000000000,  //
        0b00000000001000010000000000,  //
        0b00000000001000010000000000,  //
        0b00000000001000010000000000,  //
        0b00000000011000011000000000,  //
        0b00000001110111101110000000,  //
        0b00000011000000000011000000,  //
        0b00001110000000000001110000,  //
        0b00011000000000000000011000,  //
        0b01110000000000000000001110,  //
        0b11000000000000000000000011,  //
        0b10000000000000000000000001,  //
        0b10000000000000000000000001,  //
        0b00000000000000000000000000,  //
    },
    {
        // 4P
        0b00000000000111100000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000111100000000000,  //
        0b10000000001000010000000001,  //
        0b11111111111000011111111111,  //
        0b11111111111000011111111111,  //
        0b10000000001000010000000001,  //
        0b00000000000111100000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000011000000000000,  //
        0b00000000000111100000000000,  //
    },
};

void init_palette() {
    PALETTE[0] = 0xFFF6D3;
    PALETTE[1] = 0x7C3F58;
    PALETTE[2] = 0xEB6B6F;
    PALETTE[3] = 0xF9A875;
}

void play_fireball_throw() {
    tone(450 | (210 << 16), 0 | (12 << 8) | (12 << 16) | (6 << 24),
         20 | (20 << 8), TONE_NOISE);
}

void play_fireball_hit() {
    tone(170 | (230 << 16), 0 | (4 << 8) | (2 << 16) | (0 << 24),
         20 | (25 << 8), TONE_NOISE);
}

void play_fireball_bounce() {
    tone(170 | (230 << 16), 0 | (4 << 8) | (2 << 16) | (0 << 24),
         20 | (25 << 8), TONE_PULSE1 | TONE_MODE2);
}

void play_menu_accept() {
    tone(350 | (700 << 16), 4 | (4 << 8) | (2 << 16) | (0 << 24), 11 | (5 << 8),
         TONE_PULSE1 | TONE_MODE2);
}

void play_menu_select() {
    tone(140 | (330 << 16), 2 | (0 << 8) | (4 << 16) | (0 << 24), 3 | (11 << 8),
         TONE_PULSE1 | TONE_MODE2);
}

void fire_catch(uint8_t player_index, uint8_t fire_index) {
    fires[fire_index].holder = player_index;
    fires[fire_index].state = F_HELD;
    fires[fire_index].dy = 0;
    fires[fire_index].dx =
        players[player_index].flip ? -FIRE_SPEED : FIRE_SPEED;
}

void start_game() {
    game_countdown = 60 * STARTUP_COUNTDOWN;
    current_state = GAMEPLAY;

    static struct {
        uint8_t x, y, flip;
    } starting_positions[MAX_PLAYER_COUNT] = {0};

    switch (selected_player_count_option) {
        case 0:
            starting_positions[0].x = TILE_SIZE * 3;
            starting_positions[0].y = TILE_SIZE * MAP_HEIGHT / 2;
            starting_positions[0].flip = 0;

            starting_positions[1].x = TILE_SIZE * (MAP_WIDTH - 1 - 3);
            starting_positions[1].y = TILE_SIZE * MAP_HEIGHT / 2;
            starting_positions[1].flip = 1;
            break;
        case 1:
            starting_positions[0].x = TILE_SIZE * 3;
            starting_positions[0].y = TILE_SIZE * MAP_HEIGHT / 3;
            starting_positions[0].flip = 0;

            starting_positions[1].x = TILE_SIZE * (MAP_WIDTH - 1 - 3);
            starting_positions[1].y = TILE_SIZE * MAP_HEIGHT / 3;
            starting_positions[1].flip = 1;

            starting_positions[2].x = TILE_SIZE * MAP_WIDTH / 2;
            starting_positions[2].y = TILE_SIZE * (MAP_HEIGHT - 1 - 2);
            starting_positions[0].flip = 0;
            break;
        case 2:
            for (uint8_t pi = 0; pi < 4; pi++) {
                starting_positions[pi].x =
                    !(pi & 1) ? TILE_SIZE * 3 : TILE_SIZE * (MAP_WIDTH - 1 - 3);
                starting_positions[pi].y =
                    !(pi & 2) ? TILE_SIZE * 2
                              : TILE_SIZE * (MAP_HEIGHT - 1 - 2);
                starting_positions[pi].flip = (pi & 1);
            }
            break;
    }

    {
        uint8_t i = 0;
        for (uint8_t c = selected_player_count_option + 2; c > 0; c--) {
            uint8_t si = random() % c;
            players[i].x = starting_positions[si].x;
            players[i].y = starting_positions[si].y;
            players[i].flip = starting_positions[si].flip;
            starting_positions[si] = starting_positions[c - 1];
            i++;
        }
    }

    if (selected_fire_count_option > selected_player_count_option + 1) {
        selected_fire_count_option -=
            selected_fire_count_option - (selected_player_count_option + 1);
    }

    {
        uint8_t taken = 0;
        for (uint8_t fi = 0; fi < selected_fire_count_option + 1; fi++) {
            uint8_t starter = random() % (selected_player_count_option + 2);
            if (taken & (1 << starter)) {
                uint8_t off = random() % (selected_fire_count_option + 2 - fi);
                while (off--) do {
                        starter++;
                        starter %= selected_player_count_option + 2;
                    } while (taken & (1 << starter));
            }
            fire_catch(starter, fi);
            taken |= 1 << starter;
        }
    }

    for (uint8_t i = 0; i < selected_player_count_option + 2; i++) {
        players[i].lhx = players[i].lhy = 0xff;
        scores[i] = 0;
    }
}

void draw_title(uint8_t rx, uint8_t ry) {
    *DRAW_COLORS = 0x2;
    text(" ~ Fire     ~ ", rx, ry);
    *DRAW_COLORS = 0x3;
    text("~      Fire  ~", rx, ry);
}

void menu() {
    uint8_t gamepad = *GAMEPAD1 & ~prev_gamepads[0];
    if ((gamepad & BUTTON_DOWN) &&
        selected_player_count_option < MAX_PLAYER_COUNT - 2) {
        play_menu_select();
        selected_player_count_option++;
    }
    if ((gamepad & BUTTON_UP) && selected_player_count_option > 0) {
        play_menu_select();
        selected_player_count_option--;
    }
    if ((gamepad & BUTTON_RIGHT) &&
        selected_fire_count_option < MAX_FIRE_COUNT - 1) {
        play_menu_select();
        selected_fire_count_option++;
    }
    if ((gamepad & BUTTON_LEFT) && selected_fire_count_option > 0) {
        play_menu_select();
        selected_fire_count_option--;
    }

    if (gamepad & BUTTON_1) {
        play_menu_accept();
        start_game();
    }

    draw_title(10, 10);

    *DRAW_COLORS = 0x2;
    text("Player count:", 10, 28);
    const uint8_t ys[] = {40, 52, 64};
    text(" 2P", 10, ys[0]);
    text(" 3P", 10, ys[1]);
    text(" 4P", 10, ys[2]);
    *DRAW_COLORS = 0x3;
    text(">", 10, ys[selected_player_count_option]);

    *DRAW_COLORS = 0x2;
    text("Fire\ncount:", 10, 96);
    for (uint8_t i = 0; i < MAX_FIRE_COUNT; i++) {
        if (i <= selected_fire_count_option) {
            *DRAW_COLORS = 0x4320;
        } else {
            *DRAW_COLORS = 0x2220;
        }
        blitSub(fireball, 60 + i * fireballWidth / 4, 104, fireballWidth / 4,
                fireballHeight, fireballWidth / 4 * ((frame >> 2) & 3), 0,
                fireballWidth, fireballFlags);
    }

    *DRAW_COLORS = 0x4320;
    blit(art, 70, 40, artWidth, artHeight, artFlags);

    *DRAW_COLORS = 0x2;
    text("\x86\x87:SELECT P.CNT\n\x84\x85:SELECT F.CNT\n\x80 :PLAY", 10,
         160 - 30);
}

#define get_tile(tx, ty) ((maps[selected_player_count_option][ty] >> tx) & 1)

uint8_t is_collision(uint16_t x, uint16_t y) {
    return get_tile(x / TILE_SIZE, y / TILE_SIZE) ||
           get_tile((x + TILE_SIZE - 1) / TILE_SIZE, y / TILE_SIZE) ||
           get_tile(x / TILE_SIZE, (y + TILE_SIZE - 1) / TILE_SIZE) ||
           get_tile((x + TILE_SIZE - 1) / TILE_SIZE,
                    (y + TILE_SIZE - 1) / TILE_SIZE);
}

void draw_scoreboard(uint8_t rx, uint8_t ry) {
    const uint8_t current_player_id = (*NETPLAY & 3);
    for (uint8_t pi = 0; pi < selected_player_count_option + 2; pi++) {
        static char score_text[] = "P#:#";
        score_text[1] = '1' + (char)pi;
        score_text[3] = '0' + (char)scores[pi];
        *DRAW_COLORS = 0x2 + (pi == current_player_id);
        text(score_text, rx, ry + 8 * pi);
    }
}

void gameplay() {
    *DRAW_COLORS = 0x4320;
    switch (selected_player_count_option) {
        case 0:
            blit(bg2, 0, 0, bg2Width, bg2Height, bg2Flags);
            break;
        case 1:
            blit(bg3, 0, 0, bg3Width, bg3Height, bg3Flags);
            break;
        case 2:
            blit(bg4, 0, 0, bg4Width, bg4Height, bg4Flags);
            break;
    }

    // update players
    const uint8_t player_count = selected_player_count_option + 2;
    const uint8_t current_player_id = (*NETPLAY & 3);

    const uint8_t fire_count = selected_fire_count_option + 1;

    uint8_t holds_fire = 0;

    for (uint8_t pi = 0; pi < player_count; pi++) {
#define P players[pi]
        uint8_t gamepad = GAMEPAD1[pi] * (game_countdown == 0);
        uint8_t prev_gamepad = prev_gamepads[pi];
        uint16_t last_x = P.x;
        uint16_t last_y = P.y;

        if (gamepad & BUTTON_UP) P.y -= PLAYER_SPEED;
        if (gamepad & BUTTON_DOWN) P.y += PLAYER_SPEED;

        if ((int16_t)P.y < 0 || P.y > (MAP_HEIGHT - 1) * TILE_SIZE ||
            is_collision(P.x, P.y))
            P.y = last_y;

        if (gamepad & BUTTON_LEFT) {
            P.x -= PLAYER_SPEED;
            P.flip = 1;
        }
        if (gamepad & BUTTON_RIGHT) {
            P.x += PLAYER_SPEED;
            P.flip = 0;
        }

        if ((int16_t)P.x < 0 || P.x > (MAP_WIDTH - 1) * TILE_SIZE ||
            is_collision(P.x, P.y))
            P.x = last_x;

        for (uint8_t fi = 0; fi < fire_count; fi++) {
            holds_fire |= (fires[fi].holder == pi && fires[fi].state == F_HELD)
                          << pi;
        }

        for (uint8_t fi = 0; fi < fire_count; fi++) {
#define F fires[fi]
            if (F.holder == pi && F.state == F_HELD) {
                int8_t new_dx = (gamepad & BUTTON_RIGHT)  ? FIRE_SPEED_DIAG
                                : (gamepad & BUTTON_LEFT) ? -FIRE_SPEED_DIAG
                                                          : 0;
                int8_t new_dy = (gamepad & BUTTON_DOWN) ? FIRE_SPEED_DIAG
                                : (gamepad & BUTTON_UP) ? -FIRE_SPEED_DIAG
                                                        : 0;

                if (new_dx != 0 || new_dy != 0) {
                    F.dx = new_dx;
                    F.dy = new_dy;
                    if (F.dx == 0) {
                        F.dy =
                            (gamepad & BUTTON_DOWN) ? FIRE_SPEED : -FIRE_SPEED;
                    } else if (F.dy == 0) {
                        F.dx =
                            (gamepad & BUTTON_RIGHT) ? FIRE_SPEED : -FIRE_SPEED;
                    }
                }

                if (((gamepad & ~prev_gamepad) & BUTTON_1)) {
                    if (F.dx != 0 || F.dy != 0) {
                        play_fireball_throw();
                        F.state = F_FLYING;
                        if (P.lhx != 0xff) {
                            P.x = P.lhx;
                            P.y = P.lhy;
                            P.lhx = P.lhy = 0xff;
                        }
                    }
                }
            }

            if (F.state != F_HELD &&
                !(P.x + TILE_SIZE < F.x || P.y + TILE_SIZE < F.y ||
                  P.x > F.x + FIRE_SIZE || P.y > F.y + FIRE_SIZE)) {
                switch (F.state) {
                    case F_IDLE:
                    case F_BOUNCING:
                        if (!(holds_fire & (1 << pi))) {
                            fire_catch(pi, fi);
                            holds_fire |= 1 << pi;
                        }
                        break;
                    case F_HELD:
                        break;
                    case F_FLYING:
                        if (F.holder != pi && P.lhx == 0xff) {
                            scores[F.holder]++;
                            if (scores[F.holder] == WIN_THRESHOLD) {
                                current_state = GAMEOVER;
                                last_winner = F.holder;
                                return;
                            }
                            P.lhx = (uint8_t)P.x;
                            P.lhy = (uint8_t)P.y;
                            P.x = TILE_SIZE * MAP_WIDTH / 2 - TILE_SIZE / 2;
                            P.y = TILE_SIZE * MAP_HEIGHT / 2 - TILE_SIZE / 2;
                            play_fireball_hit();
                            if (!(holds_fire & (1 << pi))) {
                                fire_catch(pi, fi);
                                holds_fire |= 1 << pi;
                            }
                        }
                        break;
                }
            }
#undef F
        }

        /* *DRAW_COLORS = 0x31; */
        /* rect(P.x, P.y, TILE_SIZE, TILE_SIZE); */
        if (current_player_id == pi) {
            *DRAW_COLORS = 0x30;
            rect(players[pi].x + 1, players[pi].y + TILE_SIZE - 1,
                 TILE_SIZE - 2, 2);
        }
        if (P.lhx != 0xff && (frame & 0b1111) < 0b1000) {
            *DRAW_COLORS = 0x4330;
        } else {
            *DRAW_COLORS = 0x4320;
        }
        blit(mage, P.x - 1, P.y - 1, mageWidth, mageHeight,
             mageFlags | (P.flip * BLIT_FLIP_X));
#undef P
    }

    // update fire
    for (uint8_t fi = 0; fi < fire_count; fi++) {
#define F fires[fi]
        switch (F.state) {
            case F_IDLE:
                if (!--F.bounce_timer) {
                    if (!(holds_fire & (1 << F.holder))) {
                        fire_catch(F.holder, fi);
                    } else {
                        F.bounce_timer++;
                    }
                }
                break;
            case F_HELD:
                F.x = players[F.holder].x;
                F.y = players[F.holder].y;
                break;
            case F_BOUNCING:
                if (F.bounce_timer) F.bounce_timer--;
                if (!F.bounce_timer && !is_collision(F.x, F.y)) {
                    F.state = F_IDLE;
                    F.bounce_timer = FIRE_COMEBACK_TIME;
                }
            case F_FLYING:
                F.x += F.dx;
                if ((int16_t)F.x < 0 ||
                    F.x > TILE_SIZE * MAP_WIDTH - FIRE_SIZE) {
                    F.x -= F.dx;
                    F.dx = -F.dx;
                    if (F.state != F_BOUNCING)
                        F.bounce_timer = FIRE_BOUNCE_TIME;
                    F.state = F_BOUNCING;
                    play_fireball_bounce();
                }
                F.y += F.dy;
                if ((int16_t)F.y < 0 ||
                    F.y > TILE_SIZE * MAP_HEIGHT - FIRE_SIZE) {
                    F.y -= F.dy;
                    F.dy = -F.dy;
                    if (F.state != F_BOUNCING)
                        F.bounce_timer = FIRE_BOUNCE_TIME;
                    F.state = F_BOUNCING;
                    play_fireball_bounce();
                }
                break;
        }

        if (!game_countdown) {
            *DRAW_COLORS = 0x4320;
            blitSub(fireball, F.x - 1 + (F.state == F_HELD) * 1,
                    F.y - 1 - (F.state == F_HELD) * TILE_SIZE,
                    fireballWidth / 4, fireballHeight,
                    fireballWidth / 4 * ((frame >> 2) & 3), 0, fireballWidth,
                    fireballFlags);
            if (F.state == F_HELD) {
                if ((*NETPLAY & 0b100) && current_player_id != F.holder)
                    *DRAW_COLORS = 0;
                else
                    *DRAW_COLORS = 0x3;
                line(F.x + F.dx * 3 + FIRE_SIZE / 2,
                     F.y + F.dy * 3 + FIRE_SIZE / 2,
                     F.x + F.dx * 4 + FIRE_SIZE / 2,
                     F.y + F.dy * 4 + FIRE_SIZE / 2);
            }
        }
#undef F
    }

    *DRAW_COLORS = 0x4;
    vline(MAP_WIDTH * TILE_SIZE, 0, 121);
    *DRAW_COLORS = 0x2;
    rect(MAP_WIDTH * TILE_SIZE + 1, 0, 3, 121);
    *DRAW_COLORS = 0x4;
    hline(0, 120, 160 - 4);
    *DRAW_COLORS = 0x2;
    rect(0, 121, 160, 3);

    draw_scoreboard(10, 126);
    *DRAW_COLORS = 0x2;
    text("\x84\x85\x86\x87:MOVE\n\x80   :THROW", 60, 126);
    draw_title(44, 148);

    if (game_countdown) {
        uint8_t seconds = (uint8_t)((game_countdown + 59) / 60);
        static char get_ready_text[] = "Get ready!\n    %     ";
        get_ready_text[15] = '0' + (char)seconds;
        *DRAW_COLORS = 0x12;
        text(get_ready_text, 80 - 10 * 4, 40);
        game_countdown--;
    }
}

void gameover() {
    draw_title(10, 10);

    static char winner_text[] = "Player # won!";
    winner_text[7] = '1' + (char)last_winner;

    text(winner_text, 10, 30);

    draw_scoreboard(10, 50);

    text("\x80:PLAY AGAIN\n\x81:MAIN MENU", 10, 130);

    *DRAW_COLORS = 0x4321;
    blit(art, 60, 40, artWidth, artHeight, artFlags);

    uint8_t gamepad = *GAMEPAD1 & ~prev_gamepads[0];

    if (gamepad & BUTTON_1) {
        play_menu_accept();
        start_game();
    }
    if (gamepad & BUTTON_2) {
        play_menu_accept();
        current_state = MENU;
    }
}

void update() {
    init_palette();
    switch (current_state) {
        case MENU:
            menu();
            break;
        case GAMEPLAY:
            gameplay();
            break;
        case GAMEOVER:
            gameover();
            break;
    }
    random();
    prev_gamepads[0] = *GAMEPAD1;
    prev_gamepads[1] = *GAMEPAD2;
    prev_gamepads[2] = *GAMEPAD3;
    prev_gamepads[3] = *GAMEPAD4;
    frame++;
    if (frame == 60) frame = 0;
    if (frame % tone_length == 0) {
        play_music();
        music_current_tone++;
    }
}
