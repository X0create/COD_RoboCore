# msgs/

模块之间传递的消息：消息结构体、枚举、话题类型，以及认领 / 发布 / 读取函数
（例如 `robot_cmd_claim()`、`robot_cmd_publish()`、`robot_cmd_read()`）。

- 这里只定义类型和函数，**话题实例**在各兵种的 `robot.c` 中分配。
- **可以** include：`<stdint.h>`、`<stdbool.h>`、core/msg、msgs 内部。
- **禁止**：控制逻辑、algorithm、HAL。
- 只被 devices、subsystems、robots 引用。
