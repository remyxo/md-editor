// 全局编辑器 API 对象（C++ 侧通过 QWebChannel 调用其中的命令方法）
window.editorApi = {
    editor: null,
    bridge: null,

    init: function() {
        var TipTap = window.TipTap;
        if (!TipTap || !TipTap.Editor) {
            console.error('TipTap bundle 未加载，请确认 tiptap-all.js 已正确引入。');
            document.querySelector('#editor').textContent =
                '编辑器内核加载失败：未找到 window.TipTap。请检查 tiptap-all.js。';
            return;
        }

        var Editor = TipTap.Editor;
        var StarterKit = window.TipTapStarterKit;
        var Table = window.TipTapExtensionTable;
        var TaskList = window.TipTapExtensionTaskList;
        var TaskItem = window.TipTapExtensionTaskItem;
        var Highlight = window.TipTapExtensionHighlight;
        var Image = window.TipTapExtensionImage;
        var Link = window.TipTapExtensionLink;
        var Placeholder = window.TipTapExtensionPlaceholder;
        var Underline = window.TipTapExtensionUnderline;
        var CodeBlockLowlight = window.TipTapExtensionCodeBlockLowlight;

        var extensions = [
            StarterKit.configure({
                codeBlock: false,
                heading: { levels: [1, 2, 3, 4, 5, 6] }
            }),
            Table.configure({ resizable: true }),
            TaskList,
            TaskItem.configure({ nested: true }),
            Highlight,
            Image.configure({ allowBase64: true }),
            Link.configure({ openOnClick: false }),
            Placeholder.configure({ placeholder: '开始编写 Markdown...' }),
            Underline,
        ];

        // 语法高亮代码块（StarterKit 中已禁用默认 codeBlock，这里使用 lowlight 版本）
        if (CodeBlockLowlight && window.lowlight) {
            extensions.push(CodeBlockLowlight.configure({ lowlight: window.lowlight }));
        }

        this.editor = new Editor({
            element: document.querySelector('#editor'),
            extensions: extensions,
            onUpdate: function(args) {
                var editor = args.editor;
                if (this.bridge) this.bridge.contentChanged(editor.isDirty);
                this.notifyHeadings();
            }.bind(this),
            onSelectionUpdate: function(args) {
                var editor = args.editor;
                if (this.bridge) {
                    var pos = editor.state.selection.head;
                    var line = 1, col = 1;
                    editor.state.doc.nodesBetween(0, pos, function(node, start) {
                        if (start + node.nodeSize <= pos) {
                            var lines = node.textContent.slice(0, pos - start).split('\n');
                            line += lines.length - 1;
                            col = lines[lines.length - 1].length + 1;
                        }
                    });
                    this.bridge.cursorChanged(line, col);
                }
                // 更新标题结构
                this.notifyHeadings();
            }.bind(this),
        });

        this.notifyHeadings();
    },

    notifyHeadings: function() {
        if (!this.editor || !this.bridge) return;
        var headings = [];
        this.editor.state.doc.descendants(function(node, pos) {
            if (node.type.name === 'heading') {
                headings.push({ level: node.attrs.level, text: node.textContent, pos: pos });
            }
        });
        this.bridge.headingsChanged(JSON.stringify(headings));
    },

    // C++ 侧调用的命令
    getContent: function() { return this.editor ? this.editor.getHTML() : ''; },
    setContent: function(html) { if (this.editor) this.editor.commands.setContent(html); },
    toggleBold: function() { if (this.editor) this.editor.commands.toggleBold(); },
    toggleItalic: function() { if (this.editor) this.editor.commands.toggleItalic(); },
    toggleStrike: function() { if (this.editor) this.editor.commands.toggleStrike(); },
    toggleCode: function() { if (this.editor) this.editor.commands.toggleCode(); },
    toggleUnderline: function() { if (this.editor) this.editor.commands.toggleUnderline(); },
    toggleHighlight: function() { if (this.editor) this.editor.commands.toggleHighlight(); },
    setHeading: function(level) {
        if (!this.editor) return;
        if (level === 0) this.editor.commands.setParagraph();
        else this.editor.commands.toggleHeading({ level: level });
    },
    toggleBulletList: function() { if (this.editor) this.editor.commands.toggleBulletList(); },
    toggleOrderedList: function() { if (this.editor) this.editor.commands.toggleOrderedList(); },
    toggleTaskList: function() { if (this.editor) this.editor.commands.toggleTaskList(); },
    toggleBlockquote: function() { if (this.editor) this.editor.commands.toggleBlockquote(); },
    insertTable: function(rows, cols) {
        if (this.editor) this.editor.commands.insertTable({ rows: rows || 3, cols: cols || 3, withHeaderRow: true });
    },
    insertLink: function(url) {
        if (!this.editor) return;
        if (url) { this.editor.commands.setLink({ href: url }); return; }
        var u = prompt('输入链接地址:');
        if (u) this.editor.commands.setLink({ href: u });
    },
    insertImage: function(path) { if (this.editor) this.editor.commands.setImage({ src: path }); },
    insertHorizontalRule: function() { if (this.editor) this.editor.commands.setHorizontalRule(); },
    toggleSourceMode: function() {
        if (this.editor) console.log('Source mode - content:', this.editor.getHTML());
    },
    scrollToPos: function(pos) { if (this.editor) this.editor.commands.setTextSelection(pos); },
    undo: function() { if (this.editor) this.editor.commands.undo(); },
    redo: function() { if (this.editor) this.editor.commands.redo(); },
    setTheme: function(theme) {
        if (theme === 'dark') document.body.classList.add('dark');
        else document.body.classList.remove('dark');
    },
};

// DOM 加载完成后初始化
document.addEventListener('DOMContentLoaded', function() {
    window.editorApi.init();
    // QWebChannel 初始化（由 C++ 侧在页面加载后注入 qt.webChannelTransport）
    if (typeof QWebChannel !== 'undefined') {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.editorApi.bridge = channel.objects.bridge;
        });
    } else {
        console.warn('QWebChannel 未连接：请在浏览器中独立测试时忽略此警告。');
    }
});
