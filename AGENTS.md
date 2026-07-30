# AGENTS

本文件定义 `orion` 项目的项目级代理协作约定。进入本仓库工作时，默认先读取并遵守这里的规则。

## 默认约定

- 默认使用中文回答，除非用户明确要求其他语言。
- 文档、设计、审查、计划等 markdown 默认使用中文撰写；项目现有文档多为繁体中文，更新既有文档时优先保持原文风格。代码注释使用英文，除非相邻代码已有不同约定。
- 文档中的描述性语言默认使用中文；专业名词、协议字段、枚举值、配置项、命令行参数、文件路径、代码标识符、日志原文和外部 API 名称保留英文或原文。
- 提交信息建议使用英文；PR 标题和正文可按团队约定选择中文或英文。
- 优先遵循仓库现有的 `CMake + C++20` 结构、`build.sh`、vcpkg 依赖、目录结构和代码风格，不做与当前任务无关的重构。
- `orion` 是交易系统核心库，覆盖 TWSE / TAIFEX 行情接入、shared memory 数据总线、OMS、策略基底与交易接入抽象。默认同时关注正确性、确定性、低延迟、可恢复性和可观测性。
- 对性能、时延、吞吐、并发安全、撮合 / 下单 / 撤单 / 回报行为相关结论，必须基于实际测试、benchmark、profile、replay、smoke 或运行证据，不凭主观判断宣称完成。
- 代理自行创建的 log、scratch config、临时输出、benchmark 临时产物和编译 / 测试临时目录默认写入 `~/tmp` 或仓库既有 build 目录；除非用户明确要求或外部工具强制，不要把新的项目临时文件散落在仓库根目录。

## 新对话启动

每个新对话进入本仓库后，先执行：

```bash
git status --short --branch
git log --oneline -8
```

然后按顺序读取：

```text
AGENTS.md
README.md
docs/onboarding.md
docs/summary.md
docs/runtime.md
docs/testing.md
wiki/multicast_guide.md
```

如果某些文档不存在，先读取项目中最接近的 onboarding、README、架构说明、运行手册和测试说明。读取后以项目当前文档和 `git status` / `git log` 为事实源，不假设本地知识是最新的。

`docs/onboarding.md` 是新对话接手入口；`docs/summary.md` 是当前最完整的架构总结；`docs/runtime.md` 说明进程、SHM 和启停顺序；`docs/testing.md` 说明构建、replay、smoke 和提交前检查。

## 结束对话流程

当用户要求结束当前对话、交接或生成下一轮 onboarding 时，默认执行：

1. 运行 `git status --short --branch` 和 `git log --oneline -8`。
2. 对照当前实现、配置和最近提交，更新相关文档，重点同步 `docs/onboarding.md`、`docs/summary.md`、`docs/runtime.md`、`docs/testing.md` 中与本轮变更相关的入口、边界和验证命令。
3. 整理当前状态、已完成事项、验证命令、未完成风险和下一步建议。
4. 至少运行 `git diff --check`。
5. 如果本轮触碰交易行为、行情、OMS、SHM、并发、回报、风控、恢复或性能边界，运行对应 build、replay、smoke、benchmark 或最小复现实验。
6. 除非用户明确要求，不主动 push。是否提交按用户请求和当前任务约定执行；提交前不要裹带无关改动。
7. 最终回复给出验证结果、关键差异和下一轮接手提示；如果已提交，给出提交哈希。

下一轮接手提示不能只写一句“下一步做什么”。它应当是一段可直接复制给下一轮 agent 的交接说明，至少包含：

- 当前仓库路径、当前分支、是否与远端对齐、最近提交哈希。
- 工作区状态：未提交改动、未跟踪文件、stash、worktree、已生成但不纳入 git 的重要产物。
- 本轮已完成事项和已经进入 `main` / 当前分支的边界。
- 本轮修改过或新建的关键文件、配置、文档和测试入口。
- 已执行的验证命令、结果和任何未能执行的验证。
- 当前已知风险、未完成事项和下一步建议，下一步建议要具体到模块、文件或接口层级。
- 如果有未提交文档更新，说明这些更新的目的，并提示下一轮先 review diff 再决定是否提交。

对于较长生命周期任务，最终回复中建议附一个独立的“下一轮提示”代码块，内容按上面字段组织，避免下一轮只能从聊天历史中猜上下文。

## 项目背景

`orion` 当前主要模块包括：

- TWSE / TAIFEX 行情接入、dump 载入与 replay。
- shared memory 行情数据总线，包括 `DataEventQueue`、`DataBuffer` 和 symbol pool。
- OMS shared memory、订单池、账户、持仓、策略下单事件和回报 channel。
- 策略基底，包括通用 `StrategyBase` 与 TAIFEX 专用策略基底。
- 交易接入抽象 `TradeEngine`。
- `andromeda` 交易平台与 Speedy API adapter。
- `stellaris` 策略应用，目前包含期现套利入口。
- benchmark、replay、smoke 与 multicast 工具。

协作代理工作时应优先关注链路边界、事件顺序、状态一致性、幂等性、异常恢复、SHM 生命周期和验证证据。

## Skills

### 使用约定

- 每轮以当前运行环境实际暴露的 available skills 为事实源；磁盘中存在 `SKILL.md` 不等于该 skill 已启用。
- 用户明确点名某个已启用 skill，或任务明显符合其 description 时，必须先完整读取对应 `SKILL.md`，再按其中流程执行。
- 只使用覆盖当前任务所需的最小 skill 集合；skill 不跨轮次自动延续，后续轮次需重新按触发条件判断。
- 使用 skill 前要向用户说明名称和原因；如果 skill 改变了执行流程、要求暂停或造成阻塞，也要明确说明。
- skill 指令与用户要求或本文件冲突时，优先遵循用户要求和本文件。无法读取或无法适用时，简要说明并采用安全的替代流程。
- 不因发现某个 skill 已安装就自行启用、恢复或安装更多 skills；安装或更新 skill 需要用户明确要求。

### 当前可用 skills

当前 Codex 环境提供以下 skills；环境升级后若清单变化，以新会话实际暴露的 available skills 为准。

项目开发：

- `adaptive-development`：对软件设计、实现、bug fix、重构、build / runtime 配置、测试、性能优化和长期技术规划按 L0-L3 风险分级执行。

Codex 内置：

- `imagegen`
- `openai-docs`
- `plugin-creator`
- `skill-creator`
- `skill-installer`

插件提供：

- `browser:control-in-app-browser`
- `documents:documents`
- `pdf:pdf`
- `presentations:Presentations`
- `sites:sites-building`
- `sites:sites-hosting`
- `spreadsheets:Spreadsheets`
- `spreadsheets:excel-live-control`
- `template-creator:template-creator`
- `visualize:visualize`

### 禁用 skills

本仓库禁用全部 `obra/superpowers` skills。代理不得加载、调用、安装或自动恢复这些 skills，即使它们存在于本机、旧计划或历史文档中。Codex 内置能力和其他非 Superpowers 来源的 skills 不受此禁令影响。

## 推荐工程方法（非 Skills）

以下小节定义 `orion` 的工程方法论。标题可能与外部 skill 同名，但它们不是当前可调用的 skill，也不代表允许启用对应的 `obra/superpowers` 实现。

### `systematic-debugging`

优先用于以下场景：

- 单元测试、集成测试、回归测试、replay 或 smoke 失败。
- 行情链路、订单链路、OMS、风控链路或状态机行为异常。
- 并发问题、竞态、死锁、顺序错乱、queue full、gap 或偶发失败。
- benchmark、延迟、吞吐或资源占用出现回归。
- 交易所 / broker adapter 与预期协议行为不一致。

要求：

- 先复现和定位根因，再修改代码。
- 不靠猜测反复打补丁。
- 修复后用原始失败用例和相关回归测试验证。

### `verification-before-completion`

优先用于以下场景：

- 准备宣称任务完成、bug 修好、测试通过或性能改善前。
- 准备提交、合并、发 PR 或交接前。
- 修改交易、行情、OMS、SHM、回报、风控、恢复、并发或性能相关代码后。

要求：

- 必须运行新鲜的验证命令。
- 必须读取退出码和关键输出。
- 性能和低延迟结论必须附 benchmark、profile、压测、replay 或 live probe 证据。

### `test-driven-development`

优先用于以下场景：

- 新增或修改 parser、encoder、protocol adapter、data converter。
- 修改 orderbook builder、OMS order state、风控规则、重试逻辑、恢复逻辑或 reconcile。
- 修正边界条件、时序问题、幂等行为或重复 / 过期事件处理。

要求：

- 先写能暴露目标行为的失败测试，再写实现。
- 对关键交易链路，测试应覆盖正常路径、拒绝路径、重复事件、乱序事件和终态幂等。

### `writing-plans`

优先用于以下场景：

- 中等及以上规模改动。
- 需要拆分多步任务。
- 改动跨越多个模块、线程、进程、SHM 或外部系统。

典型场景：

- 新增交易所、broker 或 Speedy 类 adapter。
- 引入新的订单类型、风控能力或持仓模型。
- 调整事件流、线程模型、SHM / queue ABI 或模块边界。
- 增加 benchmark、压测、replay 对账或恢复流程。

### `brainstorming`

优先用于以下场景：

- 需求、边界或成功标准不清楚。
- 存在多种线程模型、模块划分、状态 owner 或性能优化路径。
- 需要先比较正确性、延迟、恢复复杂度和实现成本。

典型场景：

- 设计行情总线、订单路由、订单回报链路或持仓聚合模型。
- 选择同步、异步、单线程 owner、无锁队列或跨进程 SHM。
- 设计断线恢复、feedback gap、REST reconcile 和 degraded mode。

### `requesting-code-review`

适合较大改动后的结构性复核，用于提前发现：

- 边界条件遗漏。
- 状态机漏洞。
- 测试覆盖不足。
- 并发安全风险。
- 性能退化点。
- 交易行为语义与文档不一致。

#### 多轮 review

当用户要求“多轮 review”时，含义是按轮次迭代执行：

1. 先确认本轮 review 目的和范围。
2. 执行一轮结构性 review，输出按严重级别排序的 findings。
3. 对 Critical / Important 问题先验证是否成立，再按 `receiving-code-review` 流程修复。
4. 修复后运行相关验证。
5. 再启动下一轮 review，重点确认上一轮问题是否解决、是否引入新问题。
6. 如仍有阻塞问题，继续 review → fix → verify → review，直到没有 Critical / Important，或用户明确停止。

多轮 review 不是一次性启动多个 reviewer 的同义词。多个 subagent / 多视角并行审查可以作为某一轮 review 的实现方式，但不能替代修复后的下一轮复审。

每次用户要求“多轮 review”时，必须先询问本次 review 的目的；如果用户未指定或选择默认，则默认目的为：

- 代码逻辑。
- 设计结构。
- 运行性能。

### `receiving-code-review`

收到 review 反馈时使用，尤其是反馈涉及交易行为、状态机、并发或性能时。

要求：

- 先验证反馈是否成立。
- 不盲目接受或表演式同意。
- 如果反馈不清楚，先澄清预期行为和证据。

### `subagent-driven-development` / `dispatching-parallel-agents`

适合已有明确计划且子任务边界清晰时使用，例如：

- 一个 agent 调查行情路径，另一个调查订单路径。
- 一个 agent 实现 parser 测试，另一个实现 benchmark。
- 多个 adapter 或策略模块可独立调查。

要求：

- 明确每个子任务的读写范围和交付物。
- 不让多个 agent 同时修改同一模块，除非有清晰协调机制。
- 主会话负责整合、验证和最终判断。
- 如果工具支持设置 subagent 推理强度，复杂实现、审查或调查任务默认使用较高推理强度；如果当前工具不支持，应用更严格的主会话复核和验证替代。

### `using-git-worktrees`

适合长生命周期、风险较高或多人并行的功能开发。短小本地修改不必默认使用。

### `finishing-a-development-branch`

适合正式分支收尾、合并、发 PR 或清理实验分支时使用。普通本地实现阶段不必默认启用。

## 推荐工作顺序

通常按下面顺序考虑：

1. 需求或方案没想清楚时，先澄清需求和边界。
2. 需求清楚但改动较大时，先写实施计划。
3. 开始实现关键交易链路时，优先采用测试先行。
4. 出现异常、回归或性能问题时，先复现并定位根因。
5. 完成较大改动后，做结构性代码复核。
6. 准备收尾、提交或交接前，必须运行新鲜验证。

## 高频 / 低延迟设计原则

- 默认把主路径低延迟和尾延迟可控作为高优先级目标；吞吐、资源利用率和工程便利性不能破坏主路径确定性。
- 评估方案时优先关注 p50 / p99 / p99.9、抖动、排队、最长停顿和 queue backlog，不只看平均值。
- 如果某个方案能提升吞吐量，但会增加排队、批处理、额外拷贝、额外同步、动态分配或尾延迟，应谨慎采用。
- 热路径中避免不必要的动态分配、字符串格式化、日志、虚函数、锁竞争、共享状态写入和大对象拷贝。
- 对初始化后不变、且会在热路径中反复使用的信息，应在启动期解析、校验和缓存。
- 可以为高频且可明确判定的主路径设计快路径；对低概率场景不要过早泛化，避免为罕见分支引入长期复杂度。
- 交易行为的正确性、确定性和可恢复性优先级高于单点延迟优化；不能为追求几纳秒牺牲订单状态一致性。
- 性能优化必须先定位瓶颈，再通过 benchmark、profile、链路压测、replay 或线上观测验证收益。

## 交易链路设计原则

- 行情接入、策略执行、订单发送、订单回报和风控应有清晰的线程 / 进程边界。
- 策略应表达交易意图，不直接解析交易所 wire protocol。
- OMS / 订单管理模块应是订单状态 owner，统一处理本地订单、ack、accepted、rejected、partial fill、filled、cancelled 和 unknown state。
- 下单通道负责发送请求和解析轻量响应；订单回报通道负责处理交易所或 broker 的事实回报；二者不要混淆职责。
- ack 只表示外部接口收到请求，不等价于订单进入订单簿，不应直接推导持仓变化。
- 持仓变化应由成交回报、账户 / 仓位事件或 reconcile 结果驱动。
- 断线、gap、queue full、reconnect、进程重启后必须明确状态可信度和恢复策略。
- 涉及重试、撤单、replace、cancel all、reduce-only、post-only、IOC/FOK、self-trade prevention 等行为时，应明确外部系统语义和本地状态转换。

## Shared Memory、并发和进程模型

- 每个模块应明确线程 owner、对象生命周期、同步方式和内存可见性假设。
- 单 owner thread 可以显著降低共享状态复杂度；跨线程通信优先使用清晰的队列、ring buffer、SHM 或 event channel。
- 热路径避免跨线程阻塞调用；必要时用非阻塞队列和明确的 backpressure / drop / gap 语义。
- 多进程链路必须定义 ABI、版本、容量、ownership、heartbeat、recovery 和 stale owner 处理方式。
- 启动前必须明确是否保留既有 SHM。测试环境常用 `remove_existing_shm = true` 清掉旧状态；需要观察重启或 consumer attach 行为时，不应随意清除。
- 一般 replay / dry-run 启动顺序是 data engine、OMS / `trade_platform`、strategy；停止顺序建议反过来，从 consumer 到 producer。
- 并发修改必须配套测试、最小复现实验或明确运行证据，不能靠代码阅读宣称线程安全。

## 错误处理和恢复

- 区分本地错误、交易所 / broker 拒绝、网络断线、协议解析失败、队列溢出、状态 gap 和未知订单状态。
- 不要在断线时伪造 filled / cancelled / rejected 等业务事实；应显式标记 unknown 或 degraded，并通过 reconcile 恢复。
- 所有 gap / degraded 状态都应有策略侧行为：暂停新开仓、限制撤单、触发 reconcile、报警或人工介入。
- 恢复流程应明确哪些状态可信、哪些状态需要从外部系统重新查询、哪些状态需要人工确认。

## 测试和验证

- 修改交易所协议、行情处理、orderbook builder、OMS、订单状态机、风控、恢复逻辑、SHM 或线程模型时，优先补充或更新测试。
- 涉及顺序、幂等、重试、断线重连、超时、账户状态或成交回报处理的变更，必须显式验证结果。
- 新增 parser / encoder / data converter 时，至少覆盖正常样例、缺字段、字段类型错误、未知枚举、边界数值和 malformed payload。
- 新增订单状态转换时，至少覆盖 accepted、rejected、partial fill、filled、cancelled、cancel after partial、duplicate / stale feedback 和 unknown order。
- SHM / queue 相关改动至少考虑 create / attach、既有 SHM、queue full、reader 从头或从尾读、producer 重启。
- 性能结论必须注明机器、CPU、kernel、编译类型、compiler、affinity、benchmark / replay / profile 命令和关键结果。
- live exchange smoke 必须明确是否会真实下单，并给出风控限制、撤单 / 平仓策略和事后账户复核。

## C++ 编码和依赖使用约定

- 新写或修改的项目自有 C++ 代码，命名遵循仓库现有风格；缩进、换行、include 排序等机械格式遵循仓库根目录 `.clang-format`。
- 第三方代码、生成代码、Speedy vendor code 和交易所 schema 生成物优先保持上游或生成器输出风格。
- enum 转字符串优先使用 `magic_enum::enum_name(value)`，避免重复手写 enum-to-string 的 `switch`。
- 打印输出和字符串格式化优先使用项目已有 `fmt` / `quill` 路径；低延迟热路径中避免不必要的临时 `std::string`。
- 需要写入已有缓冲区、避免动态分配或控制截断行为时，优先使用固定缓冲区和明确长度返回；使用格式化库时优先考虑 `fmt::format_to` 或 `fmt::format_to_n`。
- 不新增 `printf`、`fprintf`、`snprintf`、`std::cout`、`std::format` 或 `std::to_string` 等新的格式化 / 打印路径，除非有明确兼容性或系统接口理由，并保持局部化。
- map / unordered map 选择应基于数据规模、访问模式和 benchmark；不要从其他项目照搬未在本仓库引入的容器依赖。

## 文档约定

- 设计文档应说明模块职责、数据流、线程 / 进程模型、SHM 边界、失败模式、恢复策略、测试计划和性能验证口径。
- `docs/onboarding.md` 只写当前事实、关键入口、重要边界、验证命令和下一步建议；不要把完整历史、设计推导、完整 benchmark 输出或 live smoke 原始日志写进 onboarding。
- 细节放到对应专题文档：架构摘要放 `docs/summary.md`，runtime / SHM / 启停放 `docs/runtime.md`，验证方式放 `docs/testing.md`，multicast 环境放 `wiki/multicast_guide.md`。
- 文档中不要把 benchmark micro result 写成端到端交易延迟结论。
- 对外部交易所 / broker 行为、API 语义、字段含义或限制规则，优先引用官方文档、项目现有 adapter 代码或实际运行证据。
- 当实现边界变化时，同步更新 README、onboarding、架构文档、runtime 文档和测试说明中受影响的部分。

## Git 和提交约定

- 修改前先看 `git status --short --branch`，确认是否存在用户或他人未提交改动。
- 不要 revert、reset 或覆盖自己没有创建的改动，除非用户明确要求。
- 如果工作区已有未提交更改，先读取并理解现状，再在现有基础上做最小修改。
- 提交保持原子性：实现、文档、benchmark、清理如果不是同一项最小闭环，优先拆分提交。
- 提交前至少运行 `git diff --check`，并运行与变更相关的最小测试。
- 除非用户明确要求，不主动 push。

## 常用验证命令

```bash
# 基本检查
git status --short --branch
git diff --check

# 构建
./build.sh debug
./build.sh release
./build.sh release_asan

# TAIFEX replay
./build/debug/tools/taifex_data_engine \
  --config config/taifex/taifex_future.toml \
  --mode replay

# TWSE replay
./build/debug/tools/twse_data_engine \
  --config stellaris/config/orion/twse_data_engine.toml \
  --mode replay

# TAIFEX stock future dump-to-CSV
./build/debug/tools/taifex_stock_future_data_engine \
  --config stellaris/config/orion/taifex_stock_future_data_engine.toml \
  --mode to_csv \
  --dump /path/to/taifex.dump

# TWSE dump-to-CSV
./build/debug/tools/twse_data_engine \
  --config stellaris/config/orion/twse_data_engine.toml \
  --mode to_csv \
  --dump /path/to/twse.dump

# 策略 demo
./build/debug/examples/taifex_strategy_demo \
  --config config/strategy/taifex_strategy_demo.toml

# 期现套利策略
./build/debug/stellaris/arbitrage \
  --config config/strategy/stock_index_future_arbitrage.toml

# 个股期货期现套利策略
./build/debug/stellaris/future_spot_arbitrage \
  --config \
  stellaris/config/future_spot_arbitrage/future_spot_arbitrage.toml
```

注意：config 中的 `file_path`、`symbol_file`、`account_file`、`position_file` 目前可能是开发机绝对路径；执行 replay、smoke 或 live 相关命令前需改成当前环境有效路径。

## 接手注意事项

- 先确认当前分支、未提交改动和最近提交。
- 先读 onboarding / README / 架构 / runtime / testing 文档，再修改代码。
- 如果需求涉及交易行为、资金、账户或实盘链路，先确认 dry-run / live-run 边界。
- 如果需要真实下单、访问交易所 / broker、使用 API key 或启动长期进程，必须先获得用户明确授权。
- 若要接 multicast，先确认本机 IP、网卡、group、port 和操作系统网络设置。
- 若要启动 `trade_platform`、Speedy 或 DT3 adapter，先确认 config 是否会连到实际交易 API。
- 如果发现文档和代码冲突，以当前代码和实际验证为准，并同步修正文档。

## 一句话原则

先确认链路和约束，再实现；先验证根因，再修复；先跑证据，再宣称完成。
