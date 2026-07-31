# ESP-AT 自定义 GPIO 命令使用指南

本文档介绍本仓库基于 `examples/at_custom_cmd` 组件新增的自定义 AT 命令（GPIO 控制），以及如何在 ESP32-WROOM-32（NodeMCU-32S）上编译、烧录与使用。

## 一、新增了哪些命令

| 命令 | 功能 | 示例 |
|------|------|------|
| `AT+GPIOM=<pin>,<mode>[,<pull>]` | 配置引脚方向 | `AT+GPIOM=4,1`（GPIO4 输出） |
| `AT+GPIOW=<pin>,<level>` | 输出电平 | `AT+GPIOW=4,1`（高电平） |
| `AT+GPIOR=<pin>[,<pull>]` | 读取电平 | `AT+GPIOR=4`（返回 `+GPIOR:4,1`） |
| `AT+GPIOM=?` | 打印帮助 | 会列出用法和可用引脚 |

参数说明：

- `<pin>`：GPIO 编号（见下节"可用引脚"）
- `<mode>`：`0` = 输入，`1` = 输出
- `<pull>`（可选）：`0` = 无上下拉，`1` = 上拉，`2` = 下拉。省略时默认 `0`（无上下拉）
- `<level>`：`0` = 低电平，`1` = 高电平

## 二、可用引脚（ESP32-WROOM-32 / NodeMCU-32S）

命令内置了引脚校验，使用以下引脚以外的编号会返回 `ERROR`。

**通用输入/输出引脚（推荐）**

```
GPIO4, GPIO13, GPIO14, GPIO16, GPIO17, GPIO18, GPIO19,
GPIO21, GPIO22, GPIO23, GPIO25, GPIO26, GPIO27, GPIO32, GPIO33
```

**仅输入引脚（只能用于 `AT+GPIOR`）**

```
GPIO34, GPIO35, GPIO36, GPIO39
```

> GPIO34~39 无内部上下拉，作输入时如需固定电平请外接上拉/下拉电阻。

**被保留、不可用的引脚（自动屏蔽）**

```
GPIO0, GPIO1, GPIO2, GPIO3, GPIO5, GPIO6~11, GPIO12, GPIO15
```

- GPIO1 / GPIO3：AT 串口（TX/RX），已被固件占用
- GPIO6~GPIO11：内部 SPI Flash，未引出
- GPIO0 / GPIO2 / GPIO5 / GPIO12 / GPIO15：启动配置（strapping）引脚，强制电平可能导致无法正常启动

> 如确实需要用到 strapping 引脚，可修改
> `examples/at_custom_cmd/custom/at_custom_cmd.c` 中 `at_gpio_is_valid()` 函数，删除对应屏蔽分支后重新编译。

## 三、编译固件

本仓库已通过 GitHub Actions 自动编译，push 到 `master` 后自动生成固件：

1. 打开仓库的 **Actions** 页面
2. 找到 **Build ESP-AT Project** 工作流，查看最新一次运行
3. 下载 **esp32-wroom-at** 工件（Artifact），解压得到：
   - `build/factory/factory_WROOM-32.bin` —— 出厂合并固件（推荐烧录这一个文件）
   - `build/download.config` —— 分段烧录参数

## 四、烧录固件

使用 ESP-IDF 自带的 esptool（已安装 IDF 环境）：

```powershell
cd 解压目录\build
esptool.py --chip esp32 -p COM端口号 --baud 115200 write_flash @download.config
```

> 如果自动复位不生效（连接失败），按住板上 **BOOT** 键再插电/点击烧录，出现 `Connecting...` 后松开。

## 五、使用示例

以 GPIO4 控制一个 LED 为例：

```text
AT+GPIOM=4,1        → OK                 （GPIO4 设为输出）
AT+GPIOW=4,1        → OK                 （LED 亮）
AT+GPIOW=4,0        → OK                 （LED 灭）

AT+GPIOM=4,0        → OK                 （GPIO4 设为输入）
AT+GPIOR=4          → +GPIOR:4,1  OK     （读取电平）
AT+GPIOR=4,1        → +GPIOR:4,0  OK     （输入 + 上拉后读取）
```

## 六、串口终端注意事项

- AT 串口为板载 USB 转串口（UART0），波特率 **115200**，8 数据位 / 无校验 / 1 停止位
- 每条命令必须以回车换行（`\r\n`）结尾，串口助手需勾选"发送新行"
- 命令大小写敏感，统一使用大写

## 七、如何在此基础上新增自己的命令

1. 在 `examples/at_custom_cmd/custom/at_custom_cmd.c` 中：
   - 定义命令处理函数（test / query / setup / exe）
   - 在 `at_custom_cmd[]` 数组里添加命令描述，例如 `{"+MYCMD", at_test_cmd_my, NULL, at_setup_cmd_my, NULL}`
2. 若用到新组件，在 `examples/at_custom_cmd/CMakeLists.txt` 的 `require_components` 中追加
3. push 到 `master`，GitHub Actions 会自动编译
4. 参考官方文档：`docs/zh_CN/Compile_and_Develop/How_to_add_user-defined_AT_commands.rst`

### 新增命令的常见坑

- **可选参数必须用 `para_num` 先判断**：`esp_at_get_para_as_digit/str()` 访问不存在的参数索引会返回
  `ESP_AT_PARA_PARSE_RET_FAIL` 而不是 `_OMITTED`。正确写法是先判断 `index != para_num` 再解析，否则
  `AT+XXX=1,2` 这种省略最后一个可选参数的输入会报 `ERROR`。
- **帮助文本 buffer 要够大**：`esp_at_port_write_data` 会按 `snprintf` 的"应有长度"写数据，若帮助字符串
  超过栈上 buffer 会越界输出乱码。建议 buffer 开大些并对长度做钳制。
- **命令名不要互相覆盖前缀**：如同时注册 `+GPIO` 和 `+GPIOM`，短命令可能遮蔽长命令。
- **修改固件后确认烧录的是最新版**：`AT+XXX=?` 行为异常时，先确认烧录的固件版本对应最新 commit。

## 八、引脚速查卡

```
可用输出/输入:  4  13  14  16  17  18  19  21  22  23  25  26  27  32  33
仅输入:        34  35  36  39
保留(不可用):  0   1   2   3   5   6   7   8   9  10  11  12  15
```
