# Liner Hull Customizer

[English](README.md) · **简体中文**

用于编辑 *Nebulous: Fleet Command* `.fleet` 文件中保护国（Protectorate）货舰模块化船体的桌面工具。
打开舰队，选择艏 / 中 / 艉分段、每个挂点的装饰件（dressing）变体与上层建筑，在正交侧视图与可旋转
的 3D 视图中预览装配结果，然后写回文件——未改动的内容原样保留。

| 船体类型 | 舰船 |
|---|---|
| `Stock/Bulk Hauler` | Marauder |
| `Stock/Container Hauler` | Moorline |
| `Stock/Container Hauler Refit` | Moorline |

其它船体类型只做只读列出，因此舰队里不会丢失任何信息。

## 运行要求

- Windows 10 或 11，显卡驱动支持 OpenGL 3.3
- Qt 6.8.0（MinGW 13.1 套件）、CMake 3.21 或更高、Ninja

`CMakePresets.json` 假定 Qt 位于 `C:/Qt/6.8.0/mingw_64`、工具链位于 `C:/Qt/Tools/mingw1310_64`。
若安装位置不同，请把 `CMAKE_PREFIX_PATH`、`CMAKE_CXX_COMPILER`、`CMAKE_RC_COMPILER` 指向你自己的路径。

## 构建

```powershell
./scripts/build.ps1            # 配置、编译、跑测试
./scripts/build.ps1 -Deploy    # 额外在 ./dist 产出可直接运行的目录
./scripts/package.ps1          # 把 ./dist 打包成 dist/NLHC-<version>-win64.zip
```

`-Deploy` 会先清空再重建 `dist`，而不是覆盖安装，这样发布包里不会残留当前源码已不再产出的文件。
`package.ps1` 把该目录放进单一的 `NLHC-<version>-win64/` 目录，启动一次暂存副本以确认依赖完整，
然后写出压缩包以及紧邻的 `.sha256`。

也可以直接用 CMake：

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

产物：`NLHC.exe`（应用程序）、`NLHCCore.dll`（舰队、资源与装配库），以及测试共用的静态渲染库
`NLHCRender`。

发布包根目录还会带一个 `Liner Hull Customizer.exe`：一个启动器，从解压位置启动 `bin/NLHC.exe`。

## 使用

1. **文件 → 打开舰队…** 选择一个 `.fleet` 文件。所有舰船都会列出；支持的船体类型可编辑，其余只读。
2. 在 **主结构** 中选择艏 / 中 / 艉分段。每个分段会列出它的挂点，逐个选择变体（游戏允许留空的位置
   会提供 `(empty)`）。
3. 编辑 **上层建筑** 条目（部件 / 分段 / 挂点）。会导致船体损坏的组合会被拒绝，并在 **诊断** 中说明原因。
4. **主色** 与 **装饰色** 默认是 `#1b405b` 与 `#f26522`。这两项只是预览设置，不会写入舰队文件；
   **船体色调** 是真实的舰队字段，会被写回。
5. **文件 → 保存** 就地写回，并保留一份 `.bak`。无法识别的 XML 会原样保留，因此只有你改动的地方才会变化。

视口分为两半：左半是正交侧视图，右半是可环绕的 3D 视图（左键拖拽旋转、滚轮缩放、中键或右键平移、
双击复位）。

命令行：

```text
NLHC.exe --open <fleet> [--ship <index>] [--screenshot <png>]   # 无界面渲染
NLHC.exe --render-demo <png>                                     # 渲染内置示例船体
```

## 目录结构

| 路径 | 用途 |
|---|---|
| `src/core` | `NLHCCore.dll` — `.fleet` 文档模型、资源包、船体装配、校验 |
| `src/app` | `NLHC.exe` — Qt Widgets 界面、OpenGL 视口、翻译 |
| `assets/hulls` | 视口绘制所用的船体几何与贴图（已提交） |
| `samples` | 示例舰队 |
| `i18n` | `nlhc_zh_CN.ts`；界面语言可在运行时切换 |
| `scripts` | 构建、打包与发布脚本 |

## 许可

MIT，见 [LICENSE](LICENSE)。
