# BlastPioneer（爆破先锋）

一款基于 Qt/C++ 开发的炸弹人风格游戏，包含单人闯关、局域网多人对战、道具商店、背包系统、QML 启动特效等丰富功能。

## ✨ 特性

- **🎮 单人闯关**：逐步挑战多张地图，击败敌人，收集金币和经验，提升段位
- **🌐 多人对战**：通过局域网（或本地回环）实现最多 4 人的实时对战，主机权威同步
- **🛒 商店系统**：使用金币购买各类道具（生命药水、速度靴、炸弹强化等）
- **🎒 背包管理**：查看并使用已拥有的道具，在战斗中提供增益
- **⚙️ 设置选项**：调整帧率、音量、特效质量、网格显示等
- **✨ 启动特效**：基于 QML 的粒子爆炸效果，提升游戏启动体验（可开关）
- **💾 数据持久化**：玩家信息、背包、通关进度自动保存为 JSON 文件
- **🎯 段位系统**：根据经验值自动晋升段位（萌弹新手 → 灭世弹神）

## 📁 目录结构
```text
BlastPioneer/                                      # 项目根目录
├── README.md                                      # 项目说明文档
├── LICENSE                                        # 许可证文件
└── BlastPioneer/                                  # 主程序目录（所有内容集中在此）
    ├── Assets/                                    # 游戏资源文件夹
    │   └── picture/                               # 图片资源（玩家、敌人、炸弹、道具等）
    ├── Data/                                      # 数据配置文件夹
    │   ├── playerData.json                        # 玩家存档数据（运行时自动生成）
    │   ├── settings.ini                           # 用户设置配置文件（运行时自动生成）
    │   ├── gamerules.txt                          # 游戏规则文本（已打包至qrc）
    │   └── itemInfo.json                          # 道具数据配置（已打包至qrc）
    ├── MainWindow.h/cpp                           # 主窗口（程序入口界面）
    ├── GameWindow.h/cpp                           # 单人游戏窗口
    ├── MultiGameWindow.h/cpp                      # 多人游戏窗口
    ├── LobbyWindow.h/cpp                          # 联机大厅窗口
    ├── PlayerWindow.h/cpp                         # 玩家信息窗口
    ├── BagWindow.h/cpp                            # 背包窗口
    ├── ShopWindow.h/cpp                           # 商店窗口
    ├── SettingsWindow.h/cpp                       # 设置窗口
    ├── RulesWindow.h/cpp                          # 游戏规则窗口
    ├── LevelSelectWindow.h/cpp                    # 关卡选择窗口
    ├── StartupEffectWindow.h/cpp                  # 启动特效窗口（含QML加载）
    ├── PlayerInfo.h/cpp                           # 玩家数据管理类
    ├── SettingsManager.h/cpp                      # 设置管理类（单例）
    ├── Item.h/cpp                                 # 道具类
    ├── Rank.h/cpp                                 # 段位系统类
    ├── DataLoader.h/cpp                           # 数据预加载类（qrc资源读取）
    ├── NetworkProtocol.h                          # 网络协议定义（仅头文件）
    ├── main.cpp                                   # 程序入口点
    ├── Resource.qrc                               # Qt 资源文件（打包图片、文本等）
    ├── startupEffect.qml                          # QML 启动特效动画
    ├── BlastPioneer.vcxproj                       # Visual Studio 项目文件
    └── BlastPioneer.vcxproj.filters               # VS 项目筛选器
```

## 🔧 编译要求

| 项目 | 要求 |
|------|------|
| **操作系统** | Windows 10/11（x64） |
| **编译器** | Visual Studio 2022（或 2026）带 C++ 桌面开发组件 |
| **Qt 版本** | Qt 6.0 及以上（推荐 6.10.2） |
| **Qt 模块** | `core`、`gui`、`widgets`、`network`、`qml`、`quick`、`quickwidgets` |
| **构建工具** | Visual Studio 项目文件（.vcxproj）已提供 |

## 🚀 编译与运行

### 方式 A：Visual Studio（推荐）

1. 安装 Qt 6 并配置 Visual Studio 的 Qt 插件（Qt VS Tools）
2. 打开 `BlastPioneer.vcxproj`
3. 选择构建配置（Debug/Release）和平台（x64）
4. 点击「生成解决方案」或按 `Ctrl+Shift+B`
5. 按 `F5` 运行调试

### 方式 B：Qt Creator

1. 打开 Qt Creator，选择「打开项目」
2. 选择 `BlastPioneer.vcxproj` 或自行生成 `CMakeLists.txt`
3. 配置 Qt 版本和编译器
4. 点击「运行」

### 独立部署

编译完成后，若要独立运行 `.exe`，使用 Qt 自带的部署工具将依赖的 DLL 和 QML 资源复制到 exe 所在目录：

```bash
windeployqt6.exe --qmldir <项目源码目录> BlastPioneer.exe
```

##  🎮操作指南

| 按键 | 功能 |
|------|------|
| W A S D | 上下左右移动 |
| Space | 放置炸弹 |
| B | 在游戏中打开背包（单人模式） |
| 鼠标 | 点击界面按钮导航 |

##  🌐多人联机

### 基本流程

1. 在主界面点击「多人游戏」进入联机大厅
2. **创建房间**：点击「创建房间」成为主机，等待其他玩家加入
3. **加入房间**：点击「刷新列表」发现局域网内的房间，选中后点击「加入选中房间」；或手动输入主机 IP 和端口（默认 `9999`）后点击「手动加入」
4. **开始游戏**：主机需至少 2 名玩家才能开始，倒计时 3 秒后进入对战

### 联机机制

| 项目 | 说明 |
|------|------|
| 协议 | UDP |
| 同步方式 | 主机权威（Host Authority），所有游戏逻辑由主机计算，客户端仅发送输入并接收状态 |
| 最大人数 | 4 人 |
| 端口 | 默认 `9999`（可手动指定） |

---

### ️ 免责声明

> 本项目**不提供任何网络服务**。多人联机功能基于局域网通信实现，所有数据传输均在玩家本地设备之间直接进行，不经过任何第三方服务器或中继节点。
>
> - 本功能不收集、存储或转发任何用户数据。
> - 网络连接的稳定性取决于玩家自身的局域网环境，因网络波动、防火墙策略或路由器配置等原因导致的连接异常，不在本项目的责任范围内。
> - 使用联机功能前，请确保已获得所在网络环境的管理授权，并遵守相关网络使用规定。

##  已知问题与待更新

- [ ] 联机大厅刷新列表后可能导致端口无法绑定
- [ ] 多人游戏下开局可能卡入墙中
- [ ] 非主界面关闭窗口程序并未结束，但窗口全部隐藏
- [ ] 炸弹爆炸后长时间显示红格
- [ ] 待更新背景和音效
- [ ] 待更新人物移动动作
- [ ] 待更新炸弹倒计时与爆炸提醒

  更新日志见[update_log.txt](update_log.txt)

##  🤝 贡献与反馈
本项目为个人学习作品，欢迎提交 Issue 或 Pull Request。

## 📄 许可证
本项目采用 **GNU General Public License v3.0**（GPL-3.0） 开源许可证。
你可以自由使用、修改、分发本软件，但任何分发（包括修改版本）必须同样以 GPL-3.0 开源，并保留原始版权声明。
详见 [LICENSE](LICENSE) 文件。
本软件使用了 **Qt 框架**（LGPL-3.0），
Qt 源代码可从 [Qt官方下载链接](https://www.qt.io/download) 获取。