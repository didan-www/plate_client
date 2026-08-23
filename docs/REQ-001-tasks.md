# REQ-001 Qt 车牌识别管理客户端开发任务

| 属性 | 内容 |
| --- | --- |
| 关联需求 | REQ-001 v2.1 |
| 关联设计 | REQ-001-design v1.1 |
| 执行方式 | 严格串行 |
| 目标平台 | Windows 10/11 64-bit |
| 技术基线 | C++17、Qt 6.11.1 Desktop MinGW 64-bit、CMake、Ninja、Qt Test |

## 1. 执行规则

1. 必须从 `TASK-001` 开始，严格按编号串行执行，不得并发开发多个任务。
2. 每个任务只依赖前一个任务；标记完成前必须满足该任务的全部验收标准，并保持此前测试全部通过。
3. 每个任务必须在同一次实现中提交生产代码、对应测试以及必要的构建文件更新，不允许只创建空接口或以后再补测试。
4. 测试使用临时目录、可控 HTTP Mock 和 MQTT Mock，不依赖生产 Linux 服务、生产 Broker 或真实 MySQL。
5. “预计修改文件”用于限定模块边界。实现时允许在同一目录内合并过小文件，但不得改变协议、职责或验收范围；发生调整时同步更新本文档。
6. 每个任务结束时至少执行本任务测试和全部既有回归测试。构建命令只有经本机验证后才能写入 README。
7. 不得引入 SQLite、Qt Sql、直接 MySQL、客户端车牌识别、客户端 MQTT 结果发布或其他 REQ-001 范围外功能。

## 2. 串行任务总览

| 任务 | 名称 | 直接前置依赖 |
| --- | --- | --- |
| TASK-001 | 准备项目本地 Qt MQTT 依赖 | 无 |
| TASK-002 | 建立 CMake、应用和 Qt Test 基线 | TASK-001 |
| TASK-003 | 实现引导配置加载与校验 | TASK-002 |
| TASK-004 | 实现脱敏技术日志 | TASK-003 |
| TASK-005 | 实现领域类型和 HTTP DTO 编解码 | TASK-004 |
| TASK-006 | 实现异步 HTTP 传输基础设施 | TASK-005 |
| TASK-007 | 实现登录、注销和会话服务 | TASK-006 |
| TASK-008 | 实现登录界面和应用入口流程 | TASK-007 |
| TASK-009 | 实现主窗口、导航和页面外壳 | TASK-008 |
| TASK-010 | 实现心跳和 HTTP 状态 | TASK-009 |
| TASK-011 | 实现 MQTT 连接、订阅和错误状态机 | TASK-010 |
| TASK-012 | 实现识别事件解析和实时会话存储 | TASK-011 |
| TASK-013 | 实现识别详情查询和重连校准 | TASK-012 |
| TASK-014 | 实现安全图片下载和异步解码 | TASK-013 |
| TASK-015 | 实现实时监控页面闭环 | TASK-014 |
| TASK-016 | 实现历史日志查询页面 | TASK-015 |
| TASK-017 | 实现 CSV 下载和原子保存 | TASK-016 |
| TASK-018 | 实现黑白名单 API 和业务服务 | TASK-017 |
| TASK-019 | 实现黑白名单管理页面 | TASK-018 |
| TASK-020 | 完善退出、关闭和异步销毁安全 | TASK-019 |
| TASK-021 | 完成通信与业务端到端集成测试 | TASK-020 |
| TASK-022 | 完成全量验收和开发文档收尾 | TASK-021 |

## 3. 任务明细

### TASK-001 准备项目本地 Qt MQTT 依赖

**目标**

提供可重复执行的 Qt MQTT `v6.11.1` 本地构建流程，将依赖安装到 `.deps/install/qtmqtt`，不修改全局 Qt 安装。

**前置依赖**

无。

**实现内容**

- 编写 PowerShell 引导脚本，检查 Qt 6.11.1 MinGW、CMake、Ninja 和 MinGW-w64 GCC 13.1.0。
- 固定 Qt MQTT 源码标签为 `v6.11.1`，使用 MinGW ABI 构建并安装到项目本地目录。
- 让脚本可重复执行，并在版本或 ABI 不匹配时明确失败。
- 将 `.deps` 和依赖构建目录加入忽略规则，README 只记录实际验证通过的命令。

**预计修改文件**

- `scripts/bootstrap-qtmqtt.ps1`
- `.gitignore`
- `README.md`

**独立测试**

- 在没有本地 Qt MQTT 的环境运行脚本并完成安装。
- 再次运行脚本，验证不会破坏已有正确安装。
- 使用 CMake 包探测验证 `Qt6::Mqtt` 存在且版本为 6.11.1。

**验收标准**

- `.deps/install/qtmqtt` 中存在可被 MinGW Qt 6.11.1 使用的 Qt MQTT CMake 包和库。
- 全局 `D:\Qt` 内容未被脚本修改，仓库不跟踪 `.deps` 内容。
- 版本、工具链或 ABI 错误均产生非零退出码和可读错误。

### TASK-002 建立 CMake、应用和 Qt Test 基线

**目标**

建立能够配置、编译、启动和运行测试的最小 Qt Widgets 工程。

**前置依赖**

TASK-001 已验收。

**实现内容**

- 创建顶层 CMake 工程、MinGW 开发预设、源码目标和测试目标。
- 查找并链接 Qt `Widgets`、`Network`、`Concurrent`、`Mqtt` 和 `Test`，不查找 `Qt Sql`。
- 创建最小 `QApplication` 入口和一个无业务逻辑的启动窗口。
- 由 CMake 注入应用版本和构建日期。
- 建立 Qt Test 冒烟测试和统一测试注册方式。

**预计修改文件**

- `CMakeLists.txt`
- `CMakePresets.json`
- `cmake/ProjectOptions.cmake`
- `src/CMakeLists.txt`
- `src/app/main.cpp`
- `tests/CMakeLists.txt`
- `tests/tst_smoke.cpp`
- `README.md`

**独立测试**

- 使用配置的 MinGW 预设完成全新配置和编译。
- 启动程序，验证 Qt 事件循环和最小窗口正常。
- 运行冒烟测试并确认 CTest 能发现测试。

**验收标准**

- Debug 构建和测试均通过，程序链接的是 MinGW Qt 6.11.1 与项目本地 Qt MQTT。
- 构建目录和生成物不进入版本控制。
- README 中只出现本机实际验证过的配置、构建和测试命令。

### TASK-003 实现引导配置加载与校验

**目标**

从程序旁加载 `config/client.json`，在任何网络访问前完成严格校验。

**前置依赖**

TASK-002 已验收。

**实现内容**

- 定义 `BootstrapConfig` 值类型和 `BootstrapConfigLoader`。
- 校验 `clientId` 为 UUID、`loginBaseUrl` 为合法 HTTP/HTTPS 基址、`allowInsecureTransport` 为 boolean；基址可包含路径前缀，但拒绝查询参数、片段和用户信息。
- 配置只允许三个规定字段，任何未知字段均拒绝。
- 缺少 `allowInsecureTransport` 时使用已确认默认值 `false`。
- 当明文地址未被显式允许、配置缺失、JSON 非法或字段错误时返回阻断错误，不提供隐式地址。
- 提供不含凭据的示例配置。

**预计修改文件**

- `config/client.json.example`
- `src/domain/BootstrapConfig.h`
- `src/infrastructure/config/BootstrapConfigLoader.h`
- `src/infrastructure/config/BootstrapConfigLoader.cpp`
- `tests/config/tst_bootstrap_config.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 使用临时目录测试合法 HTTPS、显式允许明文、默认禁止明文、缺字段、错误类型、非法 UUID 和非法 URL。
- 验证配置对象和错误结果不包含任何默认服务器或凭据。

**验收标准**

- 合法配置被准确转换为不可变值对象。
- 所有非法配置在创建登录窗口和网络对象前被拒绝，并提供可展示错误。
- 测试不读写真实用户配置。

### TASK-004 实现脱敏技术日志

**目标**

提供简单、可测试且不会泄露凭据的按日技术日志。

**前置依赖**

TASK-003 已验收。

**实现内容**

- 在 `QStandardPaths::AppLocalDataLocation/logs` 按本地日期追加 UTF-8 JSON Lines，文件名固定为 `plate-client-YYYY-MM-DD.log`。
- 每行记录 `timestamp`、`level`、`module`、`errorCode`、`requestId`、可选 `recognitionId` 和 `context`。
- 按敏感键名和文本模式对 Authorization/Bearer、密码、访问令牌、通用 Token 和 MQTT 密码执行统一脱敏，并省略完整 HTTP/MQTT 响应、完整 MQTT 载荷和图片内容。
- 启动时只清理日期早于“当前本地日期减 7 天”的固定格式日志，恰好 7 天前的日志保留；日志失败不得阻塞应用或网络流程。

**预计修改文件**

- `src/infrastructure/logging/TechnicalLogger.h`
- `src/infrastructure/logging/TechnicalLogger.cpp`
- `tests/logging/tst_technical_logger.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 使用临时目录测试按日文件、7 天边界、脱敏和不可写目录。
- 扫描日志内容，确认测试凭据及完整敏感载荷均未出现。

**验收标准**

- 只删除超过 7 天的技术日志，不影响其他文件。
- 任何日志写入或清理错误均不会导致程序启动失败。
- 日志输出满足 REQ-001 第 10 章的字段和安全要求。

### TASK-005 实现领域类型和 HTTP DTO 编解码

**目标**

建立不依赖 QWidget 或网络对象的领域值类型，并严格实现 HTTP/MQTT 数据契约。

**前置依赖**

TASK-004 已验收。

**实现内容**

- 实现 `SessionContext`、`RecognitionSnapshot`、`AccessListRecord`、`RequestContext`、`PageResult<T>`、`HttpEnvelope<T>` 和 `EmptyResponse`。
- 实现登录、识别、名单、统一信封和分页 JSON 编解码。
- 校验全部必填字段、JSON 类型、枚举、64 位整数、北京时间、状态相关可空性和分页边界。
- 限制名单备注为 200 个字符、识别错误信息为 512 个字符、车牌号为 1 至 16 个字符。
- DTO 解析失败时返回结构化协议错误，不返回部分对象。
- 登录解析生成 `generation=0` 的会话值，所有 DTO 拒绝未知字段，64 位 JSON number 只接受不超过 `2^53-1` 的无损整数。
- `RequestContext` 使用 `QVariantMap` 保存筛选快照；名单车牌只校验已规范化形式，`createdBy` 必须非空。

**预计修改文件**

- `src/domain/SessionContext.h`
- `src/domain/RecognitionSnapshot.h`
- `src/domain/AccessListRecord.h`
- `src/domain/RequestContext.h`
- `src/domain/PageResult.h`
- `src/domain/Errors.h`
- `src/infrastructure/http/JsonCodecs.h`
- `src/infrastructure/http/JsonCodecs.cpp`
- `tests/domain/tst_domain_types.cpp`
- `tests/http/tst_json_codecs.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖统一信封成功、失败、`data=null`、缺字段、错误类型和超限文本。
- 覆盖 `PROCESSING`、`SUCCEEDED`、`FAILED` 的合法与非法可空组合。
- 覆盖分页从 1 开始、`pageSize=100`、总数非负及 64 位整数。

**验收标准**

- REQ-001 第 8 章定义的合法 DTO 均可无损转换为领域对象。
- 缺字段与显式 `null` 被区分，任何非法组合均被拒绝。
- 领域目录不依赖 QWidget、QNetworkReply 或 QMqttClient。

### TASK-006 实现异步 HTTP 传输基础设施

**目标**

提供统一的异步 HTTP 请求、响应限制、超时、取消、错误映射和测试 Mock。

**前置依赖**

TASK-005 已验收。

**实现内容**

- 封装 UI 线程中的 `QNetworkAccessManager`，统一设置 Bearer Token、Content-Type 和请求上下文。
- 实现登录 10 秒、注销 5 秒、心跳 5 秒、查询 10 秒、图片 30 秒、名单写入 10 秒、CSV 120 秒的总时限策略。
- 区分成功、4xx、5xx、网络错误、TLS 错误、超时、用户取消、响应超限、解析失败和旧响应失效。
- JSON 按 `Content-Length` 和累计字节双重限制为 2 MiB。
- 提供可控 HTTP Mock，支持延迟、分块、乱序、超时、断开和自定义响应。
- JSON/图片上限分别按 2 MiB/20 MiB 计算并允许恰好边界；CSV 成功响应流式转发，各模式错误信封按 2 MiB 缓冲。
- 禁止自动重定向；显式取消和旧上下文各返回一次独立错误，均不得触发业务成功回调；传输层不自动重试。

**预计修改文件**

- `src/infrastructure/http/HttpClient.h`
- `src/infrastructure/http/HttpClient.cpp`
- `src/infrastructure/http/HttpRequestPolicy.h`
- `src/infrastructure/http/HttpError.h`
- `tests/support/MockHttpServer.h`
- `tests/support/MockHttpServer.cpp`
- `tests/http/tst_http_client.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 使用短测试时钟或可注入策略验证所有请求类别映射到正确生产时限。
- 验证 2 MiB 边界、分块累计超限、TLS 错误、超时与用户取消的区分。
- 验证取消或旧 `RequestContext` 响应不会调用业务成功回调。

**验收标准**

- UI 线程无阻塞式网络等待。
- 每个 reply 的总时限定时器能在完成、取消和销毁时正确停止。
- 基础设施不执行隐式自动重试，重试由具体业务服务按需求控制。

### TASK-007 实现登录、注销和会话服务

**目标**

完成认证接口、内存会话、令牌到期和统一会话失效处理。

**前置依赖**

TASK-006 已验收。

**实现内容**

- 实现 `AuthApiClient` 的登录和尽力注销请求。
- 实现 `SessionService`，唯一持有访问令牌和 MQTT 凭据。
- 每次登录生成新的 `sessionGeneration`，建立令牌到期计时器。
- 到期或业务接口 HTTP 401 时统一清理会话；不实现刷新令牌。
- 注销 5 秒超时或网络失败不得阻止本地退出。
- 登录成功直接移交完整会话给 `SessionService`，调用方只获得不含凭据的会话摘要。
- 认证 4xx/5xx 严格解析失败统一信封；当前会话代次的首次 401 触发一次失效，重复或旧代次 401 忽略。
- 注销请求先把当前 Token 移交给传输层，再立即清理本地会话，不等待响应。

**预计修改文件**

- `src/infrastructure/http/AuthApiClient.h`
- `src/infrastructure/http/AuthApiClient.cpp`
- `src/services/SessionService.h`
- `src/services/SessionService.cpp`
- `tests/http/tst_auth_api_client.cpp`
- `tests/services/tst_session_service.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 测试登录请求 DTO、成功会话、401/403/5xx、协议错误和 10 秒策略。
- 测试两个到期时间不一致时拒绝登录。
- 测试会话代次递增、旧回调失效、到期退出和敏感字符串清理。
- 测试代次 `0` 登录上下文、非敏感会话摘要、重复/旧代次 401 和注销失败不阻塞本地清理。

**验收标准**

- 密码、Token 和 MQTT 凭据只存在于会话相关内存对象，不写配置或日志。
- 任何 HTTP 401 只触发一次统一退出流程。
- 注销失败后本地会话仍能及时清理。

### TASK-008 实现登录界面和应用入口流程

**目标**

交付可用的登录窗口，并将配置、日志、认证和会话连接到应用入口。

**前置依赖**

TASK-007 已验收。

**实现内容**

- 实现固定 `520×420`、带工业蓝自绘标题栏的登录窗口，包含居中 Logo 标题、左对齐图标表单和靠下的登录按钮。
- 密码使用掩码，回车提交，提交期间禁用重复提交。
- 仅通过 `QSettings` 记住上次用户名。
- 实现 `Application`/`AppController` 的启动配置检查、登录成功和登录失败流程。
- 登录成功后通过可替换窗口工厂立即创建认证后窗口，不等待 MQTT；TASK-008 默认实现仅为最小空白承接窗口，TASK-009 再替换为正式 `MainWindow`。

**预计修改文件**

- `src/app/Application.h`
- `src/app/Application.cpp`
- `src/app/AppController.h`
- `src/app/AppController.cpp`
- `src/ui/windows/LoginWindow.h`
- `src/ui/windows/LoginWindow.cpp`
- `src/ui/resources/plate_client.qrc`
- `src/ui/resources/images/login/login_logo.png`
- `src/ui/resources/images/login/login_user.png`
- `src/ui/resources/images/login/login_pwd.png`
- `src/app/main.cpp`
- `tests/ui/tst_login_window.cpp`
- `tests/app/tst_app_controller_login.cpp`
- `tests/app/tst_application_startup.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 使用假认证响应测试回车提交、重复提交保护、错误展示和登录成功信号。
- 验证资源加载、标题栏控制、表单对齐、按钮稳定，以及 100%/150%/200% DPI 下无重叠或截断。
- 重启测试设置环境，确认只保存用户名，不保存密码或凭据。
- 测试非法引导配置阻断登录流程。

**验收标准**

- 登录页控件顺序、标题和密码行为符合 FR-010 至 FR-012。
- 自绘标题栏可拖动、最小化和关闭，三张 PNG 从 Qt 资源路径加载，布局在目标 DPI 下保持稳定。
- 登录过程不阻塞 UI，失败后可重新提交。
- 登录成功不依赖 MQTT 连接结果。
- 默认承接窗口不包含 TASK-009 的导航、连接状态或页面外壳。

### TASK-009 实现主窗口、导航和页面外壳

**目标**

建立最大化主窗口、左侧导航、连接状态区域和右侧页面容器。

**前置依赖**

TASK-008 已验收。

**实现内容**

- 实现 `MainWindow`、`NavigationSidebar` 和页面切换容器。
- 导航顺序为实时监控、视频识别、摄像头识别、日志记录、黑白名单管理。
- 视频识别和摄像头识别可见、置灰且不可点击，不创建业务页面。
- 左侧顶部显示大标题“车牌识别系统”。
- 左侧底部显示默认头像、用户名、退出按钮、HTTP/MQTT 状态、版本和构建日期。
- 顶部使用 `44px` Qt 自绘标题栏，背景从 `#2A5B78` 到 `#3C7898` 横向渐变，标题为纯白；保留最小化、最大化/还原、关闭、拖动、双击切换和普通状态边缘缩放。
- 主窗口首次显示调用 `showMaximized()`，布局适配 1366x768 至 4K 和 100%-200% DPI。

**预计修改文件**

- `src/ui/windows/MainWindow.h`
- `src/ui/windows/MainWindow.cpp`
- `src/ui/widgets/NavigationSidebar.h`
- `src/ui/widgets/NavigationSidebar.cpp`
- `src/ui/widgets/ConnectionStatusIndicator.h`
- `src/ui/widgets/ConnectionStatusIndicator.cpp`
- `tests/ui/tst_main_window_shell.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 验证导航顺序、禁用项、用户信息、两个独立状态控件和版本信息。
- 验证可用导航项只切换对应页面索引，禁用项不触发切换。
- 渲染标题栏并验证渐变端点、纯白标题、固定高度及窗口控制状态。
- 在代表性窗口尺寸和 DPI 设置下检查控件无重叠或截断。

**验收标准**

- 主窗口默认最大化，HTTP 与 MQTT 状态不会互相覆盖。
- 渐变标题栏在目标 DPI 下正确显示，窗口移动、最大化/还原、最小化、关闭和普通状态缩放可用。
- 未开发菜单严格不可操作。
- QWidget 只负责显示和交互，不拼装协议请求。

### TASK-010 实现心跳和 HTTP 状态

**目标**

实现登录后的 30 秒心跳和“检测中/在线/异常”状态机。

**前置依赖**

TASK-009 已验收。

**实现内容**

- 实现 `HeartbeatService`，登录后启动，退出时停止。
- 心跳请求包含 `clientId` 和 `appVersion`，使用 5 秒总时限。
- 首次结果前为“检测中”；连续 3 次连接失败或超时后为“异常”。
- 任意合法 HTTP 响应恢复“在线”并清零连续连接失败计数。
- 业务 4xx/5xx 表示 HTTP 可访问，但保留对应业务错误；401 触发统一退出。

**预计修改文件**

- `src/services/HeartbeatService.h`
- `src/services/HeartbeatService.cpp`
- `src/infrastructure/http/HeartbeatApiClient.h`
- `src/infrastructure/http/HeartbeatApiClient.cpp`
- `src/ui/widgets/ConnectionStatusIndicator.*`
- `src/ui/widgets/NavigationSidebar.*`
- `tests/services/tst_heartbeat_service.cpp`
- `tests/ui/tst_http_status_indicator.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 使用可控时钟测试 30 秒周期、5 秒策略和连续失败计数。
- 覆盖成功、4xx、5xx、连接失败、超时、401 和后续恢复。
- 验证 HTTP 异常时页面操作不会被预先禁用。

**验收标准**

- 状态转换与 FR-020 至 FR-028 完全一致。
- 心跳不会并发重复发送，也不会在退出后继续回调 UI。
- HTTP 状态不声称 MySQL 或模型健康。

### TASK-011 实现 MQTT 连接、订阅和错误状态机

**目标**

使用 Qt MQTT 建立管理事件订阅，正确区分暂时错误和永久错误。

**前置依赖**

TASK-010 已验收。

**实现内容**

- 实现 `MqttRecognitionSubscriber`，从 `SessionService` 获取当前会话 MQTT 配置。
- 固定 MQTT 3.1.1、QoS 1、非 retain、`cleanSession=true`，订阅登录下发的普通主题。
- 只有 CONNECT 和订阅均成功后显示“已连接”。
- 网络中断和 Broker 暂不可用按 1、2、4 秒递增到 60 秒上限重连。
- 认证、授权、Client ID、协议、订阅和 TLS 证书错误停止当前会话自动重连，显示“未连接”和脱敏原因。
- 永久错误保留 HTTP 会话；新登录会话才重置 MQTT 永久错误。

**预计修改文件**

- `src/infrastructure/mqtt/MqttRecognitionSubscriber.h`
- `src/infrastructure/mqtt/MqttRecognitionSubscriber.cpp`
- `src/infrastructure/mqtt/MqttError.h`
- `src/ui/widgets/ConnectionStatusIndicator.*`
- `src/ui/widgets/NavigationSidebar.*`
- `tests/support/MockMqttClient.h`
- `tests/mqtt/tst_mqtt_subscriber.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 模拟连接成功但订阅失败，确认从未进入“已连接”。
- 覆盖网络断开、Broker 不可用及全部永久错误类别。
- 使用可控时钟验证退避序列上限、退出取消和新会话重置。

**验收标准**

- 暂时错误持续退避重连，永久错误不产生重连循环。
- TLS 错误不能被忽略或降级为明文。
- MQTT 错误不改变 HTTP 状态或强制退出有效 HTTP 会话。

### TASK-012 实现识别事件解析和实时会话存储

**目标**

将 MQTT 完整快照转换为领域对象，并维护当前车辆和最近 100 条实时记录。

**前置依赖**

TASK-011 已验收。

**实现内容**

- 实现 `RecognitionEventCodec`，复用 `RecognitionSnapshot` 契约校验。
- 非法 JSON、缺字段、错误类型、未知 schema 或非法状态组合只写脱敏技术日志并丢弃。
- 实现 `RealtimeRecognitionStore`，按 `recognitionId + revision` 去重和覆盖。
- 最新 `PROCESSING` 成为当前车辆；旧车辆最终事件只更新其列表记录。
- 实时列表只保留本次登录期间最近 100 条，退出时清空。

**预计修改文件**

- `src/infrastructure/mqtt/RecognitionEventCodec.h`
- `src/infrastructure/mqtt/RecognitionEventCodec.cpp`
- `src/services/RealtimeRecognitionStore.h`
- `src/services/RealtimeRecognitionStore.cpp`
- `tests/mqtt/tst_recognition_event_codec.cpp`
- `tests/services/tst_realtime_store.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖三种状态、重复 revision、低 revision、乱序事件和完整最终快照。
- 连续输入 101 条记录，验证只保留最新 100 条。
- 验证旧记录完成不会替换更新的当前 `PROCESSING` 记录。

**验收标准**

- 非法事件不会进入存储或更新 UI，日志不包含完整 MQTT 载荷。
- 同一记录只占一个列表项，状态不会回退。
- 实时存储无磁盘持久化且不承担断线历史补齐。

### TASK-013 实现识别详情查询和重连校准

**目标**

在 MQTT 重连后只校准当前仍为 `PROCESSING` 的记录。

**前置依赖**

TASK-012 已验收。

**实现内容**

- 在 `RecognitionApiClient` 中实现 `GET /api/v1/recognitions/{recognitionId}`。
- 使用 10 秒总时限、统一信封和 `RecognitionSnapshot` DTO，不自动循环重试。
- 实现 `ReconnectReconciler`，仅在重新连接并订阅成功后检查当前记录。
- 查询结果按 revision 合并；失败保留当前状态。
- HTTP 从“异常”首次恢复“在线”时，若当前仍在处理，只补偿查询一次。
- 重连周期绑定订阅成功时的当前 `PROCESSING` ID；新周期、当前记录变化、会话失效或销毁时取消旧请求。
- 响应合并前复核会话代次、请求代次、活动句柄、当前 ID 和响应 ID；活动校准响应自身导致的 HTTP 在线状态不得触发立即二次查询。

**预计修改文件**

- `src/infrastructure/http/RecognitionApiClient.h`
- `src/infrastructure/http/RecognitionApiClient.cpp`
- `src/services/ReconnectReconciler.h`
- `src/services/ReconnectReconciler.cpp`
- `tests/http/tst_recognition_api_detail.cpp`
- `tests/services/tst_reconnect_reconciler.cpp`
- `src/app/Application.h`
- `src/app/Application.cpp`
- `tests/app/tst_application_startup.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖当前为空、当前已完成、当前处理中以及查询返回更高/相同/更低 revision。
- 验证查询失败不循环；HTTP 恢复时只补偿一次。
- 验证校准不加载其他断线历史记录。
- 验证新周期、当前记录变化、会话失效和销毁会取消旧请求，旧回调不能合并。
- 验证 `Application` 在登录会话、MQTT 订阅、HTTP 状态和实时 Store 之间完成协调器组合。

**验收标准**

- 只有连接并订阅成功才触发校准。
- 最终查询结果能更新当前面板数据和对应实时列表项。
- 旧会话、旧请求或不匹配 `recognitionId` 的结果被丢弃。

### TASK-014 实现安全图片下载和异步解码

**目标**

按当前 `recognitionId` 下载车辆图片，在限制内异步解码并安全返回 UI。

**前置依赖**

TASK-013 已验收。

**实现内容**

- 实现固定图片接口下载，不接受 MQTT 载荷中的任意 URL；图片请求强制 `AlwaysNetwork`、设置 `CacheSaveControl=false`，且生产代码不创建 `QNetworkDiskCache`。
- 每次 HTTP 尝试使用独立 30 秒总时限和 20 MiB 压缩响应体上限；`Content-Type` 去除首尾 ASCII 空白后按 ASCII 大小写不敏感匹配，只接受不带参数的精确 `image/jpeg` 或 `image/png`。
- 仅对连接失败、超时和 HTTP 500/502/503/504 按 1 秒、3 秒自动重试；手动重试建立新逻辑系列并重新获得最多三次机会，其他 5xx 和其他错误不自动重试。
- 在工作线程探测实际格式并用 `QImageReader::size()` 预检；实际格式只允许 JPEG/PNG，`jpg` 归一为 `jpeg` 后必须与声明 MIME 一致，再校验 8192 单边和使用 64 位乘法计算的 4000 万总像素限制，解码为 `QImage` 后二次校验。
- 将只读 `QImage` 和请求上下文通过 queued signal/slot 返回 UI 线程；服务本身不创建 `QPixmap`。
- 新系列、显式取消、会话失效或服务销毁时取消 reply 和待执行重试；已运行解码不强杀，晚到结果按 `sessionGeneration + requestGeneration + recognitionId` 失效丢弃。
- 最终失败返回占位状态和手动重试能力，不改变识别业务状态。

**预计修改文件**

- `src/services/ImageLoadService.h`
- `src/services/ImageLoadService.cpp`
- `src/infrastructure/http/RecognitionApiClient.*`
- `src/domain/ImageLoadResult.h`
- `tests/services/tst_image_load_service.cpp`
- `tests/services/tst_image_decode_limits.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖 20 MiB 边界、Content-Type 大小写/参数/缺失/错误、声明与实际格式不一致、无效图片、8192 单边、4000 万像素和 64 位乘法安全。
- 验证前两次失败第三次成功、500/502/503/504 可重试、其他 5xx 和不可重试错误不重试、手动重试获得新三次系列，以及新系列、会话失效、显式取消和销毁取消旧图片与待执行定时器。
- 验证已运行解码的晚到结果按会话代次、逻辑请求代次和识别 ID 丢弃，并确认图片请求不读取或保存磁盘网络缓存。
- 记录线程身份，确认 `QImage` 在线程池解码；使用测试 UI 接收器确认 `QPixmap` 只在 UI 线程创建。

**验收标准**

- 超限图片在调用 `QImageReader::read()` 前被拒绝，解码后尺寸仍会二次检查。
- 图片字节和图片对象只保存在内存，不产生下载文件或磁盘缓存。
- 旧图片、旧会话和已取消结果均不能覆盖当前图片。

### TASK-015 实时监控页面闭环

**目标**

将 MQTT、实时存储、图片服务和 UI 组合成完整实时监控页面。

**前置依赖**

TASK-014 已验收。

**实现内容**

- 实现 `RealtimePage` 和只读 `RecognitionTableModel`。
- 页面显示居中大标题“车牌识别系统”、当前图片、识别状态、结果、错误、识别时间和耗时。
- 在 UI 线程将 `ImageLoadService` 返回的 `QImage` 转为 `QPixmap` 并保持宽高比显示。
- `RecognitionTableModel` 只读映射 `RealtimeRecognitionStore::items`；`RealtimePage` 只订阅 Store 和图片服务信号，不直接解析 MQTT 或构造 HTTP 请求。
- 无记录显示“未启动识别”，处理中显示“识别中”，最终状态显示“已识别完成”。
- 表格显示序号、设备、车牌号、状态和时间，成功与失败均展示；表格和当前记录时间统一使用 `capturedAt`，耗时使用 `durationMs`。
- 新的当前 `PROCESSING` ID 取消旧图片系列并发起新图片；当前记录变为空时取消加载并清空图片。
- 当前同一 ID 的最终事件只更新状态并保留图片；旧记录完成只刷新列表，不抢占当前记录或图片区。
- 图片结果到达页面后再次核对 Store 当前 `recognitionId`，只有匹配时才在 UI 线程创建 `QPixmap`；页面销毁或会话清空时取消当前图片系列。
- 图片失败显示占位和手动重试按钮，MQTT 最终事件仍能即时更新。
- `Application` 持有 `ImageLoadService`，其依赖先创建且后销毁，controller/window 后创建且先销毁；默认窗口工厂把同一 Store 和图片服务注入 `MainWindow`/`RealtimePage`。

**预计修改文件**

- `src/ui/pages/RealtimePage.h`
- `src/ui/pages/RealtimePage.cpp`
- `src/ui/models/RecognitionTableModel.h`
- `src/ui/models/RecognitionTableModel.cpp`
- `src/ui/windows/MainWindow.*`
- `src/app/Application.h`
- `src/app/Application.cpp`
- `tests/ui/tst_realtime_page.cpp`
- `tests/ui/tst_recognition_table_model.cpp`
- `tests/app/tst_application_startup.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 驱动 Store 信号测试无记录、处理中、成功、失败和耗时显示。
- 模拟图片慢于新车辆返回，验证旧图片不覆盖。
- 模拟图片重试期间收到最终事件，验证结果更新不被阻塞。
- 覆盖新当前 ID、同 ID 最终、旧记录最终、当前清空、晚到图片二次 ID 校验，以及 `capturedAt`/`durationMs` 映射。
- 验证页面销毁和会话清空取消图片系列，并验证 `Application` 将同一 Store 与图片服务组合到默认 `MainWindow`/`RealtimePage` 且销毁顺序安全。

**验收标准**

- 页面行为满足 FR-040 至 FR-054。
- 表格稳定限制为 100 条，动态数据不会改变固定布局尺寸。
- 页面不直接解析 MQTT 或构造 HTTP 请求。

### TASK-016 实现历史日志查询页面

**目标**

从 Linux 服务端分页查询历史识别日志，并支持时间和设备筛选。

**前置依赖**

TASK-015 已验收。

**实现内容**

- 在 `RecognitionApiClient` 实现历史查询：时间固定使用 `+08:00` 和 `[startTime, endTime)` 半开区间，可选 `deviceId` 自由文本提交时首尾去空、为空则省略，页码从 1 开始且 `pageSize` 固定为 100。
- 历史成功响应的 `page` 必须等于请求 `page`，否则返回协议错误并保留原结果；请求使用 10 秒总时限且不自动重试。
- 实现 `HistoryService`，保存不可变筛选快照和递增 `requestGeneration`。新查询先取消旧请求，完成时同时验证 `sessionGeneration`、`requestGeneration`、活动请求句柄和筛选快照。
- 实现 `HistoryPage` 的默认北京时间当天 `00:00:00+08:00` 至次日 `00:00:00+08:00`、设备筛选、搜索、重置、上一页和下一页。搜索和重置回到第一页，重置后立即查询；翻页只使用最后已提交筛选，未搜索的编辑值不生效。
- 历史页首次显示以及每次切离后重新显示时查询或刷新最后已提交条件，切离页面取消在途请求。
- 表格固定显示十列：记录 ID、设备、车牌号、状态、错误码、错误信息、拍摄时间、开始时间、完成时间和耗时。
- 取消和旧上下文失效不显示错误；其他查询失败保留当前表格和分页状态并显示非阻断错误。HTTP 状态异常时查询仍可提交。
- `Application` 持有 `HistoryService` 并注入正式 `MainWindow`/`HistoryPage`，对象声明和销毁顺序保证窗口先于历史服务销毁，历史服务先于 HTTP、识别 API 和会话依赖销毁。
- TASK-016 不实现 CSV 下载、导出控件或 TASK-017 任何内容。

**预计修改文件**

- `src/infrastructure/http/RecognitionApiClient.*`
- `src/services/HistoryService.h`
- `src/services/HistoryService.cpp`
- `src/ui/pages/HistoryPage.h`
- `src/ui/pages/HistoryPage.cpp`
- `src/ui/models/RecognitionTableModel.*`
- `src/ui/windows/MainWindow.*`
- `src/app/Application.h`
- `src/app/Application.cpp`
- `tests/http/tst_recognition_api_history.cpp`
- `tests/services/tst_history_service.cpp`
- `tests/ui/tst_history_page.cpp`
- `tests/ui/tst_recognition_table_model.cpp`
- `tests/ui/tst_main_window_shell.cpp`
- `tests/app/tst_application_startup.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- HTTP API 覆盖固定 `+08:00`、半开区间、设备首尾去空及省略、URL 编码、固定 `pageSize=100`、Bearer Token、响应页不匹配、严格信封、4xx、5xx、10 秒超时、401、取消和无重试。
- 服务覆盖默认日期、不可变筛选、总数、第一页/末页、空结果，以及新查询、切离页面、会话失效和销毁时取消在途请求。
- 延迟返回多次快速筛选响应，验证只有同时匹配会话代次、请求代次、活动句柄和筛选快照的最后响应更新 UI。
- 页面覆盖搜索/重置回到第一页、重置立即查询、翻页忽略未提交编辑值、首次及重新显示刷新、十列表格、分页按钮和失败保留旧表格。
- Application 启动测试覆盖 `HistoryService` 的持有、注入和销毁安全；MainWindow 与表格模型回归保持实时页面既有行为。

**验收标准**

- 历史数据全部来自 HTTP，不创建本地业务副本或 SQLite。
- 页码、总数和按钮状态与通过页码匹配校验的服务端 `PageResult` 一致。
- HTTP 状态异常时查询按钮仍可使用，结果按实际响应处理。
- TASK-016 范围中不存在 CSV 请求、保存或导出 UI。

### TASK-017 实现 CSV 下载和原子保存

**目标**

下载服务端基于当前筛选生成的 CSV，并安全保存为完整文件。

**前置依赖**

TASK-016 已验收。

**实现内容**

- 在 `RecognitionApiClient` 实现 CSV 导出请求，固定复用 `HistoryService` 最后已提交的 `startTime`、`endTime` 和可选 `deviceId` 筛选快照，忽略 `page`、`pageSize` 和未搜索的编辑值。
- 只为响应开头保留有界小缓冲，严格校验唯一且语义精确的 `text/csv; charset=utf-8`、字节 `EF BB BF`、固定十列表头和 CRLF 后逐块转发；Content-Type 的 ASCII 大小写及参数周围可选空白可归一，重复头、重复或未知参数和非 `utf-8` 字符集必须拒绝，不累计正文。
- `HistoryPage` 负责保存对话框，默认文件名为 `车牌识别记录_yyyyMMdd_HHmmss.csv`、默认后缀为 `csv`；取消对话框时不得创建文件或发起请求。
- 实现 `CsvDownloadService`。先成功打开禁用 direct-write fallback 的 `QSaveFile`，再发起 120 秒流式请求；数据块增量写入，不把完整文件载入内存。
- 同一时刻只允许一个导出。下载中只禁用导出按钮，搜索、翻页和导航继续可用；页面隐藏不取消已经确认的导出，首版不增加独立取消按钮。
- 原子 `commit()` 前同时校验活动句柄、`sessionGeneration`、递增的 `requestGeneration` 和不可变筛选快照；显式取消、会话失效、退出关闭、服务析构或任何下载、协议、写入和提交失败均取消写入。
- 失败时原目标不存在则不得产生目标文件，原目标存在则保留原内容，并且不得留下 `QSaveFile` 临时文件。CSV 下载不自动重试，失败不改变历史表格。
- `Application` 持有并注入同一 `CsvDownloadService`；对象顺序保证 controller/window、CSV 服务、HistoryService、RecognitionApi/Http/Session 依赖依次销毁。现有 `HttpClient` 的 CSV 流式模式和 `HistoryService` 的最后提交筛选接口直接复用，无需修改。

**预计修改文件**

- `README.md`
- `docs/REQ-001.md`
- `docs/REQ-001-design.md`
- `docs/REQ-001-tasks.md`
- `src/services/CsvDownloadService.h`
- `src/services/CsvDownloadService.cpp`
- `src/infrastructure/http/RecognitionApiClient.*`
- `src/ui/pages/HistoryPage.*`
- `src/ui/windows/MainWindow.*`
- `src/app/Application.h`
- `src/app/Application.cpp`
- `tests/http/tst_recognition_api_csv.cpp`
- `tests/services/tst_csv_download_service.cpp`
- `tests/ui/tst_history_export.cpp`
- `tests/ui/tst_main_window_shell.cpp`
- `tests/app/tst_application_startup.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- API Mock 覆盖三个筛选字段及 URL 编码、无分页参数、Bearer Token、120 秒策略、分块转发、Content-Type 归一与拒绝边界、BOM、固定表头、CRLF、严格 4xx/5xx 信封、401、TLS、网络、超时、取消、重定向和无重试。
- 使用大于单个网络块的固定 CSV，验证保存字节完全一致且 API、服务均不累计完整正文。
- 覆盖保存对话框取消零请求、默认文件名/后缀、一次只允许一个导出、最后已提交筛选、未搜索编辑值忽略、下载期间搜索和翻页仍可用，以及页面隐藏不取消已确认导出。
- 覆盖显式取消、会话失效、退出关闭、服务析构、目标路径错误、写入失败和提交失败；验证不存在的目标仍不存在、已有目标内容不变并且均无临时文件残留。
- 验证 `Application` 持有并注入同一 CSV 服务，窗口、CSV 服务、HistoryService 和共同依赖按规定顺序安全销毁。

**验收标准**

- 导出请求与历史页最后已提交的非分页筛选完全一致，不使用未搜索编辑值或当前表格拼装 CSV。
- 文件响应只接受规定 Content-Type、BOM、十个中文列名和 CRLF，正文由服务端生成并流式完整保存。
- 下载期间 UI 保持响应且页面隐藏不打断导出；显式取消、会话失效、退出关闭和失败不改变历史表格。
- 原子保存不会以不完整内容新建或覆盖目标，失败保留既有文件且无临时残留。

### TASK-018 实现黑白名单 API 和业务服务

**目标**

实现名单分页、精确查询、新增、删除、规范化和超时结果核实。

**前置依赖**

TASK-017 已验收。

**实现内容**

- 实现 `AccessListApiClient` 的分页、精确查询、POST 和 DELETE。
- 实现 `AccessListService` 的标签、关键词、分页和请求代次管理。
- 规范化规则为首尾去空白、英文转大写、拒绝内部空白、长度 1 至 16。
- 新增和删除写请求串行化，不做乐观本地修改，不自动重发。
- 写请求 10 秒超时后按规范化车牌精确查询，返回已生效、未生效或无法确认。
- 处理 401/403/404/409，并保留服务端最终校验权。

**预计修改文件**

- `src/infrastructure/http/AccessListApiClient.h`
- `src/infrastructure/http/AccessListApiClient.cpp`
- `src/services/AccessListService.h`
- `src/services/AccessListService.cpp`
- `tests/http/tst_access_list_api.cpp`
- `tests/services/tst_access_list_service.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖白/黑名单、车牌包含搜索、创建时间倒序分页和精确查询 404。
- 覆盖规范化、同名单重复、跨名单冲突和 200 字备注边界。
- 分别模拟新增/删除超时后服务端已提交、未提交及核实失败，并验证原写请求只发送一次。

**验收标准**

- 名单数据只来自 HTTP/MySQL，不创建本地业务缓存。
- 写入结果不明时不猜测成功或失败，严格按精确查询结果提示。
- 所有旧标签、旧关键词、旧页面和旧会话响应均不能更新当前数据。

### TASK-019 实现黑白名单管理页面

**目标**

交付可浏览、搜索、新增和删除黑白名单的完整管理页面。

**前置依赖**

TASK-018 已验收。

**实现内容**

- 实现 `AccessListPage`、`AccessListTableModel` 和新增对话框。
- 页面提供白名单/黑名单标签、搜索、重置、刷新、新增、删除和分页。
- 表格显示序号、车牌号、备注、创建用户和创建时间。
- 新增继承当前标签，提交期间防重复；成功或核实已生效后刷新当前页。
- 删除前显示名单类型和车牌号；成功后处理空页回退。
- 409 显示现有名单类型，404 删除视为已不存在并刷新。

**预计修改文件**

- `src/ui/pages/AccessListPage.h`
- `src/ui/pages/AccessListPage.cpp`
- `src/ui/models/AccessListTableModel.h`
- `src/ui/models/AccessListTableModel.cpp`
- `src/ui/widgets/AccessListCreateDialog.h`
- `src/ui/widgets/AccessListCreateDialog.cpp`
- `src/ui/windows/MainWindow.*`
- `tests/ui/tst_access_list_page.cpp`
- `tests/ui/tst_access_list_dialog.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 覆盖标签切换、搜索触发、重置、翻页、刷新及快速操作的旧响应隔离。
- 覆盖新增校验、重复提交保护、409、删除确认、404 和空页回退。
- 覆盖超时核实的三种 UI 结果，不通过本地插入/删除伪造成功。

**验收标准**

- 页面满足 FR-070 至 FR-087，所有登录用户可操作且无角色 UI。
- 不提供编辑、批量删除、有效期、车主或跨名单自动转移。
- 名单页面不影响实时监控，也不发布 MQTT 变更。

### TASK-020 完善退出、关闭和异步销毁安全

**目标**

统一处理退出登录、窗口关闭、令牌到期和 HTTP 401，确保所有异步回调安全失效。

**前置依赖**

TASK-019 已验收。

**实现内容**

- 按设计顺序实现 `LoggingOut`：递增会话代次、停止定时器、取消 reply 和图片重试、尽力注销、断开 MQTT、清理数据、销毁窗口。
- 点击退出返回登录页；关闭主窗口执行同等清理后结束进程，不进入托盘。
- 清空实时列表、历史/名单模型、当前图片、Token 和 MQTT 凭据。
- 所有连接使用 QObject 上下文或 `QPointer`，跨线程结果使用 queued connection。
- 已取消或无法取消的旧回调不得访问已销毁 QWidget。

**预计修改文件**

- `src/app/AppController.*`
- `src/services/SessionService.*`
- `src/ui/windows/MainWindow.*`
- `src/infrastructure/http/HttpClient.*`
- `src/infrastructure/mqtt/MqttRecognitionSubscriber.*`
- `src/services/ImageLoadService.*`
- `tests/app/tst_logout_lifecycle.cpp`
- `tests/app/tst_async_destruction.cpp`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`

**独立测试**

- 在登录、心跳、查询、CSV、图片解码、MQTT 重连和名单写入各阶段触发退出。
- 人为延迟所有回调到窗口销毁后，验证无崩溃、无 UI 更新和无凭据残留。
- 验证注销超时不延迟本地退出，重复退出事件只执行一次清理。

**验收标准**

- FR-093 至 FR-095 和设计第 16 章的清理顺序全部满足。
- 退出后显示干净登录页，关闭后无活动重连或心跳定时器。
- 内存模型和图片均被清空，不生成本地业务文件。

### TASK-021 完成通信与业务端到端集成测试

**目标**

用可控 HTTP/MQTT 环境验证登录到实时结果、历史、CSV 和名单的完整闭环。

**前置依赖**

TASK-020 已验收。

**实现内容**

- 扩展统一 HTTP Mock 和 MQTT Mock，使其共享可控识别记录状态。
- 覆盖登录、心跳、连接订阅、`PROCESSING`、图片、`SUCCEEDED`/`FAILED` 和重连校准。
- 覆盖 HTTP/MQTT 独立故障、MQTT 永久错误保留 HTTP 功能和新会话恢复。
- 覆盖历史分页、固定 CSV、名单增删冲突及写入超时核实。
- 测试进程只使用临时配置、临时日志和临时导出目录。

**预计修改文件**

- `tests/integration/MockPlateServer.h`
- `tests/integration/MockPlateServer.cpp`
- `tests/integration/tst_realtime_flow.cpp`
- `tests/integration/tst_history_and_csv_flow.cpp`
- `tests/integration/tst_access_list_flow.cpp`
- `tests/integration/tst_independent_connectivity.cpp`
- `tests/CMakeLists.txt`

**独立测试**

- 单独运行每个集成测试目标并执行完整 CTest 回归。
- 对每个场景使用确定性事件顺序和超时，不依赖真实时间长等待。
- 检查临时目录，确认无 SQLite、车辆图片或不完整 CSV 残留。

**验收标准**

- AC-001 至 AC-015、AC-017 至 AC-020 均有自动化场景并通过。
- 测试失败能明确定位 HTTP、MQTT、领域、服务或 UI 层。
- 测试不访问生产服务、真实 Broker 或 MySQL。

### TASK-022 完成全量验收和开发文档收尾

**目标**

执行最终构建、功能、响应性、显示和安全验收，形成可复现的开发交付状态。

**前置依赖**

TASK-021 已验收。

**实现内容**

- 从干净构建目录执行 MinGW Debug 构建和全部测试。
- 增加每秒 1 条事件持续输入、100 条上限和 UI 响应性测试。
- 在 1366x768、1920x1080、4K 及 Windows 100%-200% DPI 检查登录、实时、历史和名单页面。
- 审计可执行程序运行目录和用户数据目录，确认无 SQLite、MySQL 驱动、车辆图片缓存或凭据日志。
- 核对需求追踪和 AC-001 至 AC-020；只把实际验证通过的命令、环境和已知限制写入 README。

**预计修改文件**

- `tests/performance/tst_realtime_throughput.cpp`
- `tests/ui/tst_responsive_layout.cpp`
- `tests/security/tst_local_data_boundary.cpp`
- `tests/CMakeLists.txt`
- `README.md`
- `docs/REQ-001-tasks.md`
- 根据验收缺陷定向修改的既有源文件和测试文件

**独立测试**

- 执行全部单元、组件、集成和端到端测试。
- 执行 AC-016 的每秒 1 条事件测试并监测列表上限和事件循环响应。
- 按目标分辨率和 DPI 完成 UI 检查，记录结果。

**验收标准**

- AC-001 至 AC-020 全部通过，所有自动化测试无失败。
- 所有页面在目标分辨率和 DPI 下无控件重叠、文本截断或不可达操作。
- README 中的构建和测试步骤可从干净环境重复执行。
- 首版范围外功能未被实现，所有本地数据和安全边界均保持成立。

## 4. 需求覆盖

| 需求区域 | 主要实施任务 | 最终验证任务 |
| --- | --- | --- |
| NFR-001 至 NFR-003、FR-005 | TASK-001、TASK-002 | TASK-022 |
| FR-001 至 FR-004 | TASK-003 | TASK-021、TASK-022 |
| FR-010 至 FR-017 | TASK-007、TASK-008、TASK-020 | TASK-021、TASK-022 |
| FR-020 至 FR-028 | TASK-010 | TASK-021、TASK-022 |
| FR-030 至 FR-039 | TASK-011、TASK-013 | TASK-021、TASK-022 |
| FR-040 至 FR-054 | TASK-012 至 TASK-015 | TASK-021、TASK-022 |
| FR-060 至 FR-067 | TASK-016、TASK-017 | TASK-021、TASK-022 |
| FR-070 至 FR-087 | TASK-018、TASK-019 | TASK-021、TASK-022 |
| FR-090 至 FR-095 | TASK-009、TASK-020 | TASK-021、TASK-022 |
| NFR-004 至 NFR-007、NFR-010 至 NFR-016 | TASK-002 至 TASK-020 中对应模块 | TASK-021、TASK-022 |
| AC-001 至 AC-020 | TASK-001 至 TASK-020 中对应功能 | TASK-021、TASK-022 |

验收标准逐项落点如下：

| 验收标准 | 主要实施任务 |
| --- | --- |
| AC-001 | TASK-007、TASK-009、TASK-011 |
| AC-002 | TASK-010 |
| AC-003 | TASK-012、TASK-014、TASK-015 |
| AC-004 | TASK-012、TASK-015 |
| AC-005 | TASK-014、TASK-015 |
| AC-006 | TASK-011、TASK-013 |
| AC-007 | TASK-014 |
| AC-008 | TASK-014、TASK-015 |
| AC-009 | TASK-016 |
| AC-010 | TASK-017 |
| AC-011 | TASK-018、TASK-019 |
| AC-012 | TASK-018、TASK-019 |
| AC-013 | TASK-018、TASK-019 |
| AC-014 | TASK-020 |
| AC-015 | TASK-003、TASK-014、TASK-016 至 TASK-018、TASK-022 |
| AC-016 | TASK-012、TASK-015、TASK-022 |
| AC-017 | TASK-005、TASK-006 |
| AC-018 | TASK-006、TASK-007、TASK-010、TASK-013、TASK-014、TASK-016 至 TASK-018 |
| AC-019 | TASK-011 |
| AC-020 | TASK-014 |

## 5. 完成定义

只有 `TASK-001` 至 `TASK-022` 按顺序全部验收后，REQ-001 客户端首版开发任务才视为完成。任何任务失败时必须留在当前任务修复，不得提前启动后续任务，也不得通过并行分支绕过前置依赖。
