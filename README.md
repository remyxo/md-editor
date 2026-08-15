# Markdown Editor

基于 **Qt6 + TipTap/ProseMirror** 的桌面端 Markdown 所见即所得（WYSIWYG）编辑器。

## 功能

- **WYSIWYG 编辑** — 实时渲染，所见即所得，支持源码模式切换
- **完整 Markdown 语法** — 标题、加粗、斜体、删除线、代码、引用、列表、任务列表、表格、分割线、链接、图片
- **文件管理** — 左侧文件树浏览，多标签页编辑
- **大纲导航** — 实时解析文档标题结构，点击跳转
- **导出** — PDF（Chromium 渲染）、DOCX、自包含 HTML 单文件
- **双主题** — 亮色/暗色一键切换
- **会话恢复** — 关闭时保存状态，启动时恢复标签页和光标位置
- **表格操作** — 10×10 网格拖选插入，支持行列增删
- **中文支持** — 右键菜单、界面语言均为简体中文
- **跨平台** — 基于 Qt6，支持 Linux/Windows/macOS

## 技术栈

| 层次 | 技术 |
|------|------|
| GUI 框架 | Qt 6.4+ (Widgets + WebEngine + WebChannel) |
| 编辑器内核 | TipTap v3 (基于 ProseMirror) |
| Markdown 转换 | QTextDocument (setMarkdown/toMarkdown) |
| 语法高亮 | highlight.js |
| 构建系统 | CMake 3.20+ |
| 语言 | C++17 / JavaScript |
| 打包 | CPack (DEB) |

## 构建

### 依赖

**Debian/Ubuntu:**
```bash
sudo apt install cmake g++ qt6-base-dev qt6-webengine-dev qt6-webchannel-dev
```

### 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 测试

```bash
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

### 打包 DEB

```bash
cd build
cpack -G DEB
```

## 运行

```bash
./scripts/run-editor.sh
```

## 项目结构

```
.
├── CMakeLists.txt          # 顶层构建配置 + CPack 打包
├── src/
│   ├── main.cpp            # 程序入口
│   ├── app/                # Application、ThemeManager
│   ├── core/               # EditorBridge、FileManager、MarkdownHandler、PdfExporter
│   ├── ui/                 # MainWindow、MenuBar、ToolBar、SideBar、EditorTab …
│   └── resources/
│       ├── icons/           # SVG 工具栏图标
│       ├── themes/          # light.qss / dark.qss
│       └── web/             # TipTap 编辑器前端 (editor.html/js/css)
├── tests/                  # Qt Test 单元测试
├── scripts/                # 启动/打包辅助脚本
├── docs/                   # 设计文档
└── .github/workflows/      # GitHub Actions CI/CD
```

## License

MIT
