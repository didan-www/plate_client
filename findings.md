# 发现与决策

## 需求

- 输出 `docs/REQ-001-tasks.md`。
- 每个任务必须可独立实现和测试。
- 每个任务必须标注前置依赖、预计修改文件和验收标准。
- 所有任务必须串行执行，不安排并发工作。
- 本次仅实现和验收 `TASK-001`，不开始 `TASK-002` 或任何业务代码。
- 需提供 Qt MQTT `v6.11.1` 的 PowerShell 引导脚本，安装到 `.deps/install/qtmqtt`，不修改全局 Qt。
- 必须验证首次安装、幂等重运行、CMake 包探测和错误非零退出。
- 本次仅实现和验收 `TASK-002`，不得提前实现 `TASK-003` 引导配置或后续业务。
- 应用首版版本由用户确认为 `0.1.1`。
- 必须建立可配置、编译、启动、CTest 可发现并运行 Qt Test 的最小 Widgets 工程。
- 本次仅实现和验收 `TASK-003`，不得提前实现 `TASK-004` 日志或后续业务。
- 用户确认 `loginBaseUrl` 可包含路径前缀，但必须拒绝查询参数、片段和用户信息。
- 用户确认配置严格只允许 `clientId`、`loginBaseUrl`、`allowInsecureTransport`，未知字段必须拒绝。
- 本次仅实现和验收 `TASK-004`，不得提前实现 `TASK-005` 领域 DTO 或后续业务。
- 用户确认技术日志采用 UTF-8 JSON Lines，文件名为 `plate-client-YYYY-MM-DD.log`，字段固定为时间、级别、模块、错误码、请求 ID、可选识别 ID 和上下文。
- 用户确认只删除日期早于“当前本地日期减 7 天”的同格式日志；恰好 7 天前保留，其他文件不处理。
- 用户确认按敏感键名和文本模式统一脱敏，并拒绝记录完整 HTTP/MQTT 响应、完整 MQTT 载荷和图片内容。

## 研究发现

- 当前仓库只有 README、AI 约束、需求和架构设计，尚无 CMake 工程和 C++ 源码。
- 技术基线是 Windows 64-bit、C++17、Qt 6.11.1 Desktop MinGW 64-bit、CMake、Ninja 和 Qt Test。
- 架构是单进程事件驱动模块化单体，核心边界包括配置、领域 DTO、HTTP、会话、MQTT、实时存储、图片、历史、CSV、名单和 Widgets UI。
- HTTP 和 MQTT 使用 Qt 异步 API；图片 `QImage` 解码在线程池，`QPixmap` 只在 UI 线程创建。
- SQLite、本地业务缓存、直接 MySQL、客户端识别和客户端 MQTT 结果发布均明确禁止。
- 验收必须覆盖 DTO、超时、MQTT 永久错误、图片限制、旧响应隔离、名单写入核实和退出销毁安全。
- 2026-08-11 TASK-001 环境检查：`D:\Qt\6.11.1\mingw_64` 当前不存在。
- `D:\Qt\Tools\mingw1310_64` 存在，`g++.exe` 版本为 13.1.0（x86_64-posix-seh）。
- `D:\Qt\Tools\CMake_64` 和 `D:\Qt\Tools\Ninja` 存在，但 `cmake`/`ninja` 未加入当前 `PATH`；引导脚本不应依赖用户预先修改 `PATH`。
- Git 2.53.0 可用，但项目目录不是 Git 工作树。
- `D:\Qt` 下只发现 Qt 6.10.2 MinGW Kit，未发现任何其他 `qmake.exe`。
- Qt MQTT 官方仓库 `https://code.qt.io/qt/qtmqtt.git` 的 `v6.11.1` 是附注标签：标签对象为 `8d3b643deab4b849724221e711030afdc7afd676`，指向提交 `53d13cb6c11c0d8e4451284c391e48f5efcec9b5`。
- Qt 6.10.2 对照 Kit 的 `QMAKE_XSPEC` 和 `QMAKE_SPEC` 均为 `win32-g++`，可作为脚本 ABI 检查字段。
- 本机显式工具版本为 CMake 3.30.5、Ninja 1.12.1、GCC 13.1.0，与固定基线一致。
- Qt 官方在 `download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_mingw/` 提供 Qt 6.11.1 MinGW 二进制包，可用于项目 `.deps` 内的临时验收 Kit，无需修改全局 `D:\Qt`。
- 当前机器没有 `7z`、`7za` 或 `aqt`命令，Python 3.11 可用。
- 用于临时验收的官方 `7zr.exe` 版本为 26.02，SHA-256 为 `56B8CC9F4971CEF253644FAFE54063ED7FDCA551D4DEE0F8C6BAA81B855ACD72`；文件未签名，仅保存在已忽略的 `.deps/tools` 中。
- Qt 6.11.1 MinGW `qtbase` 官方归档长度为 22,460,806 字节，其他 Qt 模块不是构建 Qt MQTT 的必要条件。
- Qt 官方 mirrorlist 返回 8 个 HTTPS 镜像；首批范围请求中 JAIST、Liquid Telecom 和 FUNET 可返回 HTTP 206，AARNet 连接超时，JAIST 暂时相对最快但仍不足 3 KB/s。
- Constant 镜像可稳定读取 Qt 归档；最终文件 SHA-1 为 `60F5E56C9BAA52941145B25E3FF40D017B08595B`，与 Qt 官方 `.sha1` 一致，7-Zip 测试 4,304 个文件均通过。
- 解压后的项目内 Qt Kit 正确报告版本 6.11.1、前缀 `.deps/qt/6.11.1/mingw_64`、`QMAKE_XSPEC=win32-g++`，并包含 `qt-cmake.bat`。
- Qt MQTT 首次构建安装成功，生成 CMake 包、`Qt6Mqtt.dll`、导入库、头文件和 SPDX SBOM；独立 CMake 探测成功创建 `Qt6::Mqtt`。
- 第二次运行在 845 ms 内返回已安装且有效，`Qt6Mqtt.dll` SHA-256 为 `53F41FC45A12648E693E2A67CAD924C41EFBAF2A72A3A994D88407BDA8BBB228`，哈希与时间戳均未改变。
- 使用 `.deps` 内最小假工具验证：GCC 12.2.0、i686 目标 ABI、CMake 3.29.0 和 Ninja 1.11.1 均被可读错误拒绝并返回退出码 1。
- 最终静态复核确认全局 `D:\Qt\6.11.1` 仍不存在，Qt 安装日志时间戳未变化；项目内 `Qt6Mqtt.dll` 为 `pei-x86-64`。
- CMake 包探测应同时执行最小消费者的编译和链接，才能验证 `Qt6::Mqtt` 导入库与 MinGW ABI，而不只是配置元数据。
- Qt MQTT 主要源码的 SPDX 标识为 `LicenseRef-Qt-Commercial OR GPL-3.0-only`；使用和分发时必须具有有效 Qt 商业许可或遵守 GPL-3.0-only，不能按普通 LGPL Qt 模块处理。
- 2026-08-12 正式 Qt `D:\Qt\6.11.1\mingw_64`、MinGW 13.1.0、CMake 3.30.5、Ninja 1.12.1 和项目本地 Qt MQTT 均已复验通过。
- 当前 Git 仓库没有已跟踪文件；现有项目文件均显示为未跟踪，实施时必须保留全部现有内容。
- 需求只规定 CMake 注入版本与构建日期；版本值原未定义，用户已明确为 `0.1.1`。
- TASK-002 冒烟测试通过直接使用对应类型，确保 Widgets、Network、Concurrent、Mqtt 和 Test 均真正进入配置/编译/链接闭环。
- 应用 `--smoke-test` 模式仍创建并显示窗口、进入 Qt 事件循环，只增加 100 ms 后自动退出，便于确定性启动验收。
- TASK-003 配置库只依赖 Qt Core，不创建 QWidget、`QNetworkAccessManager` 或 `QMqttClient`；程序目录入口固定解析 `config/client.json`。
- 所有失败结果均不携带部分配置对象，错误信息不回显配置值、未知字段名或文件内容。
- `config/client.json` 已忽略，仓库只保留不含凭据的 `client.json.example`；测试全部使用 `QTemporaryDir`。
- TASK-004 日志库只依赖 Qt Core；构造器执行目录准备与过期清理，`write()` 以单次追加写入一条紧凑 JSON 记录。
- 清理只匹配有效日期的 `plate-client-YYYY-MM-DD.log` 普通文件并排除符号链接；失败不传播到启动流程。
- TASK-004 只交付可初始化的日志组件；按任务串行边界，不提前接入尚未实现的网络、MQTT、DTO 或 UI 业务调用点。

## 技术决策

| 决策 | 理由 |
| --- | --- |
| 先搭建可测试工程，再逐步加入领域和基础设施模块 | 当前没有源码，后续任务需要稳定构建基线 |
| 每个任务依赖其前一个 TASK | 满足严格串行要求并避免依赖歧义 |
| 每个实现任务同时增加对应 Qt Test | 满足独立测试要求，避免测试集中到最后才补 |
| 最后保留集成与端到端任务 | 单元完成不代表业务闭环满足 REQ-001 |
| TASK-001 脚本接受可覆盖的工具和 Qt 路径 | 文档要求路径可通过参数覆盖，且 CMake/Ninja 未在 PATH |
| 仅为验收在 `.deps` 内准备 Qt 6.11.1 Kit | 全局固定 Qt 前缀缺失；项目内临时 Kit 不修改全局 Qt，且随 `.deps/` 统一忽略 |
| TASK-002 使用 CMake `project(VERSION 0.1.1)` 作为版本唯一来源 | 避免应用显示值和未来心跳 `appVersion` 分叉 |
| 构建日期采用 CMake 配置时本地日期 `YYYY-MM-DD` | 满足 CMake 注入要求，格式稳定且不涉及协议时间字段 |
| TASK-003 使用只读访问器的 `BootstrapConfig` 值对象 | 满足不可变值对象要求，不暴露字段修改入口 |
| 配置加载结果明确区分成功值和可展示错误 | 非法配置不产生部分对象，也不提供隐式服务器值 |
| TASK-004 日志依赖注入日志目录和当前日期 | 生产使用 `QStandardPaths`，测试使用临时目录和固定日期，不接触真实用户数据 |
| 日志以单行 JSON 追加并递归净化上下文 | 保持字段结构可测试，同时覆盖嵌套对象、数组和自由文本中的敏感信息 |

## 遇到的问题

| 问题 | 解决方案 |
| --- | --- |
| 无现有源码可引用具体文件 | 依据架构文档的建议目录列出预计创建文件，允许实现时在同一模块边界内合并文件 |
| 验收范围表达无法证明每个 AC 的具体实施落点 | 在任务文档增加 AC-001 至 AC-020 逐项追踪表 |
| 当前 PowerShell 会话禁止直接运行技能检查脚本 | 审阅脚本后改用显式执行策略的独立进程；任务文档同时保留独立结构校验 |
| 技能完成脚本在 Windows PowerShell 5 下未指定 UTF-8，无法可靠读取中文规划文件 | 三次尝试后停止使用该脚本，采用显式 UTF-8 的等价状态检查 |
| 固定 Qt 6.11.1 MinGW 前缀当前缺失 | 继续检索开发机上其他匹配 Kit；不擅自修改全局 Qt 安装 |
| `aqtinstall==3.3.0` 临时 pip 安装超时 | 目标目录未创建，无需清理；改用官方 7-Zip 工具直接解压经 Qt 官方元数据校验的 `qtbase` 归档 |
| PowerShell 将 Qt `.sha1` 响应内容返回为 `byte[]`，不能直接调用 `Trim()` | 改用 ASCII 显式解码官方哈希文件，不复用失败的字符串假设 |
| `download.qt.io` 直接归档连接 5 分钟内没有收到数据 | 部分文件长度为 0，改用官方 mirrorlist 选择直接镜像，不重复原下载 |
| 初始脚本将 Qt MQTT 标签对象 ID 作为 HEAD 提交校验 | 将标签对象和剥离后提交分别固定校验，同时保留精确标签名检查 |
| Qt 的 `qt-cmake.bat` 内部依赖 PATH 中的 `cmake` 命令 | 在构建的临时 PATH 中加入已精确校验的 CMake/Ninja 所在目录，不要求用户修改全局 PATH |
| 更新过程记录的补丁连续两次因 JavaScript 字符串转义错误未执行 | 两次补丁均未应用且文件未受影响；改用字符串数组逐行构造补丁 |
| Windows 上将 `README*` 通配符直接作为 `rg` 路径参数导致文件名语法错误 | 改用 `Get-ChildItem -Filter README*` 解析文件，再用 `rg` 检查明确目录 |
| README/规划联合补丁的阶段状态上下文不精确 | 补丁整体未应用；拆分 README 与规划补丁并精确匹配列表格式 |
| 最终状态补丁三次因上下文或补丁标记不精确而未应用 | 均未影响交付文件；读取实际内容并拆分为小补丁完成收尾 |

## 资源

- `README.md`
- `AGENTS.md`
- `docs/REQ-001.md`
- `docs/REQ-001-design.md`
- `docs/REQ-001-tasks.md`
- `scripts/bootstrap-qtmqtt.ps1`
- `https://code.qt.io/qt/qtmqtt.git`（固定 `v6.11.1`）

## 视觉/浏览器发现

- 本任务未使用视觉或浏览器资源。

## TASK-005 已确认契约（2026-08-12）

- 登录 JSON 编解码得到 `generation=0` 的 `SessionContext`，TASK-007 再分配实际正数会话代次。
- `RequestContext` 使用 `QVariantMap` 保存筛选条件快照，领域层不暴露 `QJsonObject`。
- HTTP 信封和全部 DTO 严格拒绝未知字段、缺失字段及错误 JSON 类型。
- Qt JSON 数字仅接受有限、整数、处于目标范围且可无损表示的值；大于 `2^53-1` 的 JSON number 拒绝。
- `createdBy` 必须非空；服务端车牌必须已规范化，TASK-005 只校验、不转换。
- 协议错误包含稳定枚举、字段路径和安全展示消息，失败时不产生部分 DTO。
- 所有协议时间必须是有效 ISO 8601 文本，原文明确带 `+08:00` 且解析结果保持该偏移。
- 领域/协议库只依赖 Qt Core，不得依赖 QWidget、`QNetworkReply` 或 `QMqttClient`。
