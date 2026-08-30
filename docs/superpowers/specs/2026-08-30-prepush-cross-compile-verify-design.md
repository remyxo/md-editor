# 推送前跨平台编译预检（pre-push 钩子）设计

> 日期：2026-08-30
> 状态：已与用户确认设计方向，待实施
> 关联：[`docs/windows-build-vm.md`](../../windows-build-vm.md)（win11 KVM 构建虚拟机）、[`docs/superpowers/specs/2026-08-21-windows-build-vm-design.md`](2026-08-21-windows-build-vm-design.md)

## 背景与动机

2026-08-30 v0.13.0 发布时，Windows/macOS CI 报 14 个编译错误（5 处 lambda 显式捕获函数级 `static` 正则 + 2 处花括号初始化 `qsizetype`→`int` 收窄，各编译器各报一遍）。两类写法 **GCC 扩展容忍**（本地 Debian 与 Linux CI 全绿），MSVC（C3495/C2397）与 Clang（narrowing/cannot-be-captured）严格拒绝——Linux 开发机是盲区，唯一暴露处是三平台 CI，事故烧掉两轮 tag 构建的 CI 分钟数并连坐 Release。

win11 KVM 构建虚拟机（工具链与 CI 逐字一致：VS 2022 MSVC + Qt 6.8.2 win64_msvc2022_64）2026-08-21 已建成，`./scripts/build-windows-local.sh` 一条命令即可在推送前本地验证——**但工具不在推送链路上，靠「记得先跑」已被证明不可靠**。

## 目标

把「只有 MSVC/Clang 才能发现的编译错误」拦截在 `git push` 之前：

1. 改动含 C++ 源文件时，push 前自动完成 **Clang 语法检查**（宿主，秒级～1 分钟）与 **MSVC 编译**（VM，增量 <1 分钟）
2. 编译错误**硬阻断**推送；基础设施故障（VM 起不来、SSH 超时、clang 未装）**警告放行**，不锁死推送
3. 无 C++ 改动的推送零成本（秒过）

## 非目标

- 不重复本地 GCC 构建与 ctest（开发流程已覆盖，且 GCC 轴不是盲区）
- 不验证打包链路（windeployqt/NSIS/图标）——发布前手动跑全量 `build-windows-local.sh`
- 不检查 JS/QSS/文档等平台无关改动
- 不覆盖 tests/ 目录（Windows/macOS CI 均 `BUILD_TESTS=OFF`，tests 只被 GCC 编译，本地已覆盖）

## 总体架构

```
git push
  └→ .githooks/pre-push（stdin 每行一个待推引用: <local_ref> <local_sha> <remote_ref> <remote_sha>）
       ├─ 引用归一：tag 引用 dereference 到 commit（tag^{commit}）；
       │   remote_sha 全零（新分支/tag）→ 与 origin/master 的 merge-base 比较
       ├─ 改动集 = git diff --name-only A..B
       │    无 *.cpp/*.h/*.hpp/*.c/CMakeLists.txt/*.cmake/*.qrc → 放行（秒过）
       ├─ [1] scripts/verify-clang.sh（宿主）
       │      cmake 重配置 build/ 带 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON（缓存复用，首次 ~20s）
       │      → 对 src/ 全部 .cpp 逐个 clang++ -fsyntax-only（@compile_commands.json 取编译参数）
       │      失败 = 硬阻断
       └─ [2] ./scripts/build-windows-local.sh --compile-only
              VM 开机（若关，+40s）→ ssh 等待 → C:\md-editor-build-launch.ps1 -CompileOnly
              → build-windows-vm.ps1 -CompileOnly：缓存守卫 + cmake 配置 + cmake --build
              （跳过图标/windeployqt/NSIS 打包）
              编译失败 = 硬阻断（退出码与 MSVC 日志透传到宿主终端）
       全绿 → 放行推送
```

## 组件明细

### 1. `.githooks/pre-push`（新增，进仓库）

- shebang bash；从 stdin 逐行读 `<local_ref> <local_sha> <remote_ref> <remote_sha>`
- 引用归一与改动集计算见上；多个待推引用（如 master + tag 同推）取**并集**，任一含 C++ 改动即触发检查
- 依次执行两项检查，**编译失败**（脚本非零退出且属编译错误）→ 打印错误摘要后 `exit 1` 阻断
- **基础设施故障放行**：两项检查脚本各自区分「编译失败」（exit ≥ 1 语义化）与「环境不可用」（如 exit 75/EX_TEMPFAIL 约定），钩子对后者打警告继续
- 逃生门：`git push --no-verify`（git 原生）或 `MD_EDITOR_SKIP_PRECHECK=1` 环境变量

### 2. `scripts/verify-clang.sh`（新增）

- **clang 跑在 Docker 容器内**（镜像 `mdeditor-clang:trixie`，基于 debian:trixie 内装 clang 19.1.7 + libstdc++-14-dev + Qt6 dev 头文件）：
  2026-08-30 验证发现本机存在**内核级页缓存/mmap 跨文件串页缺陷**——宿主 clang 读宿主 `/usr/include` 时缓冲区内容跨文件错乱（GCC 走 `read()` 路径幸免，容器用自己的 overlayfs 文件页亦幸免；同版本包在容器内完全正常）。因此预检不能依赖宿主 clang 直读宿主系统头文件
- 镜像一次性构建（`docker build` 或 docker commit 流程写入文档，docker.m.daocloud.io 镜像源拉基础镜像）；`verify-clang.sh` 启动时检查镜像存在，缺失则打印构建命令并以「环境不可用」放行
- 宿主侧：`cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 幂等重配置（复用现有缓存）
- 解析 `build/compile_commands.json`，把 **src/ 下全部 .cpp** 的编译参数转成容器内路径（仓库只读挂载），在容器内执行 `clang++ -fsyntax-only <完整编译参数>`；任一文件诊断失败即整体失败，输出首个失败的文件与诊断
- compile_commands 由 GCC 配置生成，个别 GCC 专用旗标若干扰 clang，在脚本内过滤白名单处理（实现时验证）
- 并行执行（xargs -P）控制总时长在 ~1 分钟内

### 3. Windows 侧改动（现有脚本加参数，不新增文件）

- `build-windows-vm.ps1` 新增 `-CompileOnly` 开关：跳过第 4 步图标生成与第 6 步打包，保留缓存守卫、cmake 配置、`cmake --build`；成功即 exit 0
- `build-windows-local.sh` 新增 `--compile-only` 参数：透传给 launch 脚本，并跳过末尾的 dist/ 产物列举
- `C:\md-editor-build-launch.ps1` 支持参数透传（VM 内一次性手工更新；`provision-windows-vm.ps1` 的 launch 生成段同步修改，保证将来重跑 provision 行为一致）

### 4. 启用与文档

- 钩子进仓库 `.githooks/`（git 不跟踪 `.git/hooks/`），一次性启用：`git config core.hooksPath .githooks`
- CLAUDE.md「构建命令」或「环境要求」补一行启用说明与逃生门；`docs/windows-build-vm.md` 交叉引用

## 错误处理

| 情形 | 处置 |
|------|------|
| clang / MSVC 编译错误 | 硬阻断（exit 1），错误摘要 + 日志位置透传 |
| docker 未装 / 镜像缺失 / compile_commands 生成失败 | 警告放行（exit 75 语义），提示镜像构建命令 |
| VM virsh 连不上 / 启动失败 / SSH 超时 | 警告放行，提示排查入口（`docs/windows-build-vm.md` 故障表） |
| 用户明确跳过 | `--no-verify` / `MD_EDITOR_SKIP_PRECHECK=1` |
| 多引用同推 | 改动集取并集 |

## 测试策略

1. **钩子分流**：doc-only 改动秒过；.cpp 改动触发；`--no-verify` 跳过；tag 推送正确 dereference；新分支（全零 remote_sha）走 merge-base
2. **事故回归验证**（核心）：临时还原 `70ff8d0` 修复前的坏代码（lambda 捕获 static + 花括号收窄），两项检查都必须报错——证明预检能拦住 2026-08-30 这次事故的每一类错误
3. `-CompileOnly` 在 VM 实测增量构建（<1 分钟）与退出码透传
4. 端到端：含 C++ 改动的完整 push 一轮（含 VM 冷启动路径）

## 风险与备注

- 首次冷路径时延：VM 开机 40s + SSH 等待 + 增量编译 ≈ 1.5–3 分钟；VM 保持开机则 ~1 分钟（文档已建议保持开机）
- `compile_commands.json` 依赖 build/ 已配置过一次；全新 clone 首跑多 ~20s 配置，之后缓存
- 钩子仅本机生效（core.hooksPath 是本地配置）；换机需重新启用一次——文档写明
- clang 版本与 macOS CI 的 Apple Clang 版本不同，诊断不完全等价，但「标准合规性」轴（捕获规则、收窄）一致，覆盖事故级问题足够（2026-08-30 已实测：容器内 clang 19.1.7 对两类事故写法分别报 `cannot be captured because it does not have automatic storage duration` 与 `cannot be narrowed from type 'long' to 'int'`，全部拦截）
- 宿主机内核页缓存缺陷（2026-08-30 取证结论）：宿主 `/usr` 文件页在 mmap 读时跨文件串页，任何直读宿主系统头文件的 clang 均不可用；容器方案是规避手段。若未来内核更新后宿主 clang 恢复正常，可改回宿主直跑（脚本留开关）
