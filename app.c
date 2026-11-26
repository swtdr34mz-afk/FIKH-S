/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: Bedienung, Seiten, Speicher, Bildschirm. Teil des Unity-Builds (device/unity.c, host/plat_host.c), nach
 * der Plattform: gfx.c und announce.c sind sichtbar. Der Klang ist dsp/fikh_core.c.
 *
 * Bedienung: die Bedienelemente des VHIKK X, auf das FM-1 verteilt
 *   Tasten     BASIS: spielen (mono, die letzte Taste gewinnt), mit Anschlag; OCT- / OCT+ die Oktave
 *   K1..K4     die vier Regler der Seite:
 *              HOME  GEN     WARP, SPAN, MORPH, FUSE
 *              FX    PROC    MIX, FEED, FORM, TIME
 *              ENV   FIELD   FIELD, CELL, ATTACK, RELEASE
 *              EDIT  MOTION  GLIDE, DRIFT, WIDTH, LEVEL
 *   SELECT     SEED (jede Raste ein neuer Wurf)          ALGORITHM  SCAN (grob, für live)
 *   PRESET     der Algorithmus 1..9 (BANK 1); jeder behält seine eigenen Regler
 *   PLAY       HOLD: der VCA bleibt offen, ein Drone; die Tasten ändern nur noch BASIS
 *   REC        WÜRFELN: SEED und SCAN neu; 1 s halten: dazu MORPH, FUSE, FORM und TIME
 *   ARP        FREEZE: das Delay hält, was es hat (endlos), Neues klingt trocken darüber
 *   SEQ        1 s halten: der Algorithmus zurück auf seine Werkseinstellung
 *   SAVE       speichern (sonst automatisch, sobald es still ist)
 *   MIDI       Noten auf allen Kanälen, Pitch Bend ±2, CC 1 = MORPH */
#include "fastmath.h"
#include "fikh_core.h"
#include "ui_serif.h"

#define SR 44100.0f

/* ------------------------------------------------------- der Klang-Speicher --- */
static int16_t mem_dl[2][FK_DL_N] __attribute__((section(".pool")));
static float mem_ch[2][FK_CH_N] __attribute__((section(".pool")));
static float mem_ap[FK_AP_N] __attribute__((section(".pool")));
static float mem_cb[4][FK_CB_N] __attribute__((section(".pool")));
static float mem_wt[FK_WT_SETS * FK_WT_WAVES * FK_WT_LEN] __attribute__((section(".pool")));
static float mem_sn[FK_SIN_N + 1] __attribute__((section(".pool")));

/* ------------------------------------------------------------- Speicher --- */
#define SET_MAGIC 0x31484B46u            /* "FKH1" */
typedef struct {
    uint32_t magic;
    uint8_t algo, oct, hold, rsv;
    uint8_t p[FK_NALGO][FK_NP];          /* jeder Algorithmus mit seinen Reglern */
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "die Einstellungen passen nicht in ein Speicherobjekt");
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static void settings_defaults(void)
{
    int a;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.oct = 2;                         /* 0..4 = -2..+2: die Tasten F3..G5 */
    for (a = 0; a < FK_NALGO; a++)
        memcpy(set.p[a], FK_ALGOS[a].init, FK_NP);
}

static void settings_load(void)
{
    int a, i, ok = plat_store_load(OBJ_SETTINGS, &set, sizeof set) == (int)sizeof set && set.magic == SET_MAGIC
                   && set.algo < FK_NALGO && set.oct <= 4u && set.hold <= 1u;
    for (a = 0; ok && a < FK_NALGO; a++)
        for (i = 0; i < FK_NP; i++)
            if (set.p[a][i] > 127u)
                ok = 0;
    if (!ok)
        settings_defaults();
}

static void dirty(void)
{
    set_dirty = 1;
    dirty_at = plat_ms();
}

/* ------------------------------------------------- an die Engine (Hauptschleife) --- */
static uint8_t *cur_p(void) { return set.p[set.algo]; }
static void push_params(void)
{
    uint32_t f = plat_lock();
    fk_params(cur_p());
    plat_unlock(f);
}
static void e_on(int note, int vel)
{
    uint32_t f = plat_lock();
    fk_note_on(note, vel);
    plat_unlock(f);
}
static void e_off(int note)
{
    uint32_t f = plat_lock();
    fk_note_off(note);
    plat_unlock(f);
}

/* ------------------------------------------------------------------ Seiten --- */
enum { PG_GEN, PG_PROC, PG_FIELD, PG_MOTION, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"GEN", "PROC", "FIELD", "MOTION"};
static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_FX, B_ENV, B_EDIT};
static const uint8_t PAGE_K[NPAGES][4] = {
    {P_WARP, P_SPAN, P_MORPH, P_FUSE},
    {P_MIX, P_FEED, P_FORM, P_TIME},
    {P_FIELD, P_CELL, P_ATK, P_REL},
    {P_GLIDE, P_DRIFT, P_WIDTH, P_LEVEL},
};

/* was neu gezeichnet werden muss: Bereiche des Bildschirms */
enum { R_KNOB0 = 1, R_KNOB1 = 2, R_KNOB2 = 4, R_KNOB3 = 8, R_HEAD = 16, R_BASIS = 32, R_SCOPE = 64, R_READ = 128,
       R_FOOT = 256, R_ALL = 0x7FFF };

static struct {
    uint8_t page, intro, eco_warned;
    uint32_t intro_at, frame;
    uint32_t keys, btn;
    int8_t key_note[NKEYS];
    uint32_t knob_at[4];                 /* wann jeder Regler zuletzt gedreht wurde (er leuchtet) */
    uint8_t rest[4];                     /* Rasten in der Mitte (bipolar) */
    int read_p;                          /* der Parameter in der Anzeige (-1: der Name der Seite) */
    uint32_t read_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t rec_t, seq_t;               /* REC / SEQ gedrückt seit (0: nicht, oder schon gehandelt) */
    uint32_t dice_at;
    uint32_t redraw;
    int note_drawn;
    int batt_drawn;
    uint32_t load_at;
    uint8_t hot;
    uint8_t saving;                      /* 1: leise geblendet, um zu speichern */
    uint32_t save_at;
    uint8_t batt_test;
} ui;

static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float fabsf_(float x) { return x < 0.0f ? -x : x; }
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    while (*b)
        b++;
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.read_until = 0;
    ui.redraw |= R_READ;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_param(int p)
{
    char t[24] = "";
    ui.read_p = p;
    ui.read_until = plat_ms() + 1500u;
    ui.msg_until = 0;
    ui.redraw |= R_READ;
    if (ann_on()) {
        char s[48] = "";
        text_cat(s, FK_PARAMS[p].name);
        text_cat(s, " ");
        text_cat(s, fk_value_text(p, cur_p()[p], t));
        ann_say(s, 0, ANN_NOW);
    }
}

static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_text(int n, char *t)
{
    t[0] = 0;
    text_cat(t, NOTE_NAME[n % 12]);
    int_text(n / 12 - 1, t);
}

static int key_base(void) { return 53 + 12 * ((int)set.oct - 2); }

/* Beschleunigung: schnell gedreht, größere Schritte */
static int32_t accel(int enc, int32_t e, int32_t slow, int32_t fast)
{
    static uint32_t last[NE];
    uint32_t now = plat_ms(), dt = now - last[enc];
    last[enc] = now;
    return e * (dt < 45u ? fast : slow);
}

/* SCAN auf ALGORITHM: grob, 2,5 pro Raste (abwechselnd 2 und 3), schnell gedreht bis 7,5 */
static int coarse(int k, int32_t e)
{
    static int rem[NE];
    int n = (int)accel(k, e, 2, 3) * 5 + rem[k], q = n / 2;
    rem[k] = n - q * 2;
    return q;
}

static void param_set(int p, int v)
{
    v = iclamp(v, 0, 127);
    if (v == cur_p()[p])
        return;
    cur_p()[p] = (uint8_t)v;
    push_params();
    dirty();
    say_param(p);
}

static void knob_turn(int k, int32_t e)
{
    int p = PAGE_K[ui.page][k], v = cur_p()[p], step;
    ui.knob_at[k] = plat_ms();
    ui.redraw |= (uint32_t)R_KNOB0 << k;
    step = (int)accel(EN_K1 + k, e, 1, 4);
    if (FK_PARAMS[p].bip && v == 64 && ++ui.rest[k] < 3) {   /* die Mitte rastet ein: drei Rasten, bis es weitergeht */
        say_param(p);
        return;
    }
    if (FK_PARAMS[p].bip && ((v < 64 && v + step >= 64) || (v > 64 && v + step <= 64))) {
        v = 64;                          /* neutral ist leicht zu finden */
        ui.rest[k] = 0;
    } else {
        v += step;
    }
    param_set(p, v);
}

static void page_set(int pg)
{
    int i;
    for (i = 0; i < 4; i++) {
        ui.knob_at[i] = 0;
        ui.rest[i] = 0;
    }
    ui.page = (uint8_t)pg;
    ui.read_until = 0;
    ui.redraw |= R_ALL;
    app_say("PAGE", PAGE_NAME[pg]);
}

static void algo_set(int a)
{
    uint32_t f;
    char t[8] = "";
    set.algo = (uint8_t)iclamp(a, 0, FK_NALGO - 1);
    f = plat_lock();
    fk_algo(set.algo);
    fk_params(cur_p());
    plat_unlock(f);
    dirty();
    ui.redraw |= R_ALL;
    if (set.algo + 1 < 10)
        text_cat(t, "0");
    int_text(set.algo + 1, t);
    app_say("ALGORITHM", t);
}

/* WÜRFELN: SEED und SCAN neu (deep: dazu MORPH, FUSE, FORM, TIME); der Zufall aus der Zeit */
static void dice(int deep)
{
    static uint32_t r = 0x6A09E667u;
    uint8_t *p = cur_p();
    r ^= plat_ms() * 2654435761u;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    p[P_SEED] = (uint8_t)(r % 128u);
    p[P_SCAN] = (uint8_t)((r >> 8) % 128u);
    if (deep) {
        p[P_MORPH] = (uint8_t)((r >> 15) % 128u);
        p[P_FUSE] = (uint8_t)(24u + (r >> 7) % 80u);
        p[P_FORM] = (uint8_t)(30u + (r >> 19) % 98u);
        p[P_TIME] = (uint8_t)(30u + (r >> 23) % 90u);
    }
    push_params();
    dirty();
    ui.dice_at = plat_ms();
    ui.redraw |= R_ALL;
    app_say(deep ? "DEEP DICE" : "DICE", "");
}

static void factory(void)
{
    memcpy(cur_p(), FK_ALGOS[set.algo].init, FK_NP);
    push_params();
    dirty();
    ui.redraw |= R_ALL;
    app_say("INIT", FK_ALGOS[set.algo].name);
}

static void hold_set(int on)
{
    uint32_t f = plat_lock();
    set.hold = (uint8_t)on;
    fk_hold(on);
    plat_unlock(f);
    dirty();
    ui.redraw |= R_FOOT | R_BASIS;
    app_say("HOLD", on ? "ON" : "OFF");
}

static void save_now(void)               /* leise blenden, dann schreiben (app_frame), dann wieder auf */
{
    if (ui.saving)
        return;
    ui.saving = 1;
    ui.save_at = plat_ms();
    fk_duck(1);
}

static void keys_input(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = iclamp(key_base() + (int)k, 0, 127);
            ui.key_note[k] = (int8_t)note;
            e_on(note, (int)touch_vel(k));
            ui.redraw |= R_BASIS;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.key_note[k] >= 0)
                e_off(ui.key_note[k]);
            ui.key_note[k] = -1;
            ui.redraw |= R_BASIS;
        }
    }
}

static void button(uint32_t i)
{
    int pg;
    for (pg = 0; pg < NPAGES; pg++)
        if (PAGE_BTN[pg] == i) {
            page_set(pg);
            return;
        }
    switch (i) {
    case B_PLAY: hold_set(!set.hold); break;
    case B_REC: ui.rec_t = plat_ms(); break;
    case B_SEQ: ui.seq_t = plat_ms(); break;
    case B_ARP: {
        uint32_t f = plat_lock();
        int on = !fk_frozen();
        fk_freeze(on);
        plat_unlock(f);
        ui.redraw |= R_FOOT;
        app_say("FREEZE", on ? "ON" : "OFF");
        break;
    }
    case B_SAVE:
        save_now();
        break;
    case B_OCTDN:
    case B_OCTUP: {
        char t[8] = "";
        set.oct = (uint8_t)iclamp((int)set.oct + (i == B_OCTUP ? 1 : -1), 0, 4);
        dirty();
        if (set.oct > 2)
            text_cat(t, "+");
        int_text((int)set.oct - 2, t);
        app_say("OCTAVE", t);
        ui.redraw |= R_FOOT;
        break;
    }
    case B_SEL:
    case B_LFO:
    case B_GLO:
        app_say("FIKH-S", "HOME FX ENV EDIT");
        break;
    default: break;
    }
}

static void held_buttons(uint32_t btn)
{
    uint32_t now = plat_ms();
    if (ui.rec_t) {
        if (!(btn >> B_REC & 1u)) {
            dice(0);
            ui.rec_t = 0;
        } else if (now - ui.rec_t >= 1000u) {
            dice(1);
            ui.rec_t = 0;
        }
    }
    if (ui.seq_t) {
        if (!(btn >> B_SEQ & 1u)) {
            app_say("SEQ", "HOLD 1 S: INIT");
            ui.seq_t = 0;
        } else if (now - ui.seq_t >= 1000u) {
            factory();
            ui.seq_t = 0;
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    keys_input(keys);
    for (i = 0; i < NB; i++)
        if ((btn >> i & 1u) && !(ui.btn >> i & 1u))
            button(i);
    held_buttons(btn);
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob_turn((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0) {      /* SEED: jede Raste ein neuer Wurf, ringsum */
        int v = ((int)cur_p()[P_SEED] + (int)accel(EN_SELECT, e, 1, 3) + 128 * 4) % 128;
        param_set(P_SEED, v);
        ui.redraw |= R_HEAD;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {        /* SCAN */
        param_set(P_SCAN, (int)cur_p()[P_SCAN] + coarse(EN_ALGO, e));
        ui.redraw |= R_HEAD;
    }
    if ((e = plat_enc(EN_PRESET)) != 0)        /* der Algorithmus */
        algo_set((set.algo + FK_NALGO + (e > 0 ? 1 : -1)) % FK_NALGO);
    if (btn != ui.btn)
        ui.redraw |= R_FOOT;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void lights(void)
{
    uint32_t now = plat_ms(), btn = 1u << PAGE_BTN[ui.page], dimb = (1u << NB) - 1u, lit = 0, k;
    if (set.hold)
        btn |= LED_PLAY_GREEN;
    if (fk_frozen())
        btn |= 1u << B_ARP;
    if (set_dirty)
        btn |= 1u << B_SAVE;
    if (now - ui.dice_at < 160u)
        btn |= 1u << B_REC;
    if (set.oct != 2)
        btn |= 1u << (set.oct > 2 ? B_OCTUP : B_OCTDN);
    for (k = 0; k < NKEYS; k++)
        if ((key_base() + (int)k) % 12 == 0)
            lit |= 1u << k;
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), ui.keys, dimb & ~(1u << B_PLAY), lit, 1);
}

/* ============================================================== Zeichnen ===
 * Das Panel des VHIKK X, so hochwertig wie es geht: schwarz eloxiertes Aluminium mit feinem Korn, darauf haarfeine
 * goldene Linien (Leiterbahnen, Rahmen, Ornamente), gravierte Serifen-Beschriftung in Gold, rote LED-Fenster; die
 * Regler schwarz und fein gerändelt mit einem weißen Strich, ein goldener Ring darum, der Wert als rotes Licht
 * im Ring. Alles kantengeglättet (ui_gfx.h). Gezeichnet wird immer die ganze Szene, aber nur in den Bereich, der
 * neu muss (ein kleiner Ausschnitt: schnell). */
#undef C_WHITE
#define C_WHITE RGB(238, 236, 228)
#include "ui_gfx.h"

#define GOLD rgb3(212, 176, 104)
#define GOLD_HI rgb3(246, 216, 150)
#define GOLD_DIM rgb3(118, 96, 58)
#define RED rgb3(246, 38, 46)
#define RED_DIM rgb3(64, 10, 14)
#define C_GOLD RGB(212, 176, 104)
#define C_GOLD_HI RGB(246, 216, 150)
#define C_GOLD_DIM RGB(126, 104, 64)
#define C_RED RGB(246, 38, 46)
#define C_INK RGB(18, 17, 15)

#define SCOPE_N 1024u
static float scope_ring[SCOPE_N];
static volatile uint32_t scope_w;

static int32_t g_ox;                     /* der Ausschnitt: links (Bildschirm-x) */
static int32_t g_x0, g_y0, g_x1, g_y1;   /* und seine Grenzen (Bildschirm) */

static int vis(float x0, float y0, float x1, float y1)
{
    return x1 >= (float)g_x0 && x0 < (float)g_x1 && y1 >= (float)g_y0 && y0 < (float)g_y1;
}
#define SX(x) ((x) - (float)g_ox)

static void s_line(float x0, float y0, float x1, float y1, col3 c, float a)
{
    if (vis(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, (x0 > x1 ? x0 : x1) + 1.0f, (y0 > y1 ? y0 : y1) + 1.0f))
        aa_line(SX(x0), y0, SX(x1), y1, c, a);
}
static void s_disc(float x, float y, float r, col3 c, float a)
{
    if (vis(x - r - 1, y - r - 1, x + r + 1, y + r + 1))
        aa_disc(SX(x), y, r, c, a);
}
static void s_bloom(float x, float y, float r, col3 c, float a)
{
    if (vis(x - r, y - r, x + r, y + r))
        bloom(SX(x), y, r, c, a);
}
static void s_arc(float x, float y, float rm, float w, float a0, float a1, col3 c, float a, float glow)
{
    float R = rm + w + glow + 2.0f;
    if (vis(x - R, y - R, x + R, y + R))
        ring_arc(SX(x), y, rm, w, a0, a1, c, a, glow);
}
static void s_rect(float x, float y, float w, float h, col3 c, float a)   /* ein gefülltes Rechteck (ganze Pixel) */
{
    int32_t i, j;
    if (!vis(x, y, x + w, y + h))
        return;
    for (j = (int32_t)y; j < (int32_t)(y + h); j++)
        for (i = (int32_t)x; i < (int32_t)(x + w); i++)
            px_blend(i - g_ox, j, c, a);
}
/* ein Rahmen mit runden Ecken (1 px, kantengeglättet) */
static void s_frame(float x0, float y0, float x1, float y1, float r, col3 c, float a)
{
    if (!vis(x0 - 1, y0 - 1, x1 + 2, y1 + 2))
        return;
    s_line(x0 + r, y0, x1 - r, y0, c, a);
    s_line(x0 + r, y1, x1 - r, y1, c, a);
    s_line(x0, y0 + r, x0, y1 - r, c, a);
    s_line(x1, y0 + r, x1, y1 - r, c, a);
    if (r > 0.5f) {
        s_arc(x0 + r, y0 + r, r, 1.0f, -90.0f, 0.0f, c, a, 0.0f);
        s_arc(x1 - r, y0 + r, r, 1.0f, 0.0f, 90.0f, c, a, 0.0f);
        s_arc(x1 - r, y1 - r, r, 1.0f, 90.0f, 180.0f, c, a, 0.0f);
        s_arc(x0 + r, y1 - r, r, 1.0f, 180.0f, 270.0f, c, a, 0.0f);
    }
}
/* Text (Bildschirm-x), mit Gravur: ein dunkler Schatten darunter */
static int32_t s_text_w(const felucca_font_t *f, const char *s) { return text_w(f, s); }
static void s_text(float x, float y, const felucca_font_t *f, const char *s, uint16_t c, int engrave)
{
    int32_t w = text_w(f, s);
    if (!vis(x - 2, y - 2, x + (float)w + 2, y + (float)f->h + 2))
        return;
    if (engrave)
        cv_text((int32_t)SX(x) + 1, (int32_t)y + 1, f, s, RGB(0, 0, 0));
    cv_text((int32_t)SX(x), (int32_t)y, f, s, c);
}
static void s_ctext(float cx, float y, const felucca_font_t *f, const char *s, uint16_t c, int engrave)
{
    s_text(cx - (float)(text_w(f, s) / 2), y, f, s, c, engrave);
}

static uint32_t fk_hash_ui(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* der Grund: schwarz eloxiertes Aluminium, warm, feines Korn, ein leichter Bürststrich, Licht von oben */
static void bg_fill(int32_t ry0, int32_t h)
{
    int32_t x, y;
    for (y = 0; y < h; y++) {
        int32_t sy = ry0 + y;
        float vy = ((float)sy - 110.0f) / 140.0f;
        uint32_t streak = fk_hash_ui((uint32_t)sy * 2246822519u);
        float st = (float)(streak >> 28) * 0.10f;
        for (x = 0; x < (int32_t)cv_w; x++) {
            int32_t sx = x + g_ox;
            uint32_t n = fk_hash_ui((uint32_t)sx * 73856093u ^ (uint32_t)sy * 19349663u);
            float vx = ((float)sx - 120.0f) / 150.0f, g;
            g = 15.2f * (1.0f - 0.30f * (vx * vx + vy * vy)) - 1.2f * vy + (float)(n >> 29) * 0.42f + st;
            cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(RGB((int)(g + 1.6f), (int)(g + 0.6f), (int)g));
        }
    }
}

/* ---- die Teile ---- */

/* eine gravierte Beschriftung im Rahmen (wie WARP, SPAN ... auf dem Panel) */
static void label_box(float cx, float y, const char *s, uint16_t tc, col3 frame, float a)
{
    float w = (float)s_text_w(&FONT_SERIF_S, s) + 10.0f;
    s_frame(cx - w * 0.5f, y, cx + w * 0.5f, y + 14.0f, 1.0f, frame, a);
    s_ctext(cx, y + 2.0f, &FONT_SERIF_S, s, tc, 1);
}

/* ein Kästchen mit hellem Grund und dunkler Schrift (SEED, SCAN auf dem Panel: grau-oliv) */
static void label_pale(float cx, float y, const char *s)
{
    float w = (float)s_text_w(&FONT_SERIF_S, s) + 10.0f;
    int32_t x0 = (int32_t)(cx - w * 0.5f);
    if (!vis((float)x0, y, (float)x0 + w, y + 14.0f))
        return;
    s_rect((float)x0, y, w, 14.0f, rgb3(122, 128, 108), 1.0f);
    s_frame((float)x0, y, (float)x0 + w, y + 14.0f, 1.0f, GOLD_DIM, 0.9f);
    s_ctext(cx, y + 2.0f, &FONT_SERIF_S, s, RGB(22, 24, 20), 0);
}

/* ein rotes LED-Fenster wie am Modul: Querstreifen hinter Rauchglas; v 0..1 wie viele leuchten, oder ein Text */
static void led_window(float x, float y, float w, float h, float v, const char *txt)
{
    int i, n = (int)((h - 3.0f) / 3.0f);
    if (!vis(x - 4, y - 4, x + w + 4, y + h + 4))
        return;
    s_rect(x, y, w, h, rgb3(26, 6, 8), 1.0f);
    s_frame(x - 0.5f, y - 0.5f, x + w + 0.5f, y + h + 0.5f, 1.0f, GOLD_DIM, 0.8f);
    for (i = 0; i < n; i++) {
        float yy = y + 2.0f + (float)i * 3.0f, lit = txt ? 0.25f : sat01(v * (float)n - (float)(n - 1 - i));
        col3 c = mix3(rgb3(40, 8, 10), RED, lit);
        s_rect(x + 2.0f, yy, w - 4.0f, 2.0f, c, 1.0f);
        if (lit > 0.5f)
            s_bloom(x + w * 0.5f, yy + 1.0f, w * 0.45f, RED, 0.04f * lit);
    }
    if (txt) {
        float tw = (float)s_text_w(&FONT_SERIF_S, txt);
        s_text(x + (w - tw) * 0.5f + 1.0f, y + (h - 12.0f) * 0.5f, &FONT_SERIF_S, txt, RGB(255, 170, 160), 0);
        s_bloom(x + w * 0.5f, y + h * 0.5f, w * 0.5f, RED, 0.10f);
    }
}

/* ein Kippschalter von oben: die Mutter in Gold/Chrom, der Hebel zeigt nach oben, in die Mitte oder nach unten */
static void toggle(float cx, float cy, int pos)
{
    float dy = pos == 0 ? -6.0f : pos == 2 ? 6.0f : 0.0f;
    if (!vis(cx - 12, cy - 14, cx + 12, cy + 14))
        return;
    s_disc(cx, cy, 8.0f, rgb3(58, 56, 52), 1.0f);
    s_disc(cx, cy, 7.0f, rgb3(150, 146, 136), 1.0f);
    s_disc(cx - 1.5f, cy - 1.5f, 4.5f, rgb3(214, 210, 198), 0.5f);
    s_disc(cx, cy, 4.2f, rgb3(40, 40, 38), 1.0f);
    s_line(cx, cy, cx, cy + dy, rgb3(190, 188, 180), 1.0f);
    s_disc(cx, cy + dy, 3.4f, rgb3(176, 174, 166), 1.0f);
    s_disc(cx - 0.8f, cy + dy - 0.8f, 1.6f, rgb3(250, 250, 244), 0.8f);
}

/* der Regler: schwarz, fein gerändelt, eine Fase mit Licht oben links, die Kappe seidenmatt, ein weißer Strich;
 * darum ein goldener Ring und Skalenstriche, der Wert als rotes Licht im Ring (bipolar: von der Mitte aus) */
static void knob(float cx, float cy, float r, float v, int bip, int hot)
{
    float va = -135.0f + 270.0f * sat01(v), a0 = bip ? 0.0f : -135.0f, lo = va < a0 ? va : a0, hi = va < a0 ? a0 : va;
    float ux = fm_sinf(va * 0.0174533f), uy = -fm_cosf(va * 0.0174533f), rt = r * 0.62f;
    int32_t x, y, R = (int32_t)(r + 10.0f), i;
    if (!vis(cx - (float)R, cy - (float)R, cx + (float)R, cy + (float)R))
        return;
    for (i = 0; i <= 10; i++) {           /* die Skala: elf feine goldene Striche */
        float an = (-135.0f + 27.0f * (float)i) * 0.0174533f, sx = fm_sinf(an), sy = -fm_cosf(an);
        float r0 = r + 5.6f, r1 = r + ((i == 0 || i == 5 || i == 10) ? 8.4f : 7.2f);
        s_line(cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, GOLD, (i == 0 || i == 5 || i == 10) ? 0.9f : 0.5f);
    }
    s_arc(cx, cy, r + 3.4f, 1.0f, -180.0f, 180.0f, GOLD, 0.75f, 0.0f);   /* der Goldring */
    s_arc(cx, cy, r + 3.4f, 2.2f, -135.0f, 135.0f, RED_DIM, 0.55f, 0.0f);
    if (hi - lo > 0.8f)                   /* der Wert: rotes Licht im Ring */
        s_arc(cx, cy, r + 3.4f, 2.0f, lo, hi, RED, hot ? 1.0f : 0.8f, hot ? 3.0f : 1.5f);
    for (y = (int32_t)(cy - r - 4.0f); y <= (int32_t)(cy + r + 4.0f); y++)
        for (x = (int32_t)(cx - r - 4.0f); x <= (int32_t)(cx + r + 4.0f); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            float sdx = dx - 1.0f, sdy = dy - 2.0f, sd = fm_sqrtf(sdx * sdx + sdy * sdy), cov, an, lam, proj, perp;
            col3 c;
            int32_t px = x - g_ox;
            if (!px_ok(px, y))
                continue;
            if (sd < r + 2.0f && d > r - 0.5f)      /* der Schatten nach unten rechts */
                px_blend(px, y, rgb3(0, 0, 0), sat01((r + 2.0f - sd) / 3.0f) * 0.7f);
            if (d > r + 0.5f)
                continue;
            cov = sat01(r - d + 0.5f);
            an = ang_deg(dx, dy);
            lam = d > 0.5f ? -(dx * 0.6f + dy * 0.8f) / d : 0.0f;   /* das Licht kommt von oben, etwas links */
            if (d > rt + 1.6f) {              /* der Rock: fein gerändelt */
                float kn = fm_cosf(an * 0.0174533f * 48.0f), rim = sat01((d - (r - 1.0f)) / 1.0f);
                float g = 13.0f + 7.0f * lam + 4.5f * kn * (0.5f + 0.5f * sat01(lam + 0.6f)) - 5.0f * rim;
                c = rgb3(g + 0.5f, g, g - 0.5f);
            } else if (d > rt) {              /* die Fase: ein Lichtkranz */
                float g = 22.0f + 40.0f * sat01(lam) * sat01(lam) + 5.0f * lam;
                c = rgb3(g + 1.0f, g, g - 1.0f);
            } else {                          /* die Kappe: matt, leicht gekörnt, ein weicher Glanz */
                float ox = dx + rt * 0.30f, oy = dy + rt * 0.45f, gl = sat01(1.0f - fm_sqrtf(ox * ox + oy * oy) / rt);
                uint32_t n = fk_hash_ui((uint32_t)x * 9781u ^ (uint32_t)y * 6271u);
                float g = 16.0f + 20.0f * gl * gl + (float)(n >> 30) * 0.8f;
                c = rgb3(g + 0.5f, g, g - 0.5f);
            }
            proj = dx * ux + dy * uy;         /* der Strich: weiß, mit runden Enden */
            perp = fabsf_(dx * uy - dy * ux);
            if (proj > r * 0.10f && proj < r - 1.2f) {
                float pc = sat01(1.3f - perp);
                if (pc > 0.0f)
                    c = mix3(c, rgb3(236, 234, 226), pc);
            }
            px_blend(px, y, c, cov);
        }
}

/* ein Ornament: konzentrische Kreise (wie die kleinen Marken auf dem Panel) */
static void ornament(float x, float y)
{
    s_arc(x, y, 4.0f, 0.9f, -180.0f, 180.0f, GOLD_DIM, 0.8f, 0.0f);
    s_arc(x, y, 1.8f, 0.9f, -180.0f, 180.0f, GOLD_DIM, 0.8f, 0.0f);
}
/* ein Zickzack-Band (die Schraffur auf dem Panel) */
static void zigzag(float x0, float x1, float y)
{
    float x;
    for (x = x0; x + 4.0f <= x1; x += 4.0f) {
        s_line(x, y + 3.0f, x + 2.0f, y, GOLD_DIM, 0.7f);
        s_line(x + 2.0f, y, x + 4.0f, y + 3.0f, GOLD_DIM, 0.7f);
    }
}
static void dotted(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), t;
    for (t = 0.0f; t <= len; t += 3.0f)
        s_disc(x0 + dx * t / len, y0 + dy * t / len, 0.45f, GOLD_DIM, 0.9f);
}
/* ein Langloch (die Befestigung am Rack) */
static void slot(float cx, float cy)
{
    if (!vis(cx - 12, cy - 6, cx + 12, cy + 6))
        return;
    s_frame(cx - 9.0f, cy - 3.5f, cx + 9.0f, cy + 3.5f, 3.5f, GOLD, 0.85f);
    s_frame(cx - 7.5f, cy - 2.0f, cx + 7.5f, cy + 2.0f, 2.0f, rgb3(10, 10, 10), 0.8f);
}

/* ---- die Lage auf dem Bildschirm ---- */
#define KX0 52.0f
#define KX1 188.0f
#define KY0 98.0f
#define KY1 158.0f
#define KR 15.5f
static float knob_x(int k) { return (k & 1) ? KX1 : KX0; }
static float knob_y(int k) { return (k & 2) ? KY1 : KY0; }

/* der Akku: ein goldener Umriss, darin rotes Licht, stufenlos; leer blinkt er; an USB ein kleiner Blitz */
static int batt_level(void)
{
#ifdef PLAT_HOST
    if (ui.batt_test)
        return ui.batt_test - 2;
#endif
    return plat_battery();
}
static int batt_pct(int lvl)
{
    int32_t raw = plat_battery_raw();
    if (lvl >= 4)
        return 100;
#ifdef PLAT_HOST
    if (ui.batt_test)
        return lvl * 33;
#endif
    if (raw <= 0)
        return lvl * 33;
    raw = raw - 515;
    return raw < 0 ? 0 : raw > 100 ? 100 : (int)raw;
}
static void battery(float x, float y, uint32_t now)
{
    int lvl = batt_level(), pct, usb;
    float w;
    if (lvl < 0 || !vis(x - 2, y - 2, x + 30, y + 10))
        return;
    usb = lvl >= 4;
    pct = batt_pct(lvl);
    w = 15.0f * (float)pct / 100.0f;
    if (!usb && pct < 8 && ((now / 500u) & 1u))
        w = 0.0f;
    s_frame(x + 0.5f, y + 0.5f, x + 18.5f, y + 8.5f, 1.5f, GOLD, 0.9f);
    s_rect(x + 19.0f, y + 3.0f, 2.0f, 3.0f, GOLD, 0.9f);
    s_rect(x + 2.0f, y + 2.0f, 15.0f, 5.0f, RED_DIM, 1.0f);
    if (w >= 1.0f)
        s_rect(x + 2.0f, y + 2.0f, (float)(int)w, 5.0f, RED, 1.0f);
    if (w > 0.5f)
        s_bloom(x + 2.0f + w * 0.5f, y + 4.5f, 3.0f + w * 0.5f, RED, 0.15f);
    if (usb) {
        s_line(x + 26.0f, y + 0.5f, x + 24.0f, y + 4.5f, GOLD_HI, 0.95f);
        s_line(x + 24.0f, y + 4.5f, x + 27.0f, y + 4.5f, GOLD_HI, 0.95f);
        s_line(x + 27.0f, y + 4.5f, x + 25.0f, y + 8.5f, GOLD_HI, 0.95f);
    }
}

/* der Scope: das echte Ausgangssignal, auf einen Nulldurchgang getriggert, rot hinter Rauchglas */
#define SC_X 90.0f
#define SC_Y 122.0f
#define SC_W 60.0f
#define SC_H 30.0f
static void scope(void)
{
    float pts[60], pk = 0.02f;
    uint32_t w = scope_w, start = w - 600u, i, trig = 0;
    int j;
    if (!vis(SC_X - 3, SC_Y - 3, SC_X + SC_W + 3, SC_Y + SC_H + 3))
        return;
    s_rect(SC_X, SC_Y, SC_W, SC_H, rgb3(20, 5, 7), 1.0f);
    s_frame(SC_X - 0.5f, SC_Y - 0.5f, SC_X + SC_W + 0.5f, SC_Y + SC_H + 0.5f, 1.5f, GOLD, 0.85f);
    for (j = 1; j < 6; j++)               /* das Raster: feine Punkte */
        dotted(SC_X + (float)j * 10.0f, SC_Y + 2.0f, SC_X + (float)j * 10.0f, SC_Y + SC_H - 2.0f);
    s_line(SC_X + 2.0f, SC_Y + SC_H * 0.5f, SC_X + SC_W - 2.0f, SC_Y + SC_H * 0.5f, RED_DIM, 0.9f);
    for (i = 0; i < 300u; i++) {          /* der Auslöser: ein Nulldurchgang nach oben */
        float a = scope_ring[(start + i) % SCOPE_N], b = scope_ring[(start + i + 1u) % SCOPE_N];
        if (a <= 0.0f && b > 0.0f) {
            trig = i;
            break;
        }
    }
    for (j = 0; j < 60; j++) {
        float v = scope_ring[(start + trig + (uint32_t)j * 4u) % SCOPE_N];
        pts[j] = v;
        pk = fabsf_(v) > pk ? fabsf_(v) : pk;
    }
    for (j = 0; j < 59; j++) {
        float g = 0.42f / pk * SC_H, y0 = SC_Y + SC_H * 0.5f - pts[j] * g, y1 = SC_Y + SC_H * 0.5f - pts[j + 1] * g;
        y0 = y0 < SC_Y + 1.0f ? SC_Y + 1.0f : y0 > SC_Y + SC_H - 1.0f ? SC_Y + SC_H - 1.0f : y0;
        y1 = y1 < SC_Y + 1.0f ? SC_Y + 1.0f : y1 > SC_Y + SC_H - 1.0f ? SC_Y + SC_H - 1.0f : y1;
        s_line(SC_X + (float)j, y0 - 0.6f, SC_X + (float)j + 1.0f, y1 - 0.6f, RED, 0.25f);
        s_line(SC_X + (float)j, y0 + 0.6f, SC_X + (float)j + 1.0f, y1 + 0.6f, RED, 0.25f);
        s_line(SC_X + (float)j, y0, SC_X + (float)j + 1.0f, y1, rgb3(255, 120, 110), 1.0f);
    }
    s_bloom(SC_X + SC_W * 0.5f, SC_Y + SC_H * 0.5f, SC_W * 0.55f, RED, 0.05f);
}

/* ein Text, der in w Pixel passt (sonst gekürzt) */
static const char *fit(const char *s, const felucca_font_t *f, int32_t w, char *buf)
{
    uint32_t n = 0;
    while (s[n] && n < 23u) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = 0;
    while (n > 0u && text_w(f, buf) > w)
        buf[--n] = 0;
    return buf;
}

/* ---- die ganze Szene ---- */
static void scene(uint32_t now)
{
    uint8_t *p = cur_p();
    int k, a = set.algo;
    char t[32];
    col3 gl = GOLD;
    /* der Rahmen, die Langlöcher, der Titel */
    s_frame(3.5f, 9.5f, 236.5f, 230.5f, 6.0f, gl, 0.85f);
    slot(22.0f, 17.0f);
    slot(218.0f, 17.0f);
    if (vis(80, 0, 160, 22)) {
        float w = (float)s_text_w(&FONT_SERIF_M, "FIKH-S") + 20.0f;
        s_rect(120.0f - w * 0.5f, 2.0f, w, 18.0f, rgb3(16, 15, 14), 1.0f);
        s_frame(120.0f - w * 0.5f, 2.0f, 120.0f + w * 0.5f, 20.0f, 1.0f, gl, 0.95f);
        s_frame(120.0f - w * 0.5f + 2.0f, 4.0f, 120.0f + w * 0.5f - 2.0f, 18.0f, 1.0f, GOLD_DIM, 0.6f);
        s_ctext(120.0f, 4.0f, &FONT_SERIF_M, "FIKH-S", C_GOLD_HI, 1);
    }
    /* die Leiterbahnen: von den Fenstern zu den Reglern, gepunktete Wege, Ornamente */
    s_line(14.0f, 30.0f, 14.0f, 78.0f, GOLD_DIM, 0.7f);
    s_line(14.0f, 78.0f, 28.0f, 78.0f, GOLD_DIM, 0.7f);
    s_line(226.0f, 30.0f, 226.0f, 78.0f, GOLD_DIM, 0.7f);
    s_line(212.0f, 78.0f, 226.0f, 78.0f, GOLD_DIM, 0.7f);
    dotted(KX0 + 26.0f, KY0, 86.0f, KY0);
    dotted(154.0f, KY0, KX1 - 26.0f, KY0);
    dotted(KX0 + 26.0f, KY1, 86.0f, KY1);
    dotted(154.0f, KY1, KX1 - 26.0f, KY1);
    s_line(86.0f, KY0, 86.0f, KY1, GOLD_DIM, 0.55f);
    s_line(154.0f, KY0, 154.0f, KY1, GOLD_DIM, 0.55f);
    ornament(14.0f, 128.0f);
    ornament(226.0f, 128.0f);
    zigzag(44.0f, 92.0f, 233.0f);
    zigzag(148.0f, 196.0f, 233.0f);
    /* SEED und SCAN: die roten Fenster, darunter die hellen Kästchen; dazwischen BANK und die Kippschalter */
    {
        char s[8] = "";
        fk_value_text(P_SEED, p[P_SEED], s);
        led_window(26.0f, 30.0f, 50.0f, 16.0f, 0.0f, s);
        label_pale(51.0f, 50.0f, "SEED");
        led_window(164.0f, 30.0f, 50.0f, 16.0f, (float)p[P_SCAN] / 127.0f, 0);
        label_pale(189.0f, 50.0f, "SCAN");
    }
    toggle(103.0f, 40.0f, a / 3);
    toggle(137.0f, 40.0f, a % 3);
    for (k = 0; k < 3; k++) {             /* die Bank-LEDs: Bank 1 leuchtet */
        float yy = 31.0f + (float)k * 6.0f;
        s_rect(116.0f, yy, 8.0f, 3.0f, k == 0 ? RED : RED_DIM, 1.0f);
        if (k == 0)
            s_bloom(120.0f, yy + 1.5f, 6.0f, RED, 0.35f);
    }
    if (vis(60, 54, 180, 82)) {
        t[0] = 0;
        if (a + 1 < 10)
            text_cat(t, "0");
        int_text(a + 1, t);
        s_ctext(120.0f, 56.0f, &FONT_SERIF_XS, "BANK 1", C_GOLD_DIM, 1);
        s_text(66.0f, 66.0f, &FONT_SERIF_S, t, C_RED, 0);
        s_ctext(124.0f, 66.0f, &FONT_SERIF_S, FK_ALGOS[a].name, C_GOLD_HI, 1);
    }
    /* die vier Regler der Seite */
    for (k = 0; k < 4; k++) {
        int pp = PAGE_K[ui.page][k], hot = ui.knob_at[k] && now - ui.knob_at[k] < 800u;
        float cx = knob_x(k), cy = knob_y(k);
        knob(cx, cy, KR, (float)p[pp] / 127.0f, FK_PARAMS[pp].bip, hot);
        label_box(cx, cy + 22.0f, FK_PARAMS[pp].name, hot ? C_RED : C_GOLD, hot ? RED : GOLD, 0.85f);
    }
    /* BASIS: die Note im roten Fenster */
    if (vis(88, 80, 152, 118)) {
        int n = fk_note();
        label_box(120.0f, 80.0f, "BASIS", C_RED, GOLD, 0.85f);
        t[0] = 0;
        if (n >= 0)
            note_text(n, t);
        else
            text_cat(t, "- -");
        s_rect(96.0f, 97.0f, 48.0f, 18.0f, rgb3(26, 6, 8), 1.0f);
        s_frame(95.5f, 96.5f, 144.5f, 115.5f, 1.5f, GOLD_DIM, 0.9f);
        s_ctext(120.0f, 100.0f, &FONT_SERIF_S, t, n >= 0 ? RGB(255, 150, 140) : RGB(110, 30, 34), 0);
        if (n >= 0)
            s_bloom(120.0f, 106.0f, 22.0f, RED, 0.12f * (0.4f + 0.6f * fk_env()));
    }
    scope();
    /* die Anzeige: der Wert, der gerade gedreht wird, sonst eine Meldung, sonst die Seite */
    if (vis(84, 156, 156, 192)) {
        const char *l1, *l2;
        char v[24] = "", v1[24], v2[24];
        if (now < ui.read_until && ui.read_p >= 0) {
            l1 = FK_PARAMS[ui.read_p].name;
            l2 = fk_value_text(ui.read_p, p[ui.read_p], v);
        } else if (now < ui.msg_until) {
            l1 = ui.msg[0];
            l2 = ui.msg[1];
        } else {
            l1 = "PAGE";
            l2 = PAGE_NAME[ui.page];
        }
        s_frame(88.5f, 158.5f, 151.5f, 188.5f, 2.0f, GOLD, 0.8f);
        s_ctext(120.0f, 161.0f, &FONT_SERIF_XS, fit(l1, &FONT_SERIF_XS, 60, v1), C_GOLD, 1);
        if (text_w(&FONT_SERIF_S, l2) <= 60)
            s_ctext(120.0f, 173.0f, &FONT_SERIF_S, l2, C_RED, 0);
        else
            s_ctext(120.0f, 175.0f, &FONT_SERIF_XS, fit(l2, &FONT_SERIF_XS, 60, v2), C_RED, 0);
    }
    /* die Seiten: vier Kästchen, die aktive rot */
    for (k = 0; k < NPAGES; k++) {
        float cx = 39.0f + (float)k * 54.0f;
        int on = k == ui.page;
        if (!vis(cx - 26, 194, cx + 26, 212))
            continue;
        s_frame(cx - 23.5f, 196.5f, cx + 23.5f, 209.5f, 1.0f, on ? RED : GOLD_DIM, on ? 0.95f : 0.7f);
        s_ctext(cx, 198.0f, &FONT_SERIF_XS, PAGE_NAME[k], on ? C_RED : C_GOLD_DIM, 1);
        if (on)
            s_bloom(cx, 203.0f, 18.0f, RED, 0.10f);
    }
    /* unten: HOLD und FREEZE als LEDs, der Name, der Akku, die Oktave */
    {
        int hold = set.hold, frz = fk_frozen();
        s_disc(16.0f, 221.0f, 2.3f, hold ? RED : RED_DIM, 1.0f);
        if (hold)
            s_bloom(16.0f, 221.0f, 7.0f, RED, 0.5f);
        s_text(22.0f, 216.0f, &FONT_SERIF_XS, "HOLD", hold ? C_RED : C_GOLD_DIM, 1);
        if (frz || fk_eco_on()) {
            s_disc(54.0f, 221.0f, 2.3f, RED, 1.0f);
            s_bloom(54.0f, 221.0f, 7.0f, RED, 0.5f);
            s_text(60.0f, 216.0f, &FONT_SERIF_XS, frz ? "FREEZE" : "ECO", C_RED, 1);
        }
        if (vis(150, 212, 192, 230)) {
            t[0] = 0;
            text_cat(t, "OCT ");
            if (set.oct > 2)
                text_cat(t, "+");
            int_text((int)set.oct - 2, t);
            s_text(156.0f, 216.0f, &FONT_SERIF_XS, t, set.oct != 2 ? C_RED : C_GOLD_DIM, 1);
        }
        battery(194.0f, 216.0f, now);
    }
    if (vis(80, 224, 160, 240)) {         /* der Name unten, im Rahmen über der Linie (wie FORGE TME am Modul) */
        float w = (float)s_text_w(&FONT_SERIF_XS, "SWTDR34MZ") + 16.0f;
        s_rect(120.0f - w * 0.5f, 225.0f, w, 12.0f, rgb3(15, 14, 13), 1.0f);
        s_frame(120.0f - w * 0.5f, 225.0f, 120.0f + w * 0.5f, 237.0f, 1.0f, gl, 0.9f);
        s_ctext(120.0f, 226.0f, &FONT_SERIF_XS, "SWTDR34MZ", C_GOLD, 1);
    }
}

/* einen Ausschnitt des Bildschirms neu zeichnen */
static void region(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t h = y1 - y0, w = x1 - x0;
    if (w <= 0 || h <= 0)
        return;
    if ((uint32_t)(w * h) > APP_CANVAS_PX) {      /* zu groß: in Streifen */
        int32_t step = (int32_t)(APP_CANVAS_PX / (uint32_t)w), y;
        for (y = y0; y < y1; y += step)
            region(x0, y, x1, y + step < y1 ? y + step : y1);
        return;
    }
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    g_ox = x0;
    g_x0 = x0;
    g_x1 = x1;
    g_y0 = y0;
    g_y1 = y1;
    cv_oy = -y0;
    bg_fill(y0, h);
    scene(plat_ms());
    cv_oy = 0;
    cv_blit((uint32_t)x0, (uint32_t)y0);
}

static void draw(uint32_t m)
{
    int k;
    if (m == R_ALL) {
        region(0, 0, 240, 120);
        region(0, 120, 240, 240);
        return;
    }
    for (k = 0; k < 4; k++)
        if (m & ((uint32_t)R_KNOB0 << k)) {
            int32_t cx = (int32_t)knob_x(k), cy = (int32_t)knob_y(k);
            region(cx - 30, cy - 28, cx + 30, cy + 40);
        }
    if (m & R_HEAD)
        region(10, 24, 230, 79);
    if (m & R_BASIS)
        region(88, 79, 152, 119);
    if (m & R_SCOPE)
        region((int32_t)SC_X - 2, (int32_t)SC_Y - 2, (int32_t)(SC_X + SC_W) + 3, (int32_t)(SC_Y + SC_H) + 3);
    if (m & R_READ)
        region(86, 156, 154, 191);
    if (m & R_FOOT)
        region(8, 194, 232, 226);
}

/* ---- die Eröffnung: die goldenen Linien zeichnen sich, die Schalter springen, die LEDs gehen an, der Name ---- */
#define INTRO_MS 2600u
static void intro_scene(float t, int32_t y0, int32_t h)
{
    float lines = sat01(t / 1.1f), name = sat01((t - 1.0f) / 0.6f), sub = sat01((t - 1.5f) / 0.5f);
    float cx = 120.0f, cy = 104.0f, r;
    int i;
    g_ox = 0;
    g_x0 = 0;
    g_x1 = 240;
    g_y0 = y0;
    g_y1 = y0 + h;
    cv_oy = -y0;
    bg_fill(y0, h);
    /* der Rahmen wächst von der Mitte oben in beide Richtungen */
    {
        float L = lines * 450.0f, x;
        for (x = 0.0f; x < L && x < 116.0f; x += 1.0f) {
            s_disc(120.0f + x, 9.5f, 0.55f, GOLD, 0.9f);
            s_disc(120.0f - x, 9.5f, 0.55f, GOLD, 0.9f);
        }
        if (L > 116.0f)
            for (x = 0.0f; x < L - 116.0f && x < 221.0f; x += 1.0f) {
                s_disc(236.5f, 9.5f + x, 0.55f, GOLD, 0.9f);
                s_disc(3.5f, 9.5f + x, 0.55f, GOLD, 0.9f);
            }
        if (L > 337.0f)
            for (x = 0.0f; x < L - 337.0f && x < 116.0f; x += 1.0f) {
                s_disc(236.5f - x, 230.5f, 0.55f, GOLD, 0.9f);
                s_disc(3.5f + x, 230.5f, 0.55f, GOLD, 0.9f);
            }
    }
    /* Kreise um die Mitte, die sich öffnen */
    for (i = 0; i < 4; i++) {
        r = 18.0f + (float)i * 14.0f;
        s_arc(cx, cy, r, 1.0f, -180.0f, -180.0f + 360.0f * sat01(lines * 1.4f - (float)i * 0.15f), GOLD_DIM, 0.7f, 0.0f);
    }
    for (i = 0; i < 3; i++) {             /* drei LEDs zählen hoch */
        int on = t > 0.5f + (float)i * 0.18f;
        s_rect(116.0f, 92.0f + (float)i * 8.0f, 8.0f, 3.0f, on ? RED : RED_DIM, 1.0f);
        if (on)
            s_bloom(120.0f, 93.5f + (float)i * 8.0f, 7.0f, RED, 0.4f);
    }
    if (name > 0.0f) {
        uint16_t c = c565(mix3(rgb3(14, 13, 12), GOLD_HI, name));
        float w = (float)s_text_w(&FONT_SERIF_XL, "FIKH-S") + 24.0f;
        s_frame(cx - w * 0.5f, 152.0f, cx + w * 0.5f, 188.0f, 1.5f, mix3(rgb3(14, 13, 12), GOLD, name), 0.95f);
        s_ctext(cx, 156.0f, &FONT_SERIF_XL, "FIKH-S", c, 1);
        s_bloom(cx, 170.0f, 70.0f * name, GOLD, 0.05f * name);
    }
    if (sub > 0.0f) {
        s_ctext(cx, 196.0f, &FONT_SERIF_XS, "EXPERIMENTAL SYNTHESIS", c565(mix3(rgb3(14, 13, 12), GOLD, sub)), 1);
        s_ctext(cx, 214.0f, &FONT_SERIF_XS, "BY SWTDR34MZ", c565(mix3(rgb3(14, 13, 12), GOLD_DIM, sub)), 1);
    }
    cv_oy = 0;
}

static void intro_draw(float t)
{
    cv_begin(240, 120, C_BLACK);
    intro_scene(t, 0, 120);
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    intro_scene(t, 120, 120);
    cv_blit(0, 120);
}

/* ------------------------------------------------------------ der Vertrag --- */
void app_init(void)
{
    fk_mem m;
    int i;
    m.dl[0] = mem_dl[0];
    m.dl[1] = mem_dl[1];
    m.ch[0] = mem_ch[0];
    m.ch[1] = mem_ch[1];
    m.ap = mem_ap;
    for (i = 0; i < 4; i++)
        m.comb[i] = mem_cb[i];
    m.wt = mem_wt;
    m.sn = mem_sn;
    settings_load();
    fk_init(&m);
    fk_algo(set.algo);
    fk_params(cur_p());
    fk_hold(set.hold);
    for (i = 0; i < NKEYS; i++)
        ui.key_note[i] = -1;
    ui.read_p = -1;
}

static void midi_in(void)                /* im Audio-Interrupt: Noten, Pitch Bend, CC 1 */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ty = st & 0xF0u;
        if (st >= 0xF0u)
            continue;
        if (ty == 0x90u && d2)
            fk_note_on((int)d1, (int)d2);
        else if (ty == 0x80u || ty == 0x90u)
            fk_note_off((int)d1);
        else if (ty == 0xE0u)
            fk_bend(((float)(int)(d2 << 7 | d1) - 8192.0f) * (2.0f / 8192.0f));
        else if (ty == 0xB0u && (d1 == 123u || d1 == 120u))
            fk_all_off();
        else if (ty == 0xB0u && d1 == 1u) {
            set.p[set.algo][P_MORPH] = (uint8_t)d2;
            fk_params(set.p[set.algo]);
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float bl[FK_BLOCK_MAX], br[FK_BLOCK_MAX];
    float gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    uint32_t i, w;
    if (n > FK_BLOCK_MAX)
        n = FK_BLOCK_MAX;
    midi_in();
    fk_render(bl, br, (int)n);
    w = scope_w;
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(bl[i] * gain);
        lr[2 * i + 1] = (int32_t)(br[i] * gain);
        scope_ring[(w + i) % SCOPE_N] = (bl[i] + br[i]) * 0.5f;
    }
    scope_w = w + n;
}

void app_panic(void) { fk_panic(); }

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, RGB(14, 13, 12));
    intro_draw(3.0f);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, RGB(14, 13, 12));
    touch_on(1);
    ui.intro = 1;
    ui.intro_at = plat_ms();
    ui.redraw = R_ALL;
    ui.note_drawn = -2;
    ui.batt_drawn = -9;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    int k;
    if (ui.intro) {
        uint32_t t = now - ui.intro_at;
        if (t < INTRO_MS && !plat_keys() && !plat_buttons()) {
            intro_draw((float)t * 0.001f);
            plat_lights(0, 0, 0, 0, 0);
            return;
        }
        ui.intro = 0;
        ui.keys = plat_keys();
        ui.btn = plat_buttons();
        ui.redraw = R_ALL;
    }
    input();
    lights();
    ui.frame++;
    if (ann_on() && ann_where()) {
        char t[64] = "FIKH-S. ";
        text_cat(t, FK_ALGOS[set.algo].name);
        text_cat(t, ". ");
        text_cat(t, PAGE_NAME[ui.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (!(ui.frame % 3u))                /* der Scope: ~20 Bilder pro Sekunde */
        ui.redraw |= R_SCOPE;
    {
        int nd = fk_note() * 4 + (int)(fk_env() * 3.0f);
        if (nd != ui.note_drawn) {
            ui.note_drawn = nd;
            ui.redraw |= R_BASIS;
        }
    }
    {   /* der Akku */
        int lvl = batt_level(), key = lvl < 0 ? -1 : lvl * 1000 + batt_pct(lvl) + (int)((now / 500u) & 1u) * 300;
        if (key != ui.batt_drawn) {
            ui.batt_drawn = key;
            ui.redraw |= R_FOOT;
        }
    }
    for (k = 0; k < 4; k++)
        if (ui.knob_at[k] && now - ui.knob_at[k] >= 800u) {
            ui.knob_at[k] = 0;
            ui.redraw |= (uint32_t)R_KNOB0 << k;
        }
    if (ui.read_until && now >= ui.read_until) {
        ui.read_until = 0;
        ui.redraw |= R_READ;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw |= R_READ;
    }
    /* die Last: bleibt sie eine Sekunde über 88 %, rechnet die Engine mit weniger Oszillatoren (ECO) */
    if (now - ui.load_at >= 500u) {
        ui.load_at = now;
        if (plat_cpu_pct() > 88u && !fk_eco_on()) {
            if (++ui.hot >= 2) {
                fk_eco(1);
                app_say("ECO MODE", "CPU HIGH");
                ui.redraw |= R_FOOT;
            }
        } else {
            ui.hot = 0;
        }
    }
    /* speichern: auf SAVE (leise geblendet), sonst von selbst, wenn es still ist */
    if (ui.saving) {
        if (fk_ducked() || now - ui.save_at > 200u) {
            set.magic = SET_MAGIC;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
            fk_duck(0);
            ui.saving = 0;
        }
    } else if (set_dirty && now - dirty_at > 2000u && fk_level() < 0.0015f && fk_env() < 0.001f) {
        set.magic = SET_MAGIC;
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
    if (ui.redraw) {
        uint32_t r = ui.redraw;
        ui.redraw = 0;
        draw(r);
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strncmp(what, "batt", 4) && what[4] >= '0' && what[4] <= '9') {   /* battN: den Akku vorgeben (Tests) */
        ui.batt_test = (uint8_t)(what[4] - '0');
        ui.redraw |= R_FOOT;
        *got = 0;
        return 1;
    }
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "algo"))
        *got = set.algo;
    else if (!strcmp(what, "engine_algo"))
        *got = fk_cur_algo();
    else if (!strcmp(what, "note"))
        *got = fk_note();
    else if (!strcmp(what, "hold"))
        *got = set.hold;
    else if (!strcmp(what, "freeze"))
        *got = fk_frozen();
    else if (!strcmp(what, "oct"))
        *got = set.oct;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "eco"))
        *got = fk_eco_on();
    else if (!strcmp(what, "level_x1000"))
        *got = (int)(fk_level() * 1000.0f);
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9')
        *got = cur_p()[atoi(what + 1)];
    else
        return 0;
    return 1;
}
#endif
