# MHI2Q-CarPlay-AltScreen 许可与版权说明 / Licenses and Copyright

Required Notice: Copyright (c) 2026 yuedizhibo (https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)

Required Notice: Licensed under the PolyForm Noncommercial License 1.0.0 with the Additional Terms in LICENSE.ADDITIONAL-TERMS.md. Selling is prohibited. Paid installation is capped at CNY 100 per vehicle per installation.

## 中文

本项目由几个独立的部分组成，各部分的许可不同。以下说明自 V3.8 起适用；此前发布的版本仍按其发布时的许可执行。

| 部分 | 路径 | 来源 | 许可 |
|---|---|---|---|
| 第二屏 hook | `Toolbox/carplay_alt_screen/src/`，以及由它编译出的 `libcarplay_altscreen.so` | 本项目原创 | PolyForm Noncommercial 1.0.0 + 附加条款 |
| 第二屏显示进程 | `Toolbox/carplay_alt_screen/mirror_display/`，包括 `release/` 中的程序和脚本 | 本项目开发；显示后端最初取自 Lanye-z 的 MMI Mirror | PolyForm Noncommercial 1.0.0 + 附加条款；MMI Mirror 原始代码仍为 Unlicense（见 `mirror_display/release/LICENSE.MMI-MIRROR`） |
| 安装、恢复、诊断脚本，绿色菜单，构建与校验脚本，说明文档 | `Toolbox/scripts/` 中本项目的脚本、`Toolbox/GEM/mqb-carplayAltScreen.esd`、`Tools/`、根目录的构建与校验脚本、`README.md`、`README_EN.md` | 本项目原创 | PolyForm Noncommercial 1.0.0 + 附加条款 |
| Java HMI，含仪表电话页 | `Toolbox/carplay_alt_screen/hmi/`，包括 `carplay_hook-basevideo3.jar` 和 `vendor/` | 基于 Luka 的 mib2q-carplay-rgi，包含本项目新增和修改的类；仪表电话页移植自 Luka | GPL-3.0 |
| RGI 原生模块，含电话 iAP2 模块 | `Toolbox/carplay_alt_screen/rgi_native/`、`rgi_meta/libcarplay_rgi_meta.so` | 基于 Luka 的 mib2q-carplay-rgi，包含本项目新增和修改的文件 | GPL-3.0 |
| RGI 转向箭头渲染器 | `Toolbox/carplay_alt_screen/rgi_renderer/`，包括 `maneuver_render` | 基于 Allemon 的 mib2-carplay-rgi-altscreen 和 Luka 的 mib2q-carplay-rgi，包含本项目新增和修改的文件 | GPL-3.0 |
| 上游 MIB2 Toolbox 相关文件 | SD 卡上 MIB2 Toolbox 自身的文件 | jilleb/mib2-toolbox | MIT（见仓库根目录 `LICENSE.TOOLBOX-MIT`） |
| 其他第三方文件 | `rgi_native/src/coverart/stb_image.h`、`hmi/resources/META-INF/UNICODE-LICENSE.txt` 对应的 Unicode 数据 | Sean Barrett、Unicode, Inc. | 各自原有许可，见文件内声明 |

许可全文：

- PolyForm Noncommercial 1.0.0：`LICENSE.POLYFORM-NONCOMMERCIAL-1.0.0.md`
- 附加条款（禁止售卖；收费安装每台车每次不超过人民币 100 元）：`LICENSE.ADDITIONAL-TERMS.md`
- GPL-3.0：`LICENSE.GPL-3.0.txt`

各部分之间的关系：

- 第二屏 hook、显示进程和脚本是独立程序，不包含也不链接 GPL-3.0 部分的代码。它们与 GPL-3.0 部分只通过原车进程、共享内存、文件和本机网络端口交换数据。安装包把这些独立程序放在一起分发，属于 GPL-3.0 第 5 条所说的聚合。
- GPL-3.0 部分的源码在 https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen 公开，与安装包版本对应。Java HMI 基线 JAR（`hmi/vendor/`）中有一部分类不是从 `hmi/src/` 编译的：其中能在 Luka 仓库找到对应版本的，源码放在 `hmi/vendor/src/`；ClusterService、CombiMapController、DisplayManagerMIB2High 的基线版本和 CarPlayScreenMonitor 的对应源码暂缺。逐个类的说明见 `hmi/vendor/SOURCES.md`。任何人都可以按 GPL-3.0 使用、修改和再分发 GPL-3.0 部分。

版权：

- 第二屏 hook、显示进程、脚本、绿色菜单和说明文档：Copyright (c) 2026 yuedizhibo
- Java HMI 与 RGI 中 Luka 的代码：Copyright (c) 2026 LuKa (@LuKa_dev)，见各源文件头部
- RGI 渲染器中 Allemon 的代码：版权归 Allemon 所有，见 https://github.com/Allemon/mib2-carplay-rgi-altscreen
- GPL-3.0 部分中本项目新增和修改的代码：Copyright (c) 2026 yuedizhibo，按 GPL-3.0 发布

## English

This project consists of several independent parts with different licenses. This notice applies from V3.8 onward; earlier releases remain under the licenses they were released with.

| Part | Path | Origin | License |
|---|---|---|---|
| Second-screen hook | `Toolbox/carplay_alt_screen/src/` and the `libcarplay_altscreen.so` built from it | Original work of this project | PolyForm Noncommercial 1.0.0 + Additional Terms |
| Second-screen display process | `Toolbox/carplay_alt_screen/mirror_display/`, including the programs and scripts in `release/` | Developed by this project; the display backend was originally taken from Lanye-z's MMI Mirror | PolyForm Noncommercial 1.0.0 + Additional Terms; the original MMI Mirror code remains under the Unlicense (see `mirror_display/release/LICENSE.MMI-MIRROR`) |
| Install, restore and diagnostic scripts, green menu, build and verification scripts, documentation | This project's scripts in `Toolbox/scripts/`, `Toolbox/GEM/mqb-carplayAltScreen.esd`, `Tools/`, the build and verification scripts in the repository root, `README.md`, `README_EN.md` | Original work of this project | PolyForm Noncommercial 1.0.0 + Additional Terms |
| Java HMI, including the cluster phone page | `Toolbox/carplay_alt_screen/hmi/`, including `carplay_hook-basevideo3.jar` and `vendor/` | Based on Luka's mib2q-carplay-rgi, with classes added and changed by this project; the cluster phone page is ported from Luka | GPL-3.0 |
| RGI native module, including the telephony iAP2 module | `Toolbox/carplay_alt_screen/rgi_native/`, `rgi_meta/libcarplay_rgi_meta.so` | Based on Luka's mib2q-carplay-rgi, with files added and changed by this project | GPL-3.0 |
| RGI maneuver renderer | `Toolbox/carplay_alt_screen/rgi_renderer/`, including `maneuver_render` | Based on Allemon's mib2-carplay-rgi-altscreen and Luka's mib2q-carplay-rgi, with files added and changed by this project | GPL-3.0 |
| Upstream MIB2 Toolbox files | MIB2 Toolbox's own files on the SD card | jilleb/mib2-toolbox | MIT (see `LICENSE.TOOLBOX-MIT` in the repository root) |
| Other third-party files | `rgi_native/src/coverart/stb_image.h`, the Unicode data covered by `hmi/resources/META-INF/UNICODE-LICENSE.txt` | Sean Barrett, Unicode, Inc. | Their own licenses, as stated in the files |

Full license texts:

- PolyForm Noncommercial 1.0.0: `LICENSE.POLYFORM-NONCOMMERCIAL-1.0.0.md`
- Additional Terms (no selling; paid installation capped at CNY 100 per vehicle per installation): `LICENSE.ADDITIONAL-TERMS.md`
- GPL-3.0: `LICENSE.GPL-3.0.txt`

How the parts relate:

- The second-screen hook, the display process and the scripts are independent programs. They neither contain nor link the code of the GPL-3.0 parts, and they exchange data with the GPL-3.0 parts only through the stock head-unit processes, shared memory, files and local network ports. The installation package distributes these independent programs together as an aggregate in the sense of GPL-3.0 section 5.
- The source code of the GPL-3.0 parts is published at https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen , matching each installation package. Some classes in the Java HMI baseline JAR (`hmi/vendor/`) are not compiled from `hmi/src/`. Where a matching version exists in Luka's repository, its source is in `hmi/vendor/src/`; matching source for the baseline versions of ClusterService, CombiMapController and DisplayManagerMIB2High, and for CarPlayScreenMonitor, is not yet available. See `hmi/vendor/SOURCES.md` for each class. Anyone may use, modify and redistribute the GPL-3.0 parts under GPL-3.0.

Copyright:

- Second-screen hook, display process, scripts, green menu and documentation: Copyright (c) 2026 yuedizhibo
- Luka's code in the Java HMI and RGI: Copyright (c) 2026 LuKa (@LuKa_dev), see the source file headers
- Allemon's code in the RGI renderer: copyright Allemon, see https://github.com/Allemon/mib2-carplay-rgi-altscreen
- Code added and changed by this project in the GPL-3.0 parts: Copyright (c) 2026 yuedizhibo, released under GPL-3.0
