// Zombie Wave : un runner pour NumWorks (jeu original inspire du genre "Zombie Tsunami")
// Controles : OK ou Haut = sauter, Retour = quitter.
// But : attrape les humains pour agrandir ta horde, saute par-dessus les obstacles.

#include <eadk.h>
#include <stdint.h>
#include <stdbool.h>

// Nom et version d'API de l'application (obligatoire pour un fichier .nwa)
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Zombie Wave";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

// ---------- Reglages du jeu (tu peux les modifier) ----------
#define SCREEN_W 320
#define SCREEN_H 240
#define GROUND_Y 200        // hauteur du sol
#define HORDE_X 30          // position horizontale de la horde
#define ZW 6                // largeur d'un zombie
#define ZH 9                // hauteur d'un zombie
#define COLS 6              // zombies par rangee
#define START_ZOMBIES 4
#define MAX_ZOMBIES 36
#define MAX_ENT 6           // nombre max d'objets a l'ecran
#define FRAME_MS 33         // ~30 images par seconde
#define JUMP_POWER 10
#define HIT_LOSS 3          // zombies perdus en touchant un obstacle
#define HUMAN_GAIN 2        // zombies gagnes en attrapant un humain

// ---------- Couleurs (format RGB565) ----------
#define RGB(r, g, b) ((eadk_color_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_SKY RGB(120, 195, 235)
#define C_GRASS RGB(70, 150, 60)
#define C_DIRT RGB(120, 85, 50)
#define C_DASH RGB(90, 62, 36)
#define C_ZOMBIE_BODY RGB(50, 110, 50)
#define C_ZOMBIE_HEAD RGB(110, 190, 90)
#define C_EYE RGB(220, 20, 20)
#define C_SKIN RGB(240, 200, 160)
#define C_SHIRT RGB(50, 80, 220)
#define C_CAR RGB(205, 50, 50)
#define C_WALL RGB(115, 115, 125)
#define C_LIGHT RGB(235, 235, 245)
#define C_BLACK RGB(0, 0, 0)
#define C_WHITE RGB(255, 255, 255)
#define C_PANEL RGB(25, 25, 40)

#define KIND_OBSTACLE 0
#define KIND_HUMAN 1

typedef struct {
  bool active;
  int kind;
  int x, y, w, h;
} entity_t;

// ---------- Outils de dessin ----------

// Remplit un rectangle (en le decoupant s'il depasse de l'ecran)
static void fill(int x, int y, int w, int h, eadk_color_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCREEN_W) { w = SCREEN_W - x; }
  if (y + h > SCREEN_H) { h = SCREEN_H - y; }
  if (w <= 0 || h <= 0) { return; }
  eadk_rect_t rect = {(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h};
  eadk_display_push_rect_uniform(rect, color);
}

static void text(const char * s, int x, int y, bool large, eadk_color_t fg, eadk_color_t bg) {
  eadk_point_t p = {(uint16_t)x, (uint16_t)y};
  eadk_display_draw_string(s, p, large, fg, bg);
}

// Ecrit un nombre dans buf, complete par des espaces jusqu'a "pad" caracteres
static void int_to_str(int v, char * buf, int pad) {
  char tmp[12];
  int n = 0;
  int k = 0;
  if (v < 0) { v = 0; }
  if (v == 0) { tmp[n++] = '0'; }
  while (v > 0 && n < 11) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  while (n > 0) { buf[k++] = tmp[--n]; }
  while (k < pad) { buf[k++] = ' '; }
  buf[k] = 0;
}

static bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void draw_background(void) {
  fill(0, 0, SCREEN_W, GROUND_Y, C_SKY);
  fill(0, GROUND_Y, SCREEN_W, 6, C_GRASS);
  fill(0, GROUND_Y + 6, SCREEN_W, SCREEN_H - GROUND_Y - 6, C_DIRT);
}

static void draw_dashes(int offset, eadk_color_t color) {
  for (int k = 0; k < 5; k++) {
    fill(k * 80 - offset, GROUND_Y + 20, 24, 3, color);
  }
}

static void draw_horde(int zombies, int jy) {
  for (int i = 0; i < zombies; i++) {
    int col = i % COLS;
    int row = i / COLS;
    int zx = HORDE_X + col * (ZW + 1);
    int zy = GROUND_Y - (row + 1) * (ZH + 1) - jy + 1;
    fill(zx, zy + 3, ZW, ZH - 3, C_ZOMBIE_BODY);
    fill(zx + 1, zy, ZW - 2, 3, C_ZOMBIE_HEAD);
    fill(zx + 3, zy + 1, 1, 1, C_EYE);
  }
}

static void draw_entity(const entity_t * e) {
  if (e->kind == KIND_HUMAN) {
    fill(e->x + 1, e->y, 4, 4, C_SKIN);
    fill(e->x, e->y + 4, e->w, e->h - 4, C_SHIRT);
  } else {
    eadk_color_t c = (e->h % 2 == 0) ? C_CAR : C_WALL;
    fill(e->x, e->y, e->w, e->h, c);
    fill(e->x + 2, e->y + 2, e->w - 4, 3, C_LIGHT);
  }
}

static void wait_release(void) {
  while (true) {
    eadk_keyboard_state_t kb = eadk_keyboard_scan();
    if (!eadk_keyboard_key_down(kb, eadk_key_ok) && !eadk_keyboard_key_down(kb, eadk_key_up)) {
      return;
    }
    eadk_timing_msleep(20);
  }
}

// Attend OK (retourne true) ou Retour (retourne false)
static bool wait_ok_or_back(void) {
  wait_release();
  while (true) {
    eadk_keyboard_state_t kb = eadk_keyboard_scan();
    if (eadk_keyboard_key_down(kb, eadk_key_back)) { return false; }
    if (eadk_keyboard_key_down(kb, eadk_key_ok)) { return true; }
    eadk_timing_msleep(30);
  }
}

static bool title_screen(void) {
  fill(0, 0, SCREEN_W, SCREEN_H, C_PANEL);
  text("ZOMBIE WAVE", 100, 40, true, C_ZOMBIE_HEAD, C_PANEL);
  text("Attrape les humains pour", 62, 90, false, C_WHITE, C_PANEL);
  text("agrandir ta horde.", 90, 106, false, C_WHITE, C_PANEL);
  text("Saute par-dessus les obstacles !", 40, 130, false, C_WHITE, C_PANEL);
  text("OK / Haut : sauter", 85, 165, false, C_SKIN, C_PANEL);
  text("Retour : quitter", 92, 183, false, C_SKIN, C_PANEL);
  text("Appuie sur OK pour jouer", 62, 212, false, C_ZOMBIE_HEAD, C_PANEL);
  return wait_ok_or_back();
}

static void game_over_screen(int score, int best) {
  char buf[16];
  fill(60, 60, 200, 110, C_PANEL);
  text("GAME OVER", 110, 70, true, C_EYE, C_PANEL);
  text("Score :", 95, 102, false, C_WHITE, C_PANEL);
  int_to_str(score, buf, 7);
  text(buf, 150, 102, false, C_WHITE, C_PANEL);
  text("Record :", 95, 120, false, C_WHITE, C_PANEL);
  int_to_str(best, buf, 7);
  text(buf, 157, 120, false, C_WHITE, C_PANEL);
  text("OK : rejouer", 95, 142, false, C_ZOMBIE_HEAD, C_PANEL);
  text("Retour : quitter", 95, 156, false, C_SKIN, C_PANEL);
}

// ---------- Une partie. Retourne true pour rejouer, false pour quitter ----------
static bool play(int * best) {
  entity_t ents[MAX_ENT];
  for (int i = 0; i < MAX_ENT; i++) { ents[i].active = false; }

  int zombies = START_ZOMBIES;
  int jy = 0;              // hauteur du saut
  int vy = 0;              // vitesse verticale
  bool airborne = false;
  long dist = 0;
  int speed = 4;
  int spawn_timer = 40;
  int dash = 0;
  int prev_dash = 0;
  bool horde_dirty = true;
  bool have_prev = false;
  int prev_x = 0, prev_y = 0, prev_w = 0, prev_h = 0;
  int last_score = -1;
  int last_zombies = -1;
  char buf[16];

  draw_background();
  text("Score:", 6, 4, false, C_BLACK, C_SKY);
  text("Horde:", 190, 4, false, C_BLACK, C_SKY);
  draw_dashes(0, C_DASH);

  while (true) {
    uint64_t t0 = eadk_timing_millis();

    // 1. Touches
    eadk_keyboard_state_t kb = eadk_keyboard_scan();
    if (eadk_keyboard_key_down(kb, eadk_key_back)) { return false; }
    bool jump = eadk_keyboard_key_down(kb, eadk_key_ok) || eadk_keyboard_key_down(kb, eadk_key_up);

    // 2. Effacer les anciens objets et les traits du sol
    for (int i = 0; i < MAX_ENT; i++) {
      if (ents[i].active) { fill(ents[i].x, ents[i].y, ents[i].w, ents[i].h, C_SKY); }
    }
    draw_dashes(prev_dash, C_DIRT);

    // 3. Saut
    if (!airborne && jump) {
      airborne = true;
      vy = JUMP_POWER;
    }
    if (airborne) {
      jy += vy;
      vy -= 1;
      horde_dirty = true;
      if (jy <= 0) {
        jy = 0;
        vy = 0;
        airborne = false;
      }
    }

    // 4. Avancer le monde
    dist += speed;
    speed = 4 + (int)(dist / 6000);
    if (speed > 9) { speed = 9; }
    dash = (dash + speed) % 80;

    for (int i = 0; i < MAX_ENT; i++) {
      if (!ents[i].active) { continue; }
      ents[i].x -= speed;
      if (ents[i].x + ents[i].w < 0) { ents[i].active = false; }
    }

    // 5. Nouveaux objets
    spawn_timer--;
    if (spawn_timer <= 0) {
      spawn_timer = (160 + (int)(eadk_random() % 180)) / speed;
      for (int i = 0; i < MAX_ENT; i++) {
        if (ents[i].active) { continue; }
        entity_t * e = &ents[i];
        e->active = true;
        e->x = SCREEN_W;
        if (eadk_random() % 100 < 55) {
          e->kind = KIND_OBSTACLE;
          e->w = 14 + (int)(eadk_random() % 8);
          e->h = 14 + (int)(eadk_random() % 14);
          e->y = GROUND_Y - e->h;
        } else {
          e->kind = KIND_HUMAN;
          e->w = 6;
          e->h = 12;
          e->y = (eadk_random() % 3 == 0) ? GROUND_Y - 57 : GROUND_Y - 12;
        }
        break;
      }
    }

    // 6. Collisions avec la horde
    int rows = (zombies + COLS - 1) / COLS;
    int cols = (zombies < COLS) ? zombies : COLS;
    int hw = cols * (ZW + 1);
    int hh = rows * (ZH + 1);
    int hx = HORDE_X;
    int hy = GROUND_Y - hh - jy;
    for (int i = 0; i < MAX_ENT; i++) {
      entity_t * e = &ents[i];
      if (!e->active) { continue; }
      if (!overlap(hx, hy, hw, hh, e->x, e->y, e->w, e->h)) { continue; }
      if (e->kind == KIND_HUMAN) {
        zombies += HUMAN_GAIN;
        if (zombies > MAX_ZOMBIES) { zombies = MAX_ZOMBIES; }
        dist += 100;
      } else {
        zombies -= HIT_LOSS;
      }
      e->active = false;
      horde_dirty = true;
    }

    int score = (int)(dist / 10);
    if (zombies <= 0) {
      if (score > *best) { *best = score; }
      game_over_screen(score, *best);
      return wait_ok_or_back();
    }

    // 7. Dessin
    draw_dashes(dash, C_DASH);
    prev_dash = dash;

    if (horde_dirty) {
      if (have_prev) { fill(prev_x, prev_y, prev_w, prev_h, C_SKY); }
      rows = (zombies + COLS - 1) / COLS;
      cols = (zombies < COLS) ? zombies : COLS;
      prev_w = cols * (ZW + 1);
      prev_h = rows * (ZH + 1);
      prev_x = HORDE_X;
      prev_y = GROUND_Y - prev_h - jy;
      have_prev = true;
      draw_horde(zombies, jy);
      horde_dirty = false;
    }

    for (int i = 0; i < MAX_ENT; i++) {
      if (ents[i].active) { draw_entity(&ents[i]); }
    }

    if (score != last_score) {
      int_to_str(score, buf, 6);
      text(buf, 52, 4, false, C_BLACK, C_SKY);
      last_score = score;
    }
    if (zombies != last_zombies) {
      int_to_str(zombies, buf, 3);
      text(buf, 236, 4, false, C_BLACK, C_SKY);
      last_zombies = zombies;
    }

    // 8. Limiter la vitesse du jeu
    uint64_t elapsed = eadk_timing_millis() - t0;
    if (elapsed < FRAME_MS) { eadk_timing_msleep((uint32_t)(FRAME_MS - elapsed)); }
  }
}

int main(int argc, char * argv[]) {
  (void)argc;
  (void)argv;
  if (!title_screen()) { return 0; }
  int best = 0;
  while (play(&best)) {
    wait_release();
  }
  return 0;
}
