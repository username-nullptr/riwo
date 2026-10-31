<!-- SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com> -->
<!-- SPDX-License-Identifier: MIT -->

# Riwo Experimental HTTP 架构与实现契约

## 1. 文档定位与当前状态

HTTP/WebSocket 实验性重构的主体已经完成。本文不再承担实施路线图的职责，而是记录
Experimental HTTP 当前的分层、公共 API 契约、协议行为、扩展边界和持续验收条件；历史阶段
及其完成顺序由版本历史保留。

公共声明以安装头为唯一事实来源。本文用于解释跨声明的约束，尤其是所有权、完成签名、错误
作用域、背压、取消和并发语义。实现若改变这些约束，必须同时更新本文和对应契约测试。

Experimental HTTP 使用以下稳定的实验模块边界，并与稳定 HTTP、Experimental WebSocket 明确
分离：

- CMake target：`riwo.exp_http`；
- 头文件根目录：`riwo/experimental/http/`，聚合头为 `<riwo/experimental/http.h>`；
- C++ 命名空间：`riwo::experimental::http`；
- 配置宏前缀：`RIWO_EXP_HTTP_`。

Experimental HTTP 与现有 `riwo.http`、`riwo::http` 并行存在。除 core 基础设施外，实验模块
不通过旧 HTTP 的 request context、reply、connection lease 等公共类型建立依赖。实验名称本身
不形成稳定兼容承诺，也不承诺与稳定 HTTP 逐符号源码兼容。

文中的公共类型名、所有权、完成签名和错误语义描述当前契约。内部组织可以调整，但不得
破坏以下核心约束：

- 应用使用同一套 URL、Request、Response 和 Body API 访问 HTTP/1.x 与 HTTP/2；
- 协议版本由连接协商和请求策略决定，不参与普通业务类型的模板实例化；
- 对高层 client/server API，HTTP/1.x 与 HTTP/2 的 framing、复用和流量控制隐藏在协议
  engine/Session 内；低层 Protocol API 则显式提供受约束的事件、action 和诊断；
- 用户可以脱离 client/server 门面独立驱动公共 H1/H2 协议引擎，并用自定义有序字节 I/O
  组合应用；
- 公共抽象以一次 HTTP exchange/stream 为中心，而不是以独占 TCP 连接为中心；
- 普通内存数据无需显式包装；文件、实时流、借用内存等额外语义必须明确表达；
- 所有流式读写都具有背压和明确的所有权、取消、完成语义。

阅读时可按关注点跳转：

| 关注点 | 章节 |
| --- | --- |
| 定位、目标、架构与术语 | 1--5 |
| 语义类型、Body 与高层 client/server API | 6--11 |
| 公共协议引擎、Session、transport 与连接池 | 12--16 |
| 并发、取消、错误、文件与双向通道 | 17--23 |
| 源码组织、构建、测试、验收与设计决策 | 24--28 |

第 20.1 节的同步 `std::fstream`/`FILE*` source/sink、`file_span`、`pipe()`、reply 文件保存入口和
server source 响应重载已经进入安装头。`file_metadata`、atomic replace 策略以及
`io_uring`/IOCP 原生异步后端仍是后续扩展；当前异步 API 形状不代表磁盘访问本身已经异步化。

### 1.1 文档约定

本文使用以下约定，避免真实 API、语义伪代码和扩展建议混在一起：

- “必须”“不得”是实现和测试需要满足的约束；
- “建议”是默认实现方向，可以在保留外部契约的前提下调整；
- 标注为“语义接口”的代码块只描述能力，不是可以直接复制的 C++ 声明；
- 公共 API 代码块必须能映射到唯一真实声明；省略模板约束或导出宏时，正文补充完成签名、
  所有权和前置条件；
- 代码块记录已交付接口的核心形状；完整签名仍以安装头为准，并由 install-consumer 编译测试
  锁定；
- 改变 public declaration、所有权、completion signature、稳定错误或 wire 行为时，必须同步
  修改本文与契约测试；只影响私有组织、算法或性能的选择记录在实现注释或短 ADR 中。

### 1.2 Experimental HTTP 与 Experimental WebSocket 的模块边界

| 项目 | Experimental HTTP | Experimental WebSocket |
| --- | --- | --- |
| Build switch | `RIWO_BUILD_EXP_HTTP` | `RIWO_BUILD_EXP_WEBSOCKET` |
| CMake target | `riwo.exp_http` | `riwo.exp_websocket` |
| 聚合头 | `<riwo/experimental/http.h>` | `<riwo/experimental/websocket.h>` |
| 命名空间 | `riwo::experimental::http` | `riwo::experimental::websocket` |
| 职责 | HTTP 语义、HTTP/1.x、HTTP/2、Session、Pool、`duplex_channel` | WebSocket handshake、frame、stream、client 与 HTTP exchange upgrade |

Experimental WebSocket 依赖 Experimental HTTP，但反向依赖禁止。`riwo.exp_http` 只提供协议
中立的 `duplex_channel`、HTTP/1 Upgrade/CONNECT 和 HTTP/2 Extended CONNECT 交接能力；
WebSocket frame、close、ping/pong、压缩、重试和 client API 全部位于
`riwo.exp_websocket`。其测试和示例分别归入 `test/functional/experimental/websocket/` 和
`examples/experimental/websocket/`。

### 1.3 规范基线

Experimental HTTP 的语义、HTTP/1.1 消息解析和 HTTP/2 状态机分别以 RFC 9110、RFC 9112
和 RFC 9113 为规范基线。TLS ALPN 使用 RFC 7301；实现 Extended CONNECT 时使用
RFC 8441。

本文描述公共抽象和实现约束，不重复抄录完整协议规则。出现以下情况时：

- 本文未覆盖的 wire 行为按对应协议规范执行；
- 示例、便利 API 或实现建议与协议规范冲突时，以协议规范为准；
- 应用提供的 `body_spec`、header 或配置无法合法映射到目标协议时，发送前返回明确错误，
  不生成非标准消息；
- 协议允许接收端采用更严格的安全策略时，实现可以拒绝有歧义或可能导致 request
  smuggling、response splitting、资源耗尽的输入，但必须记录稳定错误类别；
- 公共 API 不提供绕过协议 MUST/MUST NOT 规则的选项。

### 1.4 能力覆盖与稳定 HTTP 差异

Experimental HTTP 已覆盖 HTTP/1.x/HTTP/2 wire、client/server exchange、流式 Body、Cookie、
重定向、重试、文件 source/sink、双向通道以及自定义 protocol/transport 扩展。以下应用能力由
H1/H2 共用的协议无关层提供：

| 能力 | 当前契约 |
| --- | --- |
| 路由与参数 | `route_pattern`、有序重复参数 view、URI 解码和 copy-on-write 路由快照；请求分派无读锁 |
| 中间件与 controller | 全局/路由中间件、顺序 `before`、逆序 `after`/`exception` 以及提前响应 |
| 应用 Session | Session ID、线程安全属性、失效/清理/超时回调与 Cookie 绑定；不依赖 protocol Session |
| 默认响应 | 可覆盖、可禁用的 HTML/text fallback，覆盖欢迎页、404/405/500、HEAD 和已提交响应 |
| 文件服务 | `respond_file()`、安全资源根解析、MIME、validator、条件请求、单/多 Range、缓存字段和 gzip variant |
| HTTPS server | OpenSSL 构建下的 `server_tls_context`、TLS acceptor、握手超时/取消和 ALPN |
| 代理 | 显式/环境代理、`no_proxy`、请求覆盖、HTTP absolute-form/CONNECT、SOCKS5 和认证；解析后策略进入 session key |
| HTTP 工具 | Range/Content-Range、HTTP date、ETag/precondition、multipart、Basic/Bearer 与公开 gzip codec |
| 压缩 | zlib 构建下的有界流式 gzip、MIME/Accept-Encoding 策略、`Vary`、variant ETag；Range 禁用动态压缩 |
| 请求策略与进度 | 请求级 proxy/redirect/retry/自动解压覆盖，以及背压感知的上传/下载进度观察器 |

以下差异是实验架构的有意边界，不是待补齐的迁移缺口：

- 不恢复 `connection()`、`hand_over_connection()`、`keep_alive()`、`is_chunked()` 等物理连接或
  framing API；对应需求使用 Exchange、Body、capabilities 和 `duplex_channel`；
- 不要求为每个 well-known method 恢复独立快捷函数，通用 `method`/`request` 已能表达扩展方法；
- HTTP/2 Server Push、跨 origin connection coalescing 和已弃用的 h2c Upgrade 仍是非目标。

新增或修改上述能力时，必须同时提供 H1/H2 共用行为测试。涉及可选依赖时，应覆盖 HTTP/2、
OpenSSL 和 zlib 的开启/关闭矩阵；涉及稳定 HTTP 既有行为时，应保留新旧对照向量，避免只验证
“可以发送字节”而遗漏应用层语义。

## 2. 背景与问题

稳定 HTTP 实现以 HTTP/1.x 的连接生命周期为中心：一个 request context 独占 connection
lease，parser/generator 直接承担消息 framing，服务端在连接上串行处理请求。这套模型
不能自然表达 HTTP/2 的以下能力：

- 一条连接同时承载多个双向 stream；
- HEADERS、DATA、trailing HEADERS 等二进制帧；
- HPACK 状态属于整条连接；
- stream 级取消与连接级 GOAWAY；
- connection 与 stream 两级流量控制；
- 一个慢 stream 不应阻塞其他 stream 的应用处理。

只增加 `version::v20`、新的 parser 或 generator 会把 HTTP/2 的连接状态强行塞进
HTTP/1.x 的单消息模型，最终导致公共 API、连接池和生命周期同时泄漏协议细节。

Experimental HTTP 因此采用语义层、协议引擎、Session driver 和传输层分离的设计。协议引擎
自身不执行 I/O；
高层 Session driver 或用户代码负责把输入字节送入引擎，并把引擎产生的输出写入任意满足契约的
有序可靠字节流。

## 3. 目标与非目标

### 3.1 目标

1. 客户端保留 URL 形式，GET、POST、文件传输和流式传输默认不区分协议版本。
2. 服务端使用同一套路由、AOP、中间件、Session 和 Request/Response API。
3. 支持 HTTP/1.0、HTTP/1.1 和 HTTP/2；`https` 按 TLS/ALPN 协商，`http` 按 RFC 9113
   prior knowledge 建立明文 HTTP/2。
4. 同时支持一次性 body source 和交互式 body writer。
5. HTTP/2 使用有界队列、流量控制和公平写调度，禁止无界缓存。
6. completion token、协程、错误重定向、超时和关联 executor 保持 Riwo 风格。
7. 文件路径、窄字符 C++ 文件流、C 标准 `FILE*`、压缩、Cookie、认证、重定向等能力构建在
   协议无关语义层。
8. 为 HTTP/1 Upgrade 和 HTTP/2 Extended CONNECT 提供统一的双向通道抽象。
9. 提供可独立构造、增量消费输入并增量产生输出的公共 HTTP/1 与 HTTP/2 协议 API；用户无需
   构造 client、server、socket 或 Session Pool 即可完成序列化、反序列化和协议状态推进。
10. 提供公共 byte-stream/connector/acceptor 扩展边界，使高层 Session 能在 TCP/TLS 之外运行于
    用户实现的测试流、进程内通道、隧道或其他有序可靠字节 I/O 上。
11. 内置 client/server 与自定义 I/O 路径复用同一协议引擎和校验规则，不维护第二套 wire 行为。

### 3.2 非目标

- Experimental HTTP 不承诺稳定 HTTP API 的逐符号源码兼容；保留 client 的 URL/便利请求风格，以及 server
  的 `bind().on_request().on_default().start()`、错误 handler 和配置风格。
- 不保留 `connection()`、`hand_over_connection()`、`keep_alive()`、`is_chunked()` 等把应用
  绑定到 HTTP/1 物理连接或 framing 的接口；对应需求使用 exchange 能力、流式 body 或
  `duplex_channel` 表达。
- 不实现 HTTP/2 Server Push。
- 不实现跨 origin 的 HTTP/2 connection coalescing。
- 不实现 RFC 9113 已弃用的 `h2c` Upgrade 和 `HTTP2-Settings` 字段；明文 HTTP/2 只实现
  RFC 9113 第 3.3 节的 prior-knowledge 路径。
- 不自行实现 HPACK 和完整 HTTP/2 状态机，优先使用 nghttp2。
- 不把 nghttp2 类型、回调签名、错误码或生命周期暴露到公共 API；公共 HTTP/2 engine 是 Riwo
  自有的稳定适配层。
- 不把任意 message transport 伪装为 HTTP transport。公共 transport 扩展只接受保持字节顺序、
  不重复且能明确报告 EOF/取消/关闭的可靠字节流。
- 本设计不承诺直接支持 HTTP/3。语义层可以复用，但 QUIC 传输需要独立设计。

## 4. 总体架构

```text
Client / Server / Router / Middleware       Custom application / test harness
                  |                                      |
          HTTP Semantic API                    Public Protocol API
    Request + Response + Exchange + Body       events + actions + codecs
                  |                                      |
             Session Driver -----------------------------+
                  |                                      |
          +-------+-------+                      manual push / pull
          |               |                              |
    HTTP/1 Engine     HTTP/2 Engine <--------------------+
    one active stream multiplexed streams
          |               |
          +---- Public Byte Stream ----+
                  |                    |
               TCP / TLS          User transport
```

公共 Protocol API 与 Public Byte Stream 是两个正交扩展面：前者允许应用自己调度 I/O，后者
允许复用 Riwo Session、Exchange 和 client/server 状态机但替换底层字节流。内置 TCP/TLS 只是
byte stream 实现，不是协议引擎的前置条件。HTTP/2 engine 既可连接通过 ALPN 选中 `h2` 的 TLS
transport，也可连接由 prior knowledge 选中的明文 transport，或由用户通过带外信息明确选定。

各层职责如下：

| 层 | 职责 | 不负责 |
| --- | --- | --- |
| Semantic | URL、方法、状态、字段、Body、Cookie、重定向、路由 | 帧、chunk、HPACK、连接复用 |
| Session driver | 将协议事件映射为 exchange/body，驱动读写循环、超时与 executor | DNS、TCP/TLS 建连、wire 编解码 |
| Protocol engine | HTTP/1 framing 或 HTTP/2 frame/HPACK 映射，消费/产生字节和语义事件 | I/O、executor、业务路由、文件语义 |
| Transport | 有序字节读写、EOF、取消、关闭、可选端点/TLS 元数据 | HTTP 消息、stream 和协议状态 |
| Pool | origin 路由、连接建立、Session 容量调度 | 直接读写消息体 |

## 5. 核心术语

- **Protocol engine**：不拥有 socket、不发起异步操作的协议状态机；输入为字节和语义 action，
  输出为已消费字节数、语义 event 和待发送字节。
- **Transport**：一条已经建立的有序可靠字节流；可以是 TCP、TLS 或用户实现。
- **Session**：一条已经选定 HTTP 版本的协议连接。
- **Stream**：一次请求/响应交换。HTTP/1 Session 同时只有一个活动 stream，HTTP/2
  Session 可以同时有多个 stream。
- **Exchange**：应用持有的 stream 句柄，提供请求体、响应头、响应体和取消能力。
- **Body source**：由库主动读取的请求或响应数据源。
- **Body reader/writer**：由应用交互式消费或产生 body 的接口。
- **Draining**：Session 不再接受新 stream，但允许已有 stream 完成。

## 6. 协议选择与基础语义类型

协议版本是运行时属性，不再作为 `basic_client`、request context 或 reply 的模板参数。

```cpp
enum class protocol
{
	v1_0,
	v1_1,
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2
#endif
};

enum class protocol_requirement
{
	automatic,
	v1,
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2
#endif
};

struct protocol_policy
{
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	std::vector<protocol> preference {protocol::v2, protocol::v1_1};
#else
	std::vector<protocol> preference {protocol::v1_1};
#endif
	protocol_requirement requirement = protocol_requirement::automatic;
};
```

policy 在 client/server 启动时校验并冻结：`preference` 不得为空或包含重复值；
`automatic` 按完整列表参与对应 URI scheme 的选择或协商，`protocol_requirement::v1`
只按其中的 HTTP/1 项排序；编译 HTTP/2 时，`protocol_requirement::v2` 只取
`protocol::v2`。过滤后没有可用协议属于配置错误。未编译 HTTP/2 时，`protocol::v2` 和
`protocol_requirement::v2` 声明本身由宏屏蔽，不保留一个只能在运行时失败的伪能力。除 requirement 明确
定义的过滤外，不静默改写用户意图。

默认行为：

- `RIWO_EXP_HTTP_HTTP2_SUPPORT=1` 且 TLS transport 可用时，`https://` 使用 ALPN，客户端
  依次提供 `h2`、`http/1.1`，服务端根据配置和客户端 ALPN 选择协议；
- 宏为 0 但 OpenSSL 仍启用时，HTTPS 只提供 HTTP/1.x；OpenSSL 关闭时不编译 TLS transport；
- `http://` 不存在带内版本协商；`automatic` 在没有带外能力信息时使用 HTTP/1.1，
  `protocol_requirement::v2` 或等价的显式 endpoint 配置构成 prior knowledge，客户端连接后立即发送
  HTTP/2 connection preface；
- 同时允许 HTTP/1.x 与 HTTP/2 的明文服务端通过 HTTP/2 connection preface 识别协议，
  未匹配的已读字节必须完整交给 HTTP/1 parser；
- HTTP/1.0 主要用于兼容显式请求或服务端收到的 HTTP/1.0 消息，不参与 ALPN；
- 编译 HTTP/2 时，`protocol_requirement::v2` 对 `https://` 要求 ALPN 选中 `h2`，对 `http://` 启动
  prior-knowledge 路径；任一路径失败都返回 negotiation/protocol error，不在同一连接上静默
  改按 HTTP/1.x 解释或重试；
- 不因普通 HTTP/2 stream error 回退到 HTTP/1.1，回退只发生在协议协商阶段。

`selected_protocol()` 用于诊断和能力观察，不应成为普通请求代码的分支条件。

### 6.1 Method 与 Status

HTTP method 是大小写敏感的 token，不能用封闭枚举阻止扩展方法。公共 `method` 是拥有
自身字符串的值类型，同时为常用方法提供 `method::get`、`method::post` 等 well-known
枚举值的隐式构造。由字符串构造时必须校验 token 语法，并保留原始大小写。只有 well-known
方法默认携带 safe/idempotent 属性；扩展方法可以在构造时提供语义属性，未提供时两者均为
false。`safe=true` 隐含 `idempotent=true`，构造时统一把 `idempotent` 归一化为 true。是否真正自动重试
仍由请求的 retry policy 决定。

method 的身份、相等比较和 hash 只取决于大小写敏感的 token；`method_properties` 是重试和
诊断使用的语义 metadata，不参与路由匹配和身份比较。由字符串构造且 token 与标准方法的规范
大写拼写完全匹配时，`known()` 返回对应 well-known 值并采用该标准方法的固定属性；大小写不同
的 token 仍是合法扩展方法，不做大小写折叠，也不继承标准方法属性。带 properties 的字符串
构造重载若收到标准 token，只接受与标准属性一致的值，否则抛出 `std::invalid_argument`。

`status` 是保存三位状态码的轻量值类型，为常用状态提供 `status::ok` 等常量。构造和解析
必须拒绝不在 100--599 范围内的值，但不能因为状态码未在本地枚举表中命名而拒绝有效响应。
method/status 的显示文本只是诊断便利信息，不参与 wire 语义。

```cpp
struct method_properties
{
	bool safe = false;
	bool idempotent = false;
};

class method
{
public:
	enum well_known { get, head, post, put, patch, delete_, options, trace, connect };
	method(well_known value) noexcept;
	template <core_concepts::string_p<char> String>
	explicit method(String &&token);
	template <core_concepts::string_p<char> String>
	method(String &&token, method_properties properties);

	std::string_view text() const noexcept;
	optional<well_known> known() const noexcept;
	bool is_safe() const noexcept;
	bool is_idempotent() const noexcept;

	friend bool operator==(const method &lhs, const method &rhs) noexcept;

private:
	std::string m_token; // 仅自定义 method 使用；标准 method 使用 well_known
	method_properties m_properties;
	optional<well_known> m_known;
};

class status
{
public:
	enum well_known : uint16_t { continue_upload = 100, ok = 200, no_content = 204 /* ... */ };
	status(well_known value) noexcept;
	explicit status(uint16_t code);

	uint16_t code() const noexcept;
	std::string_view text() const noexcept;
	bool allows_body() const noexcept;

private:
	uint16_t m_code = 0;
};
```

`std::hash<experimental::http::method>` 与 `operator==` 使用同一 token-only 规则；不得把 properties 混入
hash 后破坏 unordered container 的等价关系。

直接构造非法 method/status 是同步参数错误并抛出 `std::invalid_argument`；wire parser 使用
内部非抛出工厂，把非法输入映射为稳定 protocol error。

`status::text()` 返回规范原因短语，仅用于诊断显示；`status::allows_body()` 在 1xx、204、
205 和 304 上返回 false。服务端在生成 HTTP/1.x 或 HTTP/2 响应时统一使用该判定，应用无需
为这些状态手工切换 body 类型。

## 7. Header 与消息头

### 7.1 Fields 容器

HTTP 字段允许重复并且顺序在部分场景中有意义，因此不能使用会覆盖重复值的普通 map。

```cpp
struct field
{
	std::string name;
	riwo::value value;
};

class field_range;

class fields
{
public:
	template <core_concepts::text_p<char> T>
	fields &add(T &&name, riwo::value value);

	template <core_concepts::text_p<char> T>
	fields &set(T &&name, riwo::value value);

	template <core_concepts::text_p<char> T>
	fields &erase(const T &name);

	template <core_concepts::text_p<char> T>
	field_range find_all(const T &name) const;

	template <core_concepts::text_p<char> T>
	optional<std::string_view> find_first(const T &name) const noexcept;

	const_iterator begin() const noexcept;
	const_iterator end() const noexcept;

private:
	std::vector<field> m_values;
};
```

内部建议使用有序 `vector<field>`，名称查询大小写不敏感。`set()` 保留第一个同名字段的
位置并删除其余同名字段；不存在时追加。`field_range` 是只读过滤 view，不承诺匹配字段在
底层连续。它们是轻量值/view 类型，直接保存 `vector`、iterator 和名称 view，不能仅为隐藏
这些已由公共语义确定的状态再增加 pimpl 堆分配。任何修改都使 iterator、range 和
`string_view` 失效。

字段值使用 `riwo::value`，因此调用方可以直接传入字符串、整数和布尔值等可序列化标量；
`find_first()` 仍返回指向底层字符串的零拷贝 view，不因存储类型改变而复制字段值。

`add()`/`set()` 在同步调用点拒绝非法字段名以及包含 CR、LF、NUL 的字段值；协议相关的
禁止字段、总大小和数量限制在 request/response head 冻结时再次校验。HTTP/2 编码时统一
输出小写名称，但应用无需手工转换。

### 7.2 Request 与 Response head

`request_head` 是 client、server 和协议 Session 共用的公共请求 metadata；普通客户端
`request` 是在其上组合 body/source 的可移动构建器。客户端请求以绝对 URL 为主要目标：

```cpp
class request_head
{
public:
	experimental::http::method method;
	riwo::url url;
	std::string scheme;
	std::string authority;
	std::string target;
	experimental::http::fields headers;

public:
	template <core_concepts::text_p<char> T>
	optional<std::string_view> header(const T &name) const noexcept;
	template <core_concepts::text_p<char> T>
	bool has_header(const T &name) const noexcept;
	template <core_concepts::text_p<char> T>
	request_head &set_header(T &&name, riwo::value value);
	template <core_concepts::text_p<char> T>
	request_head &add_header(T &&name, riwo::value value);
	template <core_concepts::text_p<char> T>
	request_head &erase_header(const T &name) noexcept;
	request_head &set_cookies(std::span<const cookie_pair> values);
	template <core_concepts::text_p<char> T>
	request_head &add_cookie(T &&name, riwo::value value);
	request_head &add_cookie(cookie_pair value);

	optional<std::string_view> content_type() const noexcept;
	optional<uint64_t> content_length() const noexcept;
	std::string_view host() const noexcept;
	std::string_view path() const noexcept;
	bool is_http() const noexcept;
	bool is_https() const noexcept;
	bool is_upgrade() const noexcept;
};

// total 在传输长度未知时为 0。
using progress_observer = std::function<void(
	uint64_t transferred, uint64_t total)>;

struct request_options
{
	optional<proxy_policy> proxy;
	optional<redirect_policy> redirects;
	optional<retry_policy> retries;
	optional<bool> automatic_decompression;
	progress_observer upload_progress;
	progress_observer download_progress;
};

class request
{
public:
	request(experimental::http::method method, riwo::url url);

	experimental::http::method method() const noexcept;
	const riwo::url &url() const noexcept;

	experimental::http::fields &headers() noexcept;
	const experimental::http::fields &headers() const noexcept;

	template <core_concepts::text_p<char> T>
	request &set_connect_protocol(T &&protocol);
	optional<std::string> connect_protocol() const;
	template <core_concepts::text_p<char> T>
	request &set_upgrade_protocol(T &&protocol);
	optional<std::string> upgrade_protocol() const;

	request &set_body(const const_buffer &bytes);  // 借用至 send/fetch completion
	request &set_borrowed_body(const_buffer body); // set_body() 的显式兼容别名
	request &set_file(std::filesystem::path path);
	request &set_trailers(fields values);          // 一次性 source 的静态 trailer
	request &set_options(request_options value);
	request &set_proxy(proxy_policy value);
	request &set_redirects(redirect_policy value);
	request &set_retries(retry_policy value);
	request &set_auto_decompression(bool value = true);
	request &on_upload_progress(progress_observer observer);
	request &on_download_progress(progress_observer observer);
	const request_options &options() const noexcept;

	template <typename Source>
	requires concepts::body_source_model<std::remove_cvref_t<Source>>
	request &set_source(Source &&source);

	body_spec body_description() const;
	bool has_body_source() const noexcept;
};

class response_head
{
public:
	experimental::http::status status;
	experimental::http::fields headers;

public:
	template <core_concepts::text_p<char> T>
	optional<std::string_view> header(const T &name) const noexcept;
	template <core_concepts::text_p<char> T>
	bool has_header(const T &name) const noexcept;
	template <core_concepts::text_p<char> T>
	response_head &set_header(T &&name, riwo::value value);
	template <core_concepts::text_p<char> T>
	response_head &add_header(T &&name, riwo::value value);
	template <core_concepts::text_p<char> T>
	response_head &erase_header(const T &name) noexcept;
	template <core_concepts::text_p<char> T>
	response_head &set_cookie(T &&name, riwo::value value);
	response_head &set_cookie(const response_cookie &value);

	optional<std::string_view> content_type() const noexcept;
	optional<uint64_t> content_length() const noexcept;
	optional<std::string_view> location() const noexcept;
	bool is_informational() const noexcept;
	bool is_success() const noexcept;
	bool is_redirection() const noexcept;
	bool is_client_error() const noexcept;
	bool is_server_error() const noexcept;
	bool is_error() const noexcept;
	bool allows_body() const noexcept;
};
```

上面的 `request::set_file(path)` 是当前已交付的路径便利入口。`std::ifstream`、`std::fstream` 和
`FILE*` 不继续扩张 `request` 的同名重载集合，而是通过第 20.1 节的 `file_body_source(...)`
适配后传给 `request::set_source()`；这样文件来源的所有权、范围和打开模式始终显式可见。

两者仍是聚合类型，字段保持 public；可直接使用指定初始化器和原有字段访问。Header 快捷方法
按大小写不敏感的名称查询或修改 `headers`，`content_length()` 在字段缺失或格式非法时返回空。
`request_head::set_cookies()` 替换全部 `Cookie` 字段，`add_cookie()` 保留现有有序 Cookie pair
并追加一个 pair；`response_head::set_cookie()` 追加独立的 `Set-Cookie` 字段，而不是替换已有字段。

客户端构造 `request` 时写入 method、URL 和 fields；请求冻结时校验并补齐规范化后的 scheme、
authority 和 target，再把 `request_head` 交给 Session。服务端协议引擎产生同一种
`request_head`，`server_exchange::request()` 只以 const 引用公开它。`request_head` 不拥有
body，也不包含 protocol、endpoint、connection 或 router 状态。

`request` 初始 body 为 empty。每次设置 body/source/file 都替换此前来源并清除此前 trailer；
request 冻结或移入 `send()` 后不能再修改。一次性 source 的 trailer 值通过
`request.set_trailers()` 在发送前给出；动态 trailer 使用 `open()` + `body_writer::finish()`。
普通 `set_body()` 与 `set_borrowed_body()` 都不拥有内存；调用方内存必须存活到最终 completion，
包括期间发生的 redirect/retry。

URL 到 wire format 的映射由协议引擎完成：

| URL 语义 | HTTP/1.x | HTTP/2 |
| --- | --- | --- |
| method | request line | `:method` |
| scheme | TLS/请求上下文 | `:scheme` |
| authority | `Host` 或 absolute-form | `:authority` |
| path/query | request-target | `:path` |
| response status | status line | `:status` |

普通 URL request 的冻结规则如下；这些规则属于语义层，不能由各协议 engine 分别猜测：

- 只接受具有 authority 的绝对 `http` 或 `https` URL；其他 scheme 在启动异步发送前以
  `invalid_target` 完成；
- fragment 不属于 HTTP request target。普通 request 对带 fragment 的 URL 直接返回
  `invalid_target`，不静默丢弃；userinfo 同样拒绝，如需支持必须先定义其认证
  和日志脱敏语义；
- 空 path 在 wire 上统一映射为 `/`；已经编码的 path/query 必须保留其转义语义，不能重复编码
  或把 fragment 混入 query；
- scheme 和 DNS host 在 origin/session key 中使用 ASCII 小写；默认端口按 scheme 归一化，
  `http:80` 与省略 80、`https:443` 与省略 443 分别属于同一 origin；
- IPv6 literal 在内部以不带方括号的地址身份比较，在 `Host`、`:authority` 和 absolute-form 中
  按 URI 语法恢复方括号；非默认显式端口必须进入 authority；
- Experimental HTTP 不自行引入 IDNA 转换。非 ASCII hostname 只有在 `riwo::url` 已经提供经过验证的 ASCII
  表示时才接受，否则返回 `invalid_target`。

伪头字段不进入普通 `fields`，应用不能直接设置 `:method` 等名称。

### 7.3 特殊 Request Target

URL 覆盖普通 GET、POST、PUT、PATCH、DELETE 等请求。以下少数形式通过高级 target
接口表达，不降低常规 URL API 的易用性：

```cpp
using request_target_t = std::variant <
	url_target,
	authority_target,  // CONNECT host:port
	asterisk_target    // OPTIONS *
>;
```

forward proxy 使用的 absolute-form 由 proxy/协议层根据 URL 自动生成，不要求应用改写
请求目标。

### 7.4 字段所有权和校验

以下字段由协议引擎拥有，普通应用不得直接控制 framing：

- 客户端请求的 `Host`，由 URL/target authority 生成；
- `Transfer-Encoding`；
- `Trailer`，由 `body_spec::trailer_spec` 生成；
- `Connection`；
- `Keep-Alive`；
- `Proxy-Connection`；
- `Upgrade`，除非进入明确的 Upgrade API。

`Content-Length` 默认由 body 描述生成。应用显式设置时必须与 body 长度一致，否则发送
前失败。HTTP/2 收到 connection-specific 字段时按协议要求产生 stream 或 connection
error。`TE` 在 HTTP/2 中只允许合法的 `trailers` 用法。
声明 trailer 的消息不得由应用显式设置 `Content-Length`：HTTP/1.1 需要选择 chunked，HTTP/2
是否生成语义 Content-Length 由协议引擎根据 known length 决定。

普通客户端请求以 URL/target authority 为唯一事实来源。请求 headers 中手工加入 `Host`
必须在冻结时失败，不能同时发送冲突的 `Host` 和 `:authority`。确需覆盖 authority 的 proxy、
测试或虚拟主机场景必须使用单独的高级 target/authority API，该覆盖值同时参与 session key、
TLS 身份校验和 wire head 生成，不能只改一个字段。

实现必须限制：

- 单字段名称和值长度；
- 总 header list 大小；
- 字段数量；
- HTTP/1 起始行长度；
- HTTP/2 peer SETTINGS 与本地安全上限的组合值。

### 7.5 Client Request 与共享 Head

`request` 是客户端拥有 body/source 的可移动请求对象，并组合一个 `request_head`；
`request_head` 是公共、可复制的协议无关 metadata。客户端冻结并校验 request 后把 head 交给
Session；服务端解析后把同一种 head 交给 `server_exchange`。body 只属于 `request` 或
exchange state，不进入 head。

`request_head` 必须能统一表达：

- method 和规范化后的 request target；
- scheme、authority、path/query；
- 普通 fields，不含 HTTP/2 pseudo-header；

remote/local endpoint、协商后的 protocol 和 router 产生的 path parameters 属于 exchange 或
路由上下文，不进入 `request_head`。head 不拥有 body reader，也不能暴露 connection lease 或
原始 HTTP/2 connection。`request_options` 与传输进度是公共请求级策略；`origin` 和各种
identity 仍是 pool/connector 内部语义类型。公共 byte stream、transport metadata 和
negotiated transport 由第 15 节定义。实现必须为每个概念保留单一定义，不能让 client、pool
和 protocol engine 各自复制一份结构。

## 8. Body 模型

### 8.1 设计原则

协议层看到的 body 始终是字节序列。JSON、文件和字符串不是不同的 HTTP body 类型，
而是不同的数据来源、所有权和便利操作。

普通内存数据不要求显式写 `body::string(...)`：

```cpp
auto reply = co_await client.post(url, "hello", use_awaitable);
auto reply = co_await client.post(url, riwo::buffer(bytes), use_awaitable);
```

具有额外语义的来源使用明确命名：

```cpp
auto reply = co_await client.post_file(url, path, use_awaitable);
auto reply = co_await client.post_json(url, value, use_awaitable);
```

其中 JSON 支持是可选的序列化扩展，不要求 HTTP 核心依赖特定 JSON 库。

### 8.2 所有权规则

所有权规则与原 HTTP 模块一致：

- 字符串字面量、`std::string` 和 `std::string_view`：隐式适配为借用的 `const_buffer`；
- 其他连续存储：通过 `riwo::buffer(...)` 适配并借用；
- 路径文件：request 拥有路径和打开策略，适配器负责打开、持有并关闭文件；
- 右值 `std::ifstream`/`std::fstream`：适配器移动拥有流；左值必须显式包装为
  `borrowed_file(stream)`，适配器从不关闭它；
- `FILE*` 没有隐含所有权：必须显式包装为 `owned_file(file)` 或 `borrowed_file(file)`；owned
  形式在 source/sink 最终销毁时恰好调用一次 `std::fclose()`，borrowed 形式永不关闭；
- 非 detached 操作不复制普通 body，调用方内存必须存活到对应异步完成；
- detached 写入没有 completion 生命周期屏障，因此在操作启动时复制并持有 body。

```cpp
request.set_body(text);                 // 借用
request.set_body(view);                 // 借用
request.set_body(riwo::buffer(bytes));  // 借用连续二进制数据
request.set_borrowed_body(buffer);      // 显式别名，同样借用
request.set_file(path);                 // 发送文件内容
request.set_source(file_body_source(std::move(input_stream)));
request.set_source(file_body_source(borrowed_file(input_stream)));
request.set_source(file_body_source(owned_file(c_input)));
request.set_source(file_body_source(borrowed_file(c_input)));
```

文件不能仅凭 `std::filesystem::path` 隐式判断，因为调用者可能希望发送路径文本而不是
文件内容。`FILE*` 也不能靠裸指针推断所有权；接收裸 `FILE*` 的无标签重载必须删除或不存在。

文件流只支持面向字节的 `char` 流，不支持 `std::wfstream` 等宽字符流。所有输入和输出都必须
以 binary mode 打开；text mode 的换行转换会破坏 HTTP Range 的字节索引。标准 C/C++ API
不能可移植地查询既有流的打开模式，因此这是调用方前置条件，同时适配器仍须在运行时校验
seek/tell/read/write 的实际结果。

borrowed 文件对象必须存活到最终 completion，期间不得被其他线程读取、写入、seek、关闭或
移动；detached 路径不得接受 borrowed 文件。适配器在启动时保存 borrowed 对象的当前位置；
C++ stream 还保存 `rdstate()`，borrowed `FILE*` 则要求进入时没有 EOF/error indicator。成功、
失败或取消后必须尽力恢复 cursor 和可移植状态；恢复失败进入最终错误结果，不能静默忽略。

### 8.3 Body 描述

发送消息头前必须知道 body 的 framing 需求：

```cpp
class body_spec
{

public:
	struct trailer_spec
	{
		std::vector<std::string> names;
	};

	enum class mode
	{
		empty,
		known_length,
		streaming,
		head
	};

	static body_spec empty();
	static body_spec known(uint64_t size, trailer_spec trailers = {});
	static body_spec streaming(trailer_spec trailers = {});
	static body_spec head(uint64_t content_length);

	mode type() const noexcept;
	optional<uint64_t> length() const noexcept;
	const trailer_spec &trailers() const noexcept;

private:
	body_spec(mode type, optional<uint64_t> length, trailer_spec trailers);
	mode m_type = mode::empty;
	optional<uint64_t> m_length;
	trailer_spec m_trailers;
};
```

`body_spec` 只能由工厂创建，避免 `empty + length`、`known_length + null` 等无效组合。request
默认使用 empty；内存 body 产生 known length；文件在冻结 head 前从同一个已打开对象取得稳定
文件大小；自定义 source 根据 `size()` 产生 known length 或 streaming。文件大小在打开后变化时仍由发送时的
长度校验捕获。`trailer_spec` 在发送 head 前声明允许出现的 trailer 名称；empty body 不能声明
trailer，`finish()` 提交的非空 trailer 必须是声明名称的子集。`head` 只用于响应 HEAD
请求：它产生 `Content-Length`，但不传输 body，也不要求应用再次调用 writer。
factory 同步校验 trailer 名称，拒绝大小写不敏感的重复项和禁止用于 trailer 的字段；非法 spec 抛出
`std::invalid_argument`，运行时提交不匹配则使用 `invalid_trailer` completion。

文件 source 必须先打开并持有文件句柄，再从该句柄取得长度后提交 head，不能先按路径 stat、
之后重新打开另一个文件；传入流或 `FILE*` 时同样不能另开路径副本。发送期间被截断或增长仍按
冻结长度产生 mismatch/忽略额外尾部，不能改变已经发送的 Content-Length。所有 offset、length
和加法均使用 `uint64_t` 语义，进入平台 API 前必须做有符号范围和溢出检查。

`body_spec` 表达应用的发送意图，不单独决定 wire framing。协议引擎必须同时考虑消息方向、
请求方法、响应状态和协商版本，再按照 RFC 9110、RFC 9112 或 RFC 9113 产生 framing。

基础映射如下；方法和状态的特殊规则优先于此表：

| Body 条件 | HTTP/1.0 request | HTTP/1.0 response | HTTP/1.1 | HTTP/2 |
| --- | --- | --- | --- | --- |
| empty | 无 body | 无 body | 无 body | HEADERS + END_STREAM |
| known length，无 trailer | Content-Length | Content-Length | Content-Length | DATA，并校验总长度 |
| known length，有 trailer | 拒绝 | 拒绝 | chunked，同时校验应用字节总长度 | DATA + trailing HEADERS |
| streaming，无 trailer | 拒绝，或显式有界缓冲为 known length | 关闭连接定界 | chunked | DATA + END_STREAM |
| streaming，有 trailer | 拒绝 | 拒绝 | chunked + trailing chunk | DATA + trailing HEADERS |

trailers 不是第四种 body mode。它必须在 `body_spec` 中提前声明，使 HTTP/1.1 能在 head 中
选择 chunked framing 并生成 `Trailer`；实际值仍由 `finish(fields)` 在结束时提供。HTTP/2 不需要
`Trailer` 字段完成 framing，但沿用同一声明以保持跨协议行为一致。

必须遵守以下规则：

- HTTP/1 请求不能使用连接关闭定界；未知长度的 HTTP/1.0 请求默认在发送前失败；
- HEAD 响应和 1xx、204、304 响应没有 message body 或 trailer；相关
  `Content-Length` 仅在协议允许时表达所选表示的长度，不能据此发送 body；
- 成功 CONNECT 响应按协议切换为 tunnel，不能继续按普通 body framing 处理；
- HTTP/1 收到同时包含 `Transfer-Encoding` 和 `Content-Length`、不一致的多个
  `Content-Length` 或其他有歧义的 framing 时，按 RFC 9112 和本地安全策略拒绝，
  不把歧义消息交给应用；
- HTTP/2 不使用 `Transfer-Encoding` 进行 framing；`Content-Length` 存在时只用于
  语义长度校验，消息结束由 END_STREAM 决定；
- trailers 仍需经过字段合法性和消息语义校验，不是所有字段都允许出现在 trailer 中。

### 8.4 Body Source 与自定义来源

一次性 `send(request)` 把不同输入适配为统一 source：

```cpp
namespace concepts
{

template <typename Source>
concept body_source_model = requires(
	Source &source,
	mutable_buffer buffer,
	asio::any_completion_handler<void(error_code, size_t)> read_handler,
	asio::any_completion_handler<void(error_code)> rewind_handler)
{
	{ source.size() } noexcept -> std::same_as<optional<uint64_t>>;
	{ source.replayable() } noexcept -> std::same_as<bool>;
	{ source.read_some(buffer, std::move(read_handler)) } -> std::same_as<void>;
	{ source.rewind(std::move(rewind_handler)) } -> std::same_as<void>;
	{ source.cancel() } noexcept -> std::same_as<void>;
};

} //namespace concepts

template <typename Source>
requires concepts::body_source_model<std::remove_cvref_t<Source>>
request &set_source(Source &&source);
```

以上是公共定制协议而不是仅供说明的伪接口。model 的 `read_some()` 和 `rewind()` 可以实现为
接受任意 completion token 的函数模板，但至少必须能接受表中用于探测的 move-only completion
handler，并分别以 `void(error_code, size_t)` 和 `void(error_code)` 完成。发起函数本身返回 void；
同步参数错误也通过 handler 完成。`rewind()` 即使永远不支持重放也必须存在，并以
`operation_not_supported` 完成，不能通过缺少成员改变 concept。

`request.set_source()` 移动并拥有 source，再将其包装为 Experimental HTTP 私有的 callback
type-erasure；
`asio::any_completion_handler` 只用于约束调用形状，不存入公共对象布局。公共头不暴露虚
`awaitable` 接口，也不把具体 executor 类型固化进 ABI。public-api test 必须至少包含
一个自定义 model、一个缺少 `cancel()` 的反例和一个 completion signature 错误的反例。

source 的操作由 exchange executor 串行发起。自定义 source 可以把实际工作转发到自己的
executor，但 completion 必须遵守传入 token 的 associated executor、allocator 和 cancellation
slot。`cancel()` 必须是非阻塞请求；最终完成仍通过原 completion 路径交付。

同一个 source 最多有一个未完成的 `read_some()` 或 `rewind()`，二者不能重叠。协议层只有在
前一操作完成后才能发起下一操作；调用 `cancel()` 不会提前释放传给未完成 `read_some()` 的
buffer。成功的 `rewind()` 必须使下一次读取重新从第一个 body 字节开始，并重置 source 自己的
EOF 状态。

`request.set_source()` 对左值 model 进行复制、对右值进行移动；需要借用 model 时必须使用显式
`borrowed_source()` adapter，并遵守与 `set_borrowed_body()` 相同的生命周期规则。detached 或其他
无法证明 model 生命周期的路径不得接受 borrowed source。

source 可以额外同时提供同步和异步 `finish(error_code&)`/`finish(handler)`，形成
`concepts::finalizable_body_source_model`；没有这两个操作的自定义 source 按 no-op 处理。client 在 body
读取结束、提交 HTTP message end 之前调用它；server 的 source 响应重载遵循同一顺序。
`file_body_source` 使用该钩子恢复 borrowed
文件状态，使恢复失败能进入 send completion；取消路径仍以原始取消/传输错误为主错误。

典型实现：

- `memory_body_source`；
- `file_body_source`，支持路径、移动拥有的窄字符文件流和显式 owned/borrowed `FILE*`；
- `generator_body_source`；
- `filter_body_source`，用于压缩、进度和限速；
- 用户自定义 source。

source 的 `replayable()` 决定重定向、认证重试或 GOAWAY 后能否自动重发。实时流默认
不可重放。

文件 source 的完整对象范围是半开区间 `[0, size)`；Range/切片在内部统一表达为
`{offset, length}`，不得把 wire 上闭区间的末端索引直接带入文件 API。只有支持稳定 seek 且
成功取得长度的流才可报告 replayable；pipe、终端和其他不可 seek 的 `FILE*`/iostream 只能作为
未知长度、不可重放 source。borrowed 文件 source 的 `rewind()` 回到适配器捕获的起始位置，
不是强制回到宿主文件的绝对 0。

#### 8.4.1 Source 结束与长度契约

source 的 `read_some()` 沿用现有 Riwo HTTP 的 EOF 风格：

- 非空 buffer 读到正常结束时返回 `riwo::errc::eof` 和 0 字节，不用“成功 + 0 字节”表达结束；
- 成功结果对非空 buffer 必须包含至少 1 字节；如果一次读取返回了有效数据，即使该数据
  恰好是最后一段，也先成功返回字节数，下一次读取再返回 `riwo::errc::eof`；
- 空 buffer 立即成功返回 0，不作为 EOF 探测；
- streaming source 以 `riwo::errc::eof` 表达正常结束，协议层把它转换为对应的 message end，
  不把 EOF 当作 stream failure；
- known-length source 在声明长度之前返回 EOF 是 `body_length_mismatch`；达到声明长度后协议层
  不再读取 source；source 自己报告的 `size()` 与显式长度不一致时必须在发送 head 前失败；
- `rewind()` 对不可重放 source 返回 `operation_not_supported`，不能假装成功。

从 transport 收到 EOF 与 source/body reader 的逻辑 EOF 不同。只有 HTTP/1 允许由连接关闭
定界且 parser 已确认消息完整时，transport EOF 才转换为正常 body EOF；其他提前断开必须
保留为 transport 或 protocol error。

### 8.5 交互式 Reader 与 Writer

```cpp
struct body_read_result
{
	size_t bytes = 0;
	bool eof = false;
};

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_body_reader
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;

	basic_body_reader();

	template <typename Exec0>
	explicit basic_body_reader(Exec0 &&executor);

	explicit basic_body_reader(
		std::shared_ptr<detail::body_reader_state> state);

	template <typename Exec0>
	basic_body_reader(Exec0 &&executor,
		std::shared_ptr<detail::body_reader_state> state);

	executor_t get_executor() const noexcept;
	bool is_finished() const noexcept;

	template <typename Token = use_sync_t>
	auto read_some(mutable_buffer buffer, Token &&token = {});

	template <typename Token = use_sync_t>
	auto discard(Token &&token = {});

	template <typename Token = use_sync_t>
	auto trailers(Token &&token = {});
};

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_body_writer
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;

	basic_body_writer();

	template <typename Exec0>
	explicit basic_body_writer(Exec0 &&executor);

	explicit basic_body_writer(
		std::shared_ptr<detail::body_writer_state> state);

	template <typename Exec0>
	basic_body_writer(Exec0 &&executor,
		std::shared_ptr<detail::body_writer_state> state);

	executor_t get_executor() const noexcept;

	template <typename Token = use_sync_t>
	auto write(const_buffer buffer, Token &&token = {});

	template <typename Token = use_sync_t>
	auto finish(fields trailers = {}, Token &&token = {});

	void cancel() noexcept;
};

using body_reader = basic_body_reader<>;
using body_writer = basic_body_writer<>;
```

Reader/Writer 是 body 层可独立构造的 handle，不依赖 client、reply 或 server 的友元权限。
公共 API 允许无参构造或只用 executor 构造尚未绑定协议状态的 handle；无参构造使用全局
executor。协议实现通过 body 层自己的明确 state/adapter 构造入口绑定实现，不能让高层
exchange 访问其私有成员。这样 body 只依赖 core 和 protocol 的公共消息类型，高层对象单向
组合 body。

公共 API 不提供 `is_chunked()` 或 `chunk_end()`。`finish()` 在 HTTP/1.1 中完成 chunked
消息，在 HTTP/2 中设置 END_STREAM；调用代码不区分协议。

Reader/Writer 的状态约束：

- 空 body 的第一次非空 `read_some()` 成功返回 `{0, true}`；
- `body_read_result::eof` 表示 `bytes` 对应的数据之后已无更多 body 数据；因此最后一批数据
  可以返回 `{n, true}`，不要求调用方额外读取一次来探测结束；
- `body_reader::is_finished()` 在 reader 已经产生过 `eof=true` 或 `discard()` 成功后返回 true；
  它只观察已知状态，不执行 I/O，也不承诺提前探测 chunked 或 close-delimited body 的结束；
- reader 完成后再次调用 `read_some()` 稳定成功返回 `{0, true}`；空 buffer 在尚未完成时
  返回 `{0, false}`，不作为 EOF 探测；
- `body_reader::trailers()` 只能在 `read_some()` 已返回 `eof=true` 或 `discard()` 成功后调用，提前
  调用返回 `operation_not_permitted`；没有 trailer 时成功返回空 `fields`；结果在 reader
  生命周期内缓存，后续调用返回相同内容的副本；
- `discard()` 在内部消费到 `eof=true`；它仍受 body 大小、idle timeout
  和取消限制，不能形成无界缓冲；
- known-length writer 的累计写入不得超过声明长度。超出的 `write()` 以
  `body_length_mismatch` 完成，并允许报告已经接纳的前缀字节数；
- known-length writer 在累计写入不足时调用 `finish()`，以 `body_length_mismatch` 失败并取消
  本 stream，不能发送一个与 `Content-Length` 不一致的完成消息；
- streaming writer 只有成功 `finish()` 才产生正常 message end；
- `finish()` 的 trailer 包含未在 `body_spec` 声明的名称，或消息语义禁止的字段时，以
  `invalid_trailer` 失败并终止当前 stream；
- `finish()` 成功后再次 write/finish，或者失败进入终止状态后再次操作，稳定返回
  `operation_not_permitted`；
- empty writer 在 head 提交时已经结束，任何 write/finish 都返回 `operation_not_permitted`。

同一个 reader 最多有一个未完成 read，同一个 writer 最多有一个未完成 write；一个
stream 可以同时进行一个读和一个写。

### 8.6 Asio I/O 与完成语义

Experimental HTTP 沿用 Riwo 已有的 Asio 风格，不另建线程模型或 future/task 调度模型：

- `client`、exchange、`body_reader`、`body_writer`、`duplex_channel` 等公开异步 I/O
  对象或 handle 必须定义 `executor_type` 并提供 `get_executor()`；对象的底层操作在该
  executor 或其内部 strand 上推进；
- `use_sync_t`、completion token、`redirect_error`、`redirect_time`、`use_awaitable`、
  cancellation slot 等行为与现有模块保持一致；
- 异步读写使用稳定的 `error_code` 完成通道；`read_some()` 返回 `body_read_result`，
  `write()` 返回接纳的字节数，发生错误时也允许报告已经完成的部分；正常 body EOF
  不是错误，不进入 `error_code` 通道；
- `body_writer::write()` 是覆盖整个输入 buffer 的 composed operation；成功时报告的字节数
  等于输入大小。这里的“完成”表示输入已被 stream 的有界发送路径接纳，不表示 peer 已经
  收到或处理；
- 传给异步 `read_some()` 的可写内存和传给异步 `write()` 的只读内存必须保持有效，直到
  对应 completion 被调用；completion 之后调用者可以安全复用该内存。`detached` 写入是
  唯一例外：启动时复制 body，由操作持有副本；
- `finish()` 成功表示本地协议状态已接纳结束标记和 trailers，并会按协议发送，不表示
  peer 确认；如果应用需要“所有本地输出均已写入 transport”的诊断能力，应另设内部
  drain primitive，不改变普通 `finish()` 语义；
- 同一个 reader 不得重叠两个 read；同一个 writer 不得重叠 write/write 或
  write/finish。一个 stream 允许一个 read 与一个 write 同时存在；
- 即时参数错误和启动失败仍通过所选 token 的正常完成路径交付，不绕过关联 executor；
- completion handler 的 associated executor、allocator、cancellation slot 和
  immediate executor 按 Asio 规则传播；底层协议状态仍只在 Session executor/strand
  上访问；
- `cancel()` 只是请求取消。已经开始的操作在各自 completion 交付前仍持有原有 buffer
  生命周期要求，错误通常归一化为 `operation_aborted` 或更具体的稳定协议错误。

异步对象本身的共享线程安全规则见第 17 节；关联 executor 决定 completion 在哪里执行，
不自动使同一公共对象的并发发起调用变为线程安全。

当前文件 source/sink 采用“异步形状、同步文件 I/O”：HTTP 状态机仍以 completion token 推进，
但每个有界 chunk 的 seek/read/write/flush 在 exchange executor 上同步执行，会在该系统调用完成前
占用执行线程。实现不隐式创建线程池，也不宣称同步文件操作可被 cancellation 中断；应限制单个
chunk 大小，使网络流量控制、其他 stream 和取消观察点能在 chunk 之间继续推进。

后续可在不改变 `concepts::body_source_model`、sink、完成签名、顺序和 EOF/长度规则的前提下，为库拥有的
路径文件增加 Linux `io_uring` 与 Windows IOCP/OVERLAPPED 后端。调用方传入的
`std::fstream`/`FILE*` 默认继续使用同步后端：在没有安全、独占的 native handle 与缓冲状态协调
协议时，禁止把原生异步读写和 stdio/iostream 的缓冲、当前位置混用。某个平台若能证明可安全
接管，必须作为独立 capability 明确暴露，不能悄悄改变 borrowed 对象语义。

所有持有或需要 executor 的公开异步对象都同时提供
`type(executor, args...)` 和 `type(args...)` 两种构造形式，包括 `client`、body
reader/writer、client/server exchange、`reply`、`duplex_channel`、`acceptor_wrap` 和
`server`，以及公开的 `tcp_connection`。省略 executor 的形式，包括没有其它业务参数的无参
构造，使用 `riwo::get_executor()`；模板 executor 无法从 `io_executor_t` 构造时，这些
省略形式不参与重载，调用方仍需显式传入兼容 executor。

同一规则适用于扩展边界中接受 executor 的工厂和辅助对象：`session_pool`、
`client_state` 工厂、connector adapter、HTTP/1 client session 工厂和 body duplex state
工厂都保留显式 executor 形式，并提供省略 executor、使用全局执行器的对应形式。

公共操作的 completion 形状固定如下：

| 操作 | completion signature | 成功结果 |
| --- | --- | --- |
| `body_reader::read_some` | `void(error_code, body_read_result)` | 本次读取字节数和读取后是否到达 EOF |
| `body_writer::write` | `void(error_code, size_t)` | 本次接纳的字节数 |
| `body_reader::trailers` | `void(error_code, fields)` | body 结束后的 trailer |
| `body_writer::finish`、`body_reader::discard` | `void(error_code)` | 状态完成 |
| `client::send`、`client::fetch` | `void(error_code, reply)` | 已取得最终 response head 的 `reply` |
| `client::open` | `void(error_code, client_exchange)` | 已建立的 exchange |
| `client_exchange::wait_reply` | `void(error_code, response_head)` | 最终 reply head |
| `server_exchange::respond`、`inform` | `void(error_code)` | head 已被本地协议状态接纳 |
| `reply::read_all` | `void(error_code, std::vector<std::byte>)` | 从当前偏移读取的受限剩余 body |
| `reply::save_file` | `void(error_code, uint64_t)` | 写入文件的应用 body 字节数 |

- 返回拥有型对象、head 或数据的操作不接受 `detached`，避免静默丢弃 reply 或立即销毁
  handle；如需 fire-and-forget，后续提供具有明确响应排空上限的独立 API；
- 借用调用方可写 buffer 的 `read_some()` 不接受 `detached`；`write()` 接受 `detached`，
  并为该模式复制和持有只读 body；
- 只返回状态的 `finish()`、`discard()` 可以接受 `detached`，实现必须持有内部状态直到操作
  完成；
- 默认 `use_sync_t` 在失败时抛出 `std::system_error`，显式 `error_code`/重定向 token 保留
  现有 Riwo 行为；
- `read_some()` 只用错误通道报告异常结束；正常结束通过 `body_read_result::eof` 表达。
  `read_all()`、`save_file()`、`discard()` 和 `pipe()` 必须在内部消费该结果，不把成功的完整
  读取报告为失败。

### 8.7 对象复制、移动与引用生命周期

Experimental HTTP 保留现有 HTTP 的基本风格：有活动 I/O 状态的对象不能通过普通复制产生
第二个控制者，
配置和纯数据对象保持值语义。

| 类型 | 复制/移动约束 |
| --- | --- |
| `client_config`、policy、`fields`、head、`body_spec` | 可复制、可移动 |
| `request` | 不可复制、可移动；需要重试时由内部 replayable source 重建，不复制活动 request |
| `basic_client` | 与现有 client 一致：不可复制、可移动 |
| `client_exchange`、`reply` | 不可复制、可移动 |
| `server_exchange` | 不可复制、不可移动，由 server handler 以引用接收 |
| `body_reader`、`body_writer` | 不可复制、不可移动；可按 executor 独立构造，也可从所属 exchange/reply 取得引用 |
| 内部 Session | 不可复制、不可移动，由 pool/session manager 以共享实现持有 |

移动 client/exchange/reply 后，源对象只允许析构或重新赋值。移动前取得的 reader、writer、
header view、`string_view` 和 iterator 全部失效。销毁仍有未完成操作的 exchange/reply 会
请求取消；实现状态必须存活到所有 completion 交付完毕，但这不延长调用方 buffer 的生命周期。
任何 API 都不能因内部使用 `shared_ptr` 而暗示公开对象可复制。

## 9. 客户端公共 API

### 9.1 Client 配置

```cpp
struct client_config
{
	protocol_policy protocols {};
	proxy_policy proxy {};
	tls_policy tls {};
	timeout_policy timeout {};
	pool_config pool {};
	redirect_policy redirects {};
	retry_policy retries {};
	cookie_config cookies {};
	message_limits reply_limits {};
	bool automatic_decompression = true;
};

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_client;

using client = basic_client<>;
```

客户端已接线 `protocols`、`tls`、`timeout`、`pool`、`cookies`、`reply_limits`、
`automatic_decompression`、`proxy`、`redirects` 和 `retries`。

`client` 不带 HTTP 版本模板参数。同一个 client 可以同时持有不同 origin、不同版本的
Session。`basic_client<Exec>::executor_type` 为 `Exec`；由它创建的
`basic_client_exchange<Exec>`、`basic_reply<Exec>`、`basic_body_reader<Exec>` 和
`basic_body_writer<Exec>` 保留相同的具体 executor 类型。只有用户选择无模板参数默认别名时，
`Exec` 才是 `asio::any_io_executor`，公共层不得强制擦除用户的 executor 类型。

```cpp
explicit basic_client(config_t config = {});
explicit basic_client(std::shared_ptr<client_session> session,
	config_t config = {});
explicit basic_client(
	std::shared_ptr<basic_transport_connector<Exec>> connector,
	config_t config = {});

template <typename Exec0>
explicit basic_client(Exec0 &&executor, config_t config = {});

template <typename Exec0>
basic_client(Exec0 &&executor,
	std::shared_ptr<client_session> session,
	config_t config = {});

template <typename Exec0>
basic_client(Exec0 &&executor,
	std::shared_ptr<basic_transport_connector<Exec>> connector,
	config_t config = {});

basic_client &set_config(config_t config);
config_t config() const;
```

`basic_client<Exec>` 提供接受
`std::shared_ptr<basic_transport_connector<Exec>>` 的构造重载。未传 connector 时使用内置
TCP/TLS 实现；显式传入时，DNS、建连、proxy/TLS 和协议选择由 connector 契约负责，后续 Pool、
Session、redirect/retry 和 Cookie 行为保持不变。connector 重载是应用 transport 注入点；
`client_session` 重载保留给协议层测试和 Session 注入，不是应用 transport 扩展入口。

`proxy_policy`、`tls_policy`、`timeout_policy`、`pool_config`、`redirect_policy` 和
`retry_policy` 都必须是具有安全默认值的完整公共值类型，不能只在 `.cpp` 中存在同名占位结构。
公共策略字段不得被静默忽略；当前构建不支持的可选能力必须返回明确错误。

### 9.2 简单请求

```cpp
experimental::http::client default_client;
experimental::http::client configured_client(config);
experimental::http::client client(executor);

auto reply = co_await client.get(
	"https://example.com/users/42", use_awaitable
);

auto reply = co_await client.post(
	"https://example.com/messages",
	"hello",
	use_awaitable
);
```

与稳定版 `http::client` 一致，未显式传入 executor 的构造使用
`riwo::get_executor()`。显式传入 executor 时仍保留其具体类型。

便利方法最终都调用 `send(request)`：

```cpp
request req(method::post, "https://example.com/items");
req.headers().set("authorization", "Bearer ...");
req.set_body("payload");

auto reply = co_await client.send(std::move(req), use_awaitable);
```

`send()` 在响应头可用时完成并返回 `reply`；response body 仍可流式消费。request
持有的 source、上传任务和 stream 状态由返回对象的共享实现保持，不要求调用者继续
持有原 request。

这里的“响应头”专指最终响应。客户端必须解析任意数量的 1xx informational response，
但 `send()`/`wait_reply()` 不以 1xx 完成。非 100 的 informational response 可通过
request option 或 observer hook 观察；没有注册观察者时允许忽略。101 只能由明确的
Upgrade API 接管，普通 `send()` 不把它当作一般最终 response。

`Expect: 100-continue` 由 Session 协调：配置要求等待时，head 发出后暂不读取 source，直到
收到 100、等待期限到达或收到最终响应。收到 100 或等待期限到达后开始发送 body；收到最终
响应后不再等待或继续启动新的 source read。

最终响应可能在请求体完成前到达。Session strand 按观察顺序决定结果：

- 最终响应 head 先被接受：停止请求更多 source 数据、取消未完成的 source read，并把有效
  reply 返回调用方；由最终响应导致的 request-body 提前关闭不覆盖该 reply；
- 本地 source/协议发送错误先被接受：该次 `send()` 失败并取消 exchange；之后到达的响应只
  用于协议清理和诊断；
- 交互式 writer 在最终响应后尚未完成的 write/finish 以 `request_body_closed` 或更具体的
  stream error 完成，不能静默成功；
- 停止上传不等于立即关闭 HTTP/2 Session；HTTP/1 是否可复用由消息边界能否安全恢复决定。

`fetch()` 是显式全量缓存便利接口：

```cpp
auto reply = co_await client.fetch(std::move(req), use_awaitable);
auto bytes = reply.buffered_body();
```

必须设置可配置的最大缓存大小；超过限制返回错误，不自动无限增长。`fetch()` 在最终 head、
完整 body 和 trailers 都可用后完成；返回 reply 的 body reader 已处于 EOF，trailers 仍可
通过正常 accessor 取得。

### 9.3 流式 Exchange

无法预先提供 body source，或需要请求/响应全双工时使用 `open()`：

```cpp
auto exchange = co_await client.open(
	method::post,
	"https://example.com/upload",
	body_spec::streaming(),
	use_awaitable
);

co_await exchange.request_body().write(part1, use_awaitable);
co_await exchange.request_body().write(part2, use_awaitable);
co_await exchange.request_body().finish({}, use_awaitable);

auto head = co_await exchange.wait_reply(use_awaitable);
auto &body = exchange.reply_body();
```

需要设置 header 的流式请求使用 request 重载；它是 `open()` 的规范入口，以上 method/URL
形式只是便利转发：

```cpp
request req(method::post, "https://example.com/upload");
req.headers().set("content-type", "application/octet-stream");
req.headers().set("authorization", "Bearer ...");

auto exchange = co_await client.open(
	std::move(req), body_spec::streaming(), use_awaitable
);
```

```cpp
template <typename Token = use_sync_t>
auto open(request req, body_spec spec, Token &&token = {});
```

传给该重载的 request 必须仍处于可冻结状态，并且尚未设置 memory/file/custom source；动态
body 和 trailer 的声明完全来自单独的 `body_spec`。request 的 method、target、headers 和
request options 在 `open()` 中一起冻结。发现已有 body、重复 open 或非法 spec 时，通过所选
token 以 `operation_not_permitted` 完成，不在异步发起点抛出。便利重载构造 request 后调用
此规范入口，不能形成另一套冻结、校验或 header 默认值逻辑。

```cpp
template <core_concepts::exec Exec = asio::any_io_executor>
class basic_client_exchange
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_t = basic_body_reader<Exec>;
	using body_writer_t = basic_body_writer<Exec>;

	basic_client_exchange();

	template <typename Exec0>
	explicit basic_client_exchange(Exec0 &&executor);

	explicit basic_client_exchange(std::shared_ptr<session_stream> stream,
		std::shared_ptr<cookie_jar> cookies = {}, riwo::url origin = {});

	template <typename Exec0>
	basic_client_exchange(Exec0 &&executor,
		std::shared_ptr<session_stream> stream,
		std::shared_ptr<cookie_jar> cookies = {}, riwo::url origin = {});

	executor_t get_executor() const noexcept;

	protocol selected_protocol() const noexcept;
	body_writer_t &request_body() noexcept;

	template <typename Token = use_sync_t>
	auto wait_reply(Token &&token = {});

	body_reader_t &reply_body() noexcept;
	void cancel() noexcept;

	stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	Extension *extension() noexcept;
};

using client_exchange = basic_client_exchange<>;
```

响应可以在请求体发送完成前到达。read loop 与 write pump 必须独立推进，API 不假设
严格的“上传完毕后才读取响应”。

`wait_reply()` 只允许一个未完成调用，成功返回最终 `response_head`。在它成功前取得
`reply_body()` 引用是允许的，但对该 reader 发起读取稳定返回 `operation_not_permitted`；
这样 accessor 始终无异常，而状态错误仍走异步完成通道。

### 9.4 Reply

```cpp
using reply_head = response_head;

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_reply
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_t = basic_body_reader<Exec>;

	basic_reply();

	template <typename Exec0>
	explicit basic_reply(Exec0 &&executor);

	basic_reply(protocol selected_protocol, reply_head head,
		std::shared_ptr<detail::body_reader_state> body,
		std::vector<std::byte> buffered_body = {},
		std::shared_ptr<void> owner = {},
		uint64_t max_buffered_body = 8U * 1024U * 1024U,
		protocol reply_protocol = protocol::v1_1);

	template <typename Exec0>
	basic_reply(Exec0 &&executor, protocol selected_protocol,
		reply_head head,
		std::shared_ptr<detail::body_reader_state> body,
		std::vector<std::byte> buffered_body = {},
		std::shared_ptr<void> owner = {},
		uint64_t max_buffered_body = 8U * 1024U * 1024U,
		protocol reply_protocol = protocol::v1_1);

	executor_t get_executor() const noexcept;

	protocol selected_protocol() const noexcept;
	protocol http_version() const noexcept;
	experimental::http::status status() const noexcept;
	const fields &headers() const noexcept;

	body_reader_t &body() noexcept;

	template <typename Token = use_sync_t>
	auto read_all(Token &&token = {});

	template <typename Token = use_sync_t>
	auto save_file(std::filesystem::path, Token &&token = {});

	std::span<const std::byte> buffered_body() const noexcept;
	void cancel() noexcept;
};

using reply = basic_reply<>;
```

客户端侧统一使用 `reply` 表示入站回复，并通过 `reply_head` 别名引用与协议层共享的
`response_head`。

`read_all()` 和 `save_file()` 是构建在 `body_reader` 上的便利方法，不属于具体协议实现。
`read_all()` 与 `fetch()` 一样必须使用显式参数或 client 默认值限制最终应用字节数；自动解压
时限制作用于解压后的字节数，并另行保留 wire 字节统计，避免小压缩包触发无界内存增长。
两者从 reader 当前偏移消费剩余 body，不能与手工 read 重叠；返回的数据或字节数只表示本次
消费的剩余部分，而 message 总量限制包含此前已经读取的字节。
`buffered_body()` 只对成功的 `fetch()` 结果返回缓存 view；其他来源的 reply 返回空 span。
该 view 在 reply 移动、重新赋值或析构时失效。

`save_file(path)` 仍是当前路径便利接口。窄字符输出流和 `FILE*` 通过第 20.1 节的
`file_sink(...)` 交给通用 `pipe(reply.body(), sink, token)`；顺序下载只要求 sequential sink，
Range 拼装或并发分段下载必须选择支持 `write_at(offset, ...)` 的 random-access file sink。

未消费 body 就销毁 reply 时：

- HTTP/2 取消对应 stream；
- HTTP/1.x 在不能安全排空时关闭连接，使其不能回池；
- 希望保持连接可复用的应用应显式 `discard()` 或完整读取 body。

## 10. 服务端公共 API

服务端保留稳定 HTTP 的链式门面和常用类型别名，但 handler 参数改为协议无关的
`server_exchange`。`server::context_t` 是 `server_exchange` 的兼容别名，不重新引入持有物理
connection 的旧 `service_context`：

```cpp
experimental::http::server_config config;
experimental::http::acceptor_wrap acceptor(
	experimental::http::acceptor_wrap::acceptor_t(riwo::get_executor()));
experimental::http::server server(std::move(acceptor), config);

server
	.bind({ip_type::v4, 8080})
	.on_request<method::get>("/", handler)
	.on_default(default_handler)
	.start();
```

```cpp
template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
class basic_server
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using service_executor_t = ServiceExec;
	using config_t = server_config;
	using context_t = basic_server_exchange<ServiceExec>;

	explicit basic_server(basic_acceptor_wrap<Exec> &&acceptor,
		config_t config = {});

	basic_server &bind(tcp_endpoint);

	template <method::well_known... Methods,
		core_concepts::text_p<char> Path, typename Handler>
	basic_server &on_request(Path &&path, Handler &&handler);

	template <core_concepts::text_p<char> Path, typename Handler>
	basic_server &on_request(method method, Path &&path,
		Handler &&handler);

	template <typename Handler>
	basic_server &on_default(Handler &&handler);

	basic_server &start(
		size_t backlog = asio::socket_base::max_listen_connections);
	basic_server &stop() noexcept;

	basic_server &set_config(config_t);
	config_t config() const noexcept;
};

using server = basic_server<>;
```

当前实现保留上述 TCP convenience 构造，并由 `basic_acceptor_wrap<Exec>` 统一承载 TCP、TLS
和自定义 acceptor，同时提供接管 `basic_negotiated_transport<Exec>` 的 `serve()`。所有 acceptor
使用相同的 router、service executor、server config、graceful shutdown 和错误处理契约。

模板 `on_request<method::post>()` 只接受 well-known method；扩展方法使用
`on_request(method("CUSTOM"), path, handler)` 的运行时重载，不能丢失原始大小写。空 method
模板包表示匹配任意方法。pattern router、默认 handler、server error handler、service error
handler、中间件和应用 Session 均构建在该门面之后；协议 Session 不进入这些 API。运行时路由
更新发布不可变 copy-on-write 快照，请求分派读取快照时不获取写锁。

服务端路由和 handler 不区分 HTTP/1.x 与 HTTP/2：

```cpp
server.on_request<method::post>(
	"/upload",
	[](server_exchange &exchange) -> awaitable<void>
	{
		co_await pipe(
			exchange.request_body(),
			file_sink("upload.bin")
		);

		co_await exchange.respond(
			response_head{status::ok},
			body_spec::known(2),
			use_awaitable
		);

		auto &writer = exchange.response_body();
		co_await writer.write(buffer("OK"), use_awaitable);
		co_await writer.finish({}, use_awaitable);
	}
);
```

```cpp
template <core_concepts::exec Exec = asio::any_io_executor>
class basic_server_exchange
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_t = basic_body_reader<Exec>;
	using body_writer_t = basic_body_writer<Exec>;

	basic_server_exchange();

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor);

	basic_server_exchange(std::shared_ptr<session_stream> stream,
		request_t request);

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor,
		std::shared_ptr<session_stream> stream, request_t request);

	executor_t get_executor() const noexcept;

	protocol selected_protocol() const noexcept;
	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;
	const request_t &request() const noexcept;
	const parameter_view &path_parameters() const noexcept;
	const parameter_view &query_parameters() const noexcept;
	optional<value> path_parameter(
		core_concepts::text_p<char> auto &&name) const;
	optional<value> query_parameter(
		core_concepts::text_p<char> auto &&name) const;
	app_session_ptr find_session();
	app_session_ptr session();
	body_reader_t &request_body() noexcept;
	body_writer_t &response_body() noexcept;

	template <typename Token = use_sync_t>
	auto save_file(std::filesystem::path, Token &&token = {});

	template <typename File, typename Token = use_sync_t>
	auto save_file(File &&file, Token &&token = {});

	template <typename Token = use_sync_t>
	auto respond(body_spec, Token &&token = {});

	template <typename Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {});

	template <typename Token = use_sync_t>
	auto respond(const const_buffer &body, Token &&token = {});

	template <typename Token = use_sync_t>
	auto respond(response_head, const const_buffer &body, Token &&token = {});

	template <typename Source, typename Token = use_sync_t>
	auto respond(Source &&source, Token &&token = {})
		requires concepts::body_source_model<std::remove_cvref_t<Source>>;

	template <typename Source, typename Token = use_sync_t>
	auto respond(response_head, Source &&source, Token &&token = {})
		requires concepts::body_source_model<std::remove_cvref_t<Source>>;

	template <typename Token = use_sync_t>
	auto respond_file(std::filesystem::path, Token &&token = {});

	template <typename Token = use_sync_t>
	auto respond_file(std::filesystem::path,
		file_response_options, Token &&token = {});

	template <typename Token = use_sync_t>
	auto inform(response_head, Token &&token = {});

	void cancel() noexcept;
	stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	Extension *extension() noexcept;
};

using server_exchange = basic_server_exchange<>;
```

`respond(response_head, body_spec)` 的 completion 为 `void(error_code)`；成功后从
`response_body()` 取得 exchange 所有的 writer。带 `const_buffer` 的便利重载负责发送整个
body；字符串字面量、`std::string` 和 `std::string_view` 可直接传入，其他连续存储通过
`riwo::buffer(...)` 适配。非 detached 调用借用 body 到 completion，不复制 payload；
detached 调用在启动时复制并持有 body。该重载也只以 `void(error_code)` 完成，不返回临时 writer。
满足 `concepts::body_source_model` 的重载拥有传入的 source，按 source 的已知长度或 streaming 模式提交
head，持续读取、写入并完成 response writer；source 的 finalize/finish 在 writer finish 之前执行。
因此服务端可以直接以 `file_body_source` 发送路径、C++ 文件流或 `FILE*`，无需自行维护
`response_body()` 的读写循环。同步 token 执行同步文件 I/O；异步 token 当前只提供异步组合操作
形状，文件 read/seek 本身仍是同步调用。
对于合法的 HEAD 响应，已知 source 长度转换为 `body_spec::head(length)`，只提交表示长度并完成
source，不读取或发送文件字节；未知长度 source 则发送不带表示长度的空 body。
最终 response head 只能成功提交一次；
提交前对 `response_body()` 发起 write/finish 返回 `operation_not_permitted`。

handler 在 `finish()` 成功后返回不会销毁尚待 write pump 发送的协议状态；server 持有该状态
直到已接纳的输出写出或发生错误。handler 在 writer 未 finish 时返回则按下述提前返回规则
终止当前响应，不能把未完成 body 当作成功。

pattern Router、中间件/AOP、应用 Session 和错误 handler 都工作在 `server_exchange` 上。
HTTP/2 每个 stream 可独立调度业务 handler，但协议 Session 状态仍在内部 strand 上串行化。
这些应用能力不观察也不依赖底层 H1 connection 或 H2 stream 的物理生命周期。

`inform()` 只接受合法 1xx head，不结束 response；HTTP/1.0 请求调用时返回
`operation_not_supported`。收到合法 `Expect: 100-continue` 后，应用第一次读取 request
body 时 Session 自动发送 100，除非应用已经发送最终响应。应用也可以显式 `inform(100)`，
但同一个 exchange 不得重复自动发送。

handler 可以在未完整消费 request body 时发送最终响应，但必须使用有界清理策略：

- HTTP/1 Session 在配置的字节数和时间预算内排空剩余 request body；无法确认边界时，响应
  完成后关闭连接，不得回池或继续解析下一请求；
- HTTP/2 Session 只停止/终止该 stream 的剩余入站方向，不能因此关闭承载其他 stream 的
  connection；具体 frame 顺序必须符合 RFC 9113；
- 清理过程不占用 service handler，也不能为了复用连接而无界读取或缓存；
- handler 正常返回但尚未开始 response 时，先调用自定义 default handler，再使用可配置的
  HTML/text fallback（默认 200 欢迎页）；handler 抛出异常且尚未提交 response 时生成 500
  fallback。如果 response 已经开始，则只终止当前 stream/connection 的可恢复范围，不能再
  生成第二个最终 head。

## 11. 能力与协议专属扩展

普通功能不要求检查协议版本。确实存在差异时，优先用通用能力表达：

```cpp
struct stream_capabilities
{
	bool trailers;
	bool full_duplex;
	bool tunnel;
	bool extended_connect;
};
```

只有无法统一的诊断或高级协议能力进入扩展。高层 exchange 与低层 engine 使用同一扩展类型，
但获取入口分别位于 exchange 和 engine：

```cpp
if (auto v2 = exchange.extension<experimental::http::v2::stream_extension>())
{
	std::cout << v2->stream_id();
}

if (auto v2 = engine.extension<experimental::http::v2::connection_extension>())
{
	auto peer = v2->peer_settings();
}
```

当前高层 exchange 支持 `v2::stream_extension`，公共 HTTP/2 engine 支持
`v2::connection_extension`。扩展 `stream_extension` 返回稳定的 wire stream ID；
`connection_extension` 返回调用时取得的 local/peer SETTINGS、待写出字节数和 draining
状态。HTTP/1 stream 或不受支持的扩展类型返回 `nullptr`。

适合扩展的内容包括：

- HTTP/2 stream ID；
- peer/local SETTINGS 快照；
- frame 和 flow-control 诊断；
- Extended CONNECT；
- 协议事件 observer、日志 sink 和资源分配策略；
- 将来可能实现的 Server Push。

扩展不可成为普通 body 读写、取消或超时的必经路径。公共扩展以 Riwo 自有值类型和接口表达，
不得要求应用 include nghttp2、OpenSSL 或内置 TCP transport 的头文件。未知扩展查询返回空值，
不能改变 engine 或 exchange 状态。

## 12. 公共 Protocol API 与 Session 适配

协议层必须能脱离 client、server、Session Pool、socket 和 executor 独立使用。公共协议引擎只做
确定性的状态转换：应用送入收到的字节或出站语义 action，引擎同步返回消费进度、语义 event 和
待发送字节。引擎不得主动调用 transport、等待 timer、启动协程或隐藏阻塞。

高层公共 API 仍通过 PIMPL 持有 stream state；内部 Session driver 把相同的公共协议引擎连接到
公共 byte-stream 接口，并把 event/action 适配成 exchange 和 body。不得为高层门面另写一套
parser、serializer 或 HTTP/2 callback 映射。

### 12.1 公共事件与 action

公共事件模型以拥有语义 metadata、短生命周期 body view 为基础：

```cpp
using stream_token_t = uint64_t;

using protocol_event_t = std::variant<
	request_head_event,
	response_head_event,
	body_data_event,
	trailers_event,
	stream_end_event,
	stream_reset_event,
	connection_event>;

using protocol_action_t = std::variant<
	open_request_action,
	send_response_action,
	send_data_action,
	send_trailers_action,
	reset_stream_action,
	shutdown_action>;

enum class event_flow { continue_, pause };
using protocol_event_handler_t =
	std::function<event_flow(const protocol_event_t &)>;
```

真实声明位于 `<riwo/experimental/http/protocol/event.h>`。`request_head`、`response_head`、trailers、错误和 SETTINGS 快照等 metadata
由 event 自身拥有；`body_data_event` 可以引用本次 `ingest()` 的输入，但引用只在 handler 返回前
有效。需要跨调用保留数据的应用必须复制或转移到自己的有界存储。handler 返回 `pause` 后，引擎
停止产生后续 event，并通过结果精确报告已消费的输入。`continue_` 表示接受当前 event；`pause`
表示当前 event 尚未被接受，引擎不得提交对应状态，`consumed` 停在该 event 对应输入之前，并在
恢复时重新交付它。调用方必须重新提交未消费输入，不能假定 engine 已经保留该 buffer。

`stream_token_t` 是一次 engine 生命周期内的稳定逻辑标识，不等同于 HTTP/2 wire stream ID。
HTTP/1 当前活动 exchange 也获得 token，因此上层和测试代码无需根据协议版本分叉。HTTP/2
stream ID、SETTINGS 和 frame 诊断通过第 11 节的扩展查询获得。

所有出站 action 必须拥有其异步保留的数据，或持有显式 buffer lease。引擎不得保存调用方临时
对象、裸 `string_view` 或没有 lifetime token 的 buffer。提交 action 成功表示引擎接受了语义
操作，不表示字节已经由 transport 写出；实际写出边界由 driver 或用户 I/O 循环报告。

### 12.2 增量驱动契约

HTTP/1 和 HTTP/2 分别公开 `v1::connection_engine` 与 `v2::connection_engine`，两者满足同一
`concepts::protocol_engine` concept，但保留各自配置和扩展类型。公共 surface 至少提供以下能力：

engine 构造时必须显式给出 `client` 或 `server` role。H1 配置同时给出允许的版本、消息限制和
初始 request/response 方向；H2 配置给出本地 SETTINGS、flow-control 与内存预算。role 和协议
大版本在 engine 生命周期内不可改变，所有其他状态只能通过 event/action 推进。

```cpp
struct ingest_result
{
	size_t consumed;
	bool paused;
	bool wants_write;
	bool needs_input;
};

struct produce_result
{
	size_t produced;
	bool has_more;
};

namespace concepts
{

template<class Engine>
concept protocol_engine = requires(Engine &engine, const_buffer input,
	mutable_buffer output, protocol_action_t action,
	protocol_event_handler_t events)
{
	{ engine.selected_protocol() } -> std::same_as<experimental::http::protocol>;
	{ engine.ingest(input, events) } -> std::same_as<sys_expected<ingest_result>>;
	{ engine.finish_input(events) } -> std::same_as<sys_expected<ingest_result>>;
	{ engine.submit(std::move(action)) } -> std::same_as<sys_expected<void>>;
	{ engine.produce(output) } -> std::same_as<sys_expected<produce_result>>;
	{ engine.wants_write() } noexcept -> std::same_as<bool>;
};

} //namespace concepts
```

真实声明位于 `<riwo/experimental/http/protocol/engine.h>`；声明使用项目已有 buffer 和 expected 别名，并保持以下
行为：

- 任意输入分片方式必须产生相同语义结果；不完整 frame、header 或 chunk 返回 `consumed` 和
  “需要更多输入”状态，不把正常分片报告为错误；
- `produce()` 支持任意大小的非空输出 buffer，并可多次调用；不能要求调用方先猜测完整 frame
  或 header 大小；
- 空输入不是 EOF。transport EOF 必须显式调用 `finish_input()`，由协议规则判断正常完成还是
  `unexpected_eof`；
- `ingest()`、`submit()`、`produce()` 都是同步、非阻塞的状态推进函数。一个 engine 实例的调用
  必须串行化；不同实例可以并发使用；
- engine 的 pending input、pending output、header block、HPACK table 和 stream 状态全部受配置
  上限约束。达到上限时返回背压/容量结果或稳定错误，不能无界缓存；
- `submit()` 先完成协议校验再改变可观察状态；失败 action 不得留下半提交 frame；
- event handler 可以重入查询只读状态，但不得在 handler 栈内再次调用 `ingest()`、`submit()`
  或 `produce()`；
- 错误必须带 connection/stream scope。connection error 之后不再接受普通 action，但仍允许
  `produce()` 排空规范要求的 GOAWAY/关闭输出。

公共 engine API 不承诺 wire 输出的 frame 分片与 nghttp2 版本无关，只承诺符合规范、语义事件
和错误范围稳定。依赖逐帧诊断的应用使用 observer 扩展，不能把特定 frame 切分当成 ABI。

### 12.3 HTTP/1 独立 codec

除完整的 `v1::connection_engine` 外，HTTP/1 还公开可独立使用的 head 与 chunk codec，覆盖只需
序列化、反序列化而不需要 Session 生命周期的应用：

```cpp
struct v1::serialized_head
{
	std::string wire;
	body_framing framing;
	bool close_after_message;
};

sys_expected<v1::serialized_head> v1::serialize_request_head(
	const request_head &, const body_spec &, protocol,
	request_target_form = request_target_form::origin);

sys_expected<v1::serialized_head> v1::serialize_response_head(
	const response_head &, const body_spec &, protocol,
	connection_disposition = connection_disposition::keep_alive);

class v1::request_parser;
class v1::response_parser;
class v1::chunk_decoder;
class v1::chunk_encoder;
```

真实声明位于 `<riwo/experimental/http/protocol/v1/codec.h>`。parser 是增量对象，返回已消费字节、完整 head、framing 决策以及
head 后尚未消费的字节；不能要求输入包含完整消息，也不能偷偷读取 body。`serialize_*_head()`
只生成 start-line 与 fields，并返回由 `body_spec` 决定的 framing；body/chunk 编码由 encoder 或
connection engine 完成。如此可避免名为“serialize request”的函数把大型 body 隐式聚合到内存。

### 12.4 HTTP/2 公共引擎与扩展

HTTP/2 的 HPACK、SETTINGS、stream 状态、flow control 和 CONTINUATION 属于 connection 状态，
因此不提供误导性的无状态 `serialize_frame(request_head)` API。`v2::connection_engine` 是最小
公共编解码单元；它通过通用 event/action 工作，并提供不泄漏 nghttp2 的扩展：

- local/peer SETTINGS 的拥有型快照；
- wire stream ID 与 `stream_token_t` 的查询；
- connection/stream window 和 pending-byte 诊断；
- 只读 frame observer；
- 自定义资源限制、allocator/buffer policy 和日志 sink。

observer 只能观察已通过校验的 frame/event，不得修改 payload、伪头字段或状态转换。allocator、
buffer policy 和日志回调不能绕过配置的硬上限，也不能从 nghttp2 callback 保存无所有权指针。
公共头只声明 Riwo 类型；nghttp2 头、类型、常量和回调签名保持私有。

### 12.5 内部 Session driver

内部 `client_session`/`server_session` 继续承担 stream capacity、exchange/body 适配、executor、
deadline 和关闭协调。它们组合一个公共 engine 与一个公共 byte stream，而不是继承或复制 engine：

```text
connecting -> negotiating -> ready -> draining -> closed
                         \-> failed -----------^
```

- `ready`：可以创建新 stream；
- `draining`：只推进已有 stream；
- `closed`：所有等待操作以最终错误完成；
- engine 本身没有 strand；由 Session driver 保证对同一 engine 的状态推进串行化；
- 用户手动驱动 engine 时，同样由用户保证串行调用和 buffer 生命周期；
- 公共 engine 的协议错误、event 和输出必须与 Session 路径逐项一致。

## 13. HTTP/1 Engine

HTTP/1 engine 复用第 12.3 节的公共 parser/encoder，对上产生语义 event、对下产生待发送字节：

```text
start head -> fields -> head complete -> body bytes -> trailers -> end
```

设计约束：

- HTTP/1.1 不实现 pipelining；Session capacity 固定为 0 或 1；
- HTTP/1.0 与 HTTP/1.1 共用一个 engine，根据起始行选择版本；
- parser 的 pending bytes 属于 engine；由 Session driver 或用户 I/O loop 保持尚未消费输入的
  所有权，不属于单个 request 对象；
- 完整消费 response body 后连接才可复用；
- EOF 定界 body 完成后连接必然不能复用；
- 请求取消若不能恢复消息边界，关闭整条连接；
- framing 决策完全来自 body spec 和协议规则；
- Upgrade 成功后 Session 转为 tunnel ownership，不再解析 HTTP/1 消息。

HTTP/1 的单 stream 限制通过 engine action 约束和 Session capacity 表达，不能泄漏为不同的
高层用户 API。低层用户可以观察到同一时刻只有一个活动 `stream_token_t`，但不能绕过该限制。

## 14. HTTP/2 Engine

本节全部实现只在 `RIWO_EXP_HTTP_HTTP2_SUPPORT=1` 时编译。宏为 0 时不生成 h2 Session、
nghttp2 adapter 或相关工厂分支。

### 14.1 依赖

使用 nghttp2 管理：

- frame 编码与解析；
- HPACK；
- stream 状态和协议校验；
- SETTINGS、PING、RST_STREAM、GOAWAY、WINDOW_UPDATE；
- DATA provider 和发送调度接口。

Riwo 负责将 nghttp2 callback 转换为第 12 节的公共 event/action、错误和输出模型；Session
driver 再把公共协议模型转换为 Exchange 与 Body。两级适配必须分离，HTTP/2 engine 不依赖
client/server 类型。

### 14.2 对象结构

```text
v2_connection_engine
|- nghttp2_session*
|- protocol events/actions
|- retained input and pending output
|- local and peer settings
`- unordered_map<stream_token_t, v2_stream_state>

v2_session_driver
|- byte_stream transport
|- v2_connection_engine
|- strand
|- read_loop
|- write_pump
|- outbound wakeup/queue
`- unordered_map<stream_id, shared_ptr<v2_stream>>

v2_stream
|- stream id and state
|- request/response head
|- bounded inbound body queue
|- bounded outbound body queue
|- inbound/outbound trailers
|- flow-control accounting
|- completion waiters
`- cancellation state
```

### 14.3 I/O 模型

- engine 不执行 I/O；`ingest()` 与 `produce()` 只在调用者提供的 buffer 上同步推进；
- 一个 Session driver 始终只有一个 transport read；
- 所有帧写入通过一个 write pump，不能让多个 stream 直接写 socket；
- read loop 和 write pump 可以重叠；
- Session 路径中，nghttp2 的创建、callback、submit、resume 和销毁全部在同一个 strand；手动
  engine 路径由用户保证同等串行化；
- 应用 handler 可在 service executor 并发运行，通过消息/队列与 Session strand 交互；
- write pump 应批量发送 nghttp2 产生的连续 frame，避免每帧一次系统调用；
- 新 outbound 数据、WINDOW_UPDATE、SETTINGS ACK、PING ACK 等只唤醒 write pump，
  不直接递归写 transport。

### 14.4 流量控制与背压

每个 stream 和整条 Session 都设置内存上限：

```cpp
struct v2_limits
{
	size_t inbound_stream_buffer;
	size_t outbound_stream_buffer;
	size_t inbound_connection_buffer;
	size_t outbound_connection_buffer;
	size_t max_header_list_size;
	size_t max_dynamic_table_size;
	size_t max_concurrent_streams;
};
```

接收 DATA 时：

1. 使用 nghttp2 的手动 WINDOW_UPDATE 模式；只有 `body_reader` 实际释放字节后，才在
   Session strand 上向 nghttp2 报告相应的 stream 和 connection 消费量；
2. 数据进入有界 inbound queue；已经向 peer 公告但尚未使用的 flow-control credit 必须
   有对应的内存预算或明确的 connection 级保留空间；
3. peer 可以发送其已经获得 credit 的数据。降低初始窗口或停止 WINDOW_UPDATE 不能收回
   已公告 credit，因此配置归一化必须保证 SETTINGS 生效前和在途 DATA 也能被有界接收；
4. queue 满后不再增加该 stream 的窗口；其他有窗口和预算的 stream 仍可继续推进；
5. connection 级内存达到上限时不再增加 connection window；stream queue、connection
   reserve、transport read buffer 和暂停解析时保留的输入必须统一计入 Session 上限；
6. nghttp2 input callback 提供的数据必须在 callback 返回前复制到受预算约束的存储，或按
   nghttp2 的 retained-input 规则保留原输入；不能保存无所有权的 callback 指针；
7. `NGHTTP2_ERR_PAUSE` 暂停的是当前 Session 的输入解析，不是单个 stream 的独立背压。
   它只用于 connection 级解析暂停，并且必须保留尚未消费的输入；普通单 stream 背压通过
   停止该 stream 的 WINDOW_UPDATE 实现；
8. 不能为了保持 read loop 简单而无限累积 body。

发送 DATA 时：

1. `body_writer::write()` 把数据写入同时受 stream 和 connection budget 约束的 outbound queue；
2. 任一级 budget 满时异步操作等待空间，形成背压；
3. nghttp2 data provider 从 queue 提供当前可发送部分；
4. peer window 为零时其他 stream 仍能推进；
5. 新数据到达后使用 nghttp2 resume data，并唤醒 write pump。

### 14.5 Stream 与连接错误

- 单 stream 应用取消发送 `RST_STREAM(CANCEL)`；
- stream protocol error 只终止对应 stream，除非规范要求 connection error；
- connection error 尽可能发送 GOAWAY，然后完成所有 stream waiter；
- 收到 GOAWAY 后 Session 进入 draining；
- stream ID 大于 peer GOAWAY `last_stream_id` 的本地请求标记为
  `definitely_not_processed`；
- 收到 `RST_STREAM(REFUSED_STREAM)` 且请求尚未交给应用处理时，同样标记为
  `definitely_not_processed`；没有 GOAWAY 的连接中断以及不高于 last stream ID 的未完成请求
  通常标记为 `possibly_processed`；
- 自动重试还必须同时满足方法可重试、body 可重放和用户策略允许。

## 15. 公共 Byte Stream、注入与协议协商

Transport 公开为协议无关的已连接有序字节流。公共扩展分为三个对象：byte stream 负责字节
I/O，connector 负责客户端建连与协议选择，acceptor 负责服务端接受连接。DNS、proxy、TCP、TLS
和 ALPN 不得塞入 byte-stream 接口。

当前 type-erased transport 边界为：

```cpp
template<core_concepts::exec Exec = asio::any_io_executor>
class basic_byte_stream
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using io_handler_t = asio::any_completion_handler<void(error_code, size_t)>;
	using close_handler_t = asio::any_completion_handler<void(error_code)>;

	executor_t get_executor() const noexcept;
	void read_some(mutable_buffer, io_handler_t);
	void write(const_buffer, io_handler_t);
	void close(close_handler_t);
	void cancel() noexcept;
	bool is_open() const noexcept;
	size_t read_some_sync(mutable_buffer, error_code &) noexcept;
	size_t write_sync(const_buffer, error_code &) noexcept;
	transport_capabilities capabilities() const noexcept;
	transport_metadata metadata() const;
};

using byte_stream = basic_byte_stream<>;

namespace concepts
{

template<class Stream>
concept byte_stream_model = /* get_executor/read_some/write/close/cancel/is_open */;

} //namespace concepts

template<concepts::byte_stream_model Stream>
auto make_byte_stream(Stream) -> basic_byte_stream<
	typename Stream::executor_type>;
```

真实声明位于 `<riwo/experimental/http/transport/byte_stream.h>`。`basic_byte_stream` 是 move-only type erasure；`write()` 是覆盖整个输入
的 composed operation，底层 `write_some()` 只
保留为具体 transport 的实现细节。`transport_metadata` 使用可选、协议中立的 local/remote
endpoint 文本、TLS/ALPN 和用户属性，不把 `asio::ip::tcp::endpoint` 固定进公共抽象；用户
属性的 key 使用 `std::string`，value 使用 `riwo::value` 以承载文本化标量。

byte-stream 契约为：

- `read_some()` 成功且返回非零字节，正常 EOF 使用统一 EOF 错误；空 buffer 可以立即成功返回 0；
- `write()` 成功时覆盖整个输入；部分底层写由 adapter 内部继续，失败时返回实际错误；
- 每个方向至多一个未完成操作；读写可以重叠；handler exactly once，并遵循关联 executor；
- 调用方在 handler 完成前保持 buffer 有效，stream 在所有 handler 完成前保持底层资源有效；
- `cancel()` 请求终止未完成操作但不等于协议 graceful shutdown；`close()` 完成后不再启动新 I/O；
- `capabilities().synchronous=false` 时，sync read/write 默认返回稳定的
  `operation_not_supported`；这种自定义 stream 仍可用于异步/协程 Session。同步 client 路径还
  要求 connector 支持 `connect_sync()`，不能在 executor 线程上偷偷阻塞模拟同步；
- 自定义 stream 必须保持字节顺序、不重复数据，并明确传播截断、EOF 和取消；datagram/message
  transport 必须先由用户适配成满足这些条件的可靠字节流。

连接建立结果包含协商信息和协商阶段已经读取但尚未交给 engine 的字节：

```cpp
template<core_concepts::exec Exec = asio::any_io_executor>
struct basic_negotiated_transport
{
	basic_byte_stream<Exec> stream;
	protocol selected_protocol;
	transport_metadata metadata;
	std::vector<std::byte> initial_input;
};

using negotiated_transport = basic_negotiated_transport<>;

template<core_concepts::exec Exec = asio::any_io_executor>
class basic_transport_connector
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using result_t = basic_negotiated_transport<executor_t>;
	virtual ~basic_transport_connector() = default;
	virtual executor_t get_executor() const noexcept = 0;
	virtual sys_expected<result_t>
	connect_sync(const connect_request &) noexcept;
	virtual void connect(connect_request,
		asio::any_completion_handler<void(error_code, result_t)>) = 0;
	virtual void cancel() noexcept = 0;
};

using transport_connector = basic_transport_connector<>;
```

connector 的真实声明位于 `<riwo/experimental/http/transport/connector.h>`。
`basic_client<Exec>` 增加接受共享 `basic_transport_connector<Exec>` 的构造入口；
`basic_server<Exec,...>` 始终由 `basic_acceptor_wrap<Exec>` 构造，也可以通过显式
`serve(basic_negotiated_transport<Exec>)` 接管一条预连接流。acceptor wrapper 直接拥有底层
TCP acceptor，并以 type-erased acceptor model 承载 TCP、TLS 或自定义接入逻辑，不再叠加独立
监听抽象。TLS acceptor 必须返回确定的 `selected_protocol`；Session driver 不根据任意应用
数据猜测 TLS/ALPN 结果。

`connect_request` 是拥有型值，包含目标 origin、proxy/TLS/protocol policy、connect deadline 和
可选 local-bind metadata；它不包含 HTTP request head、body 或可变 client state。自定义 connector
可以忽略不适用的网络字段，但必须明确拒绝无法满足的强制策略，例如
`protocol_requirement::v2` 或 peer
verification，不能静默降级。

`initial_input` 的所有字节必须按原顺序首先送给 engine，不能因 preface sniffing、proxy handshake
或用户 acceptor 的预读而丢失。它同样受 header/connection input 上限约束。

### 15.1 TLS ALPN

客户端请求 `https` URI 时，在 TLS handshake 前根据 policy 设置 ALPN；handshake 后读取
selected protocol。
服务端 TLS context 根据 server policy 选择 `h2` 或 `http/1.1`。没有匹配协议时按 policy
拒绝或使用允许的默认协议。

ALPN 选择属于 TLS 握手结果，不应通过解析应用数据猜测。

### 15.2 明文 prior knowledge

HTTP/2 对 `http` URI 使用 RFC 9113 第 3.3 节的 prior knowledge，不使用 Upgrade：

- 客户端只有在 `protocol_requirement::v2`、显式 endpoint 配置或可信的带外能力信息已经确定 peer 支持
  HTTP/2 时，才建立 TCP 连接并立即发送 client connection preface 和后续 HTTP/2 frames；
- `automatic` 没有 prior knowledge 时选择 HTTP/1.1，不能把一次 HTTP/1.1 请求升级为 HTTP/2；
- 支持两种协议的明文服务端读取有界前缀并与 client connection preface 匹配；完整匹配时创建
  HTTP/2 Session，否则把所有已读字节无损交给 HTTP/1 parser。前缀识别受连接/首字节超时和
  固定上限约束；
- prior-knowledge 连接未收到合法 server connection preface 时按 HTTP/2 connection/protocol
  error 失败，不能在同一连接上把响应重新解释为 HTTP/1.x；
- 不发送或接受已弃用的 `h2c` Upgrade token 和 `HTTP2-Settings` 字段。

TLS 与明文路径在选定协议后创建同一种 HTTP/2 Session；差异只留在 transport 和建连选择层。

### 15.3 Proxy

每一跳协议需要独立建模：

- forward proxy 的请求协议是 client 到 proxy 的协议；
- CONNECT 成功后，再对 tunnel 内的目标 origin 进行 TLS/ALPN；
- proxy 自身是否支持 HTTP/2 不等于目标 origin 是否支持 HTTP/2；
- HTTP forward proxy 使用 HTTP/1.1 absolute-form 或 CONNECT；SOCKS5 支持无认证和
  RFC 1929 用户名/密码认证；代理 endpoint、类型和认证 identity 都参与 Session key；
- `proxy_policy::use_environment` 显式启用 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`，并与
  配置及环境中的 `no_proxy`/`NO_PROXY` 合并。绕过规则在创建 Session key 前求值；
- 单请求 `request_options::proxy` 完整覆盖 client 级代理策略。`Proxy-Authorization` 只发送给
  HTTP proxy；HTTPS tunnel 建立后不得把该字段转发给 origin。

### 15.4 自定义 I/O 的两种组合方式

应用按所需抽象层选择一种方式，二者不能隐式混用：

1. **手动 engine loop**：应用直接构造 `v1::connection_engine` 或
   `v2::connection_engine`，把任意来源的输入传给 `ingest()`，处理 event/action，并把
   `produce()` 结果写到自己的 I/O。这条路径不要求 Asio、executor、connector 或 Session；
2. **托管 Session**：应用实现 `concepts::byte_stream_model` 并注入 connector/acceptor，由 Riwo 负责
   read loop、write pump、deadline、Exchange、Body 和 Pool。

手动 loop 的最小推进顺序为“读取输入 -> ingest 至暂停或输入耗尽 -> 处理 event/submit action ->
produce 至暂时无输出 -> 等待下一 I/O/应用事件”。只要 `wants_write()` 为 true，调用方就必须
继续排空输出；只读不写会阻塞 SETTINGS ACK、WINDOW_UPDATE、PING ACK 或错误关闭。

公共示例必须至少包含一个完全不创建 socket 的内存 transport，以及一个由应用自行调度的
HTTP/1 codec 示例。示例不能 include `detail/`，从而证明独立协议 API 和 transport 扩展确实是
安装契约而非源码树偶然可见的内部接口。

## 16. Session Pool

连接池重构为 Session 容量池。应用获取的是 stream/exchange，不是独占 connection
lease。

```cpp
class session_pool
{
public:
	template <typename Token>
	auto open(
		origin,
		request_head,
		body_spec,
		request_options,
		Token &&token
	);
};
```

调度流程：

1. 根据 session key 查找 origin bucket；
2. 在 Session strand 上原子执行“检查 ready/capacity 并预留 stream”；
3. HTTP/1 Session capacity 为 1，活动请求期间为 0；
4. HTTP/2 capacity 受 peer `SETTINGS_MAX_CONCURRENT_STREAMS`、本地上限和 draining 状态
   共同约束；
5. 没有容量时，在连接上限内新建 Session；
6. 达到上限时公平排队等待，支持取消和超时；
7. draining/closed Session 不接收新 stream；
8. idle Session 按配置发送健康检查或关闭。

Session key 至少包含：

```cpp
struct session_key
{
	scheme scheme;
	std::string host;
	uint16_t port;
	proxy_identity proxy;
	tls_identity tls;
	protocol_policy_identity protocols;
	optional<endpoint> local_bind;
};
```

当前实现禁止不同 origin 共用同一 HTTP/2 Session，即使证书覆盖多个名称。若增加
coalescing，必须同时验证证书、SNI、DNS/地址、代理路径和服务端 authority 权限。

## 17. 并发与线程安全

线程安全沿用现有 Riwo HTTP 和 Asio 的 shared-object 规则：

- distinct objects：可以由不同线程并发使用；
- shared objects：默认不安全。对同一个 `client`、`exchange`、`body_reader`、
  `body_writer` 或其他公共 I/O 对象的发起调用，应用必须通过同一个 strand 或其他外部
  串行化手段协调；
- 关联 executor 决定 completion handler 的执行位置，不代表发起函数本身可以从任意线程
  同时调用；需要跨线程共享对象时，应用应把发起操作 `post`/`dispatch` 到该对象约定的
  executor/strand；
- `cancel()` 不是 shared-object 线程安全的特例，也必须遵守同一发起调用规则；最终取消和
  协议状态转换在 Session strand 上执行；
- 同一 exchange 最多一个未完成 body read 和一个未完成 body write，两者可以重叠；不得
  重叠同方向操作；
- response head 完成后只读 metadata 保持稳定，但持有该 metadata 不改变所属 I/O 对象的
  线程安全等级；
- handler 的 associated executor、allocator、cancellation slot 和 immediate executor
  必须传播；异步发起函数的即时失败也通过正常 completion 路径交付。

Session 是内部协议对象，其协议状态、stream map、capacity 预留和写调度全部在 Session
strand 上串行化。Pool 对 Session capacity 的检查和 stream 预留必须是同一串行化操作；
`available_stream_capacity()` 只能作为观察值，不能形成“先检查、后 open”的竞态契约。

不同 HTTP/2 stream 的应用 handler 可以运行在不同 service executor 上；应用不需要把
整条 HTTP/2 Session 的业务处理放入一个 strand。Session 内部只保证协议状态安全，应用
仍需自行串行化多个 handler 对同一业务对象的访问。

borrowed `std::fstream` 或 `FILE*` 也属于 shared object：适配器活动期间，调用方不得从任何线程
对同一对象执行读、写、seek、flush、close 或移动。Experimental HTTP 只串行化自己对该适配器的调用，
不替调用方同步文件对象的外部访问。

## 18. 取消、超时与关闭

### 18.1 取消

公共语义始终是 `exchange.cancel()`：

| 协议 | 实现 |
| --- | --- |
| HTTP/1.x | 能恢复边界则停止当前操作，否则关闭连接 |
| HTTP/2 | 发送 RST_STREAM(CANCEL)，其他 stream 不受影响 |

取消只是启动完成过程。在 read/write completion 最终交付前，借用 buffer 仍必须有效。
completion token 的 terminal、total 和 partial cancellation 都映射为所属 exchange
的取消请求，而不是只移除一个 waiter；只有在能证明消息边界和调用者预期都安全时，才可
增加更细粒度的 operation-only cancellation。取消与正常完成竞争时只交付一次 completion；
`read_some()`/`write()` 即使以错误完成也保留已经完成的字节数；`read_some()` 的字节数位于
`body_read_result::bytes`。

同步文件后端的取消只在 chunk 边界观察：已经进入的 `fread`/`fwrite`、iostream 操作、seek 或
flush 不保证被抢占。该调用返回后不得再启动新的文件 I/O，并按正常竞争规则恰好完成一次；已经
读写的字节数仍必须报告。未来 `io_uring`/IOCP 后端可以尝试取消内核请求，但其公共结果必须与
此契约一致，不能因后端不同而改变对象寿命或重复 completion。

### 18.2 超时

配置至少区分：

- DNS/连接超时；
- proxy handshake 超时；
- TLS/ALPN 超时；
- response header 超时；
- body idle timeout；
- request overall timeout；
- pool capacity wait timeout；
- server 首读与 keepalive/idle timeout；
- graceful shutdown timeout。

stream timeout 不应直接关闭 HTTP/2 Session，除非产生连接级错误。

### 18.3 Graceful Shutdown

服务端停止顺序：

1. 停止 accept 新连接；
2. 所有 Session 进入 draining；
3. HTTP/1.x 完成当前响应并设置不再复用；
4. HTTP/2 发送 GOAWAY，记录 last processed stream ID；
5. 等待活动 exchange 完成或 shutdown timeout；
6. 取消剩余 stream 并关闭 transport；
7. 排空 completion 后销毁 executor 依赖对象。

## 19. 错误模型与重试

```cpp
enum class errc
{
	invalid_method,
	invalid_status,
	invalid_target,
	invalid_field_name,
	invalid_field_value,
	invalid_trailer,
	header_limit_exceeded,
	body_length_mismatch,
	body_not_allowed,
	request_body_closed,
	operation_in_progress,
	operation_not_permitted,
	buffer_limit_exceeded,
	negotiation_failed,
	connection_protocol_error,
	stream_protocol_error,
	operation_not_supported
};

enum class error_scope
{
	transport,
	negotiation,
	connection_protocol,
	stream_protocol,
	timeout,
	cancelled,
	application
};

enum class request_delivery
{
	not_started,
	definitely_not_processed,
	possibly_processed
};

struct error_info
{
	error_code code {};
	error_scope scope = error_scope::application;
	optional<protocol> selected_protocol;
	optional<int32_t> stream_id;
	optional<uint32_t> wire_error_code;
	request_delivery delivery = request_delivery::not_started;
	bool retryable = false;
};
```

`experimental::http::errc` 具有独立稳定的 error category 和 `make_error_code()`；系统、Asio、TLS 和
nghttp2 原始错误保存在诊断链中，不直接成为公共分支条件。正常 body 结束使用
`riwo::errc::eof`，不占用 Experimental HTTP 错误枚举。

### 19.1 公共操作错误归一化

以下映射由 fake Session、HTTP/1 和 HTTP/2 engine 共用：

| 场景 | completion error | 对已有操作的影响 |
| --- | --- | --- |
| 同一 reader 重叠 read，或同一 writer 重叠 write/finish | `experimental::http::errc::operation_in_progress` | 新操作失败，原操作继续 |
| 在 head 未就绪、finish 后或其他非法状态发起操作 | `experimental::http::errc::operation_not_permitted` | 不重新打开或改变终止状态 |
| 显式 `cancel()`、关联 cancellation slot 或所属 handle 析构请求取消 | `riwo::errc::operation_aborted` | 所有受影响的未完成操作最终各完成一次 |
| DNS/连接、header、body idle、overall 或 pool wait deadline 到期 | `riwo::errc::timed_out` | 按第 18 节取消最小安全范围 |
| 正常 body 读取结束 | `riwo::errc::eof` | 只用于 `read_some()` 正常控制流 |
| source 提前/超量结束，或 writer 与声明长度不符 | `experimental::http::errc::body_length_mismatch` | 终止当前 stream；HTTP/1 边界不安全时关闭连接 |
| trailer 未声明或字段非法 | `experimental::http::errc::invalid_trailer` | 终止当前 stream |

`error_scope::cancelled` 和 `error_scope::timeout` 仍用于 `error_info` 分类，但不另造与 Riwo/Asio
重复的 `experimental::http::errc::cancelled` 或 `experimental::http::errc::timed_out`。如果取消、timeout 与协议错误
竞争，Session strand 上第一个进入终止状态的原因成为公开 completion；后到原因只进入诊断链。
任何 waiter 都只能完成一次。

completion token 仍以 `error_code` 作为稳定错误通道；需要诊断时可从 exchange、日志 hook
或扩展结果获取 `error_info`。DNS、TCP、TLS 和 ALPN 失败时可能尚未选定协议，因此
`selected_protocol` 必须可空；policy 中尝试过的协议作为单独诊断字段或 observer 数据记录，
不能伪装成已经协商成功的协议。

delivery 的映射必须先于 retry policy，不能根据“是否想重试”反推：

| 观测状态 | `request_delivery` |
| --- | --- |
| 尚未预留 stream，或确认 request head 未提交给 transport | `not_started` |
| HTTP/2 stream ID 高于 GOAWAY last ID | `definitely_not_processed` |
| HTTP/2 `REFUSED_STREAM`，且 peer 保证未交给应用 | `definitely_not_processed` |
| HTTP/2 无 GOAWAY 的连接中断，或 stream ID 不高于 last ID | `possibly_processed` |
| HTTP/1 已发送任何不能证明未到达 peer 的请求字节 | `possibly_processed` |
| 已收到最终 response head 后发生 body/transport 错误 | `possibly_processed` |

`retryable` 是 delivery、method policy、source replayability、尝试次数和 deadline 的派生结果，
不能由协议层单独设置为 true。正常 EOF 不产生 `error_info`，也不参与 retry 判定。

自动重试必须同时满足：

1. `delivery` 为 `not_started` 或 `definitely_not_processed`，不能仅凭“可能未处理”重试；
2. 请求方法按策略可重试；
3. body source 可重放；
4. 未超过最大尝试次数和总 deadline；
5. 用户未禁用重试。

默认只自动重试安全/幂等请求。POST 等请求即使 body 可重放，也必须显式允许。

重定向和认证重试使用同一 replayability 判断，但分别受独立策略限制。

## 20. Compression、Cookie、认证与文件

这些能力属于语义层或 body filter：

- gzip/deflate 解压包装 reply `body_reader`；公开 `gzip_encoder`/`gzip_decoder` 同时提供增量与
  一次性 API；
- 服务端 gzip 压缩包装 `body_source`，压缩后长度按 streaming 表达，并统一处理
  `Accept-Encoding`、MIME、`Vary`、variant ETag 与 Range 排斥规则；
- Cookie jar 在发送前根据 URL 生成 Cookie，在响应头到达时接收 Set-Cookie；
- Basic/Bearer/Proxy auth 修改 fields，不参与协议 framing；
- 文件上传是 `file_body_source`；
- 文件下载是 `body_reader -> file_sink`；
- Range/Content-Range、multipart form/byte-ranges、HTTP date、ETag/precondition 和
  Basic/Bearer/Proxy auth 均作为协议无关语义工具提供；统一的 `respond_file()` 在 Body 之上
  组合这些能力，相对路径从资源根安全解析，绝对路径直接读取。

### 20.1 文件 source/sink、C/C++ 文件对象与字节索引

本节描述当前同步文件扩展及后续异步 backend 契约。文件能力只负责把字节序列适配到 Body 层，
不进入 H1/H2 engine，也不因协议版本产生两套接口。当前用法如下：

```cpp
// 上传/发送
request.set_source(file_body_source(path));
request.set_source(file_body_source(std::move(input_stream)));
request.set_source(file_body_source(borrowed_file(input_stream)));
request.set_source(file_body_source(owned_file(c_input)));
request.set_source(file_body_source(borrowed_file(c_input)));
request.set_source(file_body_source(path, file_span{offset, length}));

// 下载/接收
co_await pipe(reply.body(), file_sink(std::move(output_stream)), use_awaitable);
co_await pipe(reply.body(), file_sink(borrowed_file(c_output)), use_awaitable);
co_await reply.save_file(owned_file(c_output), use_awaitable);

// 服务端接收请求 body / 发送文件响应
co_await exchange.save_file("upload.bin", use_awaitable);
co_await exchange.save_file(
	borrowed_file(output_stream), use_awaitable);
co_await exchange.save_file(
	owned_file(c_output), use_awaitable);
co_await exchange.respond(
	response_head{status::ok},
	file_body_source("download.bin", file_span{offset, length}),
	use_awaitable);
co_await exchange.respond(
	response_head{status::ok},
	file_body_source(borrowed_file(c_input)),
	use_awaitable);
```

这里的 wrapper 必须区分资源所有权，不能从引用或裸指针猜测：

| 输入形式 | 适配器所有权 | 结束行为 |
| --- | --- | --- |
| `std::filesystem::path` | 库打开并拥有 | 库关闭 |
| 右值 `std::ifstream`/`std::ofstream`/`std::fstream` | 移动拥有 | 由拥有型流析构关闭 |
| `borrowed_file(stream&)` | 借用 | 不关闭，尽力恢复位置和 `rdstate()` |
| `owned_file(FILE*)` | 独占接管 | 恰好一次 `std::fclose()` |
| `borrowed_file(FILE*)` | 借用 | 不调用 `std::fclose()`，清除本次操作产生的 indicator 并恢复位置 |

source 只接受可读的 `std::ifstream`/`std::fstream`，sink 只接受可写的
`std::ofstream`/`std::fstream`。标准 API 不能可移植地查询既有文件对象的 open mode；调用方必须
保证 `FILE*` 的读写方向和 binary mode 正确，实现只校验 API 实际返回的失败。空指针、已失败的
流和不支持所需 seek 的对象通过稳定错误完成；方向或模式违反前置条件时不保证可恢复。owned
wrapper 一经构造即接管资源；之后即使适配器构造、发送、取消或 flush 失败，也必须保持
exactly-once close。borrowed wrapper 不延长宿主对象寿命，不得用于 detached 操作；borrowed
`FILE*` 进入适配器时还必须满足 `std::feof(file) == 0 && std::ferror(file) == 0`，因为 C 标准 API
只能 `clearerr()`，不能可移植地重建调用前已经置位的 indicator。

owned/borrowed wrapper 及其 file source/sink 都是 move-only；moved-from 对象为空且可析构。这样
owned `FILE*` 不会产生两个关闭者，也不会因复制 borrowed adapter 而并发操作同一外部 cursor。

#### 20.1.1 字节索引和大文件

HTTP Range 使用 representation 的字节索引。文件适配层因此只支持 `char` 文件流和 binary mode；
不得用字符数、code point、文本行或转换后的逻辑位置解释 offset。内部范围统一为半开区间：

```cpp
struct file_span
{
	uint64_t offset;
	uint64_t length;
};
```

wire 上的 `bytes=first-last` 在校验后转换为 `{first, last - first + 1}`；开放末端和 suffix range
必须先结合已冻结的 representation size 求值。必须检查 `last + 1`、`offset + length`、平台
offset 类型转换和 `size_t` chunk 转换的全部溢出。无符号 HTTP 偏移不能未经检查地转换为有符号
文件偏移。

`FILE*` 的 64 位定位在 POSIX 使用 `fseeko`/`ftello`，Windows 使用 `_fseeki64`/`_ftelli64`；
不得以可能只有 32 位的 `fseek`/`ftell` 实现公共 `uint64_t` 契约。C++ stream 使用
`seekg`/`tellg` 或 `seekp`/`tellp`，每次转换及 `failbit`/`badbit` 都必须检查。平台无法表示某个
合法 HTTP offset 时返回稳定的 range/overflow 错误，不能截断或回绕。

完整文件 source 的 body 是捕获起点开始的 `[0, size)` 逻辑表示；显式 `file_span` 则只发送该
窗口。对调用方传入的 owned 或 borrowed 文件对象，“0”指适配器捕获的起始位置，因而实际位置为
`captured_position + logical_offset`。size、Content-Length、读取上限和 replay 都针对同一窗口，
不能在发送 head 后重新 stat 或重新解释当前 cursor。

#### 20.1.2 顺序 sink、随机访问 sink 与文件替换

顺序 sink 只保证按接收顺序写入并在 finish 时 flush；它足以保存普通 200 response。Range 合并、
断点续传和并发分段下载需要 random-access sink，额外提供 `write_at(uint64_t, buffer, handler)`，
且必须按 representation offset 写入，不得把每个 206 body 都追加到当前 cursor。multipart/byteranges
仍由语义层解析各 part 的 `Content-Range` 后再调用 `write_at`。

当前 `file_sink(path)` 使用 create/truncate；update 与 atomic-replace 路径策略留作后续显式
policy 扩展。对已经打开的 iostream 或 `FILE*`，标准 API 无法可移植地查询其原始 open flags，
也不能可靠地按路径重开；因此适配器不承诺自动 truncate。保存完整 200 response 时，调用方必须
传入已经截断的对象，或接受新内容较短时保留旧尾部。默认路径入口不得静默保留旧尾部。
append mode、不可 seek 对象和 text mode 不能作为 random-access sink；由于既有对象的 mode
不可移植查询，这一限制属于调用方前置条件。

finish 对 iostream 执行 `flush()`，对 `FILE*` 执行 `std::fflush()`，并把失败报告给最终
completion；borrowed sink 即使 flush 失败也不关闭对象。写入发生 partial write 时，composed
operation 可继续剩余部分，但零进展或错误必须停止，并报告已经完成的字节数。文件 source 同样
处理 short read：只要有字节就先成功交付，到窗口末端才报告逻辑 EOF；在窗口长度之前到达物理
EOF 是 `body_length_mismatch`。

#### 20.1.3 metadata、同步实现与异步演进

路径可提供扩展名、mtime 和文件身份等 metadata；调用方传入的流或 `FILE*` 不携带可移植路径，
实现不得从 native descriptor 猜测。后续需要自动 Content-Type、ETag、Last-Modified 或条件请求
时，通过显式 `file_metadata` 扩展提供；当前缺失这些 metadata 时只发送可由 body 确定的长度等
信息。

当前实现保持同步文件 I/O：每个有界 chunk 的 read/write/seek/flush 在 exchange executor 上执行，
对外仍通过 source/sink completion 交付。此选择会占用 executor 线程，不能保证低延迟取消，也
不适合在单线程 executor 上执行长时间阻塞的网络文件系统操作；配置应允许限制 chunk 大小，
Session 必须在 chunk 之间重新进入正常调度而不是无界同步循环，文档和指标应能识别同步文件后端。

未来路径拥有型 adapter 可以在 Linux 使用 `io_uring`，在 Windows 使用 IOCP/OVERLAPPED。
后端选择是实现细节，不改变 source/sink、`uint64_t` offset、顺序、长度/EOF、取消竞争或
exactly-once completion 契约。`std::fstream` 和 `FILE*` 带有独立缓冲和 cursor，默认不参与原生
异步后端；除非平台 adapter 能独占接管并证明缓冲一致性，否则继续同步执行。任何实现都不得在
同一对象上混用 stdio/iostream 缓冲操作与绕过它们的 native asynchronous I/O。

### 20.2 Cookie 值、属性与字段 API

Cookie 的 name/value 和 `Set-Cookie` 属性属于 HTTP 语义，不进入 H1/H2 engine。Cookie value
是应用定义的不透明文本，公共 API 使用 `riwo::value` 保留 Riwo 现有的构造和类型转换便利：

```cpp
struct cookie_pair
{
	std::string name;
	riwo::value value;
};

enum class cookie_same_site
{
	unspecified,
	strict,
	lax,
	none
};

struct cookie_extension
{
	std::string name;
	optional<riwo::value> value;
};

struct response_cookie
{
	std::string name;
	riwo::value value;
	optional<std::string> domain;
	optional<std::string> path;
	optional<std::chrono::system_clock::time_point> expires;
	optional<std::chrono::seconds> max_age;
	cookie_same_site same_site = cookie_same_site::unspecified;
	bool secure = false;
	bool http_only = false;
	std::vector<cookie_extension> extensions;
};
```

`riwo::value` 在这里是拥有字符串的值包装，不表示 Cookie 具有动态类型。wire parser 必须把
解析得到的文本直接存入 value，不能先按 bool、整数或浮点数解释后再格式化；generator 只取
`value.to_string()` 并执行 Cookie 字符集校验。NUL、控制字符、CR/LF、分号等不合法输入必须
返回错误，不能静默删除或转义。任意二进制或超出 Cookie 字符集的数据由应用显式编码，例如
Base64；Experimental HTTP 不猜测编码。

Domain、Path、Expires、Max-Age、Secure、HttpOnly 和 SameSite 具有协议语义，必须使用上面的
强类型字段，不能全部退化为 `value_map`。未知扩展属性可以保留在 `extensions` 中，但不得覆盖
已识别属性的语义。

请求 Cookie 使用有序序列而不是 map。同名 Cookie 可能来自不同 Domain/Path，解析过程不能
丢弃或覆盖它们，服务端也不能依赖同名项的顺序。语义层至少提供以下独立工具：

```cpp
sys_expected<std::vector<cookie_pair>> parse_cookie_fields(const fields &);
sys_expected<response_cookie> parse_set_cookie(std::string_view);
std::string serialize_cookie(std::span<const cookie_pair>);
std::string serialize_set_cookie(const response_cookie &);
void set_cookie(fields &, const response_cookie &);
```

server generator 按严格语法产生 `Set-Cookie`，client generator 按严格语法产生 Cookie；
client jar 接收 `Set-Cookie` 时使用兼容性更好的宽松解析算法，但仍拒绝会造成注入或作用域
越界的值。每个 `Set-Cookie` field 单独解析和生成，绝不能用逗号折叠；一个非法
`Set-Cookie` 只被 jar 忽略，不使整个 HTTP response 失败。服务端必须容忍多个 Cookie
field，并按 Cookie 语义把它们视为一个有序序列。

### 20.3 Cookie jar 与 client 集成

`cookie_jar` 是协议无关、可注入且线程安全的状态对象。它按 `(name, canonical-domain, path)`
标识条目，并另外保存 host-only、persistent、Secure、HttpOnly、SameSite、创建顺序、最后访问
时间和过期时间。实现负责：

- canonical host、domain-match、default-path 和 path-match；
- Max-Age 优先于 Expires、过期删除和 session cookie；
- Secure 限制、secure cookie 防降级覆盖和 Cookie name prefix 约束；
- 每 Cookie、每 domain 和全局容量限制及确定的淘汰顺序；
- 可配置的 public-suffix 策略；IDNA 仍使用第 7.2 节规定的已验证 ASCII host，不在 jar 内转换；
- 按 path 长度和创建顺序产生请求 Cookie，但不向调用方承诺服务端可依赖该顺序。

SameSite 不能只根据目标 URL 判断。浏览器式调用必须额外提供 site-for-cookies、请求是否为
top-level navigation、请求方法等 `cookie_request_context`；普通非浏览器 HTTP client 可以
显式使用 non-browser context。没有上下文时不得假装已经完整执行浏览器 SameSite 策略。

```cpp
class cookie_jar
{
public:
	bool store(const riwo::url &origin, const response_cookie &);
	std::vector<cookie_pair> select(const riwo::url &target,
		const cookie_request_context &context = {});
	std::vector<cookie_entry> entries() const;
	void clear();
};

struct cookie_config
{
	bool enabled = true;
	std::shared_ptr<cookie_jar> jar {};
};
```

`client_config` 包含 `cookie_config cookies`。启用时，空 `jar` 表示为该 client 创建私有 jar；
传入 shared jar 允许多个 client 共享登录状态。`client.cookie_store()` 返回实际使用的 jar，
应用也可以通过配置完全禁用自动 Cookie。

自动处理顺序固定如下：

1. 每次物理发送前，以目标 URL 和 request context 从 jar 取得一致快照并生成 Cookie；
2. 用户显式设置的原始 Cookie field 是该请求的权威值，jar 不再自动合并或改写；
3. 收到每个最终 response head 后立即逐条解析并存储 `Set-Cookie`，不等待 body 读完；
4. 3xx response 的 Cookie 必须先存入 jar，再计算下一跳请求；
5. jar 自动生成的 Cookie 不进入重定向 clone，每一跳都按新 URL 重新计算，包括同 origin 但
   path 不同的重定向；显式 Cookie 只允许在 same-origin 重定向中保留；
6. retry 每次重新取得 jar 快照，显式 Cookie 保持不变；
7. 并发 response 对同一 Cookie 的覆盖以 jar 实际处理 response head 的线性化顺序为准。

jar 只修改 fields，不参与 framing、Session 选择或连接复用。H1 与 H2 必须共用同一套 Cookie
语义和测试向量；协议 engine 只负责原样传递已经生成或收到的 field。

自动解压后的字节数、wire 字节数和文件字节数必须在 API 文档中分别定义，不能混用。

## 21. 服务端主动消息与双向通道

### 21.1 服务端主动业务消息

HTTP/2 server 不能脱离 client 发起的 exchange 任意创建通用业务消息流。`PUSH_PROMISE`
表达的是与已有请求关联的、safe 且 cacheable 的预测请求/响应，client 可以通过 SETTINGS
禁用或拒绝；它不得被包装成 `server.send(client, message)`，也不作为通知、聊天、状态更新等
业务消息的基础。

服务端单向主动消息使用 client 先建立的长生命周期响应 stream：

```cpp
server.on_request<method::get>(
	"/events",
	[&hub](server_exchange &exchange) -> awaitable<void>
	{
		auto subscription = hub.subscribe(authenticate(exchange.request()));

		response_head head{status::ok};
		head.headers.set("content-type", "application/x-ndjson");
		co_await exchange.respond(
			std::move(head), body_spec::streaming(), use_awaitable);

		auto &writer = exchange.response_body();
		while (auto message = co_await subscription.next(use_awaitable))
			co_await write_message(writer, *message, use_awaitable);

		co_await writer.finish({}, use_awaitable);
	}
);

// 任意业务执行上下文只向应用队列发布，不直接并发调用 body_writer。
hub.publish(user_id, event);
```

`hub`、topic、用户寻址、可靠投递和离线消息属于应用层，不进入 Experimental HTTP。
Experimental HTTP 负责的
边界是长响应 stream、背压、取消和协议流量控制。应用订阅必须满足：

- 每个 subscription 使用有界消息数和有界总字节队列；满队列时显式选择等待、丢弃、拒绝
  或断开慢消费者，不得无界增长；
- 任意线程可以向应用队列 publish，但每个 exchange 只有 handler 内的一个发送泵调用
  `body_writer::write()`，保持单 writer、消息顺序和关联 executor；
- HTTP DATA/chunk 边界不是应用消息边界。应用必须选择 SSE、NDJSON、长度前缀或其他明确
  framing；可靠投递需要应用级 sequence、ACK 和重放；
- HTTP/2 的 stream window 和 connection window 与应用队列上限同时生效。一个慢 stream
  只能阻塞自己的发送泵，不得阻塞其他 stream；
- RST_STREAM、GOAWAY、timeout、取消和正常 finish 都必须关闭 subscription，并让等待中的
  producer/consumer exactly-once 完成；
- handler 必须保持存活并拥有发送泵，直到 `finish()`、取消或错误。不得把裸
  `body_writer*`/引用保存到应用 hub；将来若增加可脱离 handler 的 send handle，它只能持有
  `weak_ptr` stream state 和有界队列，不能延长整个 connection 的生命。

纯 server-to-client 通知首选上述长响应；client-to-server 也需要异步消息时使用下一节的
`duplex_channel`。HTTP/2 Server Push 不进入通用 `server_exchange` 或
`stream_capabilities`。

### 21.2 Upgrade、CONNECT 与 Experimental WebSocket 交接

HTTP/1 的 connection handover 不能作为统一公共 API，因为 HTTP/2 只能移交单个 stream，
不能移交承载其他请求的整条连接。

统一抽象为 exchange 级双向通道：

```cpp
template <core_concepts::exec Exec = asio::any_io_executor>
class basic_duplex_channel
{
public:
	basic_duplex_channel();

	template <typename Exec0>
	explicit basic_duplex_channel(Exec0 &&executor);

	explicit basic_duplex_channel(std::shared_ptr<detail::duplex_state> state);

	template <typename Exec0>
	basic_duplex_channel(Exec0 &&executor,
		std::shared_ptr<detail::duplex_state> state);

	template <typename Token = use_sync_t>
	auto read_some(mutable_buffer, Token &&token = {});

	template <typename Token = use_sync_t>
	auto write(const_buffer, Token &&token = {});

	template <typename Token = use_sync_t>
	auto close(Token &&token = {});

private:
	executor_t m_exec;
	std::shared_ptr<detail::duplex_state> m_state;
};

using duplex_channel = basic_duplex_channel<>;
```

- HTTP/1 Upgrade/CONNECT 成功后，channel 独占 transport；
- HTTP/2 Extended CONNECT 成功后，channel 只占用一个 stream；
- Experimental HTTP 的职责到 `duplex_channel` 为止，不定义 WebSocket frame 或 stream API；
- 独立的 `riwo.exp_websocket` 在该 channel 上实现 WebSocket；
- HTTP/2 WebSocket 由 Experimental WebSocket 通过 SETTINGS_ENABLE_CONNECT_PROTOCOL 和 Extended
  CONNECT 建立；
- 应用不能从普通 HTTP/2 exchange 获取原始 connection。

## 22. 可观测性

Experimental HTTP 提供结构化 hook，不要求应用读取内部对象：

| 事件 | 最少包含的信息 |
| --- | --- |
| `session_open` / `protocol_selected` | session ID、origin、proxy 路径、选定协议 |
| `stream_open` / `response_head` / `stream_close` | session ID、stream ID、方法、状态、耗时 |
| `goaway` | last stream ID、wire error code、draining 状态 |
| `retry` | delivery、方法策略、尝试次数、重试原因 |
| `error` | 稳定 `error_info` 和可选原始诊断链 |

observer 的具体存储可以是虚接口、回调集合或模板 adapter，但回调只接收不可变 event value，
不能取得 Session/stream 的可变内部对象。observer 回调异常不得穿过协议 read/write loop；实现
必须捕获并转为诊断或按配置禁用该 observer。

建议记录：

- origin、proxy 路径和选定协议；
- Session ID 与 HTTP/2 stream ID；
- 排队、连接、首字节和总耗时；
- 请求/响应 body 应用字节数和 wire 字节数；
- flow-control 等待时间；
- retry、redirect、GOAWAY 和 reset 原因。

默认日志不得记录认证信息、Cookie 或敏感 body。

## 23. 配置与资源上限

```cpp
struct server_config
{
	protocol_policy protocols;
	timeout_policy timeout;
	message_limits request_limits;
	v1_config v1;
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2_config v2;
#endif
	std::filesystem::path resource_root;
};

`resource_root` 是静态资源层预留字段；当前 server 不自动提供静态文件服务。

struct message_limits
{
	size_t max_start_line_size = 8 * 1024;
	size_t max_field_name_size = 8 * 1024;
	size_t max_field_value_size = 16 * 1024;
	size_t max_header_list_size = 64 * 1024;
	size_t max_field_count = 100;
	optional<uint64_t> max_body_size {};
	uint64_t max_buffered_body = 8 * 1024 * 1024;
	uint64_t max_discard_body = 1024 * 1024;
};

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
struct v2_config
{
	uint32_t initial_stream_window = 65'535;           // SETTINGS_INITIAL_WINDOW_SIZE
	uint32_t initial_connection_window = 1024 * 1024; // 目标 connection receive window
	uint32_t max_concurrent_streams = 100;
	uint32_t max_frame_size = 16 * 1024;
	size_t max_header_list_size = 64 * 1024;
	size_t max_dynamic_table_size = 4 * 1024;
	size_t inbound_stream_buffer = 256 * 1024;
	size_t outbound_stream_buffer = 256 * 1024;
	size_t inbound_connection_buffer = 8 * 1024 * 1024;
	size_t outbound_connection_buffer = 8 * 1024 * 1024;
	bool enable_extended_connect = false;
};
#endif
```

`v2_config` 及其 server member 与 HTTP/2 engine 使用同一编译期宏；宏为 0 时不能留下无效配置。
这些默认值是实验起点，不是稳定兼容承诺；跨平台压力测试可以调整它们。任何调整仍必须满足：

- 本地已经授予 peer 的 connection credit、transport read buffer、nghttp2 retained input 和
  control/header 队列之和不超过对应 Session 内存预算；
- 单 stream 已授予 credit 即使大于应用 queue 的即时空闲空间，也有 connection reserve 接纳；
- 所有 stream 的待发送 DATA、nghttp2 生成但未写出的 frame 和 control queue 共同受 outbound
  connection budget 约束；
- `max_body_size` 设置时限制解压后的应用总字节；null 表示不限制流式总量，但所有内存 queue
  仍然有界。`max_buffered_body` 和 `max_discard_body` 始终生效；wire 字节另行统计。流式保存
  或 pipe 不受 buffered 上限限制；
- 0、溢出、超过协议范围或彼此矛盾的配置在启动时拒绝，不能静默截断成危险值。

`initial_stream_window` 映射到 `SETTINGS_INITIAL_WINDOW_SIZE`。HTTP/2 SETTINGS 不能修改
connection flow-control window；`initial_connection_window` 大于协议初始值的部分通过连接级
`WINDOW_UPDATE` 实现，小于协议初始值时仍必须为已经授予的 credit 预留内存。`max_frame_size`
和 `max_concurrent_streams` 是本地接收/接纳策略；发送侧的有效上限来自 peer SETTINGS 与本地
安全上限的交集，不能混用 local/peer 含义。

HTTP/2 header list 上限按解码后的字段列表及 RFC 规定的每字段开销计算，不按压缩块字节数
计算；最终有效上限取对应方向的 `message_limits`、`v2_config` 和本地硬上限的最小值。

配置值必须经过安全范围归一化。peer SETTINGS 不能把本地资源上限无限放大。HPACK dynamic
table、header block/CONTINUATION、待发送 control frame、暂停解析时保留的输入和 nghttp2
内部可配置队列也必须有硬上限或采用 nghttp2 的对应限制，不能只计算 DATA queue。

`enable_extended_connect=true` 启用 HTTP/2 Extended CONNECT；WebSocket 语义由单独链接的
`riwo.exp_websocket` 提供。Server Push 是非目标，不在配置中暴露无效开关。

## 24. 源码布局与依赖方向

重构后的实验模块统一位于 `experimental/` 层级，稳定模块目录不承载实验实现：

```text
riwo/experimental/
|- http.h                            # Experimental HTTP 聚合头
|- http/                             # HTTP 语义、H1/H2、Session 与 transport
|- http2.md                          # 本架构与契约文档
|- websocket.h                       # Experimental WebSocket 聚合头
`- websocket/                        # WebSocket 协议、stream 与 client/server

examples/experimental/{http,websocket}/
test/functional/experimental/{http,websocket}/
```

HTTP 目录按协议语义、body、客户端、服务端、Session 和 transport 小模块组织。实现细节跟随
所属小模块放入各自的 `detail/`，不建立汇总所有私有实现的根级 `detail/`。以下只列用于说明
依赖方向的关键文件，不作为完整文件清单：

```text
riwo/experimental/http/
|- global.h
|- cxx/
|  |- attributes.h
|  `- configs.h.in
|- error.h
|- version.h                         # 各层共用的 HTTP 版本枚举
|- timeout.h                         # client/server 共用 deadline 策略
|- body.h                            # body 公共 API 聚合头
|- body/
|  |- source.h
|  |- reader.h
|  |- writer.h
|  |- memory.h
|  |- file.h
|  `- detail/
|     |- source.h
|     |- reader.h
|     |- writer.h
|     `- decoder.h
|- client.h                          # 客户端公共 API 聚合头
|- client/
|  |- client.h
|  |- config.h
|  |- request.h                      # request
|  |- reply.h                        # reply
|  |- exchange.h                     # client_exchange
|  `- detail/
|     |- client.h
|     |- request.h
|     |- request_access.h
|     |- reply.h
|     |- exchange.h
|     |- connector.h
|     |- connector_adapter.h          # 公共 transport connector 到 client 内部接口
|     |- factory.h
|     `- session_pool.h
|- server.h                          # 服务端公共 API 聚合头
|- server/
|  |- acceptor_wrap.h                 # acceptor ownership；不暴露 HTTP/1 connection
|  |- server.h
|  |- config.h
|  |- exchange.h                     # server_exchange
|  `- detail/
|     |- server.h
|     |- exchange.h
|     |- acceptor_wrap.h
|     `- session.h
|- protocol.h                        # 公共语义类型、codec、engine 与能力聚合头
|- transport.h                       # 公共 byte-stream 与注入聚合头
|- protocol/
|  |- method.h
|  |- status.h
|  |- fields.h
|  |- cookie.h
|  |- request_head.h                 # client/server 共用请求 metadata
|  |- response_head.h
|  |- body_spec.h                    # protocol/body 共用消息 framing 语义
|  |- policy.h
|  |- capabilities.h
|  |- event.h                        # 公共 protocol event/action
|  |- engine.h                       # concepts::protocol_engine concept 与驱动结果
|  |- duplex_channel.h
|  |- v1/
|  |  |- codec.h                     # 公共增量 parser/encoder
|  |  |- engine.h                    # 公共 H1 connection engine
|  |  `- detail/                     # H1 codec/engine 私有实现
|  |- v2/
|  |  |- config.h                    # client/server 共用 HTTP/2 配置
|  |  |- engine.h                    # 公共 H2 connection engine
|  |  |- extension.h                 # Riwo 自有设置/诊断类型
|  |  `- detail/                     # H2 engine 与 nghttp2 adapter
|  `- detail/
|     `- duplex_channel.h
|- session/                          # Session driver 组合层；版本实现私有
|  |- session.h                      # client/server Session 与 session_stream 接口
|  |- v1/
|  |  `- session.h                   # H1 driver/factory
|  `- v2/
|     |- session.h                   # H2 driver/factory
|     `- server_session.cpp
`- transport/
   |- byte_stream.h                  # basic_byte_stream 与定制 concept
   |- metadata.h
   |- connect_options.h              # proxy/TLS 连接策略
   |- negotiated_transport.h         # connector/acceptor 共用结果
   |- connector.h                    # 客户端注入接口
   |- tcp_connection.h               # 内置实现
   |- tls_connection.h               # 可选内置实现
   `- detail/
      |- byte_stream_access.h
      `- byte_stream_connection.h
```

WebSocket 代码不在 HTTP 子树中；它由独立 target 构建并单向依赖 `riwo.exp_http`。

`<riwo/experimental/http/protocol.h>` 导出通用 event/action、HTTP/1 codec/engine，并在
`RIWO_EXP_HTTP_HTTP2_SUPPORT=1` 时导出 HTTP/2 engine；`<riwo/experimental/http/transport.h>` 导出定制
byte stream 和 connector，服务端 acceptor 由 `<riwo/experimental/http/server/acceptor_wrap.h>` 导出。根聚合头
`<riwo/experimental/http.h>` 同时包含这些接口，但低层应用
应优先包含窄头以避免无关 client/server 依赖。

`request` 和 `reply` 分别是客户端的出站 request builder 和入站 response handle，因此放在
`client/`。`request_head` 同时用于客户端冻结后的请求、协议 Session 和服务端只读 metadata，
与 `response_head` 一起放在共享的 `protocol/`。服务端通过
`server_exchange::respond()` 和 `body_writer` 产生响应，不为了目录对称额外定义
`server/request.h` 或 `server/response.h`。

`client_exchange` 和 `server_exchange` 的方向、状态机和所有权不同，分别放在
`client/exchange.h` 与 `server/exchange.h`，不使用同时定义二者的根级 `exchange.h`。物理目录
归属不改变公共命名空间；这些类型仍直接位于 `riwo::experimental::http`，不引入与 `client` 类名冲突的
嵌套 `client` 命名空间。

`session/` 同样只是物理分层；Session 类型不引入 `session` 或 `detail` 命名空间：
`client_session`、`server_session` 和 `session_stream` 位于 `riwo::experimental::http`，版本相关 factory
分别位于 `riwo::experimental::http::v1` 与 `riwo::experimental::http::v2`。

每个公开模板可以在声明头末尾包含本模块对应的 `detail/<name>.h` 实现。experimental install
包含公共语义头、H1/H2 engine/codec、transport 扩展头以及 public template 实例化必需的
`detail/` 定义；后者不形成可直接使用的 API。仅由 `.cpp` 使用的 Session driver、nghttp2
adapter、preface detector、pool 实现头不安装，也不得被聚合头导出。

依赖规则为：

- 各模块的公共头不得直接包含其他模块的 `detail/`；跨模块私有依赖放在本模块实现或本模块
  `detail/` 中；
- protocol 的公共消息、event、action、codec 和 engine 不依赖 Session、client、server、
  transport 或 executor；`h2` 公共头不得包含 nghttp2；
- `protocol/detail/` 只实现公共 protocol 类型、codec 和 engine，不得包含 Session 或 transport
  头；公共 engine 实现同样不能调用 transport；
- `session/` 是私有组合层，可以依赖 protocol、body 和 transport；protocol 与 transport 均不得
  反向包含 Session driver；
- byte-stream/metadata 公共头只依赖 core/coro I/O 基础类型，不依赖 HTTP protocol、body、
  exchange、client 或 server；connect options 与 negotiated transport 是 connector/acceptor 的
  中立共享类型，acceptor 不得为了复用协商结果反向包含 connector；connector/acceptor 是组合
  边界，可以依赖 `protocol` 枚举和拥有型 metadata，但不能依赖具体 H1/H2 engine、client
  配置聚合或高层 exchange；
- `client/detail/` 和 `server/detail/` 可以依赖私有 Session 接口，client pool 和 server
  acceptor 不得被 protocol、transport 或 Session driver 反向包含；
- router、middleware 和 convenience API 只通过 exchange、protocol 公共消息类型和 body
  接口工作；
- h1 与 h2 engine 互不包含，只在公共 event/action、Session factory 和能力层汇合。

`detail/<name>.h` 原则上只实现同模块的 `<name>.h`。跨多个模块复用的概念必须提升为有明确
所有者的公共或内部接口，不能把任一模块的 `detail/` 演变为通用杂物目录。

### 24.1 编码风格与模块组合约束

Experimental HTTP 的设计和实现必须先符合 Riwo 项目已有编码风格，再考虑局部便利；新增 API 应以稳定
`riwo::http` 中相近类型的命名、模板、别名、completion token 和错误处理方式为直接参照：

- 所有公开或可由用户组合、且持有 executor 的 I/O 类型采用
  `basic_<name><Exec = asio::any_io_executor>`；`executor_type` 必须等于 `Exec`，短名称
  `<name>` 只是默认 `basic_<name><>` 别名。`asio::any_io_executor` 可以作为默认参数或私有
  type-erasure 边界，但不能写死为公开非模板类型的存储和返回类型；
- 一个对象同时暴露监听 executor 与业务 executor 时，每种 executor 都保留独立模板参数和
  类型别名。由该对象创建的 exchange、reply、reader/writer 必须继续传播业务 executor
  类型，不能在层间静默擦除；
- protocol 公共消息类型、body、exchange、client/server 门面按层级单向依赖。小模块必须能用
  自身公开构造入口独立构造和测试；禁止通过跨模块 `friend`、私有构造函数或直接访问私有字段
  把上层对象嵌入下层实现。确需共享实现状态时，由所属层定义明确的 state/adapter 接口并通过
  构造注入；
- 禁止为方便组装而在低层头中前置声明高层 client/server 类型。公共头不得形成
  body -> exchange -> body 或 protocol -> client/server 的依赖环；
- 禁用复制或移动统一使用 core 的 `RIWO_DISABLE_COPY`、`RIWO_DISABLE_MOVE`、
  `RIWO_DISABLE_COPY_MOVE` 宏，不手写一组 `= delete`。允许的移动操作按项目风格显式
  `noexcept = default`；
- 公共头不得暴露协议引擎、异步生命周期、第三方库对象等实现细节；这些复杂状态使用 pimpl、
  state/adapter 接口或 `detail/` 头隔离。`method`、`status`、`fields`、`body_spec` 等语义已经
  由公共 API 完整定义的轻量值类型直接保存值，禁止仅为统一形式增加堆分配或原子引用计数；
- 运行时多态只保留在 transport 扩展、协议选择、body source/sink 和 middleware 等确需运行时
  选择的边界。跨层传递已经擦除的对象时复用同一个状态/所有权块，不得再套等价的虚基包装；
- 类型别名统一使用项目惯用的 `*_t` 命名，如 `config_t`、`context_t`；`executor_type`、
  迭代器所需的 `value_type`/`difference_type` 等外部协议或标准库约定名称保持不变，并按
  项目惯例提供 `executor_t` 等短别名；
  模板实现留在头文件或本模块 `detail/`，非模板 ABI 和纯值语义实现再进入 `.cpp`；
- 每次新增公共 I/O 类型时，public-api 测试至少覆盖默认别名、一个具体 executor 类型、独立
  构造、executor 类型传播以及 copy/move 契约，避免实现只对 `any_io_executor` 偶然成立；
- protocol engine 不持有 executor，不套用 `basic_*<Exec>`。其 public-api 测试改为覆盖独立构造、
  move/copy 契约、任意分片输入、有限输出 buffer、暂停恢复以及不包含 transport/client/server 头
  的窄包含。

## 25. 构建与依赖

构建入口固定为：

```cmake
option(RIWO_BUILD_EXP_HTTP
	"-- ${PRO_NAME}: Build module <EXP_HTTP>." OFF
)

set(RIWO_EXP_HTTP2_SUPPORT "AUTO" CACHE STRING
	"HTTP/2 support for EXP_HTTP: AUTO, ON or OFF."
)
set_property(CACHE RIWO_EXP_HTTP2_SUPPORT PROPERTY STRINGS AUTO ON OFF)
```

`RIWO_BUILD_EXP_HTTP` 与稳定 `RIWO_BUILD_HTTP` 分开控制。实验 target 仅依赖 `riwo.core`
和自身的 transport 基础设施，不要求构建 `riwo.coro` 或链接旧 `riwo.http`。与常规模块一致，
选择构建 Experimental HTTP 就同时安装 library、公共头和 CMake package component；实验性质由独立名称、
显式 build 选项以及不进入根聚合头来表达，不另设安装开关。

安装边界必须同时作用于 library、generated config header、公共头和 CMake export，不能只跳过
其中一项：

- `RIWO_BUILD_EXP_HTTP=OFF` 时，install tree 和 `RiwoTargets.cmake` 中不得出现 Experimental HTTP 文件或
  target，`RiwoConfig.cmake` 也不得把 `exp_http` component 报告为可用；
- `RIWO_BUILD_EXP_HTTP=ON` 时安装 `<riwo/experimental/http.h>`、`protocol.h`、`transport.h`、H1/H2
  engine/codec、byte-stream 扩展等 public header 和模板实例化必需的 detail header，并导出
  `Riwo::exp_http`；experimental consumer 使用
  `find_package(Riwo COMPONENTS exp_http)`；
- 根聚合头 `<riwo.h>` 在 `RIWO_EXP_HTTP_SUPPORT=1` 时包含 `<riwo/experimental/http.h>`；只使用本模块的
  consumer 建议直接 include `<riwo/experimental/http.h>`，并直接链接 `Riwo::exp_http`；
- `RIWO_EXP_HTTP_SUPPORT` 只写入 Experimental HTTP 自身的 generated config。build tree 中该头存在且值为
  1；模块未构建时该头不生成或安装，不能让已安装的 core config 声称实验模块可用。

`RIWO_BUILD_EXP_HTTP=ON` 要求 `RIWO_BUILD_CORO=ON`，但不要求 `RIWO_BUILD_HTTP=ON`。

Experimental HTTP 的压缩依赖沿用稳定 HTTP 的显式配置逻辑：`RIWO_EXP_HTTP_ZLIB_SUPPORT` 默认 OFF；
开启时使用 `RIWO_ZLIB_INSTALL_PREFIX` 定位 zlib，configure 阶段必须找到可用包，并执行与
`RIWO_HTTP_ZLIB_SUPPORT` 相同的导入 target 校验。压缩作为独立 body filter 处理；该选项改变
`riwo.exp_http` 的链接或安装闭包时，必须同时补齐 static/shared consumer 的依赖发现测试。
Experimental HTTP 沿用项目既有的 `RIWO_OPENSSL_SUPPORT` 选项、默认值和依赖查找规则，不因
启用 Experimental HTTP 或 HTTP/2 改写 OpenSSL 配置。该选项只决定是否构建 TLS transport；关闭时
HTTP/1.x 与 HTTP/2 的明文 TCP 路径不受影响。

`RIWO_EXP_HTTP2_SUPPORT` 独立于 OpenSSL，选择规则如下：

- `AUTO`：探测到满足当前 API/链接能力的 nghttp2 时启用，否则只构建 HTTP/1.x；
- `ON`：nghttp2 能力探测失败则 configure 失败；
- `OFF`：不查找、不链接 nghttp2，只构建 HTTP/1.x；
- OpenSSL 状态只决定 TLS transport 和 HTTP/2 over TLS 是否可用，不改变有效 HTTP/2
  support；OpenSSL 关闭而 HTTP/2 开启时，configure 必须明确报告只支持明文 prior knowledge。

configure 结果生成只读宏 `RIWO_EXP_HTTP_HTTP2_SUPPORT=0/1`，用户不直接设置该宏。其值等价于
“Experimental HTTP 已构建 && HTTP/2 选项允许 && nghttp2 能力探测成功”。宏为 0 时，
public header、源文件列表、Session factory 和链接依赖必须在编译期共同屏蔽 HTTP/2；不能保留
`protocol_requirement::v2` 等入口再于运行时返回 `operation_not_supported`。
Experimental HTTP 自身的 generated config 生成 `RIWO_EXP_HTTP_SUPPORT=1`；未构建模块时不生成
或安装该配置头。公共符号统一使用
`RIWO_EXP_HTTP_API`/`RIWO_EXP_HTTP_TAPI`，其 static/shared import/export 规则放在
`riwo/experimental/http/cxx/attributes.h`，不能依赖旧 `RIWO_HTTP_API`。

安装测试必须从 install tree 编译并链接独立 protocol consumer 与 custom-transport consumer；
直接 include 源码树中的 `detail/` 不算公共能力。shared build 还必须验证非模板 engine/codec 符号
确实导出，防止只在静态库或库内测试中偶然可用。

启用 HTTP/2 后：

- 查找或构建 nghttp2；
- 将 nghttp2 作为 `riwo.exp_http` 的私有实现依赖；
- 定义 `RIWO_EXP_HTTP_HTTP2_SUPPORT=1`；
- OpenSSL/TLS 可用时，通过 ALPN `h2` 为 `https` URI 创建 HTTP/2 Session；
- 为 `http` URI 编译 RFC 9113 prior-knowledge client 路径和基于 connection preface 的 server
  分流；
- 不编译已弃用的 `h2c` Upgrade 或 `HTTP2-Settings` 路径。

当前不设置严格的 nghttp2 最低/最高版本，以 configure-time API 和链接能力探测为准。
探测应覆盖实际使用的 client/server Session 创建、callback 注册、manual flow-control、
memory send/receive、resume DATA、SETTINGS/GOAWAY 等能力，而不是只比较版本字符串。
每次 configure 和 CI 必须记录 provider、版本、静态/动态链接方式及能力探测结果。在
Windows、Linux、macOS 以及主要编译器/链接方式积累验证结果后，再逐步收敛为受支持版本范围；
版本范围在跨平台验证积累后收敛。公共头文件不得包含 nghttp2 头，避免泄漏 ABI 和实现依赖；
替换 nghttp2 provider 或版本不能改变公共 event/action、extension 值类型和错误枚举的 ABI。

### 25.1 旧 HTTP 基础设施的复用边界

Experimental HTTP 继续选择“模块内独立实现”路线。旧 HTTP 的 parser/generator 与 request context、
connection lease 和稳定公共类型耦合较深，不能把旧类型直接抽取为新 API，否则会同时扰动稳定
模块的实现与 ABI。

Experimental HTTP 自身的 H1 codec 和 H1/H2 connection engine 已通过新的 Riwo 类型公开、安装并
导出；这不等于公开旧 `riwo::http` parser，也不等于公开 nghttp2。body adapter、高层 Session
driver、pool glue、preface detector 和第三方 backend 仍放在 `detail/`。模板实例化所需的
byte-stream 单向适配与 compatibility connection 声明随安装树提供，但不属于公共扩展面。稳定
`riwo.http` 保持原实现，并继续作为独立回归与互操作基线；`riwo::http` 类型或 target 不进入
Experimental HTTP 的 public/link interface。

## 26. 测试与验证

测试与其他正式模块一样使用公共 `test/test.h` harness，支持 case 筛选、repeat、seed、fail-fast、
CTest label、timeout 和 sanitizer 环境。文件及 target 按能力命名。Experimental HTTP 的
functional targets 为：

- `exp_http.public-api`：公共编译契约和 executor/所有权；
- `exp_http.protocol-api`：独立 H1/H2 engine/codec、分片不变性、事件/action 和输出排空；
- `exp_http.transport-extension`：custom byte stream、connector/acceptor、故障注入和安装边界；
- `exp_http.body`：body source/reader/writer 与 exchange 状态机；
- `exp_http.http1`、`exp_http.regressions`：HTTP/1 loopback 与 raw-wire 回归；
- `exp_http.session-pool`：connector、协商、preface、pool 容量与公平性；
- `exp_http.preface-internal`：明文连接前言探测与回放的内部边界；
- `exp_http.http2-client`、`exp_http.http2-server`：nghttp2 可用时的 HTTP/2 双端行为；
- `exp_http.application`、`exp_http.compatibility`：公共 API 应用工作流与稳定行为对照；
- `exp_http.reliability`：超时、取消、资源上限和 exactly-once completion；
- `riwo.exp_http.interop`：复用独立 peer driver 验证 Experimental HTTP client/server 互操作。

Experimental WebSocket 不混入上述 targets；其独立 suite 为 `exp_websocket.public-api`、
`exp_websocket.protocol`、`exp_websocket.stream`、`exp_websocket.reliability` 和
`exp_websocket.client-server`。

### 26.1 语义与状态机测试

- fields 重复值、大小写和迭代顺序；
- URL 到 HTTP/1 target 和 HTTP/2 pseudo-header 的映射，包括空 path、fragment/userinfo 拒绝、
  默认端口和 IPv6 authority；
- body size、replayability、rewind 和所有权；
- 自定义 body source 的正反编译契约、取消、rewind 和重叠操作；
- 路径、移动拥有/显式借用的 `std::fstream` 家族及 `owned_file(FILE*)`/
  `borrowed_file(FILE*)` 的 source/sink 所有权，验证 owned 恰好关闭一次、borrowed 从不关闭；
- file wrapper/source/sink 的 move-only `static_assert` 和 moved-from 析构安全；
- borrowed 文件成功、错误和取消后的 cursor/state 恢复，以及外部并发访问被列为违反前置条件；
- binary byte offset、64 位 seek、`file_span` 边界与所有溢出路径，以及 short read/write、提前 EOF、
  零进展、flush 失败、不可 seek、append/text mode 拒绝；
- 顺序 sink 的 200 保存、random-access sink 的单 Range/multipart Range 写入和已打开对象的明确
  truncate/preserve-tail 策略；
- 同步文件后端只在 chunk 边界观察取消且 exactly-once completion；后续 `io_uring`/IOCP 后端
  复用同一组 source/sink 行为向量；
- body source 的非空 read 正常 EOF 必须是 `riwo::errc::eof`，空 buffer 必须成功返回 0；
- body reader 的最后一批数据必须允许返回 `{n, true}`，空 body 返回 `{0, true}`，正常 EOF
  不得进入错误通道，`is_finished()` 在 EOF 后保持为 true；
- 扩展 method 的大小写、身份/hash、非法 token 和 well-known safe/idempotent 属性；
- URL authority、手工 `Host`、forward proxy target 的冲突处理；
- known-length source/writer 的不足、超出、finish 和重试行为；
- request/reply/exchange/reader/writer 的 copy/move `static_assert`；
- 默认 `any_io_executor` 别名与具体 executor 实例的独立构造、类型保持和逐层传播；
- 公共 body/exchange/client/server 之间不存在用于组装对象的跨模块 `friend`；
- trailers 调用时序、finish 后写入和重叠操作；
- `open(request, body_spec)` 保留 headers/options，并拒绝已经绑定 body/source 的 request；
- known/streaming body 的 trailer 声明、HTTP/1 chunked 选择和未声明 trailer 拒绝；
- 1xx、100-continue、提前最终响应与上传错误的竞争顺序；
- HTTP/1 framing 决策；
- Session capacity 和状态转换；
- error scope 与 retry 判定；
- cancel/timeout/重叠操作与正常完成竞争时的稳定错误映射和 exactly-once completion。

### 26.2 协议测试

- HTTP/1 request/response head 在每一个字节边界分片时都产生相同结果；
- HTTP/1 content-length、chunked、trailers、EOF framing 和 Upgrade 的增量 codec；
- `produce()` 使用 1 字节、小 buffer 和随机 buffer 时均可排空，结果可被独立 peer 解析；
- `pause` 后的 consumed 位置、恢复、未消费输入所有权和 body view 生命周期；
- event -> action -> wire 与 raw wire -> event 的双向向量，Session 路径与手动 engine 路径一致；
- HTTP/2 preface、SETTINGS 和 ACK；
- HEADERS/CONTINUATION；
- DATA 与 stream/connection window；
- trailers；
- RST_STREAM；
- PING；
- GOAWAY last stream ID 与 `request_delivery` 映射；
- `REFUSED_STREAM`、无 GOAWAY 断开以及非幂等请求的重试门槛；
- malformed pseudo-header 和禁止字段；
- header list、frame size 和并发 stream 限制；
- connection/stream 错误 scope、错误后可排空 GOAWAY 以及拒绝后续普通 action；
- 公共 H2 extension 不出现 nghttp2 类型，替换 provider 后语义向量保持一致。

### 26.3 Transport 扩展测试

- 从 install tree 定义最小异步 memory byte stream，不 include 任意 `detail/` 头即可运行 H1/H2；
- custom connector 返回 H1、H2、带 `initial_input` 的结果，以及连接、取消、协商失败；
- custom acceptor 接受多条预连接流，验证 server routing、graceful shutdown 和 exactly-once
  completion；
- 1 字节 read、partial underlying write、读写重叠、EOF、截断、取消、close 与 handler 竞争；
- 不支持同步 I/O 的 stream 在异步/协程路径正常运行，同步入口稳定返回
  `operation_not_supported`；
- 非 TCP metadata、空 endpoint、带外选定协议和 pre-read 字节均不影响 engine 正确性；
- shared/static build 均可链接非模板 engine/codec 和 type-erasure adapter。

### 26.4 并发与压力测试

- 单连接大量并发 stream；
- 一个慢消费者与多个正常 stream；
- outbound window 长时间为零；
- stream 取消与同时到达 DATA；
- 多个长响应订阅同时接收 publish，其中一个慢消费者不得阻止其他 stream；
- 订阅队列达到消息数/字节上限时按所选策略背压、丢弃或断开，且内存保持有界；
- publish 与 RST_STREAM、GOAWAY、handler 返回和 graceful shutdown 竞争时 exactly-once 完成；
- GOAWAY 与新请求竞争；
- Session shutdown 与 handler 完成竞争；
- 多线程同时通过 client 发起请求；
- 队列达到内存上限时验证背压而不是增长。

### 26.5 互操作

- Riwo client 对独立 HTTP/1、HTTP/2 server；
- 独立 HTTP/1、HTTP/2 client 对 Riwo server；
- HTTP/2 编译开启时的 TLS ALPN h2、HTTP/1.1 fallback 和
  `protocol_requirement::v2` 协商失败；
- `http` URI 的 prior-knowledge client、明文 server preface 识别，以及对 HTTP/1-only peer 的
  确定失败；
- OpenSSL 显式关闭且 HTTP/2 开启时仍可完成明文 HTTP/2 互操作，且链接依赖不残留 OpenSSL；
- nghttp2 工具、curl `--http2`/`--http2-prior-knowledge` 和规范测试工具；
- proxy CONNECT 后的目标 ALPN；
- gzip、redirect、Cookie、路径文件、`std::fstream`、`FILE*`、Range 与 streaming body，且同一
  文件 source/sink 在 HTTP/1.1 与 HTTP/2 下保持相同字节结果。

### 26.6 模糊测试

- HTTP/1 start line/header/chunk parser；
- H1/H2 engine 的随机 ingest/produce 分片、pause/resume 与显式 EOF；
- HTTP/2 callback 到 semantic event 的转换边界；
- fields 和 pseudo-header 校验；
- cancellation/EOF/error 注入；
- 长度溢出和资源上限。

## 27. 持续验收条件

下列条件已经达到，并作为后续修改必须持续保持的回归门槛。

### 27.1 模块与 HTTP/1 基线

1. `<riwo/experimental/http.h>` 可独立包含，命名空间和 target 不依赖旧 HTTP 公共类型；
2. ordered fields、request/head、body spec 和 move-only handle 通过 public API 编译测试；
3. fake Session 覆盖 EOF、known-length、finish、取消、析构和重叠操作状态机；
4. HTTP/1 loopback 完成 GET、known-length POST、streaming request/response 和未消费 body；
5. 100-continue、提前最终响应以及 handler 提前返回均有确定结果且不复用脏连接；
6. 旧 `riwo.http` 的现有 functional、fuzz smoke 和安装测试保持不变；
7. 稳定 HTTP/Experimental HTTP 开关与默认/实验安装矩阵全部通过。

### 27.2 HTTP/2 与高层语义

1. HTTP/2 编译开启时，同一份普通 GET/POST/文件/流式示例无需判断协议即可运行在 HTTP/1.1
   与 HTTP/2；
2. HTTP/2 编译开启时，HTTPS 默认优先 h2，并可验证回退到 HTTP/1.1；
   `http` URI 可通过显式 prior knowledge 运行 HTTP/2；
3. 单条 HTTP/2 连接可并发处理多个 stream，慢 stream 不阻止其他 stream 推进；
4. 所有 inbound/outbound body 缓存有明确上限；
5. stream cancel 不会无条件关闭 HTTP/2 连接；
6. GOAWAY、RST_STREAM、trailers 和 flow control 通过互操作测试；
7. 未消费 body、取消、超时和析构不会产生 use-after-free 或复用脏连接；
8. nghttp2 类型不出现在公共头和安装 API 中；
9. CI 记录已验证的 nghttp2 provider、版本、平台和静态/动态链接方式，形成可维护的兼容矩阵；
10. Header、Cookie、Content-Length、Host/authority 和禁止字段行为符合对应协议规则；
11. 文档明确每个异步参数的所有权、EOF 约定和每个对象允许的并发操作；
12. `RIWO_OPENSSL_SUPPORT` 只控制 TLS transport，关闭时不能连带移除 HTTP/2 公共声明、
    明文实现或 nghttp2 依赖；`RIWO_EXP_HTTP_HTTP2_SUPPORT` 只由
    HTTP/2 选项与 nghttp2 能力决定。

### 27.3 公共协议引擎与自定义 I/O

1. 安装后的 H1 parser/encoder 与 connection engine 可在不构造 client/server/socket 的程序中
   独立序列化、反序列化并通过 raw-wire 向量；
2. HTTP/2 启用时，安装后的 H2 engine 可由用户手动 ingest/produce，并且公共头和符号不泄漏
   nghttp2；
3. 同一组 event/action 向量经手动 engine loop 和内置 Session driver 得到相同语义结果；
4. 用户只实现 byte-stream concept 即可运行托管 Session；只实现 connector/acceptor 即可接入
   高层 client/server，均不 include `detail/`；
5. 自定义 transport 的 EOF、取消、partial write、无同步能力和 `initial_input` 行为有稳定契约及
   故障注入测试；
6. engine 在任意输入/输出分片、pause/resume 和容量上限下保持确定、内存有界；
7. install tree 的 static/shared consumer、公共窄头 include 和非模板符号导出测试全部通过；
8. 内置 TCP/TLS Session 已切换到公共 engine/byte-stream 边界，不再维护重复 wire 实现。

### 27.4 文件 source/sink

第 20.1 节同步文件扩展使用以下验收，并在后续 backend 演进中持续保留：

1. 路径、移动拥有/显式借用的窄字符 C++ 文件流以及显式 owned/borrowed `FILE*` 均可作为
   source/sink，资源关闭、借用寿命和 cursor 恢复行为由契约测试锁定；
2. HTTP Range 与文件 API 之间只使用经溢出检查的 `uint64_t` 字节 offset/length，支持平台可表示
   的大文件，不受 text mode 换行转换影响；
3. 当前同步后端的阻塞和取消边界有明确文档；未来 `io_uring`/IOCP 实现不修改公共 API，并通过
   与同步后端相同的行为测试。

## 28. 关键设计决策摘要

- **保留 URL API**：URL 是资源定位，不是协议版本选择。
- **版本运行时协商**：删除 `basic_client<Version>` 一类版本模板。
- **以 Exchange 为中心**：请求对应 stream，不对应独占连接。
- **保留 Riwo server 风格**：保留 `bind/on_request/on_default/start` 门面；handler 和
  `context_t` 落到协议无关的 `server_exchange`。
- **主动消息依附 client stream**：单向消息使用长响应和应用有界队列，双向消息使用
  `duplex_channel`；`PUSH_PROMISE` 不充当业务消息通道。
- **Session capacity 统一池化**：HTTP/1 capacity 为 1，HTTP/2 capacity 为 N。
- **普通数据不包装**：字符串和字节容器直接传递；文件、JSON、实时流、借用内存明确
  表达额外语义。
- **文件对象显式拥有或借用**：路径由库拥有，C++ 文件流通过移动或 `borrowed_file` 区分，
  `FILE*` 必须选择 `owned_file`/`borrowed_file`，禁止从裸指针猜测关闭责任。
- **Range 只按二进制字节定位**：文件窗口使用 `uint64_t {offset, length}`，只支持 binary
  `char` 流；大文件定位、加法和平台转换全部检查溢出。
- **文件 I/O 先同步、契约保持异步形状**：当前在 executor 上按有界 chunk 同步访问文件；以后
  路径文件可替换为 `io_uring`/IOCP，`std::fstream`/`FILE*` 在无法安全接管缓冲时保持同步。
- **Body API 不暴露 framing**：统一 `read_some()`、`write()`、`finish()`、`trailers()`。
- **Fields 保留重复和顺序**：伪头字段由 request/response 语义生成。
- **协议引擎公共、backend 私有**：H1/H2 engine 以 Riwo event/action 和增量字节 API 安装导出；
  nghttp2 与 Session driver 不进入公共 API。
- **协议与 I/O 正交组合**：engine 不执行 I/O；用户可以手动驱动，也可以把自定义 byte stream、
  connector/acceptor 注入托管 Session。
- **HTTP/2 遵循标准建连方式**：`https` 使用 TLS/ALPN `h2`，`http` 使用显式 prior knowledge；
  不实现已弃用的 `h2c` Upgrade。
- **依赖按能力探测**：实验期不锁死 nghttp2 版本，跨平台验证后逐步收敛支持范围。
- **背压是硬约束**：任何协议和便利 API 都不得依赖无界 body 缓存。
- **差异通过能力和扩展暴露**：普通应用无需按 HTTP 版本编写两套代码。

