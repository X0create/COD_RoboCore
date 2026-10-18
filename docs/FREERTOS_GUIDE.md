# FreeRTOS 入门与新增任务

给没用过 FreeRTOS 的队员：先用 10 分钟弄懂“任务是怎么跑起来的”，再照第 4 节的步骤加一个自己的任务。
只讲本项目用到的部分；名词第一次出现时都有解释。

## 1. 一句话理解

单片机只有一个 CPU，同一时刻只能执行一段代码。FreeRTOS 把程序拆成几个**任务**，每个任务都是一个自己的
`for (;;)` 死循环，FreeRTOS 在它们之间快速切换，看起来就像同时在跑。

打个比方：CPU 是**一张桌子**，任务是**轮流用桌子的几个人**，每人有一个自己的**抽屉**（任务的栈）。
换人时把桌上的东西收进他的抽屉，再把下一个人的东西摆回来——所以每个人回来时都能接着上次的地方干。

和裸机写法的区别：

| 裸机（一个 `while(1)` 里轮流做） | FreeRTOS（每件事一个任务） |
| --- | --- |
| 打印日志花 3 ms，IMU 这 3 ms 就读不了 | IMU 任务优先级高，到点就打断日志任务先读 IMU |
| 用计数器凑“每 10 次做一次” | 每个任务写自己的周期 |

## 2. 上电后发生什么（会不会进 CubeMX 的 main）

**会进。** `main()` 一定先跑，FreeRTOS 是在 `main()` 最后才启动的，启动后 `main()` 不再返回，
它下面的 `while (1)` 永远执行不到。

```
复位
 → main()                          06_boards/dm_mc02_h723/Core/Src/main.c（CubeMX 生成）
    → HAL_Init()、时钟、MX_xxx_Init()     初始化外设
    → MX_FREERTOS_Init()           Core/Src/freertos.c
        → 创建 startup_task          CubeMX 只建这一个任务
        → app_main()               ← 我们加在 USER CODE 区的唯一一行
             01_applic/system/app_main.c：初始化计时、日志、全车对象，
             再按任务表 task_table[] 创建所有任务（只是创建，还没开始跑）
    → osKernelStart()              ★ FreeRTOS 开始调度，从此不回到 main
 → startup_task 先跑（优先级最高）：允许解锁，然后删除自己
 → 任务表里的任务按优先级轮流运行
```

两个要记住的点：

- **任务列表不在 CubeMX 里**，在 `01_applic/config/robot.c` 的 `task_table[]`。不要在 CubeMX 里加任务。
- CubeMX 重新生成代码可能把 `app_main();` 那一行冲掉，生成后要按 `06_boards/dm_mc02_h723/REGEN_CHECKLIST.md`
  逐条检查（那张表是“CubeMX 重新生成后必须核对的清单”，平时写代码用不到）。

## 3. 任务怎么轮流：记住四条规则

1. **永远运行“想运行的”任务里优先级最高的那个**。数字越大越优先，各任务的优先级见 `robot.c` 的任务表。
2. 高优先级任务一旦想运行，会**立刻打断**低优先级任务（叫“抢占”）。
3. 任务调用延时或等待函数时进入**阻塞**——这段时间不占 CPU，别的任务才有机会运行。
   所以**每个任务的循环里必须有一次阻塞**，否则比它低的任务永远轮不到。
4. 所有任务都在阻塞时，CPU 运行 FreeRTOS 自带的空闲任务（Idle）。

本项目里任务“休息”只有两种写法，都在 `04_core/os/os.h`：

| 写法 | 意思 | 谁在用 |
| --- | --- | --- |
| `rm_task_delay_until(&last_wake, 周期ms)` | 睡到“上次醒来 + 周期”，节拍固定、不会越拖越慢 | control、ins、detect、indicator、log |
| `rm_task_wait_notify(最长等待ms)` | 一直睡，直到中断说“有新数据了”或超时 | comm_rx（CAN、串口收到数据时被中断叫醒） |

（谁来负责切换：内核的 SysTick 时钟中断每 1 ms 检查一次“有没有任务该醒了”，真正的切换由 PendSV 中断完成。
写代码时不用管它们，想深入看第 7 节。）

## 4. 新增一个任务：4 步

### 第 0 步：先确认真的需要新任务

大多数新功能**不需要新任务**。云台、发射这类机构写成模块，在 `control_task` 里每周期调用一次即可
（`CONTRIBUTING.md` 3.2 节）。只有下面几种情况才新建任务：

- 周期和已有任务不同，例如裁判系统 UI 每 100 ms 画一次；
- 要长时间等待某个外设；
- 很耗时，不能拖慢 1 ms 的控制。

### 第 1 步：新建 `01_applic/tasks/<名字>_task.c`

复制下面的模板，把 `ui` 换成你的任务名：

```c
/**
 * @file    ui_task.c
 * @brief   ui_task（10 Hz）：这里写一句话说明这个任务干什么
 * @note    读哪些对象、写哪些对象（同一个对象只能有一个任务写）
 */
#include "01_applic/tasks/tasks.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"

#define UI_PERIOD_MS 100u /* 周期写在文件顶部 */

void ui_task_entry(void *arg)
{
    (void)arg; /* 不用参数就这样写，避免警告 */

    /* 只做一次的准备工作写在这里 */

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        /* 每个周期要做的事写在这里 */

        rm_task_delay_until(&last_wake, UI_PERIOD_MS); /* 必须有：让出 CPU */
    }
}
```

### 第 2 步：在 `01_applic/tasks/tasks.h` 里声明入口

照已有的格式加一行，并顺手更新文件开头注释里的优先级顺序：

```c
/** ui_task（10 Hz）：一句话说明 */
void ui_task_entry(void *arg);
```

### 第 3 步：在 `01_applic/config/robot.c` 的任务表里登记

在任务表那一节加三样东西：

```c
static StackType_t ui_stack[512]; /* ① 栈：512 个字 = 2 KB */

static RmTask ins_task, control_task, detect_task, indicator_task,
    log_task, ui_task;            /* ② 末尾加上任务控制块 */

const TaskTableEntry task_table[] = {
    /* …已有的行… */
    { &ui_task, "ui_task", ui_task_entry, NULL, 2u, ui_stack, STACK_WORDS(ui_stack) }, /* ③ 加一行 */
};
```

`app_main()` 会按这张表自动创建，`app_main.c` 和 CubeMX 都不用动。

- **优先级**：不要比 `control_task` 高（除非比电机控制还急）。慢的、只是显示或打印的，放在最低几级。
- **栈大小**：先开 512 字；函数里有大数组或矩阵运算再加大。上板后在 Ozone 看实际用量，留 30% 以上余量。

### 第 4 步：加进构建

1. 在 `01_applic/CMakeLists.txt` 里那串 `tasks/*.c` 后面加 `tasks/ui_task.c`。
2. 用 Keil 的话运行 `python3 tools/keil_sync.py`。
3. 在 `docs/CALL_FLOW.md` 补上这个任务的调用树，`docs/READING_GUIDE.md` 第一步的任务表加一行。

### 做完自查

- [ ] 编译 0 警告。
- [ ] 任务函数里是 `for (;;)`，循环里有 `rm_task_delay_until` 或 `rm_task_wait_notify`，不会 `return`。
- [ ] 读别的任务的数据，用对方的读取函数（如 `dr16_read()`、`ins_read()`），没有直接改别人的变量。
- [ ] 上板后 Ozone 的 FreeRTOS 窗口里能看到新任务，栈有余量。
- [ ] RTT 日志里 control 的实时性统计（`late_count`）没有因为新任务开始增长。

## 5. 常见错误

| 现象 | 原因 | 怎么改 |
| --- | --- | --- |
| 加了任务后别的任务不动了、灯不闪 | 新任务循环里没有阻塞，一直占着 CPU | 循环末尾加 `rm_task_delay_until` |
| 板子反复复位 | 栈开小了（栈溢出会触发复位），或 control 被拖慢导致看门狗复位 | 加大栈；降低新任务优先级 |
| 读到的数据偶尔是乱的 | 两个任务同时读写同一个结构体 | 一个对象只由一个任务写，别人用读取函数读 |
| 在中断里调用 `rm_task_delay_until` 后死机 | 中断里不能阻塞 | 中断里只用 `rm_task_notify_from_isr()` 叫醒任务，处理放到任务里 |
| 任务函数执行一遍就出错 | 函数 `return` 了 | 只跑一次的任务，最后调用 `rm_task_delete_self()` |
| 直接写了 `vTaskDelay`、`osDelay`、`HAL_Delay` | 绕开了项目封装；`HAL_Delay` 还会空转占 CPU | 统一用 `04_core/os/os.h` 里的 `rm_task_*` |

## 6. 在 Ozone 里看任务

烧录运行后，在 Ozone 的 FreeRTOS 任务窗口（View 菜单里的 RTOS / Tasks）可以看到每个任务的：

- 名字和优先级；
- 状态：Running（正在跑）、Ready（排队）、Blocked（在等）；
- 栈用量峰值——判断栈开得够不够就看这里。

`startup` 任务在启动完成后会删除自己，所以窗口里看不到它是正常的。

## 7. 想深入再看

- **切换的原理**：SysTick 每 1 ms 把到期的任务从“阻塞”放回“就绪”，发现有更高优先级的就绪任务时，
  只“预约”一次 PendSV 中断；PendSV 优先级最低，等其他中断都处理完才执行，它把当前任务的 CPU 寄存器存进
  该任务的栈、再从下一个任务的栈恢复，于是下一个任务从上次停下的地方继续。代码在
  `06_boards/dm_mc02_h723/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c`
  （`xPortSysTickHandler`、`xPortPendSVHandler`）。
- **中断里能调用什么**：只能调用带 `FromISR` 的函数，项目封装为 `rm_task_notify_from_isr()`；
  并且只有优先级数字不小于 `FreeRTOSConfig.h` 中 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 的中断才能调用。
  在 CubeMX 里改中断优先级前先看这一条。
- **两个任务共用数据**：读取函数内部用临界区（`rm_critical_enter()`），保证读到的是完整的一份；
  写法见 `CODING_STANDARD.md` 第 11 节。
- 为什么任务表不放在 CubeMX 里：`ADR.md` 的 0025。
- FreeRTOS 官方入门书《Mastering the FreeRTOS Real Time Kernel》（freertos.org 可免费下载）。
