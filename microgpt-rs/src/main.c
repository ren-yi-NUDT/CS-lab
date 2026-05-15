/*
 * microgpt-simd.c — SIMD-optimized microGPT in C
 *
 * Direct forward + analytical backward, no autograd graph.
 * AVX2 + FMA intrinsics for all matrix-vector and dot products.
 * f32 throughout for maximum SIMD throughput (8 floats/lane).
 *
 * Compile: gcc -O3 -march=native -lm -o microgpt-simd microgpt-simd.c
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <immintrin.h>
#include <time.h>

#define N_EMBD 16
#define BLOCK_SIZE 16
#define N_HEAD 4
#define HEAD_DIM (N_EMBD / N_HEAD)
#define MLP_DIM (4 * N_EMBD)
#define N_LAYER 1

// ==================== SIMD helpers ====================

static inline float hsum_avx(__m256 v) {
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 hi = _mm256_extractf128_ps(v, 1);
    lo = _mm_add_ps(lo, hi);         // 4 floats
    __m128 shuf = _mm_movehdup_ps(lo);
    __m128 sums = _mm_add_ps(lo, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    return _mm_cvtss_f32(sums);
}

static inline float dot_f32(const float *a, const float *b, int n) {
    __m256 sum = _mm256_setzero_ps();
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        sum = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), sum);
    }
    float r = hsum_avx(sum);
    for (; i < n; i++) r += a[i] * b[i];
    return r;
}

// y = W * x, W is [nr x nc] row-major
static inline void matvec(const float *W, const float *x, float *y, int nr, int nc) {
    for (int r = 0; r < nr; r++) {
        y[r] = dot_f32(W + r * nc, x, nc);
    }
}

// Backward: given grad_y[nr], accumulate grad_W and grad_x
static inline void matvec_bwd(float *gW, float *gx, const float *W, const float *x,
                               const float *gy, int nr, int nc) {
    for (int r = 0; r < nr; r++) {
        float g = gy[r];
        if (g == 0.0f) continue;
        const float *row = W + r * nc;
        float *grow = gW + r * nc;
        __m256 vg = _mm256_set1_ps(g);
        int c = 0;
        for (; c + 8 <= nc; c += 8) {
            __m256 vx = _mm256_loadu_ps(x + c);
            __m256 vr = _mm256_loadu_ps(row + c);
            __m256 cgw = _mm256_loadu_ps(grow + c);
            _mm256_storeu_ps(grow + c, _mm256_fmadd_ps(vg, vx, cgw));
            __m256 cgx = _mm256_loadu_ps(gx + c);
            _mm256_storeu_ps(gx + c, _mm256_fmadd_ps(vg, vr, cgx));
        }
        for (; c < nc; c++) {
            grow[c] += g * x[c];
            gx[c] += g * row[c];
        }
    }
}

// ==================== Forward primitives ====================

static inline float rmsnorm(const float *x, float *out, int n) {
    float ss = 0.0f;
    for (int i = 0; i < n; i++) ss += x[i] * x[i];
    ss = ss / n + 1e-5f;
    float inv = 1.0f / sqrtf(ss);
    for (int i = 0; i < n; i++) out[i] = x[i] * inv;
    return ss;
}

static inline void rmsnorm_bwd(const float *x, const float *out, const float *gout,
                                float *gx, float ss, int n) {
    float inv = 1.0f / sqrtf(ss);
    float dot = 0.0f;
    for (int i = 0; i < n; i++) dot += out[i] * gout[i];
    float fn = (float)n;
    for (int i = 0; i < n; i++) gx[i] += inv * (gout[i] - out[i] * dot / fn);
}

static inline void softmax_fwd(const float *x, float *out, int n) {
    float mx = x[0];
    for (int i = 1; i < n; i++) if (x[i] > mx) mx = x[i];
    float s = 0.0f;
    for (int i = 0; i < n; i++) { out[i] = expf(x[i] - mx); s += out[i]; }
    for (int i = 0; i < n; i++) out[i] /= s;
}

static inline void softmax_bwd(const float *p, const float *gp, float *gx, int n) {
    float dot = 0.0f;
    for (int i = 0; i < n; i++) dot += p[i] * gp[i];
    for (int i = 0; i < n; i++) gx[i] = p[i] * (gp[i] - dot);
}

// ==================== Parameter layout ====================
// All parameters in one contiguous float array, row-major.

typedef struct {
    int wte, wpe, lm;
    struct { int wq, wk, wv, wo, fc1, fc2; } layers[N_LAYER];
    int n_params, vocab_size;
} Layout;

static Layout build_layout(int vs) {
    Layout L = {0};
    int *o = &(int){0};
    #define TAKE(n) ({ int s = *o; *o += n; s; })
    L.wte = TAKE(vs * N_EMBD);
    L.wpe = TAKE(BLOCK_SIZE * N_EMBD);
    L.lm = TAKE(vs * N_EMBD);
    for (int i = 0; i < N_LAYER; i++) {
        L.layers[i].wq = TAKE(N_EMBD * N_EMBD);
        L.layers[i].wk = TAKE(N_EMBD * N_EMBD);
        L.layers[i].wv = TAKE(N_EMBD * N_EMBD);
        L.layers[i].wo = TAKE(N_EMBD * N_EMBD);
        L.layers[i].fc1 = TAKE(MLP_DIM * N_EMBD);
        L.layers[i].fc2 = TAKE(N_EMBD * MLP_DIM);
    }
    L.n_params = *o;
    L.vocab_size = vs;
    #undef TAKE
    return L;
}

// ==================== Training step ====================
// All intermediates in pre-allocated arrays — zero malloc per step.

typedef struct {
    // Per-position saved intermediates [BLOCK_SIZE * dim]
    float x_emb[BLOCK_SIZE * N_EMBD];
    float x_norm1[BLOCK_SIZE * N_EMBD];
    float rms1_ss[BLOCK_SIZE];
    float q[BLOCK_SIZE * N_EMBD];
    float k[BLOCK_SIZE * N_EMBD];
    float v[BLOCK_SIZE * N_EMBD];
    float attn_w[BLOCK_SIZE * N_HEAD * BLOCK_SIZE]; // padded
    float x_attn[BLOCK_SIZE * N_EMBD];
    float x_post_attn[BLOCK_SIZE * N_EMBD];
    float x_norm2[BLOCK_SIZE * N_EMBD];
    float rms2_ss[BLOCK_SIZE];
    float x_fc1[BLOCK_SIZE * MLP_DIM];
    float x_relu[BLOCK_SIZE * MLP_DIM];
    float x_out[BLOCK_SIZE * N_EMBD];
    float probs[BLOCK_SIZE * 64]; // max vocab
    // Temp buffers
    float tmp_n[N_EMBD];
    float tmp_n2[N_EMBD];
    float tmp_m[MLP_DIM];
    float gl[64]; // grad logits
    // Gradient accumulators
    float gk[BLOCK_SIZE * N_EMBD];
    float gv[BLOCK_SIZE * N_EMBD];
    float gq[BLOCK_SIZE * N_EMBD];
    float gxn1[BLOCK_SIZE * N_EMBD];
    float gxpa[BLOCK_SIZE * N_EMBD];
    // Parameter gradients
    float gp[8192]; // max params
    // Adam state
    float m[8192];
    float v_adam[8192];
    // Tokens
    int tokens[BLOCK_SIZE + 1];
    int targets[BLOCK_SIZE];
} State;

static float train_step(float *p, const Layout *L, State *s, int seqlen, int step, int num_steps) {
    const float scale = 1.0f / sqrtf((float)HEAD_DIM);
    const int vs = L->vocab_size;
    const int lo_wq = L->layers[0].wq, lo_wk = L->layers[0].wk, lo_wv = L->layers[0].wv;
    const int lo_wo = L->layers[0].wo, lo_fc1 = L->layers[0].fc1, lo_fc2 = L->layers[0].fc2;

    // Clear gradient buffers
    memset(s->gk, 0, seqlen * N_EMBD * sizeof(float));
    memset(s->gv, 0, seqlen * N_EMBD * sizeof(float));
    memset(s->gq, 0, seqlen * N_EMBD * sizeof(float));
    memset(s->gxn1, 0, seqlen * N_EMBD * sizeof(float));
    memset(s->gxpa, 0, seqlen * N_EMBD * sizeof(float));
    memset(s->gp, 0, L->n_params * sizeof(float));

    // ---- Forward all positions ----
    for (int pos = 0; pos < seqlen; pos++) {
        int tok = s->tokens[pos];
        float *xe = s->x_emb + pos * N_EMBD;
        float *xn1 = s->x_norm1 + pos * N_EMBD;
        float *qp = s->q + pos * N_EMBD;
        float *kp = s->k + pos * N_EMBD;
        float *vp = s->v + pos * N_EMBD;
        float *xa = s->x_attn + pos * N_EMBD;
        float *xpa = s->x_post_attn + pos * N_EMBD;
        float *xn2 = s->x_norm2 + pos * N_EMBD;
        float *xf = s->x_fc1 + pos * MLP_DIM;
        float *xr = s->x_relu + pos * MLP_DIM;
        float *xo = s->x_out + pos * N_EMBD;
        float *pr = s->probs + pos * vs;

        // Embedding
        for (int i = 0; i < N_EMBD; i++)
            xe[i] = p[L->wte + tok * N_EMBD + i] + p[L->wpe + pos * N_EMBD + i];

        // RMSNorm1
        s->rms1_ss[pos] = rmsnorm(xe, xn1, N_EMBD);

        // QKV
        matvec(p + lo_wq, xn1, qp, N_EMBD, N_EMBD);
        matvec(p + lo_wk, xn1, kp, N_EMBD, N_EMBD);
        matvec(p + lo_wv, xn1, vp, N_EMBD, N_EMBD);

        // Attention
        int seq = pos + 1;
        for (int h = 0; h < N_HEAD; h++) {
            int hs = h * HEAD_DIM;
            float *aw = s->attn_w + pos * N_HEAD * BLOCK_SIZE + h * BLOCK_SIZE;

            // Logits
            for (int t = 0; t < seq; t++)
                aw[t] = dot_f32(qp + hs, s->k + t * N_EMBD + hs, HEAD_DIM) * scale;

            // Softmax (in-place)
            softmax_fwd(aw, aw, seq);

            // Weighted sum of V
            for (int j = 0; j < HEAD_DIM; j++) {
                float val = 0.0f;
                for (int t = 0; t < seq; t++)
                    val += aw[t] * s->v[t * N_EMBD + hs + j];
                xa[hs + j] = val;
            }
        }

        // Output projection + residual
        matvec(p + lo_wo, xa, s->tmp_n, N_EMBD, N_EMBD);
        for (int i = 0; i < N_EMBD; i++) xpa[i] = s->tmp_n[i] + xn1[i];

        // RMSNorm2
        s->rms2_ss[pos] = rmsnorm(xpa, xn2, N_EMBD);

        // MLP
        matvec(p + lo_fc1, xn2, xf, MLP_DIM, N_EMBD);
        for (int i = 0; i < MLP_DIM; i++) xr[i] = xf[i] > 0.0f ? xf[i] : 0.0f;
        matvec(p + lo_fc2, xr, s->tmp_n, N_EMBD, MLP_DIM);
        for (int i = 0; i < N_EMBD; i++) xo[i] = s->tmp_n[i] + xpa[i];

        // LM head + softmax (reuse gl as logits buffer)
        matvec(p + L->lm, xo, s->gl, vs, N_EMBD);
        softmax_fwd(s->gl, pr, vs);
    }

    // Loss
    float loss = 0.0f;
    for (int pos = 0; pos < seqlen; pos++)
        loss -= logf(s->probs[pos * vs + s->targets[pos]]);
    loss /= seqlen;

    // ---- Backward all positions ----
    for (int pos = 0; pos < seqlen; pos++) {
        int target = s->targets[pos];
        float *pr = s->probs + pos * vs;

        // CE grad: p[i] - (i == target)
        for (int i = 0; i < vs; i++) s->gl[i] = pr[i];
        s->gl[target] -= 1.0f;

        // LM head backward
        memset(s->tmp_n, 0, N_EMBD * sizeof(float));
        matvec_bwd(s->gp + L->lm, s->tmp_n, p + L->lm, s->x_out + pos * N_EMBD,
                   s->gl, vs, N_EMBD);

        // x_out = x_mlp + x_post_attn -> grad flows to both
        // fc2 backward
        memset(s->tmp_m, 0, MLP_DIM * sizeof(float));
        matvec_bwd(s->gp + lo_fc2, s->tmp_m, p + lo_fc2, s->x_relu + pos * MLP_DIM,
                   s->tmp_n, N_EMBD, MLP_DIM);

        // ReLU backward
        for (int i = 0; i < MLP_DIM; i++)
            s->tmp_m[i] = s->x_fc1[pos * MLP_DIM + i] > 0.0f ? s->tmp_m[i] : 0.0f;

        // fc1 backward
        memset(s->tmp_n2, 0, N_EMBD * sizeof(float));
        matvec_bwd(s->gp + lo_fc1, s->tmp_n2, p + lo_fc1, s->x_norm2 + pos * N_EMBD,
                   s->tmp_m, MLP_DIM, N_EMBD);

        // RMSNorm2 backward
        float *gxpa = s->gxpa + pos * N_EMBD;
        rmsnorm_bwd(s->x_post_attn + pos * N_EMBD, s->x_norm2 + pos * N_EMBD,
                    s->tmp_n2, gxpa, s->rms2_ss[pos], N_EMBD);

        // Residual from x_out
        for (int i = 0; i < N_EMBD; i++) gxpa[i] += s->tmp_n[i];

        // wo backward
        memset(s->tmp_n, 0, N_EMBD * sizeof(float));
        matvec_bwd(s->gp + lo_wo, s->tmp_n, p + lo_wo, s->x_attn + pos * N_EMBD,
                   gxpa, N_EMBD, N_EMBD);

        // Residual to x_norm1
        float *gxn1 = s->gxn1 + pos * N_EMBD;
        for (int i = 0; i < N_EMBD; i++) gxn1[i] += gxpa[i];

        // ---- Attention backward ----
        int seq = pos + 1;
        for (int h = 0; h < N_HEAD; h++) {
            int hs = h * HEAD_DIM;
            float *aw = s->attn_w + pos * N_HEAD * BLOCK_SIZE + h * BLOCK_SIZE;

            // grad_w from weighted-sum of V
            float grad_w[BLOCK_SIZE] = {0};
            for (int j = 0; j < HEAD_DIM; j++) {
                float g = s->tmp_n[hs + j];
                for (int t = 0; t < seq; t++)
                    grad_w[t] += g * s->v[t * N_EMBD + hs + j];
            }

            // Softmax backward
            float grad_logits[BLOCK_SIZE];
            softmax_bwd(aw, grad_w, grad_logits, seq);

            // Q,K gradients from logits = dot(q,k)*scale
            for (int t = 0; t < seq; t++) {
                float gl = grad_logits[t] * scale;
                for (int j = 0; j < HEAD_DIM; j++) {
                    s->gq[pos * N_EMBD + hs + j] += gl * s->k[t * N_EMBD + hs + j];
                    s->gk[t * N_EMBD + hs + j] += gl * s->q[pos * N_EMBD + hs + j];
                }
            }

            // V gradients
            for (int j = 0; j < HEAD_DIM; j++) {
                float g = s->tmp_n[hs + j];
                for (int t = 0; t < seq; t++)
                    s->gv[t * N_EMBD + hs + j] += aw[t] * g;
            }
        }
    }

    // ---- QKV linear backward ----
    for (int pos = 0; pos < seqlen; pos++) {
        float *xn1 = s->x_norm1 + pos * N_EMBD;
        float *gxn1 = s->gxn1 + pos * N_EMBD;

        matvec_bwd(s->gp + lo_wq, gxn1, p + lo_wq, xn1, s->gq + pos * N_EMBD, N_EMBD, N_EMBD);
        matvec_bwd(s->gp + lo_wk, gxn1, p + lo_wk, xn1, s->gk + pos * N_EMBD, N_EMBD, N_EMBD);
        matvec_bwd(s->gp + lo_wv, gxn1, p + lo_wv, xn1, s->gv + pos * N_EMBD, N_EMBD, N_EMBD);

        // RMSNorm1 backward
        float gx_emb[N_EMBD] = {0};
        rmsnorm_bwd(s->x_emb + pos * N_EMBD, xn1, gxn1, gx_emb, s->rms1_ss[pos], N_EMBD);

        // Embedding backward
        int tok = s->tokens[pos];
        for (int i = 0; i < N_EMBD; i++) {
            s->gp[L->wte + tok * N_EMBD + i] += gx_emb[i];
            s->gp[L->wpe + pos * N_EMBD + i] += gx_emb[i];
        }
    }

    // ---- Adam optimizer ----
    float lr = 0.01f * (1.0f - (float)step / num_steps);
    float bc1 = 1.0f - powf(0.85f, step + 1);
    float bc2 = 1.0f - powf(0.99f, step + 1);
    for (int i = 0; i < L->n_params; i++) {
        float g = s->gp[i];
        s->m[i] = 0.85f * s->m[i] + 0.15f * g;
        s->v_adam[i] = 0.99f * s->v_adam[i] + 0.01f * g * g;
        p[i] -= lr * (s->m[i] / bc1) / (sqrtf(s->v_adam[i] / bc2) + 1e-8f);
    }

    return loss;
}

// ==================== Random number generation ====================

static uint64_t rng_state = 42;

static inline float rng_normal(void) {
    // Box-Muller
    float u1 = ((rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL) >> 33) / 2147483648.0f;
    float u2 = ((rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL) >> 33) / 2147483648.0f;
    while (u1 < 1e-30f) u1 = ((rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL) >> 33) / 2147483648.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2) * 0.08f;
}

static inline float rng_uniform(void) {
    return ((rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL) >> 33) / 2147483648.0f;
}

// ==================== Main ====================

int main(int argc, char **argv) {
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // Read input
    FILE *f = NULL;
    const char *paths[] = {"input.txt", "microgpt/input.txt", "../microgpt/input.txt"};
    for (int i = 0; i < 3; i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) { fprintf(stderr, "input.txt not found\n"); return 1; }

    // Load docs
    char **docs = NULL;
    int ndocs = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *l = line + strspn(line, " \t\n\r");
        if (*l == '\0') continue;
        char *end = l + strlen(l) - 1;
        while (end > l && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
        docs = realloc(docs, (ndocs + 1) * sizeof(char*));
        docs[ndocs++] = strdup(l);
    }
    fclose(f);

    // Shuffle
    for (int i = ndocs - 1; i > 0; i--) {
        int j = (int)(rng_uniform() * (i + 1));
        if (j > i) j = i;
        char *tmp = docs[i]; docs[i] = docs[j]; docs[j] = tmp;
    }
    printf("num docs: %d\n", ndocs);

    // Build vocabulary
    int char_set[256] = {0};
    for (int i = 0; i < ndocs; i++)
        for (const char *c = docs[i]; *c; c++) char_set[(unsigned char)*c] = 1;

    char uchars[256];
    int nchars = 0;
    for (int i = 0; i < 256; i++)
        if (char_set[i]) uchars[nchars++] = (char)i;

    int c2i[256] = {0};
    for (int i = 0; i < nchars; i++) c2i[(unsigned char)uchars[i]] = i;
    int bos = nchars;
    int vs = nchars + 1;
    printf("vocab size: %d\n", vs);

    // Build layout and init params
    Layout L = build_layout(vs);
    printf("num params: %d\n", L.n_params);

    float *params = malloc(L.n_params * sizeof(float));
    for (int i = 0; i < L.n_params; i++) params[i] = rng_normal();

    State state = {0};
    int num_steps = 1000;

    // ---- Training ----
    for (int step = 0; step < num_steps; step++) {
        const char *doc = docs[step % ndocs];
        int slen = (int)strlen(doc);
        // Tokenize: bos + doc + bos
        state.tokens[0] = bos;
        for (int i = 0; i < slen; i++) state.tokens[i + 1] = c2i[(unsigned char)doc[i]];
        state.tokens[slen + 1] = bos;
        int seqlen = slen < BLOCK_SIZE ? slen : BLOCK_SIZE;
        for (int i = 0; i < seqlen; i++) state.targets[i] = state.tokens[i + 1];

        float loss = train_step(params, &L, &state, seqlen, step, num_steps);
        printf("\rstep %4d / %d | loss %.4f", step + 1, num_steps, loss);
        fflush(stdout);
    }

    // ---- Inference ----
    printf("\n--- inference (new, hallucinated names) ---\n");
    float temp = 0.5f;

    for (int si = 0; si < 20; si++) {
        int tok = bos;
        float k_cache[BLOCK_SIZE * N_EMBD] = {0};
        float v_cache[BLOCK_SIZE * N_EMBD] = {0};
        char sample[256] = {0};
        int slen = 0;

        for (int pos = 0; pos < BLOCK_SIZE; pos++) {
            float xe[N_EMBD], xn1[N_EMBD], q[N_EMBD], k[N_EMBD], v[N_EMBD];
            for (int i = 0; i < N_EMBD; i++)
                xe[i] = params[L.wte + tok * N_EMBD + i] + params[L.wpe + pos * N_EMBD + i];
            rmsnorm(xe, xn1, N_EMBD);
            matvec(params + L.layers[0].wq, xn1, q, N_EMBD, N_EMBD);
            matvec(params + L.layers[0].wk, xn1, k_cache + pos * N_EMBD, N_EMBD, N_EMBD);
            matvec(params + L.layers[0].wv, xn1, v_cache + pos * N_EMBD, N_EMBD, N_EMBD);

            float sc = 1.0f / sqrtf((float)HEAD_DIM);
            float xa[N_EMBD] = {0};
            int seq = pos + 1;
            for (int h = 0; h < N_HEAD; h++) {
                int hs = h * HEAD_DIM;
                float aw[BLOCK_SIZE];
                for (int t = 0; t < seq; t++)
                    aw[t] = dot_f32(q + hs, k_cache + t * N_EMBD + hs, HEAD_DIM) * sc;
                softmax_fwd(aw, aw, seq);
                for (int j = 0; j < HEAD_DIM; j++)
                    for (int t = 0; t < seq; t++)
                        xa[hs + j] += aw[t] * v_cache[t * N_EMBD + hs + j];
            }

            float xo_proj[N_EMBD], xpa[N_EMBD], xn2[N_EMBD];
            matvec(params + L.layers[0].wo, xa, xo_proj, N_EMBD, N_EMBD);
            for (int i = 0; i < N_EMBD; i++) xpa[i] = xo_proj[i] + xn1[i];
            rmsnorm(xpa, xn2, N_EMBD);

            float fc1[MLP_DIM], relu_buf[MLP_DIM], mlp_buf[N_EMBD], xout[N_EMBD];
            matvec(params + L.layers[0].fc1, xn2, fc1, MLP_DIM, N_EMBD);
            for (int i = 0; i < MLP_DIM; i++) relu_buf[i] = fc1[i] > 0 ? fc1[i] : 0;
            matvec(params + L.layers[0].fc2, relu_buf, mlp_buf, N_EMBD, MLP_DIM);
            for (int i = 0; i < N_EMBD; i++) xout[i] = mlp_buf[i] + xpa[i];

            float logits[64];
            matvec(params + L.lm, xout, logits, vs, N_EMBD);
            float mx = logits[0];
            for (int i = 1; i < vs; i++) if (logits[i] > mx) mx = logits[i];
            float probs[64], s = 0;
            for (int i = 0; i < vs; i++) { probs[i] = expf((logits[i] - mx) / temp); s += probs[i]; }
            for (int i = 0; i < vs; i++) probs[i] /= s;

            // Sample
            float r = rng_uniform();
            float cdf = 0;
            tok = vs - 1;
            for (int i = 0; i < vs; i++) {
                cdf += probs[i];
                if (r < cdf) { tok = i; break; }
            }
            if (tok == bos) break;
            sample[slen++] = uchars[tok];
        }
        printf("sample %2d: %s\n", si + 1, sample);
    }

    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("\nTotal time: %.2fs\n", elapsed);

    // Cleanup
    for (int i = 0; i < ndocs; i++) free(docs[i]);
    free(docs);
    free(params);
    return 0;
}
