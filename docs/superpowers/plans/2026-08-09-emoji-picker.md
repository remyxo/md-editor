# 表情符号选择器（Emoji Picker）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给编辑器补上「插入表情符号」入口——工具栏图标 + 菜单项，点击弹出带分类与搜索的 emoji 选择器浮层，点击/回车在光标处插入，支持最近使用。

**Architecture:** 工具栏/菜单（C++ QAction）→ MainWindow::onInsertEmoji 把按钮屏幕坐标换算成 webview 内坐标 → runJavaScript 调 `editorApi.openEmojiPicker(x,y)` → editor.js 的 `_emojiPicker` 浮层（Chromium 渲染、非模态、点外部/Esc 关闭）在 webview DOM 内显示。Esc 关闭复用现有 `bridge.modalDialogChanged` + `m_modalDialogOpen` 机制。

**Tech Stack:** Qt6 (QToolBar/QMenuBar/QShortcut/QWebEngineView)、原生 JS（editor.js 直接加载，无需 esbuild）、CSS（editor.css）。

## Global Constraints

- 注释和文档用中文，代码标识符（类名/方法名/变量名）用英文。
- 版本号定义在根 `CMakeLists.txt` 的 `project(md-editor VERSION x.y.z)`，本次 0.3.1 → 0.3.2。
- 提交规范：`feat:` / `fix:` / `chore:` 前缀 + 中文描述，结尾 `Co-Authored-By: Claude <noreply@anthropic.com>`。
- 图标 SVG 约定：`viewBox="0 0 24 24"` `fill="none"` `stroke-width="2"` `stroke-linecap="round"` `stroke-linejoin="round"`；light 用 `stroke="#222"`，dark 用 `stroke="#d0d4d8"`，与现有 `diagram.svg` 等一致。
- editor.js 是直接加载的资源（`app.qrc` 里 `web/editor.js`），改完即生效，**不要**碰 `tiptap-all.js`（那是 esbuild 产物）。
- 构建目录：`/home/daijd/claude-workspace/md-editor/build`（已存在）。
- 不要改 `MarkdownHandler`（emoji 是纯 Unicode 文本，HTML 往返无损）。

---

## File Structure

| 文件 | 责任 | 任务 |
|------|------|------|
| `src/resources/icons/light/emoji.svg` | 亮色笑脸图标 | Task 1 |
| `src/resources/icons/dark/emoji.svg` | 暗色笑脸图标 | Task 1 |
| `src/resources/app.qrc` | 登记新图标资源 | Task 1 |
| `src/ui/ToolBar.h` / `.cpp` | 工具栏 emoji 按钮 + `emojiClicked` 信号 | Task 2 |
| `src/ui/MenuBar.h` / `.cpp` | 菜单「表情符号...」项 + `insertEmoji` 信号 | Task 3 |
| `src/resources/web/editor.js` | `EMOJI_CATEGORIES` 数据 + `_emojiPicker` 浮层 + `editorApi.openEmojiPicker/insertEmoji` | Task 4 |
| `src/resources/web/editor.css` | 浮层样式（亮/暗） | Task 4 |
| `src/ui/MainWindow.h` / `.cpp` | `onInsertEmoji` 定位 + 信号连接 + Esc 分支追加 close | Task 5 |
| `CMakeLists.txt` | 版本 0.3.1 → 0.3.2 | Task 6 |
| `docs/modal-dialog-esc-handling.md` | 补充 emoji 浮层复用 Esc 标记说明 | Task 6 |

---

### Task 1: Emoji 图标资源

**Files:**
- Create: `src/resources/icons/light/emoji.svg`
- Create: `src/resources/icons/dark/emoji.svg`
- Modify: `src/resources/app.qrc`（light 区第 34 行后、dark 区第 65 行后各加一行）

**Interfaces:**
- Produces: 资源路径 `:/icons/light/emoji.svg`、`:/icons/dark/emoji.svg`（供 Task 2 的 `makeAction` 引用）。

- [ ] **Step 1: 创建亮色图标**

写入 `src/resources/icons/light/emoji.svg`（一张笑脸：圆 + 两眼 + 上弯嘴，stroke `#222`）：

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#222" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><line x1="9" y1="9" x2="9.01" y2="9"/><line x1="15" y1="9" x2="15.01" y2="9"/></svg>
```

- [ ] **Step 2: 创建暗色图标**

写入 `src/resources/icons/dark/emoji.svg`（同上，stroke `#d0d4d8`）：

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#d0d4d8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><line x1="9" y1="9" x2="9.01" y2="9"/><line x1="15" y1="9" x2="15.01" y2="9"/></svg>
```

- [ ] **Step 3: 在 app.qrc 登记两个图标**

在 `src/resources/app.qrc` 的 light 区，`<file>icons/light/diagram.svg</file>`（第 34 行）之后加一行：

```xml
        <file>icons/light/emoji.svg</file>
```

在 dark 区，`<file>icons/dark/diagram.svg</file>`（第 65 行）之后加一行：

```xml
        <file>icons/dark/emoji.svg</file>
```

- [ ] **Step 4: 构建验证（资源能被 AUTORCC 编译）**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 构建成功，无「file not found」错误。

- [ ] **Step 5: 提交**

```bash
git add src/resources/icons/light/emoji.svg src/resources/icons/dark/emoji.svg src/resources/app.qrc
git commit -m "feat: 新增 emoji 表情图标资源（light/dark）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: 工具栏 emoji 按钮

**Files:**
- Modify: `src/ui/ToolBar.h`（signals 区第 38 行 `mermaidClicked` 后加信号）
- Modify: `src/ui/ToolBar.cpp`（setupUi「插入按钮」区块第 182 行后加按钮；信号循环第 230 行后加分支）

**Interfaces:**
- Consumes: Task 1 的图标路径 `:/icons/light/emoji.svg`。
- Produces: `ToolBar::emojiClicked()` 信号（供 Task 5 连接）；按钮 objectName = `emojiAction`（供 Task 5 的 `findChild` 定位）。

- [ ] **Step 1: ToolBar.h 加信号**

在 `src/ui/ToolBar.h` 的 `signals:` 区，`void mermaidClicked();`（第 38 行）之后加一行：

```cpp
    void emojiClicked();
```

- [ ] **Step 2: ToolBar.cpp setupUi 加按钮**

在 `src/ui/ToolBar.cpp` 的「插入按钮」区块，`diagramAction`（第 181-182 行）之后、`addSeparator();`（第 184 行）之前加：

```cpp
    makeAction(this, &m_iconPaths, "emojiAction",
        "插入表情", ":/icons/light/emoji.svg");
```

- [ ] **Step 3: ToolBar.cpp 信号循环加分支**

在 setupUi 的 `for (QAction *a : findChildren<QAction *>())` 循环里，`diagramAction` 分支（第 229-230 行）之后加：

```cpp
        else if (a->objectName() == "emojiAction")
            connect(a, &QAction::triggered, this, &ToolBar::emojiClicked);
```

- [ ] **Step 4: 构建验证**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 编译通过（`emojiClicked` 信号已声明并 emit 路径连通）。

- [ ] **Step 5: 提交**

```bash
git add src/ui/ToolBar.h src/ui/ToolBar.cpp
git commit -m "feat: 工具栏新增 emoji 按钮（emojiClicked 信号）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: 菜单 emoji 项

**Files:**
- Modify: `src/ui/MenuBar.h`（signals 区加 `insertEmoji`）
- Modify: `src/ui/MenuBar.cpp`（`m_insertMenu` 分割线项之后加菜单项）

**Interfaces:**
- Produces: `MenuBar::insertEmoji()` 信号（供 Task 5 连接）。

- [ ] **Step 1: MenuBar.h 加信号**

在 `src/ui/MenuBar.h` 的 `signals:` 区，`insertHorizontalRule` 之后（与其它 insert 信号同区）加一行：

```cpp
    void insertEmoji();
```

> 若 `insertHorizontalRule` 不在该文件 signals 区，则在任意 `insert*` 信号旁加即可；最终以「signals 区内存在 `void insertEmoji();`」为准。

- [ ] **Step 2: MenuBar.cpp 加菜单项**

在 `src/ui/MenuBar.cpp` 的 `m_insertMenu` 区块，`hrAct`（分割线项，第 128-129 行）之后、`// === 帮助菜单 ===`（第 131 行）之前加：

```cpp
    QAction *emojiAct = m_insertMenu->addAction("表情符号...");
    connect(emojiAct, &QAction::triggered, this, &MenuBar::insertEmoji);
```

- [ ] **Step 3: 构建验证**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add src/ui/MenuBar.h src/ui/MenuBar.cpp
git commit -m "feat: 插入菜单新增「表情符号...」项（insertEmoji 信号）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: Emoji 数据集 + 选择器浮层（editor.js + editor.css）

本任务实现纯前端浮层，可独立于 C++ 测试（在浏览器里直接打开 `src/resources/web/editor.html`，控制台调 `window.editorApi.openEmojiPicker(100,100)`）。

**Files:**
- Modify: `src/resources/web/editor.js`
  - 在 `EMOJI_SHORTCODES` 之后（第 142 行 `};` 之后、`buildEmojiExtension` 之前，约第 143 行）插入 `EMOJI_CATEGORIES` 数据。
  - 在 editorApi 的 `insertMermaid` 之后（第 736 行）加 `openEmojiPicker` / `insertEmoji` 方法。
  - 在 editorApi 的 `_mermaidEditor` 对象之后（第 1230 行 `}` 之后、第 1231 行 `};` 之前）加 `_emojiPicker` 子对象。
- Modify: `src/resources/web/editor.css`（文件末尾追加浮层样式）。

**Interfaces:**
- Produces: `window.editorApi._emojiPicker`（含 `open(x,y)` / `close()`）、`window.editorApi.openEmojiPicker(x,y)`、`window.editorApi.insertEmoji(ch)`。供 Task 5 的 C++ 调用。

- [ ] **Step 1: 插入 EMOJI_CATEGORIES 数据集**

在 `src/resources/web/editor.js` 的 `EMOJI_SHORTCODES` 定义结束（第 142 行 `};`）之后插入如下数据（每条 `{ ch: 字符, kw: 搜索关键词 }`，kw 含中文与英文短码，空格分隔；**不改动** `EMOJI_SHORTCODES`）：

```js
// 表情符号分类数据（供 emoji 选择器使用；与 EMOJI_SHORTCODES 独立，互不影响）
var EMOJI_CATEGORIES = [
    { id: 'smileys', icon: '😀', name: '笑脸', emojis: [
        { ch:'😀', kw:'笑脸 开心 grin' }, { ch:'😃', kw:'笑脸 大笑 smiley' },
        { ch:'😄', kw:'笑脸 开心 smile' }, { ch:'😁', kw:'咧嘴 grin' },
        { ch:'😆', kw:'哈哈 laughing' }, { ch:'😅', kw:'汗 笑 sweat_smile' },
        { ch:'🤣', kw:'笑哭 rolling' }, { ch:'😂', kw:'笑哭 流泪 joy' },
        { ch:'🙂', kw:'微笑 slight_smile' }, { ch:'🙃', kw:'倒脸 upside_down' },
        { ch:'😉', kw:'眨眼 wink' }, { ch:'😊', kw:'害羞 blush' },
        { ch:'😇', kw:'天使 innocent' }, { ch:'🥰', kw:'爱心 恋 love' },
        { ch:'😍', kw:'花痴 heart_eyes' }, { ch:'🤩', kw:'惊叹 star_struck' },
        { ch:'😘', kw:'飞吻 kissing_heart' }, { ch:'😗', kw:'亲吻 kissing' },
        { ch:'😚', kw:'亲亲 kissing_closed' }, { ch:'😋', kw:'美味 yum' },
        { ch:'😛', kw:'吐舌 stuck_out_tongue' }, { ch:'😝', kw:'吐舌 眯眼 tongue' },
        { ch:'🤪', kw:'搞怪 zany' }, { ch:'🤔', kw:'思考 thinking' },
        { ch:'🤨', kw:'挑眉 raised_eyebrow' }, { ch:'😐', kw:'无表情 neutral' },
        { ch:'😑', kw:'面无表情 expressionless' }, { ch:'😶', kw:'沉默 no_mouth' },
        { ch:'🙄', kw:'翻白眼 roll_eyes' }, { ch:'😏', kw:'得意 smirk' },
        { ch:'😒', kw:'不悦 unamused' }, { ch:'😞', kw:'失望 disappointed' },
        { ch:'😟', kw:'担心 worried' }, { ch:'😕', kw:'困惑 confused' },
        { ch:'🙁', kw:'皱眉 frowning' }, { ch:'😣', kw:' persevere 忍' },
        { ch:'😖', kw:'困扰 confounded' }, { ch:'😫', kw:'疲惫 tired' },
        { ch:'🥺', kw:'恳求 pleading' }, { ch:'😢', kw:'哭 cry' },
        { ch:'😭', kw:'大哭 sob' }, { ch:'😤', kw:'哼 huff' },
        { ch:'😠', kw:'生气 angry' }, { ch:'😡', kw:'怒 rage' },
        { ch:'🤬', kw:'爆粗 cursing' }, { ch:'😱', kw:'尖叫 scream' },
        { ch:'😨', kw:'害怕 fearful' }, { ch:'😰', kw:'焦虑 cold_sweat' },
        { ch:'😳', kw:'脸红 flushed' }, { ch:'😴', kw:'睡觉 sleepy' },
        { ch:'🤤', kw:'流口水 drool' }, { ch:'🥱', kw:'打哈欠 yawn' }
    ]},
    { id: 'gestures', icon: '👍', name: '手势', emojis: [
        { ch:'👍', kw:'赞 好 +1 thumbsup' }, { ch:'👎', kw:'踩 差 -1 thumbsdown' },
        { ch:'👌', kw:'好的 ok' }, { ch:'✌️', kw:'胜利 v' },
        { ch:'🤞', kw:'祈祷 手指 crossed' }, { ch:'🤟', kw:'爱你 love_you' },
        { ch:'🤘', kw:'摇滚 metal' }, { ch:'🤙', kw:'call' },
        { ch:'👈', kw:'左 point_left' }, { ch:'👉', kw:'右 point_right' },
        { ch:'👆', kw:'上 point_up' }, { ch:'👇', kw:'下 point_down' },
        { ch:'☝️', kw:'一 指 point_up_2' }, { ch:'✋', kw:'停 手 hand' },
        { ch:'🤚', kw:'手背 raised_back' }, { ch:'🖐️', kw:'五 手 hand_splayed' },
        { ch:'👋', kw:'挥手 wave' }, { ch:'🤝', kw:'握手 handshake' },
        { ch:'👏', kw:'鼓掌 clap' }, { ch:'🙌', kw:'举手 raised_hands' },
        { ch:'🙏', kw:'祈祷 thanks pray' }, { ch:'💪', kw:'肌肉 力量 muscle' },
        { ch:'✍️', kw:'写 writing' }, { ch:'👀', kw:'眼睛 eyes' }
    ]},
    { id: 'animals', icon: '🐱', name: '动物', emojis: [
        { ch:'🐶', kw:'狗 dog' }, { ch:'🐱', kw:'猫 cat' },
        { ch:'🐭', kw:'老鼠 mouse' }, { ch:'🐹', kw:'仓鼠 hamster' },
        { ch:'🐰', kw:'兔 rabbit' }, { ch:'🦊', kw:'狐狸 fox' },
        { ch:'🐻', kw:'熊 bear' }, { ch:'🐼', kw:'熊猫 panda' },
        { ch:'🐨', kw:'考拉 koala' }, { ch:'🐯', kw:'老虎 tiger' },
        { ch:'🦁', kw:'狮子 lion' }, { ch:'🐮', kw:'牛 cow' },
        { ch:'🐷', kw:'猪 pig' }, { ch:'🐸', kw:'青蛙 frog' },
        { ch:'🐵', kw:'猴子 monkey' }, { ch:'🐔', kw:'鸡 chicken' },
        { ch:'🐧', kw:'企鹅 penguin' }, { ch:'🐦', kw:'鸟 bird' },
        { ch:'🦆', kw:'鸭 duck' }, { ch:'🦉', kw:'猫头鹰 owl' },
        { ch:'🐢', kw:'龟 turtle' }, { ch:'🐠', kw:'鱼 fish' },
        { ch:'🐬', kw:'海豚 dolphin' }, { ch:'🐳', kw:'鲸 whale' },
        { ch:'🦋', kw:'蝴蝶 butterfly' }, { ch:'🐝', kw:'蜜蜂 bee' },
        { ch:'🐛', kw:'虫 bug' }, { ch:'🦄', kw:'独角兽 unicorn' }
    ]},
    { id: 'food', icon: '🍎', name: '食物', emojis: [
        { ch:'🍎', kw:'苹果 apple' }, { ch:'🍊', kw:'橙子 orange' },
        { ch:'🍌', kw:'香蕉 banana' }, { ch:'🍉', kw:'西瓜 watermelon' },
        { ch:'🍇', kw:'葡萄 grapes' }, { ch:'🍓', kw:'草莓 strawberry' },
        { ch:'🍑', kw:'桃 peach' }, { ch:'🍍', kw:'菠萝 pineapple' },
        { ch:'🥭', kw:'芒果 mango' }, { ch:'🍔', kw:'汉堡 burger' },
        { ch:'🍟', kw:'薯条 fries' }, { ch:'🍕', kw:'披萨 pizza' },
        { ch:'🌭', kw:'热狗 hotdog' }, { ch:'🥗', kw:'沙拉 salad' },
        { ch:'🍦', kw:'冰淇淋 icecream' }, { ch:'🍰', kw:'蛋糕 cake' },
        { ch:'🍫', kw:'巧克力 chocolate' }, { ch:'🍿', kw:'爆米花 popcorn' },
        { ch:'☕', kw:'咖啡 coffee' }, { ch:'🍵', kw:'茶 tea' },
        { ch:'🍺', kw:'啤酒 beer' }, { ch:'🍷', kw:'红酒 wine' },
        { ch:'🥤', kw:'饮料 cup' }, { ch:'🧋', kw:'奶茶 boba' }
    ]},
    { id: 'nature', icon: '🌸', name: '自然', emojis: [
        { ch:'🌸', kw:'樱花 cherry_blossom' }, { ch:'🌹', kw:'玫瑰 rose' },
        { ch:'🌻', kw:'向日葵 sunflower' }, { ch:'🌷', kw:'郁金香 tulip' },
        { ch:'🍀', kw:'四叶草 clover' }, { ch:'🌿', kw:'草 herb' },
        { ch:'🌳', kw:'树 tree' }, { ch:'🌴', kw:'棕榈 palm' },
        { ch:'☀️', kw:'太阳 sunny' }, { ch:'🌞', kw:'太阳脸 sun_face' },
        { ch:'🌙', kw:'月亮 moon' }, { ch:'🌕', kw:'满月 full_moon' },
        { ch:'⭐', kw:'星 star' }, { ch:'🌟', kw:'闪光 stars' },
        { ch:'✨', kw:'闪烁 sparkles' }, { ch:'⚡', kw:'闪电 zap' },
        { ch:'🔥', kw:'火 fire' }, { ch:'💧', kw:'水滴 droplet' },
        { ch:'🌈', kw:'彩虹 rainbow' }, { ch:'☁️', kw:'云 cloud' },
        { ch:'❄️', kw:'雪花 snowflake' }, { ch:'🌊', kw:'海浪 ocean' },
        { ch:'⛱️', kw:'伞 beach' }, { ch:'🌋', kw:'火山 volcano' }
    ]},
    { id: 'activities', icon: '⚽', name: '活动', emojis: [
        { ch:'⚽', kw:'足球 soccer' }, { ch:'🏀', kw:'篮球 basketball' },
        { ch:'🏈', kw:'橄榄球 football' }, { ch:'⚾', kw:'棒球 baseball' },
        { ch:'🎾', kw:'网球 tennis' }, { ch:'🏐', kw:'排球 volleyball' },
        { ch:'🎱', kw:'台球 pool' }, { ch:'🏓', kw:'乒乓球 ping_pong' },
        { ch:'🎮', kw:'游戏 video_game' }, { ch:'🎯', kw:'靶 dart' },
        { ch:'🎨', kw:'艺术 调色板 art' }, { ch:'🎵', kw:'音符 musical_note' },
        { ch:'🎸', kw:'吉他 guitar' }, { ch:'🎤', kw:'麦克风 mic' },
        { ch:'🎉', kw:'派对 tada' }, { ch:'🎈', kw:'气球 balloon' },
        { ch:'🎁', kw:'礼物 gift' }, { ch:'🏆', kw:'奖杯 trophy' },
        { ch:'🥇', kw:'金牌 first' }, { ch:'🚴', kw:'骑车 bike' },
        { ch:'🏊', kw:'游泳 swim' }, { ch:'🏃', kw:'跑步 runner' },
        { ch:'🎲', kw:'骰子 dice' }, { ch:'🧩', kw:'拼图 puzzle' }
    ]},
    { id: 'travel', icon: '🚗', name: '旅行', emojis: [
        { ch:'🚗', kw:'汽车 car' }, { ch:'🚕', kw:'出租车 taxi' },
        { ch:'🚌', kw:'公交车 bus' }, { ch:'🚆', kw:'火车 train' },
        { ch:'✈️', kw:'飞机 airplane' }, { ch:'🚀', kw:'火箭 rocket' },
        { ch:'🚢', kw:'船 ship' }, { ch:'🚲', kw:'自行车 bike' },
        { ch:'🗺️', kw:'地图 map' }, { ch:'🧭', kw:'指南针 compass' },
        { ch:'🏠', kw:'房子 home' }, { ch:'🏢', kw:'办公楼 office' },
        { ch:'🏫', kw:'学校 school' }, { ch:'🏥', kw:'医院 hospital' },
        { ch:'⛰️', kw:'山 mountain' }, { ch:'🏕️', kw:'露营 camping' },
        { ch:'🏖️', kw:'海滩 beach' }, { ch:'🌍', kw:'地球 earth' },
        { ch:'🗽', kw:'自由女神 statue' }, { ch:'🏰', kw:'城堡 castle' },
        { ch:'🌉', kw:'桥 bridge' }, { ch:'🎡', kw:'摩天轮 ferris' },
        { ch:'🌋', kw:'火山 volcano' }, { ch:'🌃', kw:'夜景 night' }
    ]},
    { id: 'objects', icon: '💡', name: '物品', emojis: [
        { ch:'💡', kw:'灯泡 想法 bulb' }, { ch:'🔔', kw:'铃 bell' },
        { ch:'📚', kw:'书 books' }, { ch:'📖', kw:'阅读 book' },
        { ch:'📝', kw:'笔记 备忘 memo' }, { ch:'✏️', kw:'铅笔 pencil' },
        { ch:'📌', kw:'图钉 pushpin' }, { ch:'📎', kw:'回形针 paperclip' },
        { ch:'✂️', kw:'剪刀 scissors' }, { ch:'💻', kw:'电脑 laptop' },
        { ch:'🖥️', kw:'台式机 desktop' }, { ch:'📱', kw:'手机 phone' },
        { ch:'⌨️', kw:'键盘 keyboard' }, { ch:'🖱️', kw:'鼠标 mouse' },
        { ch:'💾', kw:'保存 floppy' }, { ch:'📷', kw:'相机 camera' },
        { ch:'📺', kw:'电视 tv' }, { ch:'🔋', kw:'电池 battery' },
        { ch:'🔑', kw:'钥匙 key' }, { ch:'🔒', kw:'锁 lock' },
        { ch:'🔨', kw:'锤子 hammer' }, { ch:'🛠️', kw:'工具 tools' },
        { ch:'💣', kw:'炸弹 bomb' }, { ch:'🔮', kw:'水晶球 crystal' }
    ]},
    { id: 'symbols', icon: '❤️', name: '符号', emojis: [
        { ch:'❤️', kw:'心 红 heart' }, { ch:'🧡', kw:'心 橙 orange_heart' },
        { ch:'💛', kw:'心 黄 yellow_heart' }, { ch:'💚', kw:'心 绿 green_heart' },
        { ch:'💙', kw:'心 蓝 blue_heart' }, { ch:'💜', kw:'心 紫 purple_heart' },
        { ch:'🖤', kw:'心 黑 black_heart' }, { ch:'🤍', kw:'心 白 white_heart' },
        { ch:'💔', kw:'心碎 broken_heart' }, { ch:'💯', kw:'满分 100' },
        { ch:'💢', kw:'怒 anger' }, { ch:'💤', kw:'睡 zzz' },
        { ch:'💥', kw:'爆炸 boom' }, { ch:'💫', kw:'头晕 dizzy' },
        { ch:'✅', kw:'对 勾 check' }, { ch:'❌', kw:'错 叉 x cross' },
        { ch:'⚠️', kw:'警告 warning' }, { ch:'❓', kw:'问号 question' },
        { ch:'❗', kw:'感叹号 exclamation' }, { ch:'ℹ️', kw:'信息 info' },
        { ch:'➕', kw:'加 plus' }, { ch:'➖', kw:'减 minus' },
        { ch:'✖️', kw:'乘 multiply' }, { ch:'➗', kw:'除 divide' },
        { ch:'©️', kw:'版权 copyright' }, { ch:'®️', kw:'注册 registered' },
        { ch:'™️', kw:'商标 tm' }, { ch:'♻️', kw:'回收 recycle' },
        { ch:'⭕', kw:'圈 o' }, { ch:'🚫', kw:'禁止 prohibited' }
    ]}
];
```

- [ ] **Step 2: editorApi 加 openEmojiPicker / insertEmoji 方法**

在 `src/resources/web/editor.js` 的 `insertMermaid`（第 733-736 行）之后加：

```js
    // 打开 emoji 选择器浮层（C++ 侧传入定位坐标）
    openEmojiPicker: function(x, y) {
        window.editorApi._emojiPicker.open(x, y);
    },

    // 在光标处插入单个 emoji（供其它入口/测试复用）
    insertEmoji: function(ch) {
        if (this.editor) this.editor.commands.insertContent(ch);
    },
```

- [ ] **Step 3: 加 _emojiPicker 子对象**

在 `src/resources/web/editor.js` 的 `_mermaidEditor` 对象结束（第 1230 行的 `}`）之后、editorApi 对象结束（第 1231 行的 `};`）之前，插入下面整个 `_emojiPicker` 对象。注意：在 `_mermaidEditor` 的 `}` 后先加一个逗号，再贴入：

```js
,

    // ===== emoji 选择器浮层（非模态，锚定按钮下方，点外部/Esc 关闭） =====
    _emojiPicker: {
        root: null, searchInput: null, grid: null, tabBar: null,
        _recent: [], _curCat: 'smileys', _filter: '', _highlight: -1,
        _rendered: [], _charMap: null, _outsideHandler: null,
        RECENT_KEY: 'md-editor:emoji-recent', RECENT_MAX: 32, COLS: 10,

        open: function(x, y) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._loadRecent();
            this._filter = '';
            this.buildDom();
            this._curCat = (this._recent && this._recent.length) ? 'recent' : EMOJI_CATEGORIES[0].id;
            this._position(x, y);
            this._renderTabBar();
            this.render();
            this._registerOutside();
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            var self = this;
            setTimeout(function(){ if (self.searchInput) self.searchInput.focus(); }, 0);
        },

        _position: function(x, y) {
            var W = window.innerWidth, H = window.innerHeight;
            this.root.style.left = '0px'; this.root.style.top = '0px';
            var w = this.root.offsetWidth || 360;
            var h = this.root.offsetHeight || 360;
            var px = (typeof x === 'number') ? x : 16;
            var py = (typeof y === 'number') ? y : 60;
            if (px + w > W - 8) px = Math.max(8, W - w - 8);  // 右边溢出 → 右对齐
            if (py + h > H - 8) py = Math.max(8, H - h - 8);  // 底部溢出 → 上移
            this.root.style.left = px + 'px';
            this.root.style.top = py + 'px';
        },

        buildDom: function() {
            this._cleanupDom();
            var self = this;
            this.root = document.createElement('div');
            this.root.className = 'emoji-picker';

            var head = document.createElement('div');
            head.className = 'emoji-picker-head';
            var title = document.createElement('span');
            title.className = 'emoji-picker-title';
            title.textContent = '插入表情';
            head.appendChild(title);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'emoji-picker-close';
            closeBtn.type = 'button';
            closeBtn.textContent = '×';
            closeBtn.title = '关闭';
            closeBtn.addEventListener('click', function(e){ e.stopPropagation(); self.close(); });
            head.appendChild(closeBtn);
            this.root.appendChild(head);

            this.searchInput = document.createElement('input');
            this.searchInput.type = 'text';
            this.searchInput.className = 'emoji-picker-search';
            this.searchInput.placeholder = '搜索表情（如 笑 / heart / +1）';
            this.searchInput.addEventListener('input', function(){
                self._filter = this.value.trim().toLowerCase();
                self._curCat = self._filter ? '' : (self._curCat || EMOJI_CATEGORIES[0].id);
                self._renderTabBar();
                self.render();
            });
            this.searchInput.addEventListener('keydown', function(e){ self._onKeydown(e); });
            this.root.appendChild(this.searchInput);

            this.grid = document.createElement('div');
            this.grid.className = 'emoji-picker-grid';
            this.root.appendChild(this.grid);

            this.tabBar = document.createElement('div');
            this.tabBar.className = 'emoji-picker-tabs';
            this.root.appendChild(this.tabBar);

            // 浮层内 mousedown 不冒泡到 document（避免点内部误触发关闭）
            this.root.addEventListener('mousedown', function(e){ e.stopPropagation(); }, true);
            document.body.appendChild(this.root);
        },

        render: function() {
            var list;
            if (this._filter) {
                list = [];
                for (var i = 0; i < EMOJI_CATEGORIES.length; i++) {
                    var ems = EMOJI_CATEGORIES[i].emojis;
                    for (var j = 0; j < ems.length; j++) {
                        if ((' ' + ems[j].kw).toLowerCase().indexOf(this._filter) >= 0) list.push(ems[j]);
                    }
                }
            } else {
                list = this._getCat(this._curCat).emojis;
            }
            this._rendered = list;
            this._highlight = list.length ? 0 : -1;
            this.grid.innerHTML = '';
            var self = this;
            for (var k = 0; k < list.length; k++) {
                (function(item, idx){
                    var btn = document.createElement('button');
                    btn.type = 'button';
                    btn.className = 'emoji-cell';
                    btn.textContent = item.ch;
                    btn.title = item.kw;
                    if (idx === self._highlight) btn.classList.add('active');
                    btn.addEventListener('click', function(e){ e.stopPropagation(); self._insert(item); });
                    self.grid.appendChild(btn);
                })(list[k], k);
            }
            if (!list.length) {
                var empty = document.createElement('div');
                empty.className = 'emoji-empty';
                empty.textContent = '无匹配表情';
                this.grid.appendChild(empty);
            }
        },

        _renderTabBar: function() {
            var self = this;
            this.tabBar.innerHTML = '';
            var addTab = function(cat){
                var b = document.createElement('button');
                b.type = 'button';
                b.className = 'emoji-tab';
                b.textContent = cat.icon;
                b.title = cat.name;
                if (cat.id === self._curCat && !self._filter) b.classList.add('active');
                b.addEventListener('click', function(e){
                    e.stopPropagation();
                    self._filter = '';
                    if (self.searchInput) self.searchInput.value = '';
                    self._curCat = cat.id;
                    self._renderTabBar();
                    self.render();
                });
                self.tabBar.appendChild(b);
            };
            if (this._recent && this._recent.length) addTab({ id:'recent', icon:'🕘', name:'最近使用' });
            for (var i = 0; i < EMOJI_CATEGORIES.length; i++) addTab(EMOJI_CATEGORIES[i]);
        },

        _getCat: function(id) {
            if (id === 'recent') {
                var cm = this._getCharMap(), arr = [];
                for (var i = 0; i < this._recent.length; i++) {
                    var ch = this._recent[i];
                    arr.push(cm[ch] || { ch: ch, kw: ch });
                }
                return { id:'recent', icon:'🕘', name:'最近', emojis: arr };
            }
            for (var i = 0; i < EMOJI_CATEGORIES.length; i++)
                if (EMOJI_CATEGORIES[i].id === id) return EMOJI_CATEGORIES[i];
            return EMOJI_CATEGORIES[0];
        },

        _getCharMap: function() {
            if (this._charMap) return this._charMap;
            var m = {};
            for (var i = 0; i < EMOJI_CATEGORIES.length; i++)
                for (var j = 0; j < EMOJI_CATEGORIES[i].emojis.length; j++) {
                    var e = EMOJI_CATEGORIES[i].emojis[j];
                    m[e.ch] = e;
                }
            this._charMap = m;
            return m;
        },

        _insert: function(item) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            ed.commands.insertContent(item.ch);
            this.addToRecent(item.ch);
            // 插入后清空搜索、回到当前分类视图（便于连插）
            this._filter = '';
            if (this.searchInput) this.searchInput.value = '';
            this._renderTabBar();
            this.render();
            if (this.searchInput) this.searchInput.focus();
        },

        _onKeydown: function(e) {
            var n = this._rendered.length;
            if (!n) return;
            var c = this.COLS;
            if (e.key === 'ArrowRight') { e.preventDefault(); this._highlight = (this._highlight + 1) % n; this._applyHighlight(); }
            else if (e.key === 'ArrowLeft') { e.preventDefault(); this._highlight = (this._highlight - 1 + n) % n; this._applyHighlight(); }
            else if (e.key === 'ArrowDown') { e.preventDefault(); this._highlight = (this._highlight + c) % n; this._applyHighlight(); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); this._highlight = (this._highlight - c + n) % n; this._applyHighlight(); }
            else if (e.key === 'Enter') { e.preventDefault(); var it = this._rendered[this._highlight] || this._rendered[0]; if (it) this._insert(it); }
        },

        _applyHighlight: function() {
            var cells = this.grid.querySelectorAll('.emoji-cell');
            for (var i = 0; i < cells.length; i++) {
                if (i === this._highlight) { cells[i].classList.add('active'); cells[i].scrollIntoView({ block:'nearest' }); }
                else cells[i].classList.remove('active');
            }
        },

        addToRecent: function(ch) {
            var idx = this._recent.indexOf(ch);
            if (idx >= 0) this._recent.splice(idx, 1);
            this._recent.unshift(ch);
            while (this._recent.length > this.RECENT_MAX) this._recent.pop();
            this._saveRecent();
        },

        _loadRecent: function() {
            try {
                var s = localStorage.getItem(this.RECENT_KEY);
                this._recent = s ? JSON.parse(s) : [];
                if (!Array.isArray(this._recent)) this._recent = [];
            } catch (e) { this._recent = []; }
        },

        _saveRecent: function() {
            try { localStorage.setItem(this.RECENT_KEY, JSON.stringify(this._recent)); } catch (e) {}
        },

        _registerOutside: function() {
            if (this._outsideHandler) return;
            var self = this;
            this._outsideHandler = function(e) {
                if (self.root && !self.root.contains(e.target)) self.close();
            };
            document.addEventListener('mousedown', this._outsideHandler, true);
        },

        _unregisterOutside: function() {
            if (this._outsideHandler) {
                document.removeEventListener('mousedown', this._outsideHandler, true);
                this._outsideHandler = null;
            }
        },

        _cleanupDom: function() {
            if (this.root && this.root.parentNode) this.root.parentNode.removeChild(this.root);
            this.root = null; this.searchInput = null; this.grid = null; this.tabBar = null;
            this._rendered = []; this._highlight = -1;
        },

        close: function() {
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
            this._unregisterOutside();
            this._cleanupDom();
            var ed = window.editorApi.editor;
            if (ed) ed.view.focus();
        }
    }
```

- [ ] **Step 4: editor.css 追加浮层样式**

在 `src/resources/web/editor.css` 末尾追加：

```css

/* ===== 表情符号选择器（非模态浮层） ===== */
.emoji-picker {
    position: fixed;
    width: 360px;
    max-height: 420px;
    background: #ffffff;
    border-radius: 10px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.18);
    border: 1px solid #e0e0e0;
    z-index: 1100;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Color Emoji",
                 "Apple Color Emoji", "Segoe UI Emoji", sans-serif;
}
.emoji-picker-head {
    display: flex; align-items: center; justify-content: space-between;
    padding: 8px 10px 4px;
}
.emoji-picker-title { font-size: 13px; color: #666; }
.emoji-picker-close {
    width: 26px; height: 26px; border: none; background: transparent;
    font-size: 18px; line-height: 1; color: #888; cursor: pointer; border-radius: 4px;
}
.emoji-picker-close:hover { background: #eee; color: #333; }
.emoji-picker-search {
    margin: 4px 10px 8px; padding: 7px 10px;
    border: 1px solid #d0d0d0; border-radius: 6px;
    font-size: 14px; outline: none; color: #222; background: #fafafa;
}
.emoji-picker-search:focus {
    border-color: #0078d4; background: #fff; box-shadow: 0 0 0 2px rgba(0,120,212,0.15);
}
.emoji-picker-grid {
    flex: 1 1 auto; overflow-y: auto;
    display: grid; grid-template-columns: repeat(10, 1fr); gap: 2px;
    padding: 4px 8px; min-height: 180px; max-height: 300px;
}
.emoji-cell {
    font-size: 22px; line-height: 1; padding: 4px 0;
    border: none; background: transparent; cursor: pointer;
    border-radius: 5px; text-align: center;
}
.emoji-cell:hover, .emoji-cell.active { background: #eef3fb; }
.emoji-empty { grid-column: 1 / -1; text-align: center; color: #999; font-size: 14px; padding: 30px 0; }
.emoji-picker-tabs {
    display: flex; flex-wrap: wrap; gap: 2px;
    padding: 6px 8px; border-top: 1px solid #eee; background: #fafafa;
}
.emoji-tab {
    font-size: 18px; padding: 3px 5px; border: none; background: transparent;
    cursor: pointer; border-radius: 5px; line-height: 1;
}
.emoji-tab:hover { background: #e6e6e6; }
.emoji-tab.active { background: #dde9f7; }

body.dark .emoji-picker { background: #2d2d2d; border-color: #3d3d3d; box-shadow: 0 8px 32px rgba(0,0,0,0.5); }
body.dark .emoji-picker-title { color: #aaa; }
body.dark .emoji-picker-close { color: #999; }
body.dark .emoji-picker-close:hover { background: #3d3d3d; color: #ddd; }
body.dark .emoji-picker-search { background: #1a1a1a; color: #e0e0e0; border-color: #555; }
body.dark .emoji-picker-search:focus { border-color: #8ab4f8; background: #1e1e1e; box-shadow: 0 0 0 2px rgba(138,180,248,0.2); }
body.dark .emoji-cell:hover, body.dark .emoji-cell.active { background: #3a4a5a; }
body.dark .emoji-picker-tabs { background: #262626; border-top-color: #3d3d3d; }
body.dark .emoji-tab:hover { background: #3d3d3d; }
body.dark .emoji-tab.active { background: #2a3f5f; }
```

- [ ] **Step 5: 构建验证（资源更新）**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 构建成功（editor.js/css 是纯资源，AUTORCC 重新打包）。

- [ ] **Step 6: 浏览器独立验证浮层（无需 C++ 入口）**

用浏览器打开 `src/resources/web/editor.html`（QWebChannel 未连接时会打印警告，可忽略），在控制台执行：

```js
window.editorApi.openEmojiPicker(120, 120);
```

Expected：
- 浮层出现在左上角附近，含搜索框、网格、分类标签。
- 控制台输入 `window.editorApi._emojiPicker.searchInput.value = '心'; window.editorApi.searchInput.dispatchEvent(new Event('input'));` → 网格过滤出心形类。
- 点击某 emoji → 该字符追加进编辑区光标处；再次 `openEmojiPicker` 后首部分类变为「🕘 最近使用」。
- 点浮层外部 → 关闭；刷新页面重开 → 「最近使用」仍保留（localStorage 生效）。

> 注：浏览器里点击 emoji 是否插入取决于 TipTap 是否初始化成功（editor.html 应已加载 tiptap-all.js）。若未初始化，至少验证浮层 UI、搜索、分类、关闭、最近使用逻辑正确。

- [ ] **Step 7: 提交**

```bash
git add src/resources/web/editor.js src/resources/web/editor.css
git commit -m "feat: emoji 选择器浮层（分类+搜索+最近使用，非模态）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: MainWindow 编排 + Esc 分支

**Files:**
- Modify: `src/ui/MainWindow.h`（private slots 加 `onInsertEmoji()` 声明）
- Modify: `src/ui/MainWindow.cpp`：
  - `connectSignals()`（第 214 行附近工具栏信号区）加工具栏/菜单连接。
  - 新增 `onInsertEmoji()` 实现。
  - Esc 分支（第 108-111 行）的 JS 串追加 `window.editorApi._emojiPicker.close();`。

**Interfaces:**
- Consumes: Task 2 的 `ToolBar::emojiClicked`、Task 3 的 `MenuBar::insertEmoji`、Task 4 的 `window.editorApi.openEmojiPicker(x,y)` 与 `window.editorApi._emojiPicker.close()`。

- [ ] **Step 1: MainWindow.h 声明槽**

在 `src/ui/MainWindow.h` 的 `private slots:` 区，「工具栏响应」分组里 `onInsertImage();`（第 33 行）之后加：

```cpp
    void onInsertEmoji();
```

- [ ] **Step 2: connectSignals 连接工具栏与菜单**

在 `src/ui/MainWindow.cpp` 的 `connectSignals()` 中，工具栏 `mermaidClicked` 连接处（或任意工具栏插入类连接旁）加：

```cpp
    connect(m_toolBar, &ToolBar::emojiClicked, this, [this]() { onInsertEmoji(); });
    connect(m_menuBar, &MenuBar::insertEmoji, this, [this]() { onInsertEmoji(); });
```

> 放在工具栏信号连接区块内即可（参考已有 `connect(m_toolBar, &ToolBar::mermaidClicked, ...)` 的位置）。

- [ ] **Step 3: 实现 onInsertEmoji**

在 `src/ui/MainWindow.cpp` 中（与 `onInsertTable` 实现相邻处）加：

```cpp
void MainWindow::onInsertEmoji()
{
    EditorTab *tab = m_tabWidget->currentEditorTab();
    if (!tab) return;
    if (tab->isPermanentReadOnly()) return;  // 只读模式不弹选择器

    // 锚点：工具栏 emoji 按钮左下角；菜单触发时无按钮，用光标位置兜底
    QAction *a = m_toolBar->findChild<QAction *>("emojiAction");
    QWidget *btn = a ? m_toolBar->widgetForAction(a) : nullptr;
    QPoint gbl = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
    QPoint local = tab->webView()->mapFromGlobal(gbl);  // 默认 zoom=1.0：CSS px = 部件 px
    QString js = QString("window.editorApi.openEmojiPicker(%1, %2);")
                     .arg(local.x()).arg(local.y());
    tab->runJsCommand(js);
}
```

- [ ] **Step 4: Esc 分支追加关闭浮层**

在 `src/ui/MainWindow.cpp` 的 Esc 快捷键 lambda（第 108-111 行）里，把：

```cpp
            runEditorCommand("window.editorApi._mermaidEditor.close();"
                             "window.editorApi._mathEditor.close();");
```

改为：

```cpp
            runEditorCommand("window.editorApi._mermaidEditor.close();"
                             "window.editorApi._mathEditor.close();"
                             "window.editorApi._emojiPicker.close();");
```

- [ ] **Step 5: 构建验证**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 编译通过。

- [ ] **Step 6: 运行 ctest 回归**

Run: `ctest --test-dir /home/daijd/claude-workspace/md-editor/build --output-on-failure`
Expected: 全部已有测试通过。

- [ ] **Step 7: 运行应用做集成手动验证**

Run: `/home/daijd/claude-workspace/md-editor/build/src/md-editor`（或 `./scripts/run-editor.sh`）

按 spec §9 清单逐项核对：
1. 工具栏出现 emoji 图标；切亮/暗主题图标颜色正确。
2. 菜单「插入 > 表情符号...」存在。
3. 点工具栏图标 → 浮层在按钮正下方，不遮挡/暗化编辑区。
4. 搜索「心」「heart」「+1」→ 实时过滤命中。
5. 点分类标签 → 切换分类。
6. 方向键移动高亮、Enter 插入；点 emoji 插入。
7. 插入后浮层保持，可连插；emoji 落在原光标处。
8. Esc → 关闭；点编辑区/外部 → 关闭。
9. 切换文档标签页时浮层关闭（切 tab 前先点开浮层，切 tab 后浮层应消失——若残留，记录为待修）。
10. 只读模式下点工具栏/菜单入口无浮层弹出。
11. emoji 保存重开后仍正确。

> 若第 7 项「emoji 落在原光标处」失败（例如插到了错误位置），原因是 TipTap 失焦后选区漂移：把 `_insert` 里 `ed.commands.insertContent(item.ch)` 之前临时 `ed.view.focus()`、插入后再把焦点转回搜索框。本计划默认信任 TipTap 失焦插入行为，仅在验证失败时启用此回退。

- [ ] **Step 8: 提交**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat: 接线 emoji 选择器入口（工具栏+菜单+定位+Esc 关闭）

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: 文档与版本号

**Files:**
- Modify: `CMakeLists.txt`（第 2 行版本号）
- Modify: `docs/modal-dialog-esc-handling.md`（补充 emoji 浮层复用标记说明）

**Interfaces:** 无（收尾任务）。

- [ ] **Step 1: 推进版本号**

在 `CMakeLists.txt` 第 2 行，把：

```
project(md-editor VERSION 0.3.1 LANGUAGES CXX)
```

改为：

```
project(md-editor VERSION 0.3.2 LANGUAGES CXX)
```

- [ ] **Step 2: 补充 Esc 文档**

在 `docs/modal-dialog-esc-handling.md` 的「## 教训」之前，加一节：

```markdown
---

## emoji 选择器浮层对 Esc 标记的复用

emoji 选择器是**非模态浮层**（无遮罩、不锁定滚动），但同样需要 Esc 关闭。它复用了 `bridge.modalDialogChanged(true/false)` 与 `MainWindow::m_modalDialogOpen` 标记：

- 浮层 `open()` 调 `modalDialogChanged(true)`，`close()` 第一时间调 `modalDialogChanged(false)`。
- 因此 `m_modalDialogOpen` 的语义从「模态对话框打开」放宽为「某个 JS 浮层/面板打开、希望接管 Esc」。
- 因浮层无遮罩，编辑区仍可交互；按 Esc 时 `m_modalDialogOpen == true` → Esc 分支关闭浮层（而非隐藏搜索栏），行为正确。
- MainWindow 的 Esc 分支 JS 串因此追加 `window.editorApi._emojiPicker.close();`。
```

- [ ] **Step 3: 构建验证（版本宏更新）**

Run: `cmake --build /home/daijd/claude-workspace/md-editor/build -j$(nproc)`
Expected: 构建成功，`MD_EDITOR_VERSION` 宏更新为 `0.3.2`。

- [ ] **Step 4: 提交**

```bash
git add CMakeLists.txt docs/modal-dialog-esc-handling.md
git commit -m "chore: 版本推进 0.3.2；补充 emoji 浮层 Esc 复用文档

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Self-Review 结论

- **Spec 覆盖**：分类+搜索（Task 4 render/filter/_renderTabBar）、最近使用（addToRecent/_load/_save + open 默认 + tab）、非模态浮层锚定（Task 5 onInsertEmoji 定位 + Task 4 _position 钳制）、点外部关闭（_registerOutside）、Esc 关闭（Task 5 Esc 分支 + Task 4 modalDialogChanged）、连插（_insert 保持打开）、只读拦截（onInsertEmoji isPermanentReadOnly）、图标 light/dark（Task 1）、菜单（Task 3）、主题图标（makeAction 自动）、版本 0.3.2（Task 6）、文档（Task 6）—— spec 各节均有对应任务。
- **占位符扫描**：所有代码块为可直接使用的真实代码；EMOJI_CATEGORIES 为约 230 条真实 emoji 数据（非示例）。
- **类型/命名一致性**：objectName `emojiAction`（Task 2 创建、Task 5 findChild）一致；`_emojiPicker.open/close`、`openEmojiPicker`、`insertEmoji` 在 Task 4 定义、Task 5 调用一致；`emojiClicked`/`insertEmoji` 信号在 Task 2/3 定义、Task 5 连接一致。
