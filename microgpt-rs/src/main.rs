// Rust port of Karpathy's microgpt.py — arena-based autograd
// Single-threaded optimized version with fused dot product

use rand::prelude::*;
use rand_distr::Normal;
use std::io::Write;
use std::time::Instant;

// --- Hyperparameters ---
const N_LAYER: usize = 1;
const N_EMBD: usize = 16;
const BLOCK_SIZE: usize = 16;
const N_HEAD: usize = 4;
const HEAD_DIM: usize = N_EMBD / N_HEAD;
const NONE: usize = usize::MAX;

// --- Arena (fixed-size children, no heap alloc per node) ---

#[derive(Clone)]
struct Node {
    data: f64,
    grad: f64,
    child0: usize,
    child1: usize,
    lg0: f64,
    lg1: f64,
}

struct Arena { nodes: Vec<Node> }

impl Arena {
    fn with_capacity(n: usize) -> Self { Self { nodes: Vec::with_capacity(n) } }

    fn leaf(&mut self, data: f64) -> usize {
        let i = self.nodes.len();
        self.nodes.push(Node { data, grad: 0.0, child0: NONE, child1: NONE, lg0: 0.0, lg1: 0.0 });
        i
    }
    fn add(&mut self, a: usize, b: usize) -> usize {
        let i = self.nodes.len();
        self.nodes.push(Node { data: self.nodes[a].data + self.nodes[b].data, grad: 0.0,
            child0: a, child1: b, lg0: 1.0, lg1: 1.0 });
        i
    }
    fn mul(&mut self, a: usize, b: usize) -> usize {
        let (da, db) = (self.nodes[a].data, self.nodes[b].data);
        let i = self.nodes.len();
        self.nodes.push(Node { data: da * db, grad: 0.0, child0: a, child1: b, lg0: db, lg1: da });
        i
    }
    fn pow(&mut self, a: usize, exp: f64) -> usize {
        let da = self.nodes[a].data;
        let i = self.nodes.len();
        self.nodes.push(Node { data: da.powf(exp), grad: 0.0, child0: a, child1: NONE,
            lg0: exp * da.powf(exp - 1.0), lg1: 0.0 });
        i
    }
    fn log(&mut self, a: usize) -> usize {
        let da = self.nodes[a].data;
        let i = self.nodes.len();
        self.nodes.push(Node { data: da.ln(), grad: 0.0, child0: a, child1: NONE, lg0: 1.0 / da, lg1: 0.0 });
        i
    }
    fn exp(&mut self, a: usize) -> usize {
        let e = self.nodes[a].data.exp();
        let i = self.nodes.len();
        self.nodes.push(Node { data: e, grad: 0.0, child0: a, child1: NONE, lg0: e, lg1: 0.0 });
        i
    }
    fn relu(&mut self, a: usize) -> usize {
        let da = self.nodes[a].data;
        let i = self.nodes.len();
        self.nodes.push(Node { data: if da > 0.0 { da } else { 0.0 }, grad: 0.0,
            child0: a, child1: NONE, lg0: if da > 0.0 { 1.0 } else { 0.0 }, lg1: 0.0 });
        i
    }
    fn scalar_mul(&mut self, a: usize, s: f64) -> usize {
        let i = self.nodes.len();
        self.nodes.push(Node { data: self.nodes[a].data * s, grad: 0.0, child0: a, child1: NONE, lg0: s, lg1: 0.0 });
        i
    }

    fn backward(&mut self) {
        let n = self.nodes.len();
        if n == 0 { return; }
        self.nodes[n - 1].grad = 1.0;
        for i in (0..n).rev() {
            let g = self.nodes[i].grad;
            let (c0, c1, l0, l1) = (self.nodes[i].child0, self.nodes[i].child1,
                                     self.nodes[i].lg0, self.nodes[i].lg1);
            if c0 != NONE { self.nodes[c0].grad += l0 * g; }
            if c1 != NONE { self.nodes[c1].grad += l1 * g; }
        }
    }
}

// --- Dot product ---
fn dot(a_idx: &[usize], b_idx: &[usize], a: &mut Arena) -> usize {
    let mut acc = a.mul(a_idx[0], b_idx[0]);
    for i in 1..a_idx.len() {
        let p = a.mul(a_idx[i], b_idx[i]);
        acc = a.add(acc, p);
    }
    acc
}

// --- Parameter Layout ---

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

// --- Model ---

fn linear(x: &[usize], w: &[Vec<usize>], a: &mut Arena) -> Vec<usize> {
    w.iter().map(|wo| dot(wo, x, a)).collect()
}

fn softmax(logits: &[usize], a: &mut Arena) -> Vec<usize> {
    let max_val = logits.iter().map(|&l| a.nodes[l].data).fold(f64::NEG_INFINITY, f64::max);
    let max_n = a.leaf(max_val);
    let neg_max = a.scalar_mul(max_n, -1.0);
    let mut exps = Vec::with_capacity(logits.len());
    for &l in logits {
        let shifted = a.add(l, neg_max);
        exps.push(a.exp(shifted));
    }
    let mut total = exps[0];
    for i in 1..exps.len() { total = a.add(total, exps[i]); }
    let inv = a.pow(total, -1.0);
    exps.iter().map(|&e| a.mul(e, inv)).collect()
}

fn rmsnorm(x: &[usize], a: &mut Arena) -> Vec<usize> {
    let n = x.len();
    let mut sum_sq = a.mul(x[0], x[0]);
    for i in 1..n {
        let sq = a.mul(x[i], x[i]);
        sum_sq = a.add(sum_sq, sq);
    }
    let ms = a.scalar_mul(sum_sq, 1.0 / n as f64);
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
        kv_k[li].push(k.clone());
        kv_v[li].push(v.clone());

        let scale_val = 1.0 / (HEAD_DIM as f64).sqrt();
        let mut x_attn = Vec::with_capacity(N_EMBD);
        for h in 0..N_HEAD {
            let hs = h * HEAD_DIM;
            let q_h = &q[hs..hs + HEAD_DIM];
            let k_h: Vec<&[usize]> = kv_k[li].iter().map(|ki| &ki[hs..hs + HEAD_DIM]).collect();
            let v_h: Vec<&[usize]> = kv_v[li].iter().map(|vi| &vi[hs..hs + HEAD_DIM]).collect();

            let scale = a.leaf(scale_val);
            let mut attn_logits = Vec::with_capacity(k_h.len());
            for ki in &k_h {
                let d = dot(q_h, ki, a);
                attn_logits.push(a.mul(d, scale));
            }
            let w = softmax(&attn_logits, a);

            for j in 0..HEAD_DIM {
                let mut out = a.mul(w[0], v_h[0][j]);
                for t in 1..w.len() {
                    let prod = a.mul(w[t], v_h[t][j]);
                    out = a.add(out, prod);
                }
                x_attn.push(out);
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
    let mut s = vals[0];
    for i in 1..vals.len() { s = a.add(s, vals[i]); }
    a.scalar_mul(s, 1.0 / vals.len() as f64)
}

fn tokenize(doc: &str, bos: usize, c2i: &std::collections::HashMap<char, usize>) -> Vec<usize> {
    let mut t = vec![bos];
    t.extend(doc.chars().map(|c| c2i[&c]));
    t.push(bos);
    t
}

// --- Main ---

fn main() {
    let t0 = Instant::now();

    let input_path = if std::path::Path::new("input.txt").exists() { "input.txt" }
        else if std::path::Path::new("microgpt/input.txt").exists() { "microgpt/input.txt" }
        else { panic!("input.txt not found") };
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

    // --- Training (single-threaded, arena reused via truncation) ---
    let mut arena = Arena::with_capacity(80_000);
    // Initialize arena with parameter leaf nodes
    for &d in &param_data { arena.leaf(d); }
    for step in 0..num_steps {
        arena.nodes.truncate(n_params);
        for n in &mut arena.nodes { n.grad = 0.0; }

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
            let g = arena.nodes[i].grad;
            m[i] = beta1 * m[i] + (1.0 - beta1) * g;
            v[i] = beta2 * v[i] + (1.0 - beta2) * g * g;
            arena.nodes[i].data -= lr * (m[i] / bc1) / ((v[i] / bc2).sqrt() + eps_adam);
        }

        print!("\rstep {:4} / {} | loss {:.4}", step + 1, num_steps, arena.nodes[loss].data);
        let _ = std::io::stdout().flush();
    }

    // Copy final param data
    for i in 0..n_params { param_data[i] = arena.nodes[i].data; }

    // --- Inference ---
    println!("\n--- inference (new, hallucinated names) ---");
    let temp = 0.5f64;

    for si in 0..20 {
        arena.nodes.truncate(n_params);
        for n in &mut arena.nodes { n.grad = 0.0; }
        let mut kv_k: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut kv_v: Vec<Vec<Vec<usize>>> = vec![vec![]; N_LAYER];
        let mut tok = bos;
        let mut sample = Vec::new();

        for pos in 0..BLOCK_SIZE {
            let logits = gpt(tok, pos, &pidx, &mut kv_k, &mut kv_v, &mut arena);
            let max_l = logits.iter().map(|&l| arena.nodes[l].data).fold(f64::NEG_INFINITY, f64::max);
            let mut probs: Vec<f64> = logits.iter()
                .map(|&l| ((arena.nodes[l].data - max_l) / temp).exp()).collect();
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
