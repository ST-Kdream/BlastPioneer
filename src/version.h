#pragma once

// 版本号的**唯一来源**。
//
// 为什么单独一个头文件：这个数字现在有三处要用 ——
//   ① 程序自己（main.cpp 里 setApplicationVersion，界面上显示 v0.x.y）；
//   ② 打包脚本（tools/package_release.ps1 写进发布包的 version.txt）；
//   ③ 更新器（比较本地版本与远端 manifest 的版本）。
// 三处各写一遍就一定会漂移，所以只允许改这里。
//
// 版本号规则（与 mod 接口版本是两件事，不要混）：
//   主.次.修订   ——  修订 = 只修 bug；次 = 加功能但存档兼容；主 = 破坏性改动
// 当前阶段仍是 0.x：语义化版本里 0.x 的"次版本"允许破坏性改动。
#define BP_VERSION_STRING "0.1.0"

// 更新通道：发布包里的 version.txt 会带上它，更新器据此决定去哪个通道找清单
#define BP_UPDATE_CHANNEL "stable"
