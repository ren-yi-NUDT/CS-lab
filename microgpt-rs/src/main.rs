// Rust port of Karpathy's microgpt — optimized arena autograd
// Key optimizations: fused dot/sum ops reduce graph nodes ~10x, SoA layout, unsafe backward

use rand::prelude::*;
use rand_distr::Normal;
use std::io::Write;
use std::time::Instant;

const N_LAYER: usize = 1;
const N_EMBD: usize = 16;
const BLOCK_SIZE: usize = 16;
const N_HEAD: usize = 4;
const HEAD_DIM: usize = N_EMBD / N_HEAD;

// --- Fused operations (local grads recomputed from data during backward) ---
enum Op {
    Leaf,
    Add(usize, usize),
    Mul(usize, usize),
    Pow(usize, f64),
    Log(usize),
    Exp(usize),
    Relu(usize),
    Smul(usize, f64),
    Dot(usize, usize), // (start, count) in dot_pairs
    Sum(usize, usize), // (start, count) in sum_idx
}

struct Arena {
    data: Vec<f64>,
    grad: Vec<f64>,
    ops: Vec<Op>,
    dot_pairs: Vec<(usize, usize)>,
    sum_idx: Vec<usize>,
}

impl Arena {
    fn with_capacity(n: usize) -> Self {
        Self {
            data: Vec::with_capacity(n),
            grad: Vec::with_capacity(n),
            ops: Vec::with_capacity(n),
            dot_pairs: Vec::with_capacity(n * 4),
            sum_idx: Vec::with_capacity(n),
        }
    }

    #[inline] fn leaf(&mut self, d: f64) -> usize {
        let i = self.data.len();
        self.data.push(d); self.grad.push(0.0); self.ops.push(Op::Leaf); i
    }
    #[inline] fn add(&mut self, a: usize, b: usize) -> usize {
        let i = self.data.len();
        self.data.push(self.data[a] + self.data[b]);
        self.grad.push(0.0); self.ops.push(Op::Add(a, b)); i
    }
    #[inline] fn mul(&mut self, a: usize, b: usize) -> usize {
        let i = self.data.len();
        self.data.push(self.data[a] * self.data[b]);
        self.grad.push(0.0); self.ops.push(Op::Mul(a, b)); i
    }
    #[inline] fn pow(&mut self, a: usize, e: f64) -> usize {
        let i = self.data.len();
        self.data.push(self.data[a].powf(e));
        self.grad.push(0.0); self.ops.push(Op::Pow(a, e)); i
    }
    #[inline] fn log(&mut self, a: usize) -> usize {
        let i = self.data.len();
        self.data.push(self.data[a].ln());
        self.grad.push(0.0); self.ops.push(Op::Log(a)); i
    }
    #[inline] fn exp(&mut self, a: usize) -> usize {
        let i = self.data.len();
        let e = self.data[a].exp();
        self.data.push(e); self.grad.push(0.0); self.ops.push(Op::Exp(a)); i
    }
    #[inline] fn relu(&mut self, a: usize) -> usize {
        let d = self.data[a];
        let i = self.data.len();
        self.data.push(if d > 0.0 { d } else { 0.0 });
        self.grad.push(0.0); self.ops.push(Op::Relu(a)); i
    }
    #[inline] fn scalar_mul(&mut self, a: usize, s: f64) -> usize {
        let i = self.data.len();
        self.data.push(self.data[a] * s);
        self.grad.push(0.0); self.ops.push(Op::Smul(a, s)); i
    }

    /// Fused dot product: one node instead of 2n-1
    fn dot(&mut self, ai: &[usize], bi: &[usize]) -> usize {
        let start = self.dot_pairs.len();
        let mut val = 0.0;
        for k in 0..ai.len() {
            val += self.data[ai[k]] * self.data[bi[k]];
            self.dot_pairs.push((ai[k], bi[k]));
        }
        let i = self.data.len();
        self.data.push(val); self.grad.push(0.0);
        self.ops.push(Op::Dot(start, ai.len())); i
    }

    /// Fused sum: one node instead of n-1 adds
    fn sum(&mut self, idx: &[usize]) -> usize {
        let start = self.sum_idx.len();
        let mut val = 0.0;
        for &j in idx { val += self.data[j]; self.sum_idx.push(j); }
        let i = self.data.len();
        self.data.push(val); self.grad.push(0.0);
        self.ops.push(Op::Sum(start, idx.len())); i
    }

    fn backward(&mut self) {
        let n = self.data.len();
        if n == 0 { return; }
        self.grad[n - 1] = 1.0;
        for i in (0..n).rev() {
            let g = self.grad[i];
            if g == 0.0 { continue; }
            unsafe {
                match *self.ops.get_unchecked(i) {
                    Op::Leaf => {},
                    Op::Add(a, b) => {
                        *self.grad.get_unchecked_mut(a) += g;
                        *self.grad.get_unchecked_mut(b) += g;
                    }
                    Op::Mul(a, b) => {
                        *self.grad.get_unchecked_mut(a) += *self.data.get_unchecked(b) * g;
                        *self.grad.get_unchecked_mut(b) += *self.data.get_unchecked(a) * g;
                    }
                    Op::Pow(a, e) => {
                        *self.grad.get_unchecked_mut(a) += e * self.data.get_unchecked(a).powf(e - 1.0) * g;
                    }
                    Op::Log(a) => {
                        *self.grad.get_unchecked_mut(a) += g / *self.data.get_unchecked(a);
                    }
                    Op::Exp(a) => {
                        *self.grad.get_unchecked_mut(a) += *self.data.get_unchecked(i) * g;
                    }
                    Op::Relu(a) => {
                        if *self.data.get_unchecked(a) > 0.0 {
                            *self.grad.get_unchecked_mut(a) += g;
                        }
                    }
                    Op::Smul(a, s) => {
                        *self.grad.get_unchecked_mut(a) += s * g;
                    }
                    Op::Dot(start, count) => {
                        for j in start..start + count {
                            let (ai, bi) = *self.dot_pairs.get_unchecked(j);
                            *self.grad.get_unchecked_mut(ai) += *self.data.get_unchecked(bi) * g;
                            *self.grad.get_unchecked_mut(bi) += *self.data.get_unchecked(ai) * g;
                        }
                    }
                    Op::Sum(start, count) => {
                        for j in start..start + count {
                            *self.grad.get_unchecked_mut(*self.sum_idx.get_unchecked(j)) += g;
                        }
                    }
                }
            }
        }
    }

    fn reset(&mut self, keep: usize) {
        self.data.truncate(keep);
        self.grad.truncate(keep);
        self.ops.truncate(keep);
        for g in &mut self.grad { *g = 0.0; }
        self.dot_pairs.clear();
        self.sum_idx.clear();
    }
}

// --- Model functions ---

fn linear(x: &[usize], w: &[Vec<usize>], a: &mut Arena) -> Vec<usize> {
    w.iter().map(|wo| a.dot(wo, x)).collect()
}

fn softmax(logits: &[usize], a: &mut Arena) -> Vec<usize> {
    let max_val = logits.iter().map(|&l| unsafe { *a.data.get_unchecked(l) }).fold(f64::NEG_INFINITY, f64::max);
    let mx = a.leaf(max_val);
    let neg_max = a.scalar_mul(mx, -1.0);
    let exps: Vec<usize> = logits.iter().map(|&l| { let s = a.add(l, neg_max); a.exp(s) }).collect();
    let total = a.sum(&exps);
    let inv = a.pow(total, -1.0);
    exps.iter().map(|&e| a.mul(e, inv)).collect()
}

fn rmsnorm(x: &[usize], a: &mut Arena) -> Vec<usize> {
    let sq: Vec<usize> = x.iter().map(|&xi| a.mul(xi, xi)).collect();
    let ss = a.sum(&sq);
    let ms = a.scalar_mul(ss, 1.0 / x.len() as f64);
    let eps = a.leaf(1e-5);
    let ms_eps = a.add(ms, eps);
    let scale = a.pow(ms_eps, -0.5);
    x.iter().map(|&xi| a.mul(xi, scale)).collect()
}

fn gpt(
    token_id: usize, pos_id: usize, pidx: &ParamIdx,
    kv_k: &mut Vec<Vec<Vec<usize>>>, kv_v: &mut Vec<Vec<Vec<usize>>>,
    a: &mut Arena,
) -> Vec<usize> {
    let mut x: Vec<usize> = pidx.wte[token_id].iter().zip(pidx.wpe[pos_id].iter())
        .map(|(&t, &p)| a.add(t, p)).collect();
    x = rmsnorm(&x, a);

    for li in 0..N_LAYER {
        let layer = &pidx.layers[li];
        let x_res = x.clone();
        x = rmsnorm(&x, a);

        let q = linear(&x, &layer.attn_wq, a);
        let k = linear(&x, &layer.attn_wk, a);
        let v = linear(&x, &layer.attn_wv, a);
        kv_k[li].push(k);
        kv_v[li].push(v);

        let scale = a.leaf(1.0 / (HEAD_DIM as f64).sqrt());
        let mut x_attn = Vec::with_capacity(N_EMBD);
        for h in 0..N_HEAD {
            let hs = h * HEAD_DIM;
            let q_h = &q[hs..hs + HEAD_DIM];
            let k_h: Vec<&[usize]> = kv_k[li].iter().map(|ki| &ki[hs..hs + HEAD_DIM]).collect();
            let v_h: Vec<&[usize]> = kv_v[li].iter().map(|vi| &vi[hs..hs + HEAD_DIM]).collect();

            let attn_logits: Vec<usize> = k_h.iter().map(|ki| { let d = a.dot(q_h, ki); a.mul(d, scale) }).collect();
            let w = softmax(&attn_logits, a);

            for j in 0..HEAD_DIM {
                let wv: Vec<usize> = w.iter().zip(v_h.iter()).map(|(&wi, vi)| a.mul(wi, vi[j])).collect();
                x_attn.push(a.sum(&wv));
            }
        }

        x = linear(&x_attn, &layer.attn_wo, a);
        x = x.iter().zip(x_res.iter()).map(|(&ai, &b)| a.add(ai, b)).collect();

        let x_res = x.clone();
        x = rmsnorm(&x, a);
        x = linear(&x, &layer.mlp_fc1, a);
        x = x.iter().map(|&xi| a.relu(xi)).collect();
        x = linear(&x, &layer.mlp_fc2, a);
        x = x.iter().zip(x_res.iter()).map(|(&ai, &b)| a.add(ai, b)).collect();
    }
    linear(&x, &pidx.lm_head, a)
}

fn neglog(a: &mut Arena, x: usize) -> usize { let l = a.log(x); a.scalar_mul(l, -1.0) }

fn mean(vals: &[usize], a: &mut Arena) -> usize {
    let s = a.sum(vals);
    a.scalar_mul(s, 1.0 / vals.len() as f64)
}

fn tokenize(doc: &str, bos: usize, c2i: &std::collections::HashMap<char, usize>) -> Vec<usize> {
    let mut t = vec![bos];
    t.extend(doc.chars().map(|c| c2i[&c]));
    t.push(bos);
    t
}

// --- Parameter layout ---

struct LayerIdx {
    attn_wq: Vec<Vec<usize>>,
    attn_wk: Vec<Vec<usize>>,
    attn_wv: Vec<Vec<usize>>,
    attn_wo: Vec<Vec<usize>>,
    mlp_fc1: Vec<Vec<usize>>,
    mlp_fc2: Vec<Vec<usize>>,
}

struct ParamIdx {
    wte: Vec<Vec<usize>>,
    wpe: Vec<Vec<usize>>,
    lm_head: Vec<Vec<usize>>,
    layers: Vec<LayerIdx>,
}

fn make_param_idx(vocab_size: usize) -> (ParamIdx, usize) {
    let mut next = 0usize;
    let mut mat = |nout: usize, nin: usize| -> Vec<Vec<usize>> {
        (0..nout).map(|_| (0..nin).map(|_| { let i = next; next += 1; i }).collect()).collect()
    };
    let pidx = ParamIdx {
        wte: mat(vocab_size, N_EMBD),
        wpe: mat(BLOCK_SIZE, N_EMBD),
        lm_head: mat(vocab_size, N_EMBD),
        layers: (0..N_LAYER).map(|_| LayerIdx {
            attn_wq: mat(N_EMBD, N_EMBD),
            attn_wk: mat(N_EMBD, N_EMBD),
            attn_wv: mat(N_EMBD, N_EMBD),
            attn_wo: mat(N_EMBD, N_EMBD),
            mlp_fc1: mat(4 * N_EMBD, N_EMBD),
            mlp_fc2: mat(N_EMBD, 4 * N_EMBD),
        }).collect(),
    };
    (pidx, next)
}

fn init_param_data(n: usize, rng: &mut StdRng) -> Vec<f64> {
    let normal = Normal::new(0.0, 0.08).unwrap();
    (0..n).map(|_| rng.sample(normal)).collect()
}

fn main() {
    let t0 = Instant::now();

    let candidates = ["input.txt", "microgpt/input.txt", "../microgpt/input.txt"];
    let input_path = candidates.iter().find(|p| std::path::Path::new(p).exists())
        .unwrap_or_else(|| panic!("input.txt not found, searched: {:?}", candidates));
    let mut docs: Vec<String> = std::fs::read_to_string(input_path).expect("read")
        .lines().map(|l| l.trim().to_string()).filter(|l| !l.is_empty()).collect();
    let mut rng = StdRng::seed_from_u64(42);
    docs.shuffle(&mut rng);
    println!("num docs: {}", docs.len());

    let mut uchars: Vec<char> = docs.iter().flat_map(|d| d.chars())
        .collect::<std::collections::HashSet<_>>().into_iter().collect();
    uchars.sort();
    let c2i: std::collections::HashMap<char, usize> = uchars.iter().enumerate().map(|(i, &c)| (c, i)).collect();
    let bos = uchars.len();
    let vocab_size = uchars.len() + 1;
    println!("vocab size: {}", vocab_size);

    let (pidx, n_params) = make_param_idx(vocab_size);
    let mut param_data = init_param_data(n_params, &mut rng);
    println!("num params: {}", n_params);

    let mut m = vec![0.0f64; n_params];
    let mut v = vec![0.0f64; n_params];
    let lr0 = 0.01f64;
    let beta1 = 0.85f64;
    let beta2 = 0.99f64;
    let eps_adam = 1e-8f64;
    let num_steps = 1000usize;

    // Training (arena reused via reset — fewer nodes thanks to fused ops)
    let mut arena = Arena::with_capacity(30_000);
    for &d in &param_data { arena.leaf(d); }

    for step in 0..num_steps {
        arena.reset(n_params);

        let doc = &docs[step % docs.len()];
        let tokens = tokenize(doc, bos, &c2i);
        let seqlen = std::cmp::min(BLOCK_SIZE, tokens.len() - 1);
        let mut kv_k: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut kv_v: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut losses = Vec::with_capacity(seqlen);

        for pos in 0..seqlen {
            let logits = gpt(tokens[pos], pos, &pidx, &mut kv_k, &mut kv_v, &mut arena);
            let probs = softmax(&logits, &mut arena);
            losses.push(neglog(&mut arena, probs[tokens[pos + 1]]));
        }
        let loss = mean(&losses, &mut arena);
        arena.backward();

        let lr = lr0 * (1.0 - step as f64 / num_steps as f64);
        let bc1 = 1.0 - beta1.powi(step as i32 + 1);
        let bc2 = 1.0 - beta2.powi(step as i32 + 1);
        for i in 0..n_params {
            let g = arena.grad[i];
            m[i] = beta1 * m[i] + (1.0 - beta1) * g;
            v[i] = beta2 * v[i] + (1.0 - beta2) * g * g;
            arena.data[i] -= lr * (m[i] / bc1) / ((v[i] / bc2).sqrt() + eps_adam);
        }

        print!("\rstep {:4} / {} | loss {:.4}", step + 1, num_steps, arena.data[loss]);
        let _ = std::io::stdout().flush();
    }

    for i in 0..n_params { param_data[i] = arena.data[i]; }

    // Inference
    println!("\n--- inference (new, hallucinated names) ---");
    let temp = 0.5f64;

    for si in 0..20 {
        arena.reset(n_params);
        let mut kv_k: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut kv_v: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut tok = bos;
        let mut sample = Vec::new();

        for pos in 0..BLOCK_SIZE {
            let logits = gpt(tok, pos, &pidx, &mut kv_k, &mut kv_v, &mut arena);
            let max_l = logits.iter().map(|&l| arena.data[l]).fold(f64::NEG_INFINITY, f64::max);
            let mut probs: Vec<f64> = logits.iter()
                .map(|&l| ((arena.data[l] - max_l) / temp).exp()).collect();
            let s: f64 = probs.iter().sum();
            for p in &mut probs { *p /= s; }
            tok = rng.sample(rand::distr::weighted::WeightedIndex::new(&probs).unwrap());
            if tok == bos { break; }
            sample.push(uchars[tok]);
        }
        println!("sample {:2}: {}", si + 1, sample.iter().collect::<String>());
    }

    println!("\nTotal time: {:.2}s", t0.elapsed().as_secs_f64());
}
