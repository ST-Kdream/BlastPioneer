# BlastPioneer

炸弹人风格的闯关 + 局域网对战，Qt 6 / C++20 / MSVC。这个仓库自带一套原生模组接口
（C ABI，`sdk/`），模组可以用 C++ 或 Python 写，并且能给游戏添加自己的模式。

---

## 代码结构

```
<repo>/
├── src/                        程序本体（只放源码，不放资源）
│   ├── game/                   引擎：游戏规则、棋盘、关卡数据、道具效果、模组 ABI 的引擎侧
│   ├── core/                   单例与服务：目录、文件、设置、存档、数据库、网络、主题、崩溃保护
│   ├── ui/                     界面框架：窗口壳、页面骨架、路由、自绘控件（颜色全在 content/ui/*.qss）
│   ├── screens/                14 个界面
│   ├── main.cpp                入口：命令行、目录解析、按界面 id 注册、自检模式
│   ├── version.h               版本号的唯一来源（界面、打包脚本、更新器都读它）
│   ├── Resource.qrc            只把"缺了也得能跑"的几样打进去（用 alias 保持 :/Data/... 路径）
│   └── startupEffect.qml       启动特效（唯一的 QML，走 QQuickWidget）
│
├── content/                    随程序发布的内容（改这些不用重新编译）
│   ├── Data/                   配置与数据：玩法默认值由代码兜底、关卡 JSON、模组兼容声明
│   ├── Assets/picture/         图片
│   ├── ui/                     主题：tokens.json + light.qss + dark.qss（模组可覆盖）
│   └── mods/                   随仓库发布的示例模组（默认不启用）
│
├── sdk/                        原生模组 SDK
│   ├── bp_mod_api.h            唯一的对外契约（追加式演进，偏移量由 static_assert 冻结）
│   ├── python/                 作者用的 ctypes 声明 bp_mod_api.py + 两个验证脚本
│   ├── python_shim/            把 mod.py 变成合规模组的胶水 DLL（含构建脚本）
│   └── samples/                C++ 与 Python 示例模组
│
├── tests/                      引擎测试（qmake，不需要界面；报错信息必须是 ASCII）
├── tools/                      构建与发布工具
│   ├── package_release.ps1     打发布包：白名单取 Qt 运行库 + 干净化 + 启动自检
│   ├── make_manifest.ps1       生成差分更新清单（manifest.json）
│   ├── test_updater.ps1        更新器端到端自测（本地 HTTP 服务，不需要外网）
│   └── updater/                更新器源码（独立 exe，QtCore+Network+Widgets，不用 moc）
│
├── build/                      构建产物（不进版本库）
├── dist/                       发布包产物（不进版本库）
│
├── BlastPioneer.vcxproj        唯一工程文件（Debug/Release 都在里面）
├── BlastPioneer.slnx
├── README.md  LICENSE  .gitignore
```

**两条约定**，破坏它们会让后面的人很难受：

1. `src/` 里只有代码，`content/` 里只有内容。运行期两者会**并排放到 exe 旁边**
   （`Assets/` `Data/` `ui/` `mods/`），所以代码里读的路径一直是 `Data/...`、`ui/...`，
   与仓库里的 `content/Data/...` 无关 —— 这个映射由 `BlastPioneer.vcxproj` 的
   `CopyContentToOutDir` 目标负责。
2. 颜色、字号、圆角只写在 `content/ui/*.qss`，控件里只写 `objectName`。
   **没有 qss 规则的控件会用调色板颜色**，在深色系统上会变成浅字压浅底。

---

## 发布包结构

由 `tools/package_release.ps1` 生成到 `dist/BlastPioneer-<version>/`。
**不用 windeployqt**：它会把用不上的模块全拷进来（软件 OpenGL 19.7MB、D3D12 着色器编译器、
qmltooling 开发工具……），实测直接部署是 120MB/96 文件，白名单后是 **65MB/73 文件**。

```
BlastPioneer-<version>/
├── BlastPioneer.exe            主程序
├── Updater.exe                 更新器（独立 exe，与主程序共用下面的 Qt 运行库）
├── version.txt                 version / channel / built / graphics=d3d11（更新器读它）
├── NOTICE.txt                  第三方许可（Qt LGPLv3、d3dcompiler、ICU）
├── LICENSE
│
├── Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6Network.dll
├── Qt6OpenGL.dll  Qt6Quick.dll  Qt6Qml.dll  Qt6QmlModels.dll
├── Qt6QmlMeta.dll  Qt6QmlWorkerScript.dll  Qt6QuickWidgets.dll  Qt6Svg.dll
├── d3dcompiler_47.dll  icuuc.dll
│
├── platforms/                  qwindows.dll（没有它起不来）
├── styles/                     qmodernwindowsstyle.dll
├── imageformats/               gif ico jpeg svg
├── iconengines/                svg 图标引擎
├── tls/                        schannel（HTTPS，更新器要用）
├── networkinformation/
├── qml/                        QML + QtQml + QtQuick + QtQuick.Window 共 17 个文件（启动特效）
│
├── Assets/  Data/  ui/  mods/  内容（与仓库 content/ 一一对应）
└── Update/                     运行时创建：staging/（下载中）backup/（回滚用）
```

包内**不包含**任何用户数据：`Data/playerData.json`（存档）、`Data/settings.ini`（设置）、
`Data/launch_state.json`（崩溃保护记录）、`logs/`、`cache/` 都会被脚本删掉并在最后校验 ——
校验不通过脚本直接失败，不会产出"能发但脏"的包。

**图形策略**：只支持 D3D11（`src/main.cpp` 里 `QQuickWindow::setGraphicsApi(Direct3D11)` 钉死）。
所以不带 `opengl32sw.dll`，也不留 `QT_OPENGL` / `QT_QUICK_BACKEND` 之类的兜底环境变量。

---

## 更新（Updater.exe）

外部更新器：**文件级差分**，只下载变化的文件，校验 sha256，先备份再替换，自检失败自动回滚。
源是一个**有序列表**，按顺序试，谁先成谁完成整次更新（不交叉重试）：

| 顺序 | 源 | 说明 |
|---|---|---|
| 1 | `cdn.jsdelivr.net/gh/ST-Kdream/BlastPioneer@release/latest` | GitHub 的公共 CDN，国内通常最快；单文件 20 MB 上限 |
| 2 | `ghfast.top/https://raw.githubusercontent.com/...` | raw 的反代，CDN 不可用时的主力 |
| 3 | `raw.githubusercontent.com/ST-Kdream/BlastPioneer/release/latest` | 官方源，永远最后 |

三条源的目录结构完全一致（`manifest.json` + `files/<路径>`），所以代码只有一套。
在 `Data/settings.ini` 里可以覆盖：

```ini
[update]
sources=https://my.mirror/blastpioneer          ; 逗号/分号/空格分隔，越靠前越先试
channel=stable
keepBackup=true
```

发布一次更新（镜像与官网都是同一份东西）：

```powershell
pwsh -File tools\package_release.ps1                      # 1. 出包（含自检）
pwsh -File tools\make_manifest.ps1 -Dir dist\BlastPioneer-0.1.1 -Version 0.1.1 \
     -Delete ui/old.qss                                   # 2. 生成 manifest.json
# 3. 把 dist\BlastPioneer-0.1.1\* 上传到 <base>\files\ ，manifest.json 放 <base>\
# 4. 官网：把同一份内容推到 release 分支的 latest\ 目录
pwsh -File tools\test_updater.ps1                         # 端到端自测（本地起 HTTP 服务）
```

## 构建与运行

```powershell
# 构建（唯一构建目录 build/；两个工程都产出到 build\Release\）
& 'D:\Mygrammer\aboutcpp\grammer\MSBuild\Current\Bin\MSBuild.exe' BlastPioneer.vcxproj /p:Configuration=Release /p:Platform=x64
& 'D:\Mygrammer\aboutcpp\grammer\MSBuild\Current\Bin\MSBuild.exe' tools\updater\Updater.vcxproj /p:Configuration=Release /p:Platform=x64

# 跑
.\build\Release\BlastPioneer.exe

# 引擎测试
cd tests ; .\run_tests.bat

# 界面结构自检（14 个界面构造一遍后退出）
.\build\Release\BlastPioneer.exe --ui-selftest

# 界面截图（设计自查，改配色后看一眼）
.\build\Release\BlastPioneer.exe --root=<临时目录> --ui-shot=<截图目录>

# 打发布包（构建 + 取运行库 + 干净化 + 启动自检）
pwsh -File tools\package_release.ps1
```

调试用：`--root=<目录>` 把数据目录指到别处、`--play-mode=<modId>/<modeId>` 直接进一个模组模式、
`--safe-mode` 本次不加载原生模组。
