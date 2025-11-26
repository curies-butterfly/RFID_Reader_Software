# RFID Reader
## 环境搭建
环境：idf版本5.2.1 +vscode + ESP-IDF 5.2 CMD
linux 环境搭建：WSL2 Ubuntu 22.04 +vscode + ESP-IDF 5.2.5 推荐这个！需安装windows子系统

>windows安装WSL 📖 安装参考：https://blog.csdn.net/Natsuago/article/details/145594631


>WSL安装路径迁移到别的盘，
📖 安装参考：在 Win11安装 Ubuntu20.04子系统 WSL2 到其他盘（此处为D盘，因为C盘空间实在不能放应用）：
https://blog.csdn.net/orange1710/article/details/131904929






## 硬件指示灯（大致排查问题）
电源灯：上电后常亮
网络灯：4G/以太网 网络灯为一秒闪烁，表示未连接网络（4G/以太网），若为常亮，则通信正常，已连接网络
       LoRa： 未连接时，现象是慢闪，joined（与网关成功通信）为常亮，出现错误，ERROR，2(无效参数) 忙 ERROR，4(设备忙) 为快闪 ， ERROR，3(未入网) 为慢闪
运行灯，程序正常启动后，以一秒时间间隔闪烁


## 使用中系统参数配置
### 串口配置命令
（上电10s设置），否则默认原配置；
例如发送：#4G=0;ETH=1;LORA=0;TYPE=XY;WIFI=self_test,147258368;MQTT=mqtt://8.159.138.166;
各字段以分号;分隔，采用 KEY=VALUE 的键值对形式，具体含义如下：

#### 发送数据方式
| 参数     | 值 | 说明                  |
| ------  | - | ------------------- |
| `4G`   | 1 | 启用 4G 网络模块      |
| `ETH`  | 0 | 禁用有线以太网（Ethernet）接口 |
| `LORA` | 0 | 禁用 LoRa 通信模块     |
注：0表示禁用，1表示启用，三种方式选一，根据用户需求

注：默认开启wifi ap+station模式
#### 标签类型选择
| 参数  | 值  | 说明            |
| ------| -- | ------------- |
| `TYPE`| XY | 设备类型标识，杭州星沿科技标签 |
| `TYPE`| YH | 设备类型标识，杭州悦和科技标签 |
注：星沿科技标签：EPCID和温度存放在一个扇区（EPC）中，计算温度和校验值 详见说明书

#### Wi-Fi 配置（sta）
| 参数     | 值                     | 说明                                    |
| ------ | --------------------- | ------------------------------------- |
| `WIFI` | `self_test,147258369` | 使用的 Wi-Fi 热点名称和密码，格式为 `SSID,PASSWORD` |

ap热点配置信息默认，如需更改，执行idf.py menuconfig命令

### 使用 Wi-Fi 热点 (AP 模式) 在网页端进行配置

- **AP SSID**：`RFID_AP_(DEVICE_ID)`  
- **密码**：`12345678`  
- **配置地址**：在浏览器中输入 `http://192.168.4.1`

### mqtt_address配置
| 参数     | 值                     | 说明                                    |
| ------ | --------------------- | ------------------------------------- |
| `MQTT` | `mqtt://8.159.138.166` | mqtt服务地址 |


通过该网页可以配置读写器的相关参数，或控制其工作模式。

### 系统开发
#### 串口连接WSL
串口连接WSL，以管理员权限方式运行power shell，请执行以下命令：
`usbipd list`:查看串口设备
`usbipd attach --wsl --busid=<busid>`:挂载串口设备 <busid>为串口设备的busid号
`usbipd detach --busid=<busid>`:从wsl中卸载串口设备

#### 终端
`idf.py build`:编译项目
`idf.py flash <-p serial_port> <-b 921600>`:烧录固件至MCU  serial_port为串口设备，921600为波特率，也可以不指定追加参数，如idf.py flash
`idf.py monitor`:串口监视器
`idf.py fullclean`:清理项目
`idf.py menuconfig`:配置项目
`idf.py erase_flash`:擦除flash
`idf.py set-target <target>`:设置目标设备 例如target为esp32s3

#### 添加组件

**方式一**：以文件夹的形式将组件添加到工程目录中，并在对应位置编写或修改 `CMakeLists.txt` 文件。  
> 注意：`CMakeLists.txt` 文件的命名必须严格正确。在 Linux 系统中大小写敏感，命名不规范可能会导致编译失败；而在 Windows 系统中不区分大小写。

**方式二**：在 `idf_component.yml` 文件中添加组件依赖项。  
> 该方式适用于使用 IDF 管理的组件库，系统将在编译时自动下载所需依赖，并以组件形式集成到工程中。



## BUG 修改记录

在 `rx_task` 线程中，接收到来自 RFID 模组的串口数据后，会通过 `malloc` 动态分配内存，用于存储 EPC 标签的温度等相关数据，并将其通过消息队列发送给后续的标签数据处理线程。

原问题在于：**正常情况下，队列发送成功后，由标签数据处理线程释放该内存；但若队列发送失败，则未释放这块动态内存，导致内存泄漏**。

本次修改已在队列发送失败时，添加了对应的内存释放逻辑，避免内存泄漏问题。




