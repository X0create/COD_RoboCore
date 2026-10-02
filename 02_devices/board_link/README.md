# board_link/：设备：板间通信

**状态：通用收发已完成（2026-10-02，ADR 0064），编译 + 主机测试；未上板。当前单板固件没有接线。**

- `board_link.c/.h`：一条跨板消息占一个 CAN ID；帧头 2 字节（`seq` 序号、`age_ms` 数据年龄）+ 载荷。
  载荷 ≤ 6 字节发经典帧，7–62 字节发 CAN FD 帧（只能在全 FD 总线上，ADR 0023）。不做多帧拼接。
- 设计依据：《架构设计》运行时契约第 8 节“板间通信 BoardLink”、ADR 0024、0064。
- 尚未实现（阶段 8，与安全门一起设计，需用户确认）：心跳里的 `boot_id`、协议版本、两块板的解锁互锁
  （`local_fault` / `arm_request` / `armed`）。

## 加一条跨板消息

1. **写编码表**：在本目录新建 `board_link_msgs.h`（两块板的仓库用同一份），每条消息写一个 `BoardLinkConfig`、
   一个载荷结构体和一对编码 / 解码函数。每个字段写明单位、分辨率、范围；统一小端，用 `board_link_put_*` / `get_*`、
   `board_link_float_to_i16()`（超范围截到边界，NaN 编成“无效”）。**不要 memcpy 结构体**（填充字节、编译器可能不同）。
2. **ID**：不能和这路总线上任何电机的控制 / 反馈 ID 重复（DJI 占 0x1FF、0x200、0x2FF、0x201–0x20B 等，达妙占
   配置的 CAN ID 和 Master ID），也不能和别的跨板消息重复。`board_link_init()` 会对本板电机组和本板的其他消息检查。
3. **发送方**：`objects_init()` 里电机都初始化之后 `board_link_init(&msg, &cfg, BOARD_LINK_TX, &motors)`；
   任务里按周期编码载荷 → `board_link_send(&msg, payload, age_ms)`。
4. **接收方**：同样初始化，方向 `BOARD_LINK_RX`；`comm_rx_task.c` 里把这路 CAN 的帧交给它
   （`board_link_receive(&msg, bus, &frame)`，和 `motor_receive` 并列）；使用方 `board_link_read()`，
   返回 false（超时或从未收到）时依赖它的机构执行机构停。
5. 在 `01_applic/README.md`“参数在哪里”登记，并在 `tests/host/02_devices/` 加编码 / 解码的测试。

## 例子：云台板把底盘速度发给底盘板（只是示意，未进代码）

| 字节 | 字段 | 类型 | 分辨率 | 范围 |
| --- | --- | --- | --- | --- |
| 0–1 | `vx_m_s` | int16 小端 | 0.001 m/s | ±32.767 m/s |
| 2–3 | `vy_m_s` | int16 小端 | 0.001 m/s | ±32.767 m/s |
| 4–5 | `wz_rad_s` | int16 小端 | 0.001 rad/s | ±32.767 rad/s |

```c
static const BoardLinkConfig chassis_cmd_link = {
    .name = "chassis_cmd", .can_bus = CAN_BUS_1, .id = 0x110, .payload_len = 6, .timeout_ms = 50,
};

/* 发送方 */
uint8_t p[6];
board_link_put_i16(&p[0], board_link_float_to_i16(vx, 0.001f));
board_link_put_i16(&p[2], board_link_float_to_i16(vy, 0.001f));
board_link_put_i16(&p[4], board_link_float_to_i16(wz, 0.001f));
(void)board_link_send(&chassis_cmd, p, 0u); /* 发送队列满就丢这一帧，下个周期发新的 */

/* 接收方 */
uint8_t p[6];
float vx;
if (!board_link_read(&chassis_cmd, p) || !board_link_i16_to_float(board_link_get_i16(&p[0]), 0.001f, &vx))
{
    /* 离线或无效 → 机构停 */
}
```

- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并运行 `tools/keil_sync.py` 把文件加进 Keil 工程。
