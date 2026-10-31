# 基线 JAR 中各类的源码 / Sources of the classes in the baseline JAR

## 中文

上车的 `hmi/carplay_hook-basevideo3.jar` 由基线 JAR `hmi/vendor/carplay_hook-wheel-v25.jar`，加上 `hmi/src/` 编译出的类和两个构建期补丁工具生成。下表列出 JAR 中不是从 `hmi/src/` 编译出来的类及其源码。这些类都属于 GPL-3.0 部分。

| 类 | 源码 | 对应关系 |
|---|---|---|
| `com.luka.carplay.DeviceStateHandler`、`coverart.CoverArt`、`cursor.CursorController`、`org.dsi.ifc.carplay.AppState`、`AppConnectorTerminalMode`（含 `$CoverArtProvider`）、`TerminalModeBapCombi$ActiveDeviceStateListener`、`TerminalModeBapCombi$EventListener`、`CarplayDSILifecycleController$TerminalModeDSIKeyEventsController` | `vendor/src/`，取自 [Luka 的 mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) 标签 `MHI2Q-2026-05-16`（提交 `f810ac0`）的 `java_patch/` | 与该标签发布的 `carplay_hook.jar` 中的类逐字节相同 |
| `com.luka.carplay.cluster.ClusterGeomOverride` | `vendor/src/`，取自 Luka 提交 `ab6962e` 的 `java_patch/` | 以 `-source 1.3 -target 1.2` 编译后逐字节相同 |
| `de.audi.tghu.navi.app.command.DSIResponseContainer` | 原车类，构建时由 `hmi/src/com/luka/carplay/cluster/NativeRouteStateJarPatch.java` 修改 | 补丁工具源码在本仓库 |
| `de.audi.tghu.fwhmi.DisplayManagerMIB2High`（含 `$1`、`$DisplayManagerProvider`） | 基线版本见下一行；构建时再由 `hmi/src/com/luka/carplay/cluster/RgiRefreshJarPatch.java` 修改 | 补丁工具源码在本仓库 |
| `de.audi.tghu.navi.app.cluster.ClusterService`、`de.esolutions...CombiMapController`、`DisplayManagerMIB2High` 的基线版本 | Luka 和 Allemon 的仓库中有同名文件（`java_patch/`），但其中 30 个历史版本都编译不出与 JAR 相同的类 | 对应源码暂缺，未放入 |
| `com.luka.carplay.cluster.CarPlayScreenMonitor` | Luka 的仓库中没有；来自 Lanye-z 的 MMI Mirror。JAR 中没有其他类引用它 | 对应源码暂缺，未放入 |

`vendor/src/` 中的文件保持 Luka 仓库中的原样，不参与本项目的构建，只用于提供源码。

## English

The vehicle JAR `hmi/carplay_hook-basevideo3.jar` is produced from the baseline JAR `hmi/vendor/carplay_hook-wheel-v25.jar`, the classes compiled from `hmi/src/`, and two build-time patch tools. The table lists the classes in the JAR that are not compiled from `hmi/src/` and where their source is. All of them belong to the GPL-3.0 parts.

| Class | Source | Correspondence |
|---|---|---|
| `com.luka.carplay.DeviceStateHandler`, `coverart.CoverArt`, `cursor.CursorController`, `org.dsi.ifc.carplay.AppState`, `AppConnectorTerminalMode` (with `$CoverArtProvider`), `TerminalModeBapCombi$ActiveDeviceStateListener`, `TerminalModeBapCombi$EventListener`, `CarplayDSILifecycleController$TerminalModeDSIKeyEventsController` | `vendor/src/`, taken from `java_patch/` at tag `MHI2Q-2026-05-16` (commit `f810ac0`) of [Luka's mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) | Byte-identical to the classes in the `carplay_hook.jar` released with that tag |
| `com.luka.carplay.cluster.ClusterGeomOverride` | `vendor/src/`, taken from `java_patch/` at Luka's commit `ab6962e` | Byte-identical when compiled with `-source 1.3 -target 1.2` |
| `de.audi.tghu.navi.app.command.DSIResponseContainer` | Stock class, modified at build time by `hmi/src/com/luka/carplay/cluster/NativeRouteStateJarPatch.java` | The patch tool's source is in this repository |
| `de.audi.tghu.fwhmi.DisplayManagerMIB2High` (with `$1`, `$DisplayManagerProvider`) | Baseline version: see the next row; modified at build time by `hmi/src/com/luka/carplay/cluster/RgiRefreshJarPatch.java` | The patch tool's source is in this repository |
| Baseline versions of `de.audi.tghu.navi.app.cluster.ClusterService`, `de.esolutions...CombiMapController` and `DisplayManagerMIB2High` | Luka's and Allemon's repositories contain files with these names (`java_patch/`), but none of their 30 historical versions compiles to the classes in the JAR | Matching source not yet available; not included |
| `com.luka.carplay.cluster.CarPlayScreenMonitor` | Not in Luka's repository; it comes from Lanye-z's MMI Mirror. No other class in the JAR references it | Matching source not yet available; not included |

The files in `vendor/src/` are kept exactly as they are in Luka's repository. They are not part of this project's build and are provided only as source.
