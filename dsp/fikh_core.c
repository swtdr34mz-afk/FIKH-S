/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: der Klangkern als eine Übersetzungseinheit (auf dem FM-1 mit -O2, fm1.toml "o2"). Hier: der Zustand,
 * die Noten, der Wurf von SEED, die Drift, und der Block. Die Teile sind eingebunden:
 *   fikh_params.c  Parameter, Algorithmen, Anzeige     fikh_tables.c  Wavetables
 *   fikh_gen.c     die neun Generatoren                fikh_proc.c    Filter, Effekte, der Prozessor */
#include <string.h>
#include "fikh_core.h"
#include "fikh_dsp.h"

float *fk_sin_tab;

typedef struct {
    float chorus, sat, res, bpm, psine, dmod, diff, crush, phaser;
} fk_cfg;

typedef struct {                         /* die geglätteten Werte: bipolar -1..1, sonst 0..1 */
    float warp, span, morph, fuse, mix, feed, form, time, field, cell, atk, rel, glide, drift, width, level, scan;
} fk_vals;

static struct {
    fk_mem m;
    int algo, algo_next;
    float xf, xf_dir;                    /* die Blende beim Wechsel */
    uint8_t raw[FK_NP];
    volatile uint8_t fresh;
    float tgt[FK_NP];                    /* die Ziele, normiert */
    fk_vals v;                           /* geglättet, mit DRIFT */
    float sm[FK_NP];                     /* geglättet, ohne DRIFT */
    int seed;                            /* der Wurf, der gerade gilt (-1: neu würfeln) */
    float r_off[8], r_off2[8], r_wave[8], r_wave2[8], r_vol[8], r_pan[8];
    int r_set;
    float tap_t[2][4], tap[2][4];
    /* die Noten */
    int stack[16], nstack;
    int note, gate, hold;
    float vel, pitch, pitch_t, bend, f0, env;
    /* die Oszillatoren */
    float ph[2][8], fb[2][8], semi[4], nz[2];
    float dr[2][8], drt[2][8];           /* die eigene Drift jedes Oszillators, je Kanal (Cent) */
    float mac_ph, mac_a, mac_b, mac_v[2];   /* DRIFT: die langsame Bewegung von MORPH und SCAN */
    uint32_t noise;
    fk_cfg cfg;
    /* Filter, Effekte */
    fk_svf flt[2], bpf[2];
    fk_svf_c fc, bpc;
    int field_hp;
    uint32_t chw;
    float ch_lfo, ph_lfo, phz[2][4], sine_ph;
    uint32_t cbw;
    float cbz[4];
    /* der Prozessor */
    uint32_t dw;
    float dsm, dz[2], hz[2], gp;
    float hil[2][2][16], hd[2], rc, rs, shift_hz;
    /* der Ausgang */
    float dcx[2], dcy[2], lvl, wlvl, duck_g;
    int duck, eco, freeze;
    float frz;                           /* FREEZE, weich ein- und ausgeblendet (0..1) */
} E;

static float fk_time_ms(float u);
static float fk_atk_ms(float u);
#include "fikh_params.c"
#include "fikh_tables.c"
#include "fikh_gen.c"
#include "fikh_proc.c"

/* ------------------------------------------------------------------ SEED --- */
static void reseed(int s)
{
    uint32_t r = fk_hash((uint32_t)s * 2654435761u + (uint32_t)E.algo * 977u + 12345u), k;
    for (k = 0; k < 8u; k++) {
        E.r_off[k] = fk_rand11(&r);
        E.r_off2[k] = fk_rand11(&r);
        E.r_wave[k] = fk_rand01(&r);
        E.r_wave2[k] = fk_rand01(&r);
        E.r_vol[k] = fk_rand01(&r);
        E.r_pan[k] = fk_rand11(&r);
    }
    E.r_off[0] *= 0.25f;                 /* eine Stimme bleibt nah an BASIS: der Grundton trägt */
    E.r_set = (int)(fk_rng(&r) % 3u);
    for (k = 0; k < 2u; k++) {
        E.tap_t[k][0] = 1.0f;
        E.tap_t[k][1] = 0.30f + 0.55f * fk_rand01(&r);
        E.tap_t[k][2] = 0.14f + 0.40f * fk_rand01(&r);
        E.tap_t[k][3] = 0.55f + 0.42f * fk_rand01(&r);
    }
    E.seed = s;
}

/* ------------------------------------------------------------------ Noten --- */
void fk_note_on(int note, int vel)
{
    int i, j = 0;
    for (i = 0; i < E.nstack; i++)       /* dieselbe Note nicht doppelt */
        if (E.stack[i] != note)
            E.stack[j++] = E.stack[i];
    E.nstack = j;
    if (E.nstack >= 16) {
        for (i = 1; i < 16; i++)
            E.stack[i - 1] = E.stack[i];
        E.nstack = 15;
    }
    E.stack[E.nstack++] = note;
    if (E.note < 0)
        E.pitch = (float)note;           /* die erste Note: kein Gleiten von irgendwo */
    E.note = note;
    E.pitch_t = (float)note;
    E.vel = 0.55f + 0.45f * (float)vel / 127.0f;
    E.gate = 1;
}

void fk_note_off(int note)
{
    int i, j = 0;
    for (i = 0; i < E.nstack; i++)
        if (E.stack[i] != note)
            E.stack[j++] = E.stack[i];
    E.nstack = j;
    if (E.nstack > 0) {                  /* zurück zur Taste davor, die noch gehalten wird */
        E.note = E.stack[E.nstack - 1];
        E.pitch_t = (float)E.note;
    } else {
        E.gate = 0;
    }
}

void fk_hold(int on) { E.hold = on; }
void fk_all_off(void)
{
    E.nstack = 0;
    E.gate = 0;
}
void fk_panic(void)
{
    fk_all_off();
    E.hold = 0;
    E.env = 0.0f;
}
void fk_bend(float semis) { E.bend = semis; }
void fk_freeze(int on) { E.freeze = on; }
int fk_frozen(void) { return E.freeze; }
void fk_eco(int on) { E.eco = on; }
int fk_eco_on(void) { return E.eco; }
void fk_duck(int on) { E.duck = on; }
int fk_ducked(void) { return E.duck && E.duck_g <= 0.0f; }

/* ------------------------------------------------------------- Parameter --- */
void fk_params(const uint8_t p[FK_NP])
{
    memcpy(E.raw, p, FK_NP);
    E.fresh = 1;
}

void fk_algo(int a)
{
    if (a < 0 || a >= FK_NALGO || (a == E.algo && E.algo_next < 0))
        return;
    E.algo_next = a;
    E.xf_dir = -1.0f;                    /* ausblenden; gewechselt wird, wenn es still ist */
}

static void targets(void)
{
    int i;
    for (i = 0; i < FK_NP; i++)
        E.tgt[i] = FK_PARAMS[i].bip ? fk_bip(E.raw[i]) : (float)E.raw[i] / 127.0f;
    if (E.seed != (int)E.raw[P_SEED])
        reseed(E.raw[P_SEED]);
}

void fk_init(const fk_mem *m)
{
    int i, c;
    memset(&E, 0, sizeof E);
    E.m = *m;
    memset(E.m.dl[0], 0, FK_DL_N * sizeof(int16_t));
    memset(E.m.dl[1], 0, FK_DL_N * sizeof(int16_t));
    memset(E.m.ch[0], 0, FK_CH_N * sizeof(float));
    memset(E.m.ch[1], 0, FK_CH_N * sizeof(float));
    memset(E.m.ap, 0, FK_AP_N * sizeof(float));
    for (i = 0; i < 4; i++)
        memset(E.m.comb[i], 0, FK_CB_N * sizeof(float));
    fk_sin_tab = E.m.sn;
    for (i = 0; i <= (int)FK_SIN_N; i++)
        E.m.sn[i] = fm_sin_turns((float)i / (float)FK_SIN_N);
    wt_build(E.m.wt);
    proc_init();
    E.algo_next = -1;
    E.note = -1;
    E.xf = 1.0f;
    E.duck_g = 1.0f;
    E.rc = 1.0f;
    E.noise = 0x2545F491u;
    E.seed = -1;
    for (c = 0; c < 2; c++)
        for (i = 0; i < 8; i++)
            E.ph[c][i] = (float)((i * 37) % 64) / 64.0f;   /* links und rechts in Phase: breit nur durch die Drift */
    memcpy(E.raw, FK_ALGOS[0].init, FK_NP);
    targets();
    for (i = 0; i < FK_NP; i++)
        E.sm[i] = E.tgt[i];
    E.dsm = fk_time_ms(E.sm[P_TIME]) * (FK_SR / 1000.0f);
    E.pitch = E.pitch_t = 48.0f;
}

/* ------------------------------------------------------------------ Block --- */
/* Zustände, die gegen null auslaufen, werden sonst denormal (winzig): das rechnet langsam. Einmal je Block auf 0 */
static void flush_states(void)
{
    int c, i;
    float *f;
    E.env = fm_flush(E.env);
    for (c = 0; c < 2; c++) {
        E.dz[c] = fm_flush(E.dz[c]);
        E.hz[c] = fm_flush(E.hz[c]);
        E.dcx[c] = fm_flush(E.dcx[c]);
        E.dcy[c] = fm_flush(E.dcy[c]);
        E.hd[c] = fm_flush(E.hd[c]);
        E.nz[c] = fm_flush(E.nz[c]);
        E.flt[c].ic1 = fm_flush(E.flt[c].ic1);
        E.flt[c].ic2 = fm_flush(E.flt[c].ic2);
        E.bpf[c].ic1 = fm_flush(E.bpf[c].ic1);
        E.bpf[c].ic2 = fm_flush(E.bpf[c].ic2);
        for (i = 0; i < 4; i++)
            E.phz[c][i] = fm_flush(E.phz[c][i]);
        for (i = 0; i < 8; i++)
            E.fb[c][i] = fm_flush(E.fb[c][i]);
    }
    f = &E.hil[0][0][0];
    for (i = 0; i < 2 * 2 * 16; i++)
        f[i] = fm_flush(f[i]);
    for (i = 0; i < 4; i++)
        E.cbz[i] = fm_flush(E.cbz[i]);
}

static void block_controls(int n)
{
    float k = 1.0f - fm_expf(-(float)n / (0.035f * FK_SR)), dk, gl;
    int i, c;
    if (E.fresh) {
        E.fresh = 0;
        targets();
    }
    for (i = 0; i < FK_NP; i++)
        E.sm[i] = fm_flush(E.sm[i] + (E.tgt[i] - E.sm[i]) * k);
    /* DRIFT: jeder Oszillator wandert für sich (bis ±30 Cent), MORPH und SCAN atmen langsam mit */
    dk = E.sm[P_DRIFT];
    for (c = 0; c < 2; c++)
        for (i = 0; i < 8; i++) {
            if (fk_rand01(&E.noise) < (float)n / (0.45f * FK_SR))
                E.drt[c][i] = fk_rand11(&E.noise) * 30.0f;
            /* rechts: zum Teil die Drift von links (WIDTH: wie viel eigene), damit es breit, aber nicht hohl wird */
            E.dr[c][i] += ((c ? fk_lerp(E.drt[0][i], E.drt[1][i], 0.25f + 0.45f * E.sm[P_WIDTH]) : E.drt[0][i]) * dk
                           - E.dr[c][i]) * (float)n * (1.0f / (0.8f * FK_SR));
        }
    E.mac_ph += (0.05f + 0.25f * dk) * (float)n / FK_SR;
    if (E.mac_ph >= 1.0f) {
        E.mac_ph -= 1.0f;
        E.mac_a = E.mac_b;
        E.mac_b = fk_rand11(&E.noise);
    }
    {
        float t = 0.5f - 0.5f * fm_cosf(E.mac_ph * FK_PI), w = fk_lerp(E.mac_a, E.mac_b, t);
        E.mac_v[0] = w;
        E.mac_v[1] = fk_sin(E.mac_ph * 0.5f + w * 0.2f);
    }
    E.v.warp = E.sm[P_WARP];
    E.v.span = E.sm[P_SPAN];
    E.v.morph = fk_clamp(E.sm[P_MORPH] + E.mac_v[0] * 0.18f * dk, 0.0f, 1.0f);
    E.v.fuse = E.sm[P_FUSE];
    E.v.mix = E.sm[P_MIX];
    E.v.feed = E.sm[P_FEED];
    E.v.form = E.sm[P_FORM];
    E.v.time = E.sm[P_TIME];
    E.v.field = E.sm[P_FIELD];
    E.v.cell = E.sm[P_CELL];
    E.v.atk = E.sm[P_ATK];
    E.v.rel = E.sm[P_REL];
    E.v.glide = E.sm[P_GLIDE];
    E.v.drift = dk;
    E.v.width = E.sm[P_WIDTH];
    E.v.level = E.sm[P_LEVEL];
    E.v.scan = fk_clamp(E.sm[P_SCAN] + E.mac_v[1] * 0.14f * dk, 0.0f, 1.0f);
    for (c = 0; c < 2; c++)
        for (i = 0; i < 4; i++)
            E.tap[c][i] += (E.tap_t[c][i] - E.tap[c][i]) * k * 0.5f;
    /* BASIS: die Tonhöhe gleitet (GLIDE) */
    gl = fk_glide_ms(E.v.glide);
    if (gl <= 0.0f)
        E.pitch = E.pitch_t;
    else
        E.pitch += (E.pitch_t - E.pitch) * (1.0f - fm_expf(-(float)n / (gl * 0.001f * FK_SR)));
    E.f0 = 440.0f * fm_exp2f((E.pitch + E.bend - 69.0f) * (1.0f / 12.0f));
    /* FIELD */
    E.field_hp = E.v.field > 0.0f;
    memset(&E.cfg, 0, sizeof E.cfg);
}

void fk_render(float *l, float *r, int n)
{
    static float gl[FK_BLOCK_MAX], gr[FK_BLOCK_MAX];
    int i, c;
    float lg, wd;
    if (n > FK_BLOCK_MAX)
        n = FK_BLOCK_MAX;
    if (E.algo_next >= 0 && E.xf <= 0.0f) {   /* still: jetzt wechseln, dann einblenden */
        E.algo = E.algo_next;
        E.algo_next = -1;
        E.seed = -1;
        targets();
        for (c = 0; c < 2; c++)
            for (i = 0; i < 8; i++)
                E.fb[c][i] = 0.0f;
        E.xf_dir = 1.0f;
    }
    block_controls(n);
    switch (E.algo) {
    case 0: gen_metal(gl, gr, n); break;
    case 1: gen_shaper(gl, gr, n); break;
    case 2: gen_sines(gl, gr, n); break;
    case 3: gen_quad(gl, gr, n); break;
    case 4: gen_diatonic(gl, gr, n); break;
    case 5: gen_chords(gl, gr, n); break;
    case 6: gen_partials(gl, gr, n); break;
    case 7: gen_combs(gl, gr, n); break;
    default: gen_grain(gl, gr, n); break;
    }
    /* das Filter: FIELD und die Resonanz, die FUSE dazugibt */
    fk_svf_set(&E.fc, fk_field_hz(E.v.field), 0.08f + E.cfg.res * 0.9f);
    if (E.cfg.bpm > 0.0f)
        fk_svf_set(&E.bpc, E.f0 * 3.0f * fm_exp2f(E.v.morph * 2.0f), 0.75f);
    E.shift_hz = E.algo == 8 ? 0.0f : fk_cell_hz(E.v.cell);
    proc_block(gl, gr, l, r, n);
    /* der Ausgang: Gleichanteil weg, Breite, Pegel, weich begrenzt */
    lg = fm_exp2f(fk_level_db(E.v.level) * 0.1660964f) * 0.9f;
    wd = E.v.width * 1.1f;
    for (i = 0; i < n; i++) {
        float a = l[i], b = r[i], m, s;
        E.dcy[0] = a - E.dcx[0] + 0.9975f * E.dcy[0];
        E.dcx[0] = a;
        E.dcy[1] = b - E.dcx[1] + 0.9975f * E.dcy[1];
        E.dcx[1] = b;
        m = (E.dcy[0] + E.dcy[1]) * 0.5f;
        s = (E.dcy[0] - E.dcy[1]) * 0.5f * wd;
        a = fk_sat((m + s) * lg);
        b = fk_sat((m - s) * lg);
        l[i] = a;
        r[i] = b;
        a = fk_abs(a) > fk_abs(b) ? fk_abs(a) : fk_abs(b);
        E.lvl += (a - E.lvl) * (a > E.lvl ? 0.01f : 0.0004f);
    }
    flush_states();
}

/* ------------------------------------------------------------- Anzeige --- */
int fk_note(void) { return (E.gate || E.hold) ? E.note : -1; }
float fk_pitch(void) { return E.pitch; }
int fk_gate(void) { return E.gate; }
float fk_env(void) { return E.env; }
float fk_level(void) { return E.lvl; }
float fk_wet_level(void) { return E.wlvl; }
int fk_cur_algo(void) { return E.algo_next >= 0 ? E.algo_next : E.algo; }
uint32_t fk_seed_hash(void) { return (uint32_t)E.seed; }
