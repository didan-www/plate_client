# 车牌识别系统 Qt 管理客户端

本项目是三端车牌识别教学系统中的 Windows 管理客户端，使用 C++17 和 Qt 6 Widgets 开发。

系统由嵌入式端、Linux 服务端和 Qt 管理端组成。车辆图片的保存、模型识别、MySQL 日志以及向嵌入式端发布放行结果均由 Linux 服务端负责；Qt 客户端不执行识别，只负责实时监控、历史查询和业务配置管理。

> 当前状态：TASK-002 构建基线已完成，当前包含最小 Qt Widgets 启动窗口和 Qt Test 冒烟测试，业务功能尚未开始实现。

## 系统职责

| 系统 | 职责 |
| --- | --- |
| 嵌入式端 | 拍摄车辆照片并上传 Linux 服务端；接收服务端 MQTT 识别结果并控制抬杆 |
| Linux 服务端 | 保存图片、调用模型、写入 MySQL、发布实时识别事件和嵌入式结果 |
| Qt 管理端 | 用户登录、HTTP 心跳、MQTT 实时监控、图片查看、历史查询、CSV 下载和黑白名单管理 |

Qt 客户端与 Linux 服务端的通信分工如下：

- HTTP：登录、注销、心跳、识别记录查询、图片下载、CSV 导出和黑白名单增删查。
- MQTT：在线期间的识别状态和结果实时通知。
- MySQL：只由 Linux 服务端访问，是识别日志和黑白名单的业务数据源。

## 客户端核心流程

```text
HTTP 登录
   -> 进入主界面并启动 HTTP 心跳
   -> 后台连接 MQTT 并订阅管理事件
   -> 收到 PROCESSING 事件
   -> 按 recognitionId 从 HTTP 下载车辆图片
   -> 内存展示图片和“识别中”状态
   -> 收到 SUCCEEDED/FAILED 事件
   -> 更新实时结果列表
```

## 首版功能

- 用户名密码登录，使用单访问令牌；HTTP 和 MQTT 凭据同时到期。
- HTTP 服务状态和 MQTT 连接状态独立显示。
- MQTT 3.1.1、QoS 1、干净会话的实时识别事件订阅。
- 当前车辆图片、服务器识别状态、结果和耗时展示。
- 本次登录期间最近 100 条实时识别结果。
- 从服务端 MySQL 分页查询历史日志，支持时间和设备筛选。
- 下载由服务端直接生成的 UTF-8 CSV。
- 黑名单、白名单浏览、车牌搜索、新增和删除。
- 简单按日技术日志，自动保留 7 天。

首版不包含：

- Qt 客户端发起或执行车牌识别。
- Qt 客户端向嵌入式端发布放行结果。
- SQLite、本地业务数据同步或直接连接 MySQL。
- 车辆图片长期保存。
- 视频识别和摄像头识别。
- 黑白名单参与抬杆决策。
- 角色权限、多语言、系统托盘和 Windows 安装包。

## 技术环境

| 组件 | 基线 |
| --- | --- |
| 操作系统 | Windows 10/11 64-bit |
| 语言 | C++17 |
| Qt | 6.11.1 Desktop MinGW 64-bit |
| 编译器 | MinGW-w64 GCC 13.1.0 |
| 构建系统 | CMake 3.30.5、Ninja 1.12.1 |
| MQTT | Qt MQTT `v6.11.1` |
| 测试 | Qt Test |

客户端计划使用 `Widgets`、`Network`、`Concurrent`、`Mqtt` 和 `Test` 模块，不使用 `Qt Sql`。

项目默认的 Qt 前缀为 `D:\Qt\6.11.1\mingw_64`，MinGW 位于 `D:\Qt\Tools\mingw1310_64`。这些路径必须能够通过脚本参数、CMake Preset 或缓存变量覆盖，不能硬编码到业务代码。

Qt MQTT 不安装到全局 Qt Kit。项目从官方 `v6.11.1` 标签构建到 `.deps/install/qtmqtt`，不得修改全局 `D:\Qt` 安装或混入 MSVC ABI 库。

## Qt MQTT 本地依赖

运行 `scripts/bootstrap-qtmqtt.ps1` 前，需要准备 Qt 6.11.1 Desktop MinGW 64-bit、MinGW-w64 GCC 13.1.0、CMake 3.30.5、Ninja 1.12.1 和 Git。脚本会精确校验版本、`win32-g++` Qt ABI 和 `x86_64-w64-mingw32` 编译器目标，固定检出官方 `v6.11.1` 标签，然后在 `.deps` 内完成源码、构建、安装和 CMake 包探测。

本机 TASK-001 验收时，全局 `D:\Qt` 中尚无 Qt 6.11.1 Kit，因此使用经 Qt 官方归档哈希校验的临时 Kit 放在已忽略的 `.deps/qt`，并实际验证了以下命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\bootstrap-qtmqtt.ps1 `
  -QtPrefix "$PWD\.deps\qt\6.11.1\mingw_64"
```

正常开发机已在默认路径安装 Qt 6.11.1 时可省略 `-QtPrefix`。CMake、Ninja、MinGW 和 Git 路径也可分别通过 `-CMakePath`、`-NinjaPath`、`-MingwRoot` 和 `-GitPath` 覆盖。重复运行会先用独立 CMake 工程验证现有 `Qt6::Mqtt` 包；安装正确时不会重建或改写库。

Qt MQTT 主要源码采用 `LicenseRef-Qt-Commercial OR GPL-3.0-only`。分发使用该模块的客户端前，必须具有有效 Qt 商业许可或满足 GPL-3.0-only 的要求。

## 计划目录

```text
plate_client/
├── README.md
├── AGENTS.md
├── CMakeLists.txt                 # 顶层 CMake 工程
├── config/
│   └── client.json.example       # 实施阶段创建
├── docs/
│   ├── REQ-001.md
│   └── REQ-001-design.md
├── src/                           # 最小 Qt Widgets 应用
├── tests/                         # Qt Test 冒烟测试
└── .deps/                         # 本地依赖，不提交
```

## 引导配置

客户端运行前需要程序旁的 `config/client.json`：

```json
{
  "clientId": "00000000-0000-0000-0000-000000000000",
  "loginBaseUrl": "https://127.0.0.1:8443",
  "allowInsecureTransport": false
}
```

配置文件不得包含用户名、密码、访问令牌或 MQTT 密码。`allowInsecureTransport` 默认必须为 `false`；任何模式都不得静默忽略 TLS 证书错误。

## 本地数据边界

- 只通过 `QSettings` 记住上次用户名。
- 实时记录、查询结果、名单页面数据和当前图片只保存在内存。
- 技术日志按日期写入 `QStandardPaths::AppLocalDataLocation/logs`，保留 7 天并脱敏。
- 不创建 SQLite 数据库，不加载 MySQL 驱动，不启用图片磁盘缓存。

## 文档

- [需求规格](docs/REQ-001.md)：功能、接口、边界和验收标准的来源。
- [架构设计](docs/REQ-001-design.md)：模块边界、状态、线程、时序和错误处理方案。
- [AI 开发约束](AGENTS.md)：后续 AI 代理必须遵守的工程规范。

任何影响业务范围、公开接口、MQTT 消息、页面行为或验收标准的变更，应先更新需求文档，再同步架构设计和代码。

## 构建状态

先确认 TASK-001 已将 Qt MQTT 安装到 `.deps/install/qtmqtt`。以下命令已在 Qt 6.11.1、MinGW-w64 GCC 13.1.0、CMake 3.30.5 和 Ninja 1.12.1 环境实际验证：

```powershell
D:\Qt\Tools\CMake_64\bin\cmake.exe --preset mingw-debug
D:\Qt\Tools\CMake_64\bin\cmake.exe --build --preset mingw-debug
D:\Qt\Tools\CMake_64\bin\ctest.exe --preset mingw-debug
```

`mingw-debug` Preset 生成 Debug 构建到 `build/mingw-debug`。测试 Preset 会设置 Qt 和项目本地 Qt MQTT 的运行时路径，并使用 Qt offscreen 平台运行 `unit.smoke` 与 `app.startup`；实际窗口启动也已在 Windows 桌面环境验证。
