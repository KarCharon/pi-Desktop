<h1 align="center">DeepSeek 余额功能交接</h1>

<h2 align="center">最新变更：五分钟轮询与五秒动画（2026-09-25）</h2>

<p align="center">用户最新要求已实现：启用时首次查询，此后每 5 分钟查询，保留手动刷新；模型响应与压缩结束仅记录诊断，不再触发余额查询。禁用、断线及 Profile 变化取消旧周期，重新启用后重新计时；周期刷新仍遵守单并发、合并、超时、有限重试及 429 冷却。model_response 的 accepted 固定为 false，reason=periodic_only，属于正常策略，不再按旧版方法判断为 provider 不匹配。周期查询日志 source=periodic。</p>

<p align="center">扣减浮字总时长改为 5000 ms，上浮 64 px，前 2000 ms 保持不透明、后 3000 ms 渐隐；字号、三条容量限制、账户隔离保持不变。浮字表示相邻两次有效余额的累计差额，不是单次模型费用。</p>

<p align="center">本轮读取的真实诊断日志来自 PID 5668，run=a5060781-003c-475c-b7fa-8b3a9b57f3e1，时间 20:22:19–20:22:46。首次查询、两次响应结束自动刷新、五次手动刷新共八次均 HTTP 200，耗时 55–150 ms，余额均为 CNY 151.30；确认该轮旧策略自动触发生效，未观察到扣减，不能据此判定结算延迟或账户不一致。</p>

<p align="center">Release 构建及 CTest 8/8 通过，含新增周期生命周期测试与五秒 QML 动画测试。周期测试检查 300000 ms 配置并模拟 timeout，不等待真实五分钟。日志为 .tmp/balance-periodic-build.log、.tmp/balance-periodic-tests.log、.tmp/balance-periodic-deploy.log。新发布入口：<code>F:\Code\pi-DeskTop\dist\PiDesktop-DeepSeekBalance-5min\bin\pi_Desktop.exe</code>。未覆盖在运行的诊断版，未发送付费请求，未提交或推送。真实五分钟等待及新动画肉眼验收仍待用户操作。</p>

<p align="center"><strong>以下章节为上一版历史交接记录；刷新策略、动画参数、发布入口及 accepted 判断以本节为准。</strong></p>

<p align="center">用于下一轮继续排查“调用模型后余额看起来未变化”。当前功能及用户要求的业务诊断日志均已实现并构建。用户最新要求是在日志完成后更新本文。下一步是请用户切换到独立诊断版，操作几轮，再读取本地日志定位；不能在取得真实操作日志前直接断言故障原因。</p>

<h2 align="center">1. 用户已确认的需求</h2>

<p align="center">只在当前已确认模型 provider=deepseek 时显示并启用；其他模型默认关闭。余额请求强制直连，不使用设置代理、系统代理或环境代理。密钥只取当前 Profile/auth.json 的 deepseek 项，不回退环境变量或其他 Profile；失败显示“余额获取失败”。</p>

<p align="center">余额格式为“💵 ¥110.00”，人民币不显示 CNY；扣减动画为“💸 -¥0.02”，金额红色、字号与余额一致、不放大，向上移动约 32px 并在约 1 秒内渐隐。只有同一账户、同一币种两次有效余额减少才播放；首次读取、充值和失败不播放。</p>

<p align="center">每次可观测模型响应结束后刷新；单次请求总超时 1000ms；失败后最多重试 3 次，即首次加重试总计最多 4 次。临时故障退避 1/2/4 秒；429 遵守 Retry-After，无有效头部时等待 30 秒。固定周期轮询未启用，突发刷新合并并限制并发。</p>

<h2 align="center">2. 当前代码与职责</h2>

<p align="center"><code>src/config/DeepSeekBalanceController.h/.cpp</code>：新增独立余额控制器，专用 QNetworkAccessManager 显式 NoProxy；固定官方 HTTPS 地址、拒绝重定向；凭据读取、请求代次隔离、1 秒总截止、有限重试、64KiB 响应上限、严格解析、字符串十进制减法和扣减信号。金额不使用浮点运算。当前直接密钥校验要求 sk- 前缀；命令型或未知凭据表达式不执行。</p>

<p align="center"><code>src/agent/AgentSessionController.h/.cpp</code>：新增 currentProvider() 与 modelResponseCompleted(provider, source)。source 明确区分 assistant_message_end 与 compaction_end；AppController 传递提供方是否为 DeepSeek 的布尔值及内部事件名，避免日志记录不受控的原始 provider。在实时 message_end 且 role=assistant 时发出通知，优先使用消息自身 provider，否则回退当前 provider；compaction_end 也通知。token、用户消息及工具消息结束不触发。Pi 内部未暴露的 HTTP 请求不能保证逐一对应刷新。</p>

<p align="center"><code>src/app/AppController.h/.cpp</code>：持有余额控制器；监听连接和模型确认状态决定启用；Profile 改动、重连时先失效旧上下文；连接完成后重新启用。构造新增 balanceEnabled 参数，现有相关测试使用 AppController(nullptr, false) 避免真实凭据与外网请求。</p>

<p align="center"><code>qml/components/DeepSeekBalance.qml</code>：新增余额刷新按钮、悬浮提示和上浮渐隐动画；浮字列表最多 3 条；切换账户清空；浮字不接收输入。<code>qml/Main.qml</code> 将组件放入 footer，并将 floatLayer 设为 root.contentItem，避免被 footer 裁剪且保持在弹窗 Overlay 下方。<code>main.cpp</code> 注册不可由 QML 创建的余额控制器枚举类型。</p>

<p align="center"><code>CMakeLists.txt</code>：引入 Qt6::Network、新控制器和 QML 组件，新增 pi_balance_tests 测试目标；相关 agent 测试目标补 Network 链接。<code>qml/components/qmldir</code> 新增目录导入声明。</p>

<p align="center">文档已同步 <code>README.md</code>、<code>docs/DeepSeek 余额显示方案.md</code>、<code>docs/Pi Desktop 技术方案.md</code>。动画分镜为 <code>docs/DeepSeek余额动画分镜.svg</code>。</p>

<h2 align="center">3. 已完成的验证</h2>

<p align="center">Release 构建通过。CTest 共 8 个测试目标全部通过；QML 汇总 50 项通过。新增 <code>tests/DeepSeekBalanceTest.cpp</code> 覆盖精确金额、响应校验、无凭据、基线和密钥身份切换、取消旧请求、401 不重试、429 冷却与突发合并、超大响应和超时重试预算。新增 <code>tests/qml/tst_DeepSeekBalance.qml</code> 覆盖字号、上浮、渐隐、容量与清理；AgentSessionTest 增加响应事件触发测试。</p>

<p align="center">构建目录：<code>build/Release-fixed</code>。独立发布目录：<code>dist/PiDesktop-DeepSeekBalance</code>。用户运行入口：<code>F:\Code\pi-DeskTop\dist\PiDesktop-DeepSeekBalance\bin\pi_Desktop.exe</code>。已确认发布包含 Qt6Network.dll 和 plugins/tls/qschannelbackend.dll，原发布版本未覆盖。</p>

<p align="center">构建工具：CMake 位于 F:/CMAKE/bin，MinGW 位于 F:/Develop/Tools/mingw1310_64/bin，Qt 位于 F:/Develop/6.11.2/mingw_64。可沿用以下命令，输出存入 .tmp 避免混入源码：</p>

```bash
export PATH=/f/Develop/Tools/mingw1310_64/bin:/f/Develop/6.11.2/mingw_64/bin:$PATH
cmake -S . -B build/Release-fixed -DBUILD_TESTING=ON
cmake --build build/Release-fixed -j 6
ctest --test-dir build/Release-fixed --output-on-failure
cmake --install build/Release-fixed --prefix "$PWD/dist/PiDesktop-DeepSeekBalance"
```

<p align="center">首轮构建日志：<code>.tmp/balance-build.log</code>、<code>.tmp/balance-tests.log</code>、<code>.tmp/balance-deploy.log</code>。日志增强版的验证记录为 <code>.tmp/balance-diagnostics-build.log</code>、<code>.tmp/balance-diagnostics-tests.log</code>、<code>.tmp/balance-diagnostics-deploy.log</code>。这些不是运行时业务日志。日志增强版全量 CTest 8/8 通过；首次运行曾有既有 WorkspaceInteraction::test_scrollWhileBusy 偶发失败，未修改该用例，重新全量运行通过。新增日志隐私与轮转测试、QML 动画诊断断言均通过。</p>

<h2 align="center">4. 用户反馈与真实窗口观察</h2>

<p align="center">用户反馈：“不行啊，好像余额没变，我现在开了一个新版的 pi-desktop，你可以拿这个来测试”。用户授权使用正在运行的新版窗口排查；本轮没有主动发送付费模型请求，没有输出密钥，也没有终止用户的进程。</p>

<p align="center">检查时同时存在两个进程：新版 PID 25800，路径为本项目 dist/PiDesktop-DeepSeekBalance/bin/pi_Desktop.exe；旧版 PID 27076，路径 F:/Application/PiDesktop-Release/bin/pi_Desktop.exe。PID 仅为当时观察值，后续必须重新查询，不能直接依赖硬编码值。</p>

<p align="center">新版窗口显示模型 deepseek/deepseek-flash，当前 Profile 为 <code>C:/Users/Thave/.pi/profiles/DeepSeek</code>，初始观察余额为 ¥151.31。截图 <code>.tmp/balance-current-screen.png</code> 含用户聊天内容，只用于本地排查，不提交、不上传。</p>

<p align="center">从该 Profile 读取密钥，仅在内存中用于直连官方 /user/balance。首次独立查询连续三次均返回 CNY 151.31，请求耗时分别约 134/109/85ms。没有读取其他 Profile，也没有回退环境密钥。</p>

<p align="center">随后通过 Windows UI Automation 对新版窗口的余额按钮执行手动刷新，并将鼠标移到按钮查看提示。截图 <code>.tmp/balance-refresh-tooltip.png</code> 显示“更新于 02:23:53”，余额已变为 ¥151.30。之后独立查询三次均返回 CNY 151.30，耗时约 126/108/107ms。</p>

<p align="center"><strong>能确认的事实：</strong>余额服务可达，当前 Profile 密钥可用，桌面端手动刷新能更新成功时间和余额。该次实际官方响应仅返回两位小数。<strong>尚不能确认：</strong>自动触发是否每次执行、为何前面的请求看似未更新、扣减动画在真实窗口是否播放。没有记录刷新前完整业务日志，也没有在 1 秒动画窗口内截图，因此不能声称已经验证动画。官方余额结算延迟或显示精度只是候选原因，不能据此直接定案。</p>

<p align="center">还只读检查了当前 Profile 最近会话的非正文元数据，能看到 assistant 响应 provider=deepseek、stopReason 为 stop/toolUse 和 usage.cost.total。该值是 Pi 上报成本，不等于官方人民币扣费，不应直接拿它替代余额差额或据此推断精确结算时间。</p>

<h2 align="center">5. 已完成：脱敏业务日志与下一步复现</h2>

<p align="center">用户排查要求：“你要不先给这个功能的代码加点日志？我操作几下你看日志来定位问题”。<strong>日志现已实现。</strong>没有改变刷新触发、1 秒超时、重试或动画业务策略，没有发送付费测试请求，没有终止用户正在运行的窗口。独立诊断版入口为 <code>F:\Code\pi-DeskTop\dist\PiDesktop-DeepSeekBalance-Diagnostics\bin\pi_Desktop.exe</code>；原 DeepSeekBalance 发布目录不覆盖。用户需要自行关闭旧的测试窗口并启动此诊断版，原进程不会热加载新增日志。</p>

<p align="center"><strong>已记录的事件链：</strong>controller_started → context_change/context_cancel → model_response（含 source、deepseek、accepted）→ refresh_trigger → refresh_scheduled/refresh_merged_active/refresh_merged_scheduled/refresh_ignored_disabled → task_begin → baseline_reset（首次或密钥身份改变）→ request_start → response_received/request_timeout → response_valid/query_failed/retry_scheduled → balance_compare → decrease_signal → animation_received → animation_created/animation_skip_hidden/animation_skip_no_layer/animation_create_failed → animation_finished；另外记录 animation_cleared、animation_capacity_drop、task_complete、response_discarded。</p>

<p align="center"><strong>日志格式：</strong>UTF-8 JSONL，每行一个事件。公共字段包含 time（本地时间与时区）、elapsed_ms（控制器存活的单调耗时）、pid、run（随机运行标识，不是密钥摘要）、sequence、task、generation、retry、enabled、state、active、pending。HTTP 响应另含 http_status、network_error、request_elapsed_ms、bytes、timeout、oversized；比较事件另含 currency、previous、current、delta、outcome（baseline/unchanged/decreased/increased）。本次排查确实记录本地余额和差额，不自动上传，分享日志前应注意金额隐私。state 数值为 0=Disabled、1=Loading、2=Ready、3=Error。</p>

<p align="center"><strong>严禁记录：</strong>API Key、Authorization、auth.json 正文、完整 HTTP 请求头、原始响应正文、聊天正文、附件内容、任何可恢复密钥的值或身份指纹。不要把全量 Profile 配置或会话内容写入日志；Profile 可使用“上下文序号”代替真实路径。错误原因只输出枚举或本地脱敏文案。</p>

<p align="center"><strong>实际落盘：</strong>控制器使用独立 trace() 写入器，路径为 <code>QStandardPaths::AppLocalDataLocation/logs/deepseek-balance.jsonl</code>。当前 Windows 默认应为 <code>C:\Users\Thave\AppData\Local\PiDesktop\Pi Desktop\logs\deepseek-balance.jsonl</code>；实际路径可在余额按钮悬浮提示中查看。每行写入后 flush，单文件约 1MiB 轮转，仅保留一份 <code>deepseek-balance.jsonl.1</code>。多实例使用 QLockFile 保护写入与轮转，锁竞争或写入失败时跳过该条、不阻塞聊天线程，并仅向 Qt 输出一次固定脱敏警告；日志因此可能有序号缺口。未安装全局 Qt 消息处理器。</p>

<p align="center"><strong>测试隔离：</strong>请求工厂注入时默认不写真实用户日志；日志测试显式传入临时目录。AppController(nullptr, false) 同时禁用余额日志。新增 diagnosticPrivacyAndRotation 用例验证事件落盘、JSONL 字段、密钥/认证头/原始响应/真实 Profile 路径不进入日志、未知动画阶段被拒绝及 1MiB 轮转。QML 测试桩验证 received/created/finished 和隐藏/清理/容量事件。</p>

<p align="center"><strong>下一轮复现步骤：</strong>用户启动上述诊断版 → 确认当前模型为 DeepSeek 且首次余额取得成功 → 用户自行发送两三轮提示（可以包含工具调用）→ 等待模型完成并观察余额/动画 → 手动点击余额刷新一次 → 告知操作完成及大致时间。随后读取日志及 .1 备份，按 pid/run 分组，沿 source/task/generation/sequence 追踪完整事件链。日志只对新诊断版进程有效，不能把旧截图时间与新运行任务号混用。</p>

<p align="center"><strong>判断方法：</strong>无 model_response：检查 RPC 事件接入；accepted=false：查 provider 匹配与启用状态；有触发无 request_start：查合并与冷却；有 HTTP 错误：查状态码、网络枚举及超时；balance_compare.outcome=unchanged：说明此次官方返回同值；baseline：首次或身份重置不播放动画；decreased 后无 animation_received：查信号/QML 连接；有 received 但 skip：查隐藏状态或覆盖层；有 created/finished 但肉眼不可见：再查位置、裁剪和遮挡。当前尚未取得用户操作诊断版的真实日志，未实现延迟结算补查，也未增加周期轮询。</p>

<h2 align="center">6. 本地排查脚本与注意事项</h2>

<p align="center"><code>.tmp/check-deepseek-balance.py</code>：针对当前已确认 Profile，直连查询三次官方余额；仅输出余额和耗时，不打印密钥。该脚本用 urllib 的 1 秒网络 timeout，不等同于生产控制器覆盖整个请求的 1000ms 总截止时间。脚本还会在存在 models.json 时输出 deepseek 配置域名与是否存在 apiKey 覆盖，不输出覆盖值。</p>

<p align="center"><code>.tmp/balance-ui-check.ps1</code>：对当时新版 PID 的余额按钮执行 Invoke 并截图悬浮提示。运行前需更新 PID。Windows PowerShell 5 直接 -File 读取无 BOM UTF-8 中文注释会解析异常；本次通过以下形式显式 UTF-8 读取后执行成功：</p>

```powershell
Invoke-Expression (Get-Content -Raw -Encoding UTF8 .tmp/balance-ui-check.ps1)
```

<p align="center">工具环境没有 rg，可用 grep/find。工具读取文件应使用 read，不使用 cat/sed。所有新增函数、字段、类型按项目要求补中文注释。用户之前指出 HTML 居中标签在聊天输出中显示有问题，对用户的简短汇报用正常中文；文档文件继续遵循项目居中段落约定。</p>

<h2 align="center">7. Git 与工作区边界</h2>

<p align="center">本功能的代码、测试和文档均未提交、未推送。当前工作区包含已有的 docs 图片删除和 Error/ 未跟踪目录，这些不是本次余额排查造成的，不恢复、不删除、不擅自纳入提交。新增控制器、组件及测试目前仍是未跟踪文件，交接时不能仅看 git diff --stat 而遗漏它们，应同时看 git status --short。</p>

<p align="center">之前用户批准过一次独立干净快照推送到 ssh://gitea@8.130.143.54:2222/cc/pi-DeskTop.git 的 main，提交为 83cfe9b；那次推送不包含本次余额功能。原项目 GitHub origin 未修改。后续提交或推送仍须重新说明内容并等待用户明确批准，不沿用旧授权。</p>

<h2 align="center">8. 官方参考</h2>

<p align="center"><a href="https://api-docs.deepseek.com/api/get-user-balance">余额接口</a> · <a href="https://api-docs.deepseek.com/quick_start/rate_limit">通用限流说明</a> · <a href="https://api-docs.deepseek.com/quick_start/error_codes">通用错误码</a></p>

<p align="center">此前查阅未找到余额接口专属请求频率上限或结算即时性承诺；模型并发限制不能直接套用到 /user/balance，也不能据此宣称余额查询不限流。官方金额为字符串，设计与代码支持超出两位的小数精度，但实际本账户此次返回的是两位小数。</p>
