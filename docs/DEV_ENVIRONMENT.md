# COD RoboCore 开发环境配置（Windows + WSL）

更新时间：2026-09-28（原为工作区根目录的 `Configuration Environment.md`，2026-09-28 移入仓库）
适用：Windows 10/11 电脑，为 COD RoboCore 搭建编译、测试和调试环境。第 6–9 节是用 COD\_UniCFramework 做的基线验证，它的固件可作为“已知正常”的对照固件，排查硬件问题时先烧它。

> 每一步都标了验证状态：**已验证**表示在一台电脑上实际跑过，**待验证**表示还没跑过。
> 遇到和文档不一样的输出，先不要自己乱改，把完整输出发给懂的人看。

## 0. 为什么要这样搭

- UniC 框架只支持在 **Linux（Ubuntu 24.04）** 下编译和测试。
- Windows 上用 **WSL**（Windows Subsystem for Linux）运行 Linux。它是 Windows 自带的功能，
  不用装双系统，也不用虚拟机软件。WSL 里装的每个 Linux 叫一个“发行版”。
- 必须用 **24.04**：项目 CI 跑在 Ubuntu 24.04 上，格式检查工具固定为 `clang-format-18`、
  `clang-tidy-18`。22.04 默认软件源没有这两个版本，格式检查可能误报失败。
- 已经装了其他版本的 Ubuntu 也没关系，可以和 24.04 并存，互不影响。

## 1. 查看已安装的 WSL 发行版（已验证 2026-09-24）

**终端：** Windows PowerShell，任意目录。只查看，不修改。

```powershell
wsl --list --verbose
```

正常输出示例：

```
  NAME            STATE           VERSION
* Ubuntu-22.04    Stopped         2
```

- `VERSION` 应为 `2`。
- 带 `*` 的是默认发行版。
- 如果列表里已有 `Ubuntu-24.04`，跳过第 2 步。

## 2. 安装 Ubuntu 24.04（已验证 2026-09-24）

**终端：** Windows PowerShell，任意目录，需要联网。
**会改动：** 新建一个 Ubuntu-24.04 系统，默认占用 C 盘几 GB；不影响已有的其他发行版。

```powershell
wsl --install -d Ubuntu-24.04
```

过程：

1. 显示“正在下载 / 正在安装 Ubuntu 24.04 LTS”。
2. 提示 `Create a default Unix user account:`：输入全小写英文用户名，不带空格。
3. 提示 `New password:` 和 `Retype new password:`：设置密码。**输入时屏幕不显示任何字符，
   这是正常的。** 以后 `sudo` 会用到这个密码，请记住。
4. 出现 `用户名@电脑名:/mnt/c/Users/你的用户名$`，说明已经进入 Ubuntu 24.04。

说明：

- `/mnt/c` 是 Windows 的 C 盘，`/mnt/d` 是 D 盘。
- 以后从 PowerShell 进入它：`wsl -d Ubuntu-24.04`。只输入 `wsl` 进的是默认发行版，不一定是它。
- 如果提示需要重启，重启后再执行一次同一条命令。

## 3. 确认系统版本（已验证 2026-09-24）

**终端：** Ubuntu 24.04，任意目录。只查看，不修改。

```bash
lsb_release -a
```

应看到 `Release: 24.04`，`Codename: noble`。第一行 `No LSB modules are available.` 可以忽略。

## 4. 安装编译、测试和检查工具（已验证 2026-09-24）

**终端：** Ubuntu 24.04，任意目录，需要联网。
**会改动：** 只在这个 Ubuntu 系统里安装软件，不改项目文件。

**不用代理、能直接访问 Ubuntu 软件源时：**

```bash
sudo apt update && sudo apt install -y git cmake make gcc python3 ruby cppcheck clang-format clang-format-18 clang-tidy-18 clangd openocd
```

**Windows 上开着代理软件（如 Clash）时**，改用下面两条。`127.0.0.1:7897` 换成你代理软件的
HTTP 端口；可以在 Ubuntu 里执行 `env | grep -i proxy` 查看，`http_proxy=` 后面就是地址：

```bash
sudo apt -o Acquire::http::Proxy=http://127.0.0.1:7897 -o Acquire::https::Proxy=http://127.0.0.1:7897 update
```

```bash
sudo apt -o Acquire::http::Proxy=http://127.0.0.1:7897 -o Acquire::https::Proxy=http://127.0.0.1:7897 install -y git cmake make gcc python3 ruby cppcheck clang-format clang-format-18 clang-tidy-18 clangd openocd
```

COD RoboCore 模板另外需要 Ninja（CubeMX 生成的 CMake 预设默认用它）。长命令容易折行，可以先把代理存进变量：

```bash
P=http://127.0.0.1:7897
sudo apt -o Acquire::http::Proxy=$P -o Acquire::https::Proxy=$P install -y ninja-build
```

- `sudo`：以管理员身份执行，会要求输入 Linux 密码（不显示字符）。
- `apt update`：刷新软件清单；`apt install`：安装软件；`&&`：前一条成功才执行后一条。
- `-o Acquire::...::Proxy=...`：只对这一次 apt 指定代理，不改任何配置文件。
- **长期做法（2026-09-27 起本机已采用）**：写入 `/etc/apt/apt.conf.d/95proxy`，让所有 apt（包括 WSL 启动后的自动更新）都走代理，
  以后不用再加 `-o`。代价是代理软件没开时 apt 连不上；撤销用 `sudo rm /etc/apt/apt.conf.d/95proxy`。
  ```bash
  L1='Acquire::http::Proxy "http://127.0.0.1:7897";'
  L2='Acquire::https::Proxy "http://127.0.0.1:7897";'
  printf '%s\n' "$L1" "$L2" | sudo tee /etc/apt/apt.conf.d/95proxy
  apt-config dump | grep -i proxy
  ```
- **命令必须作为一整行粘贴。** 如果粘贴时被折成两行，第二行会被当成另一条命令，出现
  `clang-tidy-18: command not found` 这类报错。

| 软件 | 用途 |
| --- | --- |
| `git` | 版本管理 |
| `cmake`、`make` | 构建工具 |
| `gcc` | 电脑本机 C 编译器，用于在电脑上跑单元测试 |
| `python3`、`ruby` | 测试辅助脚本；CMock 用 Ruby 生成模拟的硬件函数 |
| `cppcheck` | 静态代码检查，质量门禁需要 |
| `clang-format(-18)`、`clang-tidy-18` | 代码格式和风格检查，版本与 CI 一致 |
| `clangd` | VS Code 代码跳转和补全 |
| `openocd` | 烧录和调试单片机（WSL 访问 USB 调试器需要另外配置） |

成功标志：最后没有以 `E:` 开头的行，末尾是若干 `Processing triggers for ...`，并回到 `$` 提示符。

实测安装到的版本（与项目 CI 固定的版本一致）：`clang-format-18` / `clang-tidy-18`
1:18.1.3-1ubuntu1，`cppcheck` 2.13.0-2ubuntu3，`ruby3.2` 3.2.3-1ubuntu0.24.04.8；另有
`cmake` 3.28.3（项目要求 ≥ 3.22）、`gcc` 13.3.0、`openocd` 0.12.0。

### 可选：换成国内镜像源

软件源默认是 Ubuntu 官方服务器。国内直连慢时可以换成中科大镜像。**会改动**
`/etc/apt/sources.list.d/ubuntu.sources`，先备份：

```bash
sudo cp /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources.bak
```

然后把该文件内容替换为（`URIs` 一行是镜像地址）：

```
Types: deb
URIs: https://mirrors.ustc.edu.cn/ubuntu
Suites: noble noble-updates noble-backports noble-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
```

恢复官方源：`sudo cp /etc/apt/sources.list.d/ubuntu.sources.bak /etc/apt/sources.list.d/ubuntu.sources`。

注意：如果电脑开着代理软件，换镜像**不能**解决连不上的问题，仍需按上面的代理写法执行。

## 5. 安装 ARM 交叉编译器 arm-gcc（进行中）

单片机固件要用专门的 `arm-none-eabi-gcc` 编译，项目使用 15.2 版本。它不从 apt 安装，
而是从 ARM 官网下载压缩包。

> 交叉编译：在电脑上编译出给另一种芯片（这里是 ARM 单片机）运行的程序。
> `arm-none-eabi`：目标是 ARM、无操作系统（裸机）、遵循 ARM 的 EABI 二进制接口标准。

`~` 是 Linux 家目录（`/home/你的用户名`），与 Windows 的 `C:\Users\...` 不是同一个地方。

### 5.1 下载（已验证 2026-09-24）

**终端：** Ubuntu 24.04，任意目录。不需要 `sudo`，所以会自动使用 `http_proxy` 代理。
**会改动：** 新建 `~/Downloads`、`~/tools` 两个文件夹，下载约 149 MB 压缩包到 `~/Downloads`。

```bash
mkdir -p ~/Downloads ~/tools && curl -L --fail -o ~/Downloads/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz && ls -lh ~/Downloads/
```

- UniC README 的原命令直接下载到 `~/Downloads`，但新装的 WSL 没有这个目录，所以这里先 `mkdir -p`。
- `-L` 跟随官网跳转（进度表第一行约 257 字节是跳转通知，正常）；`--fail` 出错时不把错误网页存成文件。
- 成功标志：`ls` 显示压缩包约 **149M**。只有几 KB 说明下载的不是压缩包。

### 5.2 校验（已验证 2026-09-24）

用 ARM 官方公布的 SHA-256 “指纹”确认文件完整、未被改动。只多下载一个约 100 字节的文件。

```bash
cd ~/Downloads
curl -L --fail -O https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz.sha256asc
cat *.sha256asc
sha256sum -c *.sha256asc
```

成功标志：最后一行是 `arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz: OK`。
2026-09-24 实测官方 SHA-256：
`597893282ac8c6ab1a4073977f2362990184599643b4c5ee34870a8215783a16`。
出现 `FAILED` 时删掉压缩包重新下载。

### 5.3 解压并试运行（已验证 2026-09-24）

**会改动：** 在 `~/tools` 下新建工具链文件夹（约 1 GB）；压缩包保留。`tar` 执行期间没有输出，
需要等几十秒。

```bash
tar -xf ~/Downloads/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz -C ~/tools
ls ~/tools
~/tools/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc --version
```

成功标志：版本行为
`arm-none-eabi-gcc (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203`。

### 5.4 加入 PATH，设置 `ARM_TOOLCHAIN_BIN`（已验证 2026-09-24）

- `PATH`：系统查找命令的目录清单。加入后可直接输入 `arm-none-eabi-gcc`。
- `ARM_TOOLCHAIN_BIN`：项目约定的变量，`.vscode/settings.json` 里 clangd 的 `--query-driver`
  用它找编译器；不设会影响 VS Code 代码跳转和补全，不影响编译。
- `~/.bashrc`：每次打开终端自动执行的配置脚本。

**会改动：** 备份 `~/.bashrc` 为 `~/.bashrc.bak`，并在 `~/.bashrc` 末尾追加两行。
**两条 `echo` 只执行一次**，重复执行会产生重复行。`>>` 是追加，写成 `>` 会清空整个文件。

```bash
cp ~/.bashrc ~/.bashrc.bak
echo 'export PATH=$HOME/tools/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin:$PATH' >> ~/.bashrc
echo 'export ARM_TOOLCHAIN_BIN=$HOME/tools/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin' >> ~/.bashrc
source ~/.bashrc
tail -n 2 ~/.bashrc
which arm-none-eabi-gcc
echo $ARM_TOOLCHAIN_BIN
arm-none-eabi-gcc --version | head -n 1
```

成功标志：`which` 输出 `/home/你的用户名/tools/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc`，
最后一行是 15.2.1 版本号。

## 6. 获取项目代码（进行中）

### 为什么要在 WSL 里重新克隆，而不是直接用 Windows 上的那份

2026-09-24 检查发现：在 Windows 上克隆的 `COD_UniCFramework`，文件行尾是 Windows 格式
（CRLF），`build.sh` 等全部 21 个脚本都是如此；仓库配置里 `filemode = false`，也丢了脚本的
“可执行”权限。

> **行尾（换行符）是什么：** 每行文字末尾有一个看不见的“换行”标记。Linux 用一个字符 `LF`，
> Windows 用两个字符 `CRLF`。Windows 版 Git 默认会在取出代码时把 `LF` 自动改成 `CRLF`。

在 Linux 里使用这份代码会出现：

- 运行脚本报 `/usr/bin/env: 'bash\r': No such file or directory` 或 `bad interpreter`；
- 质量门禁校验 `05_vender` 第三方文件的 SHA-256 指纹，行尾一变指纹就全部对不上；
- 此外在 WSL 里访问 `/mnt/d` 上的文件比访问 Linux 家目录慢很多，编译会明显变慢。

所以：**在 WSL 的 Linux 家目录里用 Linux 的 `git` 重新克隆一份用于开发。**Windows 上那份只作阅读参考，
不在 WSL 里用它构建。

### 6.1 克隆并检查（已验证 2026-09-24）

**终端：** Ubuntu 24.04。git 会自动使用 `https_proxy` 代理。
**会改动：** 新建 `~/work/COD_UniCFramework`，不动 Windows 上的文件。

```bash
mkdir -p ~/work && cd ~/work
git clone https://github.com/fakeSkyy/COD_UniCFramework.git
cd COD_UniCFramework
git log --oneline -3
grep -c $'\r' build.sh
ls -l build.sh
```

成功标志：

- `grep -c $'\r' build.sh` 输出 `0`，即没有 Windows 行尾；
- `ls -l build.sh` 以 `-rwxr-xr-x` 开头，`x` 表示可执行；
- 实测 `build.sh` 为 12958 字节，Windows 副本为 13264 字节，差的 306 字节正好是 306 个 `\r`。

2026-09-24 克隆到的最新提交：`4220d9c build: support J-Link alongside CMSIS-DAP, and fix the RTT search order`。

## 7. 基线构建 H723 固件（已验证 2026-09-24）

**终端：** Ubuntu 24.04，目录 `~/work/COD_UniCFramework`。不需要连接开发板。
**会改动：** 新建 `build/` 存放编译产物。它已在 `.gitignore` 中，不会影响 Git 状态，也不会修改源码。

```bash
time ./build.sh
```

- 构建分三步：配置（CMake 生成 Makefile）、编译（`.c` 生成 `.o`）、链接（生成 `.elf`，并统计内存）。
- `build.sh` 只要出现警告就判定失败，项目要求保持 **0 警告**。不要用 `ALLOW_WARNINGS=1` 绕过。
- 成功标志：结尾是 `==> OK  (N file(s) compiled, 0 warnings)`。
- 构建类型只在 `build/` 第一次配置时读取，默认 `RelWithDebInfo`。要换类型，用 `BUILD_TYPE=... ./build.sh clean`。

2026-09-24 实测基线（`4220d9c`，RelWithDebInfo，arm-gcc 15.2.1）：

| 内存区 | 已用 | 总量 | 占比 |
| --- | --- | --- | --- |
| FLASH | 106584 B | 1 MB | 10.16% |
| DTCMRAM | 74872 B | 128 KB | 57.12% |
| RAM_D1（AXI SRAM） | 32 B | 320 KB | 0.01% |
| RAM_D2 / RAM_D3 / ITCMRAM | 0 B | 32 / 16 / 64 KB | 0% |

编译了 120 个文件，0 警告；首次构建耗时 8.5 s（real）。

## 8. 主机测试（已验证 2026-09-24）

在电脑上运行框架代码的单元、集成和质量测试。硬件相关函数由 CMock 生成的“替身”代替，
所以**主机测试全过不等于上板没问题**（时序、中断、DMA、电机响应都测不到）。

**终端：** Ubuntu 24.04，目录 `~/work/COD_UniCFramework`。
**会改动：** 测试构建放在 `/tmp/COD_UniCFramework-build-host`。脚本要求构建目录在仓库外；
`/tmp` 在 WSL 重启后可能被清空，重新运行即可。不改源码。

```bash
time ./tests/run_tests.sh /tmp/COD_UniCFramework-build-host
```

成功标志：`100% tests passed, 0 tests failed out of 258`。`Label Time Summary` 按标签分类统计，
一个测试可以带多个标签，所以各行数量加起来会超过总数。`quality` 标签下的 2 个测试
需要 `cppcheck`、`clang-tidy-18`、`clang-format`，缺少时会失败。

报告位置：`/tmp/COD_UniCFramework-build-host/reports/current.md`。

2026-09-24 实测：258/258 通过（含 2 个 quality 门禁），CTest 用时 17.1 s，全程 23.2 s。

## 9. Ozone 调试工程（Windows 端，已验证 2026-09-25）

用法：在 WSL 里编译，在 Windows 上用 Ozone + J-Link 烧录和调试。Windows 能直接读取 WSL 文件
（`\\wsl.localhost\Ubuntu-24.04\...`），所以不需要 usbipd。**Ozone 只支持 J-Link**，DAP 类调试器不能用。

### 安装注意

- 新旧版本可以并存。J-Link 软件和 Ozone 都按版本装在各自的文件夹里；USB 驱动只有一份，新版兼容旧版软件。
- 安装时弹出 **J-Link DLL Updater**：如果不想改动 Keil 等软件所用的 J-Link 组件，就全部不选。
- **兼容版（非原厂）J-Link**：新版软件提示升级 J-Link 固件时，要谨慎，可能升级后无法使用。

### 新建工程（已验证 2026-09-25，Ozone V3.50b）

**File → New → New Project Wizard**：

| 页面 | 设置 |
| --- | --- |
| Device | `STM32H723VG`；Register Set `Cortex-M7`；Peripherals 选仓库自带的 `05_vender/stm32cubemx/STM32H723.svd`（通过 `\\wsl.localhost\...` 路径选择）。Ozone 自带的 `Config\Peripherals` 里没有 H7 的文件，这是正常的 |
| Connection | SWD；首次连接用 1 MHz；Host Interface 选 USB |
| Program File | `\\wsl.localhost\Ubuntu-24.04\home\<用户名>\work\COD_UniCFramework\build\COD_UniFramework_H7.elf` |

工程文件保存在 WSL 仓库之外（例如 `D:\RoboMaster\Ozone\MC02_H723.jdebug`），否则它会出现在 Git 的未跟踪文件里。

### 手动在 `.jdebug` 的 `OnProjectLoad` 中补两行

加在 `File.Open` 之前。先关闭 Ozone，用记事本修改，再重新打开工程：

```c
  Project.SetOSPlugin ("FreeRTOSPlugin_Cortex-M");
  Project.AddPathSubstitute ("/home/<用户名>", "//wsl.localhost/Ubuntu-24.04/home/<用户名>");
```

- `SetOSPlugin`：开启 FreeRTOS 支持，可以查看每个任务的状态、优先级和栈使用量。固件使用的是
  FreeRTOS 的 `ARM_CM4F` 移植；`FreeRTOSPlugin_CM4/_CM7` 在 V3.50b 中会提示已弃用，所以用通用的 `Cortex-M` 版。
- `AddPathSubstitute`：ELF 里记录的是 Linux 源码路径，Windows 上的 Ozone 需要把它映射成 `//wsl.localhost/...`
  才能找到源码。只映射 `~/work` 时，`~/tools` 下的 6 个工具链头文件（`stdarg.h`、`math.h` 等）会找不到，
  所以映射整个家目录。

成功标志：Console 中出现 `RTOS awareness plugin loaded ... FreeRTOSPlugin_Cortex-M.js`、`RTT active`、
`192 source file paths resolved`（数量随代码变化），并且没有 `source files not found`；双击 `main` 能打开
`05_vender/stm32cubemx/Core/Src/main.c`。（2026-09-25 已验证）

### 首次连接：先 Attach，不烧录（已验证 2026-09-25）

先确认接线和通信正常，不改板上原有程序。

- 断电状态下接 SWD：J-Link 1 脚 VTref 接板上 **3.3V**，7 脚接 SWDIO，9 脚接 SWCLK，4 脚（或其他偶数脚）接 GND。
  **19 脚（5V）不接**。以板上丝印为准。CAN 口不接电机和电调。
- 先插 J-Link USB，再接通电池。
- 点 Ozone 绿色电源按钮旁的 **▾ → Attach to Running Program**。直接点按钮本身会下载并复位。
- 成功标志：`Found Cortex-M7 r1p2`、`Connected to target device`；DPIDR 为 `0x6BA02477`。
- 如果提示更新 J-Link 固件，兼容版 J-Link 选 No。

### 烧录并运行（已验证 2026-09-25）

**会覆盖芯片 Flash 中原有的程序，无法撤销。**

1. **Debug → Stop Debug Session**，结束 Attach。
2. 直接点绿色电源按钮本身（Download & Reset Program）。烧录后程序会停在 `main` 开头。
3. 按 F5 继续运行。**上电或复位后约 2 s 内不要移动板子**，这段时间在做陀螺仪零偏校准。
4. **View → Terminal** 查看 RTT 日志。

成功标志：

- Console 出现 `Flash download: ... Verify`，并且没有错误。之后 ELF 没变时再次烧录，会显示 `Skipped. Contents already match`。
- LED **绿色每秒闪 2 下**。
- Terminal 中出现 `[I][imu] BMI088 ready`、`[I][app] starting scheduler`、`imu ok`、`gyro calibrated`、`indicating: alive`。

Console 里出现大量 `RTT: Failed to load RTT control block at address ...` **不用管**：它们出现在固件初始化
RTT 之前，或者板上还是旧程序时，程序跑起来后就会停止。

### 查看 FreeRTOS 任务（已验证 2026-09-25）

先暂停程序（Debug → Halt），再打开 **View → FreeRTOS**。读数时注意：

- `Stack Info` 的格式是 `剩余字节 / 总大小`，并且是**暂停那一刻**的值，不是历史最大占用。
  总大小显示 `N/A`，是因为 FreeRTOS 没有开启 `configRECORD_STACK_HIGH_ADDRESS`。
- `Run Count` 显示 `N/A`，是因为 `configGENERATE_RUN_TIME_STATS 0`。
- **接着电机时不要随手暂停**：暂停后 CAN 指令会停发，电机怎么反应取决于电调，尚未实测。

注意：Ozone 使用它自带的 J-Link DLL（Console 中的 `J-Link software found at: ...Ozone V3.50b/JLink_x64.dll`），
而不是单独安装的 J-Link 软件包。

## 10. 烧录 COD RoboCore 新模板固件（已验证 2026-09-28）

新模板在 D 盘的仓库里构建（WSL 路径 `/mnt/d/...`），ELF 可以直接用 Windows 路径打开，不用 `\\wsl.localhost`。

1. WSL 中构建：在仓库根目录 `cmake --preset h723-template-debug && cmake --build --preset h723-template-debug`。
2. Ozone 打开 `MC02_H723.jdebug`。2026-09-28 起它的 `OnProjectLoad` 已改为默认打开 COD RoboCore 的 ELF，
   并加了 `Project.AddPathSubstitute ("/mnt/d", "D:");`（ELF 里记的是 `/mnt/d/...`，不加则源码窗口找不到文件）。
   2026-09-29 起根目录 `CMakeLists.txt` 用 `-fdebug-prefix-map` 把调试信息里的 `/mnt/<盘符>/` 直接写成 `<盘符>:/`，
   这行映射已用不上，留着无害。
   改之前的原文件备份为 `MC02_H723.jdebug.bak-2026-09-28`；要调试 UniC 时把文件里注释掉的那行 `File.Open` 换回来。
3. **Download & Reset → Continue**，**View → Terminal** 看 RTT。

注意：Terminal 窗口底部的输入框是**发给单片机的 RTT 数据**，Ozone 命令要输在 Console 窗口的输入框里；
输错时会报 `... could not be written to target's real time transfer buffer`。

正常输出：`booting` → `starting scheduler` → `startup done` → 每 500 ms 一行 `alive N`，左边是上电以来的毫秒数。

Console 里在程序跑到 `rm_log_init()` 之前会刷 `RTT: Failed to load RTT control block at address ...`，这是正常的：
RTT 控制块在运行时才写好。地址应与 ELF 中 `_SEGGER_RTT` 一致（2026-09-28 为 `0x2000322C`）。

## 11. CLion 编译与写代码（CLion 2026.2，已验证 2026-09-28）

前提：WSL 里装好 `g++`、`gdb`（`sudo apt install -y g++ gdb`；项目是纯 C，但 CLion 检查工具链时需要它们）。

1. **设置 → 构建、执行、部署 → 工具链**：`+` → **WSL**，环境选 Ubuntu-24.04，其余自动检测
   （`/usr/bin/cmake`、`/usr/bin/ninja`、`/usr/bin/gcc`、`/usr/bin/g++`、WSL GDB），并移到列表最上面作为默认。
2. **File → Open** 选仓库根目录 `COD_RoboCore/`。**设置 → CMake** 里启用 `host-tests` 和 `h723-template-debug`
   两个来自 `CMakePresets.json` 的配置文件，关掉 CLion 默认的 `Debug`。列表里灰色的同名条目是预设原件，不用管。
3. **编译固件**：右边运行配置框选 `COD_RoboCore`，左边自动是 `h723-template-debug`，按 **Ctrl+F9（构建）**。
   **不要按运行 ▶**：ELF 是单片机程序，电脑上执行会报 `Exec format error`；烧录和调试用 Ozone（第 10 节）。
4. **跑单元测试**：右边运行配置框选 `All CTest`（左边会自动切到 `host-tests`），按 **运行 ▶**。
   左边框只列出“当前选中的目标”所在的配置，所以要先选目标再看配置。
5. **格式化**：CLion 检测到仓库的 `.clang-format` 后会自动启用 ClangFormat，**Ctrl+Alt+L** 即按项目规则格式化。
   CLion 自带的 clang-format 版本可能与 CI 固定的 18.1.3 不同，提交前以 WSL 里 `clang-format-18` 的检查结果为准。

交叉编译器由 `cmake/toolchain-arm-gcc.cmake` 在 `~/tools/arm-gnu-toolchain-*/bin` 中自动查找，CLion 不需要额外设置环境变量。
构建目录与命令行共用（`build/host`、`build/h723-template-debug`）。

### 11.1 在 CLion 里烧录和调试（J-Link，2026-09-29 烧录已验证，断点见下）

原理：CLion 在 **Windows 上**启动 J-Link 的 GDB 服务器（连板子），再用 **Windows 版** `arm-none-eabi-gdb` 连
`localhost:2331`。WSL 里的 GDB 用不了：“自定义 GDB”一栏填 `/home/...` 会被 CLion 改成 `\home\...`，
Ubuntu 自带的 `/usr/bin/gdb` 又只认电脑程序。调试时要先关掉 Ozone（J-Link 同一时间只能被一个程序占用）。

**运行 → 编辑配置… → `+` → 嵌入式 GDB 服务器**：

| 字段 | 填写 |
| --- | --- |
| 目标 / 可执行的二进制文件 | `COD_RoboCore` |
| 调试器 | 自定义 GDB 可执行文件：CLion 自带的 `C:\Users\<用户名>\AppData\Local\Programs\CLion\bin\gdb\win\x64\bin\gdb.exe`（带 Python，能读 ARM 程序）。STM32CubeCLT 的 `arm-none-eabi-gdb.exe` 也能用，但不带 Python，会报 pretty-printer 错误 |
| 上传可执行文件 | 始终（“如果已更新”在程序没变时不下载也不复位，会直接连上正在跑的程序，`app_main` 的断点停不下来） |
| 'target remote' 实参 | `localhost:2331` |
| GDB 服务器 | `C:\Program Files\SEGGER\JLink_V980\JLinkGDBServerCL.exe` |
| GDB 服务器实参 | `-select USB -device STM32H723VG -if SWD -speed 1000 -port 2331 -nogui -singlerun -rtos GDBServer/RTOSPlugin_FreeRTOS` |
| 重置命令 | `monitor reset`（下载后） |
| 执行前 | 构建 |

配置保存在 `.idea/runConfigurations/MC02_J_Link.xml`（`.idea/` 不进 Git）。手写这个文件时注意：
`CONFIG_NAME` 必须是 CLion 里的 CMake 配置全名 `h723-template-debug - h723-template-debug`
（写错时报“未指定可执行文件”）；自定义调试器写成 `<debugger kind="GDB">路径</debugger>`；
GDB 服务器路径是 `custom-gdb-server` 的 `executable` 属性，实参是 `PROGRAM_PARAMS`。

- **RTT 日志**：CLion 本身不显示 RTT。另建一个“Shell 脚本”运行配置“RTT 日志”（解释器 `C:\Windows\System32\cmd.exe`、
  选项 `/c`、脚本 `C:\Program Files\SEGGER\JLink_V980\JLinkRTTClient.exe`，文件 `.idea/runConfigurations/RTT.xml`），
  再建“复合”配置“MC02 调试 + RTT”同时启动两者（**未验证**：2026-09-30 贴回的 RTT 实际来自单独打开的 exe）。
  更好的做法是 CLion 的 **Segger J-Link 调试服务器**：自带 RTT 控制台和实时监视（见 11.2，待验证）。
  RTTClient 连的是 GDB 服务器的 `localhost:19021`。
- **实时监视**：只有“调试服务器”方式（如 Segger J-Link 调试服务器）才能用；“嵌入式 GDB 服务器”运行配置下不可用（2026-09-30 用户实测）。
- **断点与源码**：ELF 里的源码路径若是 `/mnt/d/...`，Windows 版 GDB 报 `No source file named D:/...`、断点打不上。
  根目录 `CMakeLists.txt` 已在 `/mnt/<盘符>/` 下构建时加 `-fdebug-prefix-map`，把路径写成 `D:/...`。
- `Error during python setup: Undefined info command: "pretty-printer"`：CubeCLT 的 GDB 不带 Python 时出现，不影响调试；
  换成 CLion 自带的 GDB 后消失（2026-09-30）。自带 GDB 启动时的 `A handler for the OS ABI "Windows" is not built into
  this configuration` 警告不用管，它随后按 armv7e-m 工作。
- 兼容版 J-Link 用 J-Link V9.80 软件可以连接和烧录（2026-09-29），没有提示升级固件。

## 12. 推送到 GitHub（已验证 2026-09-28）

仓库：<https://github.com/X0create/COD_RoboCore>（公开）。以下设置只写在本仓库的 `.git/config`，不影响其他仓库：

```bash
git config user.email "<你的 GitHub 隐私邮箱>"   # GitHub → Settings → Emails，形如 数字+用户名@users.noreply.github.com
git config credential.helper "/mnt/c/Program\ Files/Git/mingw64/bin/git-credential-manager.exe"
git config http.proxy http://127.0.0.1:7897
git remote add origin https://github.com/<用户名>/COD_RoboCore.git
git push -u origin main
```

- 用 Windows Git 自带的凭据管理器（GCM）登录：第一次推送弹出窗口，选 **Sign in with your browser** 授权，之后自动记住。
  **不要在 `Username for 'https://github.com':` 提示里输密码**，GitHub 不接受密码推送。
- 用隐私邮箱是为了公开仓库时不暴露真实邮箱；GitHub 的 Emails 设置里可以再勾选
  “Block command line pushes that expose my email”，用了真实邮箱的提交会被拒绝推送。
- GCM 是 Windows 程序，需要 WSL 能运行 `.exe`（见常见问题中的 `Exec format error`）。

## 常见问题

| 现象 | 原因和处理 |
| --- | --- |
| 输入密码时屏幕没反应 | 正常，Linux 输入密码不显示字符，输完回车即可 |
| 下载慢或 `Failed to fetch` | 网络问题；检查代理后重试 |
| 不确定自己在哪个 Ubuntu 里 | 执行 `lsb_release -a` 看 `Release` |
| `apt update` 一直 `Ign:`，最后 `Connection timed out`，IP 是 `198.18.x.x` | 代理软件的“假 IP”模式把域名解析成了只有经过代理才能访问的地址，而 `sudo` 默认不继承你的代理环境变量，所以 apt 直连失败。按第 4 步的代理写法执行 |
| `curl` 等普通命令能下载，`sudo apt` 不行 | 同上：普通命令会用 `http_proxy` 环境变量，`sudo` 执行的命令默认不会 |
| `xxx: command not found`，而 xxx 是上一条命令的参数 | 长命令粘贴时被折成了两行，重新作为一整行粘贴 |
| `curl: (2) no URL specified`，下一行把网址当命令报 `No such file or directory` | 同上，网址被换行拆开了。从文档代码框复制，不要从终端历史复制（终端的显示折行会混进换行） |
| WSL 里运行任何 Windows 程序（`cmd.exe`、Git 凭据管理器、`code .`）都报 `Exec format error` | WSL 开了 systemd（`/etc/wsl.conf` 中 `systemd=true`）后，`systemd-binfmt` 重新加载时会清掉运行 `.exe` 的注册。永久修复：`echo ':WSLInterop:M::MZ::/init:PF' \| sudo tee /etc/binfmt.d/WSLInterop.conf`，再 `sudo systemctl restart systemd-binfmt`；用 `/mnt/c/Windows/System32/cmd.exe /c ver` 验证（2026-09-28 实际遇到并按此解决）。2026-09-30 又出现：`WSLInterop.conf` 还在，但 `/proc/sys/fs/binfmt_misc/` 里没有 `WSLInterop`（`git push` 卡住 40 多分钟，加 `GIT_TRACE=1` 才看到凭据管理器 `Exec format error`）。临时办法：在 Windows 的 Git Bash 里推送；恢复要 `sudo systemctl restart systemd-binfmt` |
| apt 一直显示 `Waiting for cache lock ... held by process N (unattended-upgr)` | WSL 启动后的自动安全更新不走代理，卡在下载。先用 `ps -o pid,etime,cmd --ppid N` 确认子进程是 `/usr/lib/apt/methods/https` 之类的下载器、不是 `dpkg`，再 `sudo kill -TERM <下载器 PID>`，它会自行退出并放开锁；**不要** `kill -9`，不要删锁文件（2026-09-27 实际遇到并按此解决） |

## 验证记录

| 日期 | 步骤 | 结果 |
| --- | --- | --- |
| 2026-09-24 | 1–3 | 在一台 Windows 11 电脑上完成，Ubuntu 24.04.5 LTS，WSL 2 |
| 2026-09-24 | 4 | 同一台电脑；开着代理，换中科大镜像并用 `-o Acquire::...Proxy` 后安装成功，无 `E:` 报错 |
| 2026-09-24 | 5.1–5.2 | 下载 149M，`sha256sum -c` 结果 OK |
| 2026-09-24 | 5.3–5.4 | arm-none-eabi-gcc 15.2.1 20251203，PATH 和 `ARM_TOOLCHAIN_BIN` 生效 |
| 2026-09-24 | 6.1 | 克隆到 `4220d9c`，与 Windows 副本 HEAD 相同；LF 行尾，脚本可执行 |
| 2026-09-24 | 7 | `./build.sh` OK，120 文件，0 警告，FLASH 106584 B，DTCMRAM 74872 B |
| 2026-09-24 | 8 | 258/258 通过，含 2 个 quality 门禁 |
| 2026-09-25 | 9（离线部分） | Ozone V3.50b 加载 ELF、两份 SVD、Cortex-M FreeRTOS 插件；映射家目录后 192 个源文件全部找到，`main.c` 可显示 |
| 2026-09-25 | 9（Attach） | 兼容版 J-Link，SWD 1 MHz，连上 Cortex-M7 r1p2 |
| 2026-09-25 | 9（烧录、运行） | 烧录约 3.1 s 并通过校验；LED 绿色心跳；RTT 日志正常；不接电机 |
| 2026-09-27 | 4（补充） | ninja-build 1.11.1 安装成功；cmake 3.28.3、gcc 13.3.0、clang-format 18.1.3 |
| 2026-09-28 | 12 | 修复 WSL interop 后，GCM 浏览器登录，`git push -u origin main` 成功（24 个提交），远程与本地 `940dbf0` 一致 |
| 2026-09-28 | 11 | CLion 2026.2 + WSL 工具链：Rebuild 固件 81 个文件，FLASH 89904 B；All CTest 2/2 通过 |
| 2026-09-28 | 10 | COD RoboCore `ebb48ed` 烧录运行，RTT 心跳正常，时间戳跨过 DWT 回绕连续 |
| 2026-09-29 | 11.1 | CLion 嵌入式 GDB 服务器 + J-Link V9.80 + CubeCLT 1.19 的 GDB：兼容版 J-Link 连接、FreeRTOS 插件加载、烧录成功；RTTClient 收到启动日志 |
| 2026-09-29 | 11.1（断点） | `-fdebug-prefix-map` 后 Windows 版 GDB 离线对 ELF 设 `app_main.c`、`tasks.c` 断点成功（未上板复测） |
| 2026-09-30 | 11.1（GDB） | 换 CLion 自带 GDB 17.1 后 python 报错消失；RTT 复合配置未验证，实时监视在此方式下不可用 |
| 2026-09-27 | 4（apt 永久代理） | 写入 `95proxy` 后，不带 `-o` 的 `sudo apt update` 成功（7144 kB，2 s） |
