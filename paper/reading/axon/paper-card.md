# Axon: A Synthesizing Superoptimizer for Tensor Programs — 精读卡

> Source coverage: Full paper
> Extraction confidence: High
> Locator mode: page-grounded
> Primary analytical lens: methods
> Secondary analytical lens: None
> Context verification: Targeted external check
> Card completeness: Complete relative to supplied source

## 01 基本信息

- 标题：*Axon: A Synthesizing Superoptimizer for Tensor Programs*
- 作者：Akash Kothari, Shaowei Zhu, Daniel Kroening, Chungha Sung
- 单位：University of Illinois at Urbana-Champaign；Amazon Web Services
- 时间与标识：2026，arXiv:2606.26344；当前 PDF 是预印本，正式 DOI 尚为占位符。
- 类型：编译器/程序综合系统论文；面向 tile-based AI accelerator kernel。
- 输入与输出：NumPy-like tensor program → NKI program → Neuron compiler → Trainium kernel。
- 规模：约 16,500 行 Python 3.10，Z3 4.12.6；20 个 kernel benchmark。
- 在 Joggle 研究中的位置：Axon 是“语义规格驱动的自动优化”强对照，Joggle 当前是“可组合、可检查、可发射的研究控制平面”，两者不处于同一性能成熟度。

术语账本：

| 规范术语 | 本卡含义 | 不混用的近似词 |
| --- | --- | --- |
| νGraph | 同时保存整图等价变体及其逐步累积元数据的表示 | 普通 graph IR、e-graph |
| algebraic transformation | 通过 operator propagation 得到的跨算子等价重排 | 常规局部 fusion |
| symbolic tiling | strip/block/tile 三层符号参数及约束 | 单一 loop tile size |
| ISA synthesis | 从 tensor/ISA semantics 综合目标指令序列 | 固定 lowering template |
| empirical selection | 编译并在目标硬件执行候选后选优 | 静态 cost model |

## 02 一句话总结

[Paper] Axon 以 tensor 与 ISA 的细粒度语义规格为输入，在 νGraph 中联合枚举代数重排、三级 tiling、指令选择及两级融合，并通过 SMT 过滤语义错误候选、在 Trainium 上实测选优，从而在限定的 kernel 级任务中取得相对 Neuron compiler、手写 NKI 与 Mirage 的加速。[Paper: PDF pp. 1–5, Fig. 1]

## 03 研究问题

[Paper] 具体问题不是“能否生成代码”，而是：能否无需预写每个高层 rewrite 和目标 lowering template，仅凭可组合语义规格，自动发现适合 tile accelerator 的等价计算图、目标指令、tiling 与融合方案，并找到真实硬件上的快解？[Paper: PDF pp. 1–4]

[Analysis] 该问题重要，因为高性能来自多个决策的乘积；如果先贪心固定图重写，再固定指令，再单独调 tile，早期决策会封闭后续的跨层机会。

## 04 研究背景与发展路线

[Paper-framed; external verification not performed] 论文将相关路线分为四类：基于规则/等式饱和的 tensor graph 变换；Halide/TVM/Ansor/TensorIR 一类 schedule 搜索；Hydride/MISAAL/Diospyros 一类 synthesis-based instruction selection；NKI/Pallas/Triton/TileLang 一类 tile programming language。Axon 声称其差异是同时处理 2-D tile ISA、代数重排、硬件约束 tiling 与融合。[Paper: PDF pp. 22–23, Related Work]

[External] arXiv 主记录确认标题、作者、日期和“语义规格 → 指令综合 → 等价变体 → 目标实测选优”的核心摘要：https://arxiv.org/abs/2606.26344 。

[Analysis] 对 Joggle 最有价值的不是复制 SMT，而是学习其“优化选择必须保持为显式候选直至目标反馈”的组织原则。

## 05 论文识别的核心痛点

| 痛点 | 表现 | 原因或作者解释 | 论文证据 |
| --- | --- | --- | --- |
| 多维优化互相耦合 | 图重排会改变可并行分支、tiling 和 fusion 机会 | 过早固定一种 lowering 会丢失组合解 | [Paper: PDF pp. 2–4, Figs. 1 and 3] |
| 手写 rewrite 覆盖有限 | RMSNorm+MatMul 与 Softmax+MatMul 的可交换结构需提前预见 | pattern matcher 不从算子语义自动推导未知组合 | [Paper: PDF p. 4; pp. 27–28, Fig. 14] |
| 固定 tiling heuristic 跨 shape 失效 | 大 shape 上 DMA 利用与片上复用差距增大 | block 大小受复用和 SBUF 容量共同约束 | [Paper: PDF pp. 6–12; pp. 18–20, Tables 4–5] |
| 目标 ISA lowering 依赖专家 template | `nc_matmul` 消费转置的首操作数 | 高层语义与具体 ISA 访问语义不一致 | [Paper: PDF pp. 12–14, Fig. 11] |
| 搜索成本很高 | 复杂 kernel 产生约 30K 候选，选优最长约 1.7 小时 | 候选必须经后端编译并在硬件执行 | [Paper: PDF pp. 21–23, Table 7] |

## 06 核心思想

1. [Paper] 表层方法：用 NumPy-like tensor program 与两层 semantics specification 描述问题，再综合 NKI。
2. [Paper] 核心机制：νGraph 不过早提交选择，而是让代数变体、symbolic tiling、ISA choices 和 fusion choices 共同存在，最后实测抽取。[Paper: PDF pp. 6–8, Fig. 6]
3. [Analysis] 可迁移原则：编译器研究平台不必内置某个搜索算法，但必须让同一函数体能产生、命名、约束、编译、测量和缓存多个候选；否则“可扩展 pass”仍只能做一次性贪心改写。

## 07 方法总览

[Paper] 数据流为：

`tensor program + tensor semantics` → operator propagation → `νGraph variants` → symbolic tiling → ISA sketch synthesis under hardware constraints → node/instruction fusion → concrete NKI candidates → Neuron compile and Trainium execution → best kernel。[Paper: PDF p. 2, Fig. 1]

- 训练：不需要模型训练。
- 外部工具：Z3、Neuron compiler、Trainium。
- 反馈环：硬件实测 latency 决定最终 candidate。
- 关键假设：kernel 规模通常小于约 10 个 graph operator；目标是具有 tile、多级存储及专用执行引擎的 accelerator；输入 rank 已知。

## 08 核心模块拆解

| 模块 | 功能 | 必要性 | 输入与输出 | 支撑证据 | 移除后的已测/预期影响 |
| --- | --- | --- | --- | --- | --- |
| νGraph | 保存整图等价变体和各自约束/元数据 | 避免贪心阶段决策 | graph → variant set | [Paper: PDF pp. 6–8] | [Analysis] 退化为固定流水线，无法联合搜索 |
| Graph Mutator | 向下传播 operator，尝试与 successor 交换并按 clone degree 扩展 | 自动发现跨算子代数机会 | graph variants → graph variants | [Paper: PDF pp. 8–10, Algorithm 1] | 无代数变换时四个 benchmark 的 geomean 为完整 Axon 的 0.65–0.85x [Paper: PDF p. 21, Table 6] |
| Tensor semantics | rank/shape precondition、access map、granular semantics | 为交换和 ISA 综合提供可推理定义 | operator definition → solver formula | [Paper: PDF p. 10, Fig. 9] | [Analysis] 需回退到手写规则或模板 |
| Tiling pass | 为 2-D op 引入 strip/block/tile 参数 | 表达指令粒度、片上复用和 HBM 分块 | variant → symbolic tiled variant | [Paper: PDF pp. 10–12] | 论文未做 no-tiling ablation，因为大 tensor 无法直接执行 [Paper: PDF p. 21] |
| ISA synthesizer | sketch-driven 组合目标指令并验证 | 处理高层/ISA 访问语义差异 | tiled op + ISA semantics → ISA variants | [Paper: PDF pp. 12–14] | [Analysis] 退化为每算子 target template |
| Fusion mutator | node fusion 保留片上中间值；instruction fusion 合并搬运/布局操作 | 降低 HBM 往返并利用新增机会 | ISA graph → fused variants | [Paper: PDF pp. 14–15] | no-fusion geomean 为完整 Axon 的 0.50–0.65x [Paper: PDF p. 21, Table 6] |
| Emitter + selector | 实例化参数、编译候选、目标执行、选优 | 静态 heuristic 无法稳定预测目标性能 | variants → best NKI artifact | [Paper: PDF pp. 15 and 21–23] | [Analysis] 性能结论失去目标反馈依据 |

## 09 必要公式与符号

- [Paper] RMSNorm：`RMSNorm(X) = X ⊙ 1 / sqrt((1/N) Σ_i X_i² + ε)`，用于说明 element-wise scale 与沿相同 contraction dimension 的 MatMul 可交换。[Paper: PDF p. 3, Eq. 1]
- [Paper] tiling 元数据写作 `[n0,n1,b0,b1] op [t0,t1]`：`t` 是单条 engine instruction 的 tile，`b` 是片上同时驻留的 tile 组，`n` 是 HBM 中的 block 数。[Paper: PDF pp. 10–11]
- [Paper] 语义等价 `G1 ≡ G2` 要求所有满足 operator preconditions 的输入 tensor 上输出相同。[Paper: PDF p. 8, Definition 4.2]

## 10 实验设计与证据链

[Paper] 平台为一台 `trn1.32xlarge` 的单个 NeuronCore；每个 shape 使用 20 次 warm-up、100 次测量并报告平均时间。基线为 Neuron compiler 2.21.18209.0、固定提交的 hand-optimized NKI，以及作者获取的 Mirage NKI extension。所有 NKI 最终都经过相同 Neuron compiler。[Paper: PDF pp. 17–18]

| 实验 | 检验主张 | 比较与条件 | 结果 | 支持的结论 | 不支持的更强结论 | 来源 |
| --- | --- | --- | --- | --- | --- | --- |
| 单算子 25-shape sweep | tiling/ISA selection 跨 shape 的收益 | Axon/Neuron；Cumsum、RoPE 对手写 NKI | Neuron 对比 geomean 1.23–1.90x，最大 3.7x；对手写 NKI geomean 1.06–1.07x | 搜索可在该硬件/shape 集合改善单 kernel | 不代表端到端模型推理 | [Paper: PDF p. 19, Table 4] |
| MatMul 与多算子 27-shape sweep | 联合重排、tiling、fusion 的收益 | Axon/Neuron；Mirage 只在支持项比较 | 对 Neuron 多数组合 geomean 1.08–1.32x，Transpose+MM 最大 19x；对 Mirage 三组 geomean 1.06–1.10x | 联合搜索在支持 kernel 上优于固定 heuristic/template | 不代表普遍优于所有 production compilers | [Paper: PDF pp. 19–20, Table 5 and Fig. 13] |
| ablation | algebraic transformation 与 fusion 是否贡献性能 | 完整 Axon vs 分别关闭组件，27 shapes | no-algebraic 0.65–0.85x；no-fusion 0.50–0.65x | 两组件互补，fusion 影响更大 | 未隔离 ISA synthesis 或 tiling 的独立贡献 | [Paper: PDF p. 21, Table 6] |
| 编译开销 | 搜索是否可承受 | 统计 phase 1、候选数、编译/实测 phase 2 | 约 5 分钟至 1.7 小时；60–76% 候选可编译；phase 2 占 >97% | 可 AOT 并缓存的 kernel 搜索可行 | 不支持低延迟 JIT | [Paper: PDF pp. 21–23, Table 7] |

## 11 结论的正确解释

[Paper] 论文证据支持：在第一代 Trainium、单 NeuronCore、20 个选定 kernel 和给定 shape grid 上，语义规格驱动的联合搜索可产生快于所列基线的候选。[Paper: PDF pp. 17–23]

[Analysis] 不能推出：Axon 端到端推理更快、对 CPU/GPU 直接有效、支持卷积/稀疏/训练、SMT 与浮点语义完全一致，或其 1.7 小时搜索适合在线 JIT。最强的、可复现边界内的表述应是“kernel-level ahead-of-time superoptimization on one tile accelerator family”。

## 12 作者明确承认的局限

| 局限 | 具体表现 | 作者提出的方向 | 来源 |
| --- | --- | --- | --- |
| 非浮点精确证明 | SMT 用 real arithmetic；非线性函数作 uninterpreted | 随机浮点输入作数值验证；完整 FP theory 当前过慢 | [Paper: PDF p. 17, Sec. 5.2] |
| 非端到端评估 | 未计 kernel launch、collective、framework scheduling | 论文将其界定为与 kernel optimization 正交 | [Paper: PDF p. 27, Appendix A] |
| 平台泛化受限 | 当前仅第一代 Trainium；GPU/many-core execution model 不直接适配 | 新 tile target 需 ISA semantics、constraints、emitter | [Paper: PDF pp. 5–6 and p. 27] |
| benchmark 不完整 | 无 convolution、sparse、backward | 未给出具体实现计划 | [Paper: PDF p. 27] |
| 搜索空间与资源模型不完备 | 一部分候选因累计 live tile 超出 SBUF 被后端拒绝 | 当前依靠 Neuron compiler 过滤 | [Paper: PDF pp. 21–23] |

## 13 批判性分析

| `[Analysis]` 观察 | 潜在问题或替代解释 | 为什么重要 | 如何检验 | 依据 |
| --- | --- | --- | --- | --- |
| 最大 19x 是局部极值 | 可能主要暴露 Neuron 固定 tiling 在特定大 shape 的病态点 | 标题级数字会掩盖大多数 1.0–1.3x 单元格 | 报告每组分布、bootstrap CI 与稳健 geomean | [Paper: PDF pp. 19–20, Table 5] |
| 使用平均数而非中位数/分布 | 硬件 timing 尾部可能影响均值 | 表中只有比率，看不到方差和漂移 | 公开 raw trials、median/MAD/CI 与运行顺序 | [Paper: PDF p. 17] |
| 候选公平预算不对称 | Neuron 默认 heuristic 几乎无搜索，而 Axon 花最多 1.7 小时实测 | 说明 AOT search 的价值，但不是等编译预算比较 | 加入同等 tuning-budget curve | [Paper: PDF pp. 21–23] |
| 资源约束委托给后端拒绝 | 24–40% 候选编译失败 | 扩大到复杂 kernel/新 target 时浪费会放大 | 加 live-range/SBUF estimator 并做 pruning ablation | [Paper: PDF pp. 21–23, Table 7] |
| end-to-end 收益未知 | kernel 加速可能被 dispatch、layout conversion 或 fallback 稀释 | Joggle 面向模型编译，不能照搬 kernel 结论 | 在真实 transformer block/模型上测端到端 | [Paper: PDF p. 27] |

## 14 学到的知识

### Agent-derived knowledge candidates

- 性能表之所以需要 25/27 个 shape，不是为了视觉密度，而是证明固定 heuristic 的 failure region 与搜索收益随维度改变。
- 多算子收益的关键不是“少几个 loop”，而是 intermediate residency、数据搬运融合和异构 engine overlap。
- 一个研究编译器若只允许 pass 原地覆盖 IR，就难以复现实测选优；候选身份、约束、provenance、artifact 与 measurement 应成为可组合对象。
- 对边缘 CPU，Axon 的 Trainium 指令综合不能直接迁移，但“shape-parametric candidate + legality/capability pruning + target feedback + cache”可以迁移。

## 15 与现有知识的连接

[Analysis] 与 Joggle 的连接可分三层：

1. Joggle 已有普通 `Fn`/`Blk`/`Op`/`Val`、函数体 exposure、transactional edit 和 module function，可承载候选生成；Axon 说明还缺候选集合及选优协议。
2. Joggle 的当前 `tile.reorder` 与 `tile.fuse` 是单次 legality transform；Axon 说明 legality 与 profitability 必须分离，且 fusion 需围绕 memory residency 而不是 loop count。
3. Joggle 的 C emitter 仅证明 artifact reach。要逼近 ORT/TVM，必须补齐 canonical affine loops、cache tiling、weight packing、SIMD/vector code、alignment/alias contracts、候选实测与 artifact cache。

## 16 研究想法

### Agent-derived research candidates

**候选 1：模块化候选空间，而非内置 superoptimizer。**

- 来源：νGraph 的联合候选与 Joggle 当前单次原地 pass 的落差。[Paper: PDF pp. 6–8]
- 假设：让普通 module function 返回带 provenance/constraints 的候选集，并由统一 evaluator 编译、测量、缓存，可在不把搜索固化进 core 的情况下获得接近专用 tuner 的收益。
- 变化：新增候选生命周期与 measurement protocol，不新增 tensor/graph IR 类别。
- Validation：在 Add/ReLU/LayerNorm/Softmax/MatMul 与 fused pairs 上，对比 greedy、random、bounded search；固定候选预算并报告 best-so-far curve。
- Failure modes：IR clone 成本过高；模块生成的候选缺少共享规范导致组合爆炸；测量噪声误选。
- 创新状态：unverified；需与 Ansor、MetaSchedule、OpenTuner、Mirage/Axon 做专门 prior-art search。

**候选 2：边缘 CPU 的 capability-constrained schedule space。**

- 来源：Axon 用 hardware constraints 限制 tile/ISA，但其 execution model 不适配 CPU。[Paper: PDF pp. 5–6 and 12–15]
- 假设：将 cache size、SIMD width、alignment、legal vector type、thread budget 作为 target module 的可查询函数，而不是 core machine class，可让同一 scheduling module 在 x86/Arm 上生成有效候选并保持 Joggle 轻量。
- 变化：target 提供 capability functions；policy 组合 interchange、tile、pack、vectorize；C/LLVM-like emitter 只消费显式决定。
- Validation：25 个 M×N 与 27 个 M×K×N shape；同机单线程比较 ORT、TVM 和 Joggle，报告 speedup matrix、compile budget、code size、correctness。
- Failure modes：C 编译器不稳定实现 SIMD；capability 抽象不足以预测 cache conflict；小 shape 调优成本无法摊销。
- 创新状态：unverified。

**候选 3：从负优化中学习的 profitability cache。**

- 来源：Axon 依靠目标实测，而 Joggle 当前 fusion 在 MobileNetV2 上使 latency 变差。
- 假设：以 canonical body hash、shape、target fingerprint、policy parameters 为 key 的持久 cache 能让失败候选成为跨模型可复用的负证据，减少重复 tuning。
- 变化：缓存的不只是最优 artifact，还包括失败原因和置信区间。
- Validation：跨多个模型复用相同算子/shape 家族，测首次搜索成本、复用命中率、错误迁移率与端到端 latency。
- Failure modes：上下文（alignment、fusion 邻域、cache pressure）使 body hash 不足；硬件状态噪声污染 cache。
- 创新状态：unverified；需与 TVM MetaSchedule database 和 auto-tuning cache 机制比较。
