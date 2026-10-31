# 爆破先锋 · BlastPioneer

> 炸弹人风格的闯关 + 局域网对战游戏（Qt 6 / C++20 / Windows x64）。
> 关卡、道具、界面配色、甚至**整个玩法模式**都可以用 JSON 或模组替换 —— 游戏本体只是"一个能跑的壳"。

![status](https://img.shields.io/badge/version-v0.1.0--rc.1-orange)
![platform](https://img.shields.io/badge/platform-Windows%20x64-blue)
![qt](https://img.shields.io/badge/Qt-6.10-41CD52)
![license](https://img.shields.io/badge/license-GPL3.0-lightgrey)

---

## ✨ 特性

- **单人闯关**：多张手工 / 程序化地图，清掉敌人过关，金币与经验累积，段位成长。
- **局域网对战**：最多 4 人，主机权威；两端模组集合不一致会明确拒绝加入（不让"半个模组"进对局）。
- **关卡是数据**：`content/Data/levels/*.json`，加关卡＝加文件，不用重新编译；也支持手绘字符地图。
- **道具系统**：商店购买、背包使用、图鉴查看；道具效果由效果表驱动。
- **界面可换皮**：`content/ui/tokens.json` + `light.qss` / `dark.qss`，令牌 + 样式表两层，配色不写在代码里。
- **模组支持（数据级）**：改玩法数值、道具字段、关卡、界面外观 —— 只放几个 JSON。
- **模组支持（原生级）**：写一个 DLL（C/C++/Rust/C#/Go/**Python**）就能订阅事件、读写世界，
  并且可以**给游戏加一个自己的模式**（自己铺地图、决定敌人怎么走、自己判胜负）。
- **崩溃保护**：原生模组连续两次异常结束会自动进入安全模式（本次不加载任何原生模组），
  也可以随时按住 `Shift` 启动或加 `--safe-mode`；顶部横幅会显示当前状态。
- **绿色免安装**：解压即玩，存档与设置在程序目录内，不写注册表。
- **安装器**：按用户安装到 `%LOCALAPPDATA%\Programs\BlastPioneer`，不需要管理员权限；
  升级不覆盖存档，卸载也保留 `Data\` 里的玩家数据。
- **内置更新器**：`Updater.exe` 做**文件级差分**更新（只下变化的文件），多源可切换，失败自动回滚。

---

## 🖥️ 编译要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10 / 11 x64 |
| 编译器 | Visual Studio 2022（或更新）带 **C++ 桌面开发**工作负载 |
| Qt | **6.10.x**，`msvc2022_64` 套件（更高版本通常也能编，未逐一验证） |
| 磁盘 | 约 3 GB（Qt + 中间产物 + 发布包） |
| 运行库 | 打包出来的发布包**自带** VC++ 运行库；从源码直接跑则需要本机已装 VS 的 C++ 运行库 |

构建前让工具链能找到 Qt 与 MSVC：

```powershell
# 临时指定（不改系统环境变量）
$env:QTDIR   = "C:\Qt\6.10.2\msvc2022_64"      # 指向 msvc2022_64 这一层
$env:MSBUILD = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"  # 可省：脚本会用 vswhere 自动找
```

也可以把这两行写进仓库根目录的 `qt-env.bat`（已在 `.gitignore` 里，不会被提交；批处理脚本会自动 `call` 它）。

```powershell
# 主程序
msbuild BlastPioneer.vcxproj /p:Configuration=Release /p:Platform=x64
# 更新器
msbuild tools\updater\Updater.vcxproj /p:Configuration=Release /p:Platform=x64

# 跑（产物在 build\Release）
.\build\Release\BlastPioneer.exe
```

> 只想玩、不想编？看下面的"下载与安装"。

---

## 🏗️ 项目架构

```
BlastPioneer/
├── src/                     程序本体（只有代码）
│   ├── game/                引擎：规则、棋盘、关卡数据、道具效果、模组 ABI 的引擎侧
│   ├── core/                服务：目录 / 文件 / 设置 / 存档 / 数据库 / 网络 / 主题 / 崩溃保护
│   ├── ui/                  界面框架：窗口壳、页面骨架、路由、自绘控件
│   ├── screens/             14 个界面
│   ├── main.cpp             入口：命令行、目录解析、界面注册、自检模式
│   ├── version.h            版本号唯一来源（界面 / 打包 / 更新器都读它）
│   └── Resource.qrc         打进二进制的兜底资源（道具表、规则文本、启动特效）
├── content/                 随程序发布的内容（改这些不用编译）
│   ├── Data/                玩法默认值、道具、关卡 JSON、模组兼容声明
│   ├── Assets/picture/      图片
│   ├── ui/                  主题：tokens.json + light.qss + dark.qss
│   └── mods/                随仓库发布的示例模组（默认不启用）
├── sdk/                     原生模组 SDK：C 头文件 + Python 声明 + 载入器 + 示例
├── tools/                   打包 / 清单生成 / 更新器自测 / 更新器源码
├── tests/                   引擎测试（qmake，不需要界面）
├── docs/                    开发者文档与模组开发指南
├── build/                   构建产物（不进版本库）
└── BlastPioneer.vcxproj  BlastPioneer.slnx
```

几条设计上的硬边界（读懂这几条，代码就基本能看懂了）：

1. **`src/` 只有代码，`content/` 只有内容**。运行期两者并排放在 exe 旁边
   （`Assets/` `Data/` `ui/` `mods/`），所以代码里的路径永远是 `Data/...`、`ui/...`。
2. **引擎不依赖界面**：`src/game/` 只用 QtCore，测试可以完全脱机跑（`tests/run_tests.bat`，500+ 断言）。
3. **颜色只在 `content/ui/*.qss`**，控件里只写 `objectName`。
   没有 qss 规则的控件会用系统调色板颜色 —— 在深色系统上会变成浅字压浅底（踩过）。
4. **模组只能通过一张 C 函数表拿能力**，写操作全部要过引擎校验；
   模组可以决定"规则"，但绕不过"执行"（碰撞、放弹上限、结算流程仍在引擎里）。
5. **界面是预创建的**：构造函数只搭结构，内容一律在 `onEnter` 里填（否则会显示启动瞬间的旧数据）。

更细的分层、数据流、"改某功能该动哪个文件"，见 [`docs/开发者文档.md`](docs/开发者文档.md)；
模组怎么写见 [`docs/模组开发指南.md`](docs/模组开发指南.md)。

---

## ⚠️ 注意事项

- **原生模组默认关闭**：设置 → 模组 里打开「允许原生模组」才加载 DLL，改开关需要**重启**生效。
  原生模组会完全接管进程，崩溃会带走整个游戏 —— 只启用你信任的来源。
- **打不开游戏怎么办**：按住 `Shift` 启动，或命令行加 `--safe-mode`；本次会跳过所有原生模组。
- **存档与设置在程序目录**（`Data/playerData.json`、`Data/settings.ini`）：
  换机器拷这两个文件即可；删掉就是"全新存档"。程序不写注册表。
- **只支持 D3D11**：没有可用显卡驱动的机器（部分虚拟机、远程桌面）可能起不来。
  这是刻意取舍：不打包 20 MB 的软件 OpenGL 兜底，避免"能开但帧率掉到个位数"。
- **更新器是可选的**：`Updater.exe` 与游戏放在同一目录才可用；它会先备份旧文件，自检失败自动回滚。
- **调试开关**：`--root=<目录>` 把数据目录指到别处；`--play-mode=<modId>/<modeId>` 直接进一个模组模式；
  `--ui-selftest` / `--ui-shot=<目录>` 见"开发自检"。
- **杀毒软件误报**：游戏是未签名的自编译 exe，可能被启发式扫描拦一下 —— 加白名单即可。

---

## 📥 下载与安装

目前提供两种包（内容相同，按喜好选一种）：

| 包 | 大小 | 适合 |
|---|---|---|
| `BlastPioneer-0.1.0-rc.1.zip`（绿色版） | 约 37 MB（解压后约 67 MB） | 想解压即玩、放在 U 盘或自定义目录 |
| `BlastPioneer-0.1.0-rc.1-setup.exe`（安装版） | 约 32 MB | 想出现在开始菜单/桌面，按用户安装、不弹 UAC |

**绿色版**：

1. 到 [Releases](https://github.com/ST-Kdream/BlastPioneer/releases) 下载最新 zip。
2. 解压到**任意可写目录**（不要放 `C:\Program Files`，那里没有写权限，存档会失败）。
3. 双击 `BlastPioneer.exe`。首次启动会让你创建存档。
4. 想更新：双击同目录下的 `Updater.exe`，或 设置 → 更新 → 检查更新。

**安装版**：

1. 下载 `BlastPioneer-0.1.0-rc.1-setup.exe` 并双击。
2. 默认按用户安装到 `%LOCALAPPDATA%\Programs\BlastPioneer`，**不需要管理员权限**；
   卸载会保留 `Data\` 里的存档与设置。
3. 安装器与绿色版目录布局完全一致，更新器照常可用。

> Releases 页面同时给出每个版本的更新清单；老版本可以直接用 `Updater.exe` 差分升级，
> 不必重新下一整包。

---

## 🧩 模组

三条路径，从轻到重：

| 想做的事 | 需要写代码吗 | 放哪里 |
|---|---|---|
| 改数值 / 加道具 / 加关卡 / 换配色 | 不用，只写 JSON | `mods/<id>/config/*.json`、`config/ui.json`、`ui/override.qss` |
| 订阅事件、读写世界（C/C++/Rust/C#/Go） | 一个 DLL | `mods/<id>/<id>.dll` + `mod.json` |
| **加一个新玩法模式** | 同上（引擎跑内核，模组接管规则） | 同上，调用 `register_mode` |
| 用 Python 写逻辑 | 只写 `mod.py` | 附带的 `bp_python_shim.dll` + `bp_mod_api.py` |

完整说明与示例：[`docs/模组开发指南.md`](docs/模组开发指南.md)；
可运行示例在 `sdk/samples/`（`hello_native`、`hello_python`）；
接口契约以 `sdk/bp_mod_api.h` 的头注释为准。

---

## 🧪 开发自检

```powershell
cd tests ; .\run_tests.bat                          # 引擎测试，期望 passed 601, failed 0
.\build\Release\BlastPioneer.exe --ui-selftest       # 14 个界面构造一遍（不弹窗）
.\build\Release\BlastPioneer.exe --ui-shot=<目录>    # 把每个界面截图存 PNG（改配色后自查）
pwsh -File tools\package_release.ps1                 # 打包：取运行库 + 清用户数据 + 启动自检
pwsh -File tools\test_updater.ps1                    # 更新器端到端测试（本地起 HTTP，不联网）
```

---

## 📄 许可证

代码见 [`LICENSE`](LICENSE)。
第三方组件：Qt 6（LGPLv3，动态链接，发布包内可替换）、Microsoft Direct3D 着色器编译器、
ICU（Unicode License）—— 发布包内附 `NOTICE.txt`。

**关于 Qt 的 LGPLv3**：本项目动态链接 Qt，发布包把 Qt 的 DLL 原样放在游戏目录里，
你可以用自己编译的同版本 Qt 替换它们；源码在仓库内。分发修改版时请同样遵守 LGPLv3 与本仓库许可证。

---

<sub>游戏图片与关卡数据属于本项目；模组作者对自己的模组负责。</sub>
