// 全局编辑器 API 对象（C++ 侧通过 QWebChannel 调用其中的命令方法）
window.editorApi = {
    editor: null,
    bridge: null,
    _dirty: false,
    _loading: false,
    _notifyDirty: function() {
        if (this.bridge) this.bridge.contentChanged(!!this._dirty);
    },

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
        var TableKit = window.TipTapExtensionTableKit;
        var TaskList = window.TipTapExtensionTaskList;
        var TaskItem = window.TipTapExtensionTaskItem;
        var Highlight = window.TipTapExtensionHighlight;
        var Image = window.TipTapExtensionImage;
        var Placeholder = window.TipTapExtensionPlaceholder;
        var CodeBlockLowlight = window.TipTapExtensionCodeBlockLowlight;
        // v3 StarterKit includes Link and Underline

        var extensions = [
            StarterKit.configure({
                codeBlock: false,
                heading: { levels: [1, 2, 3, 4, 5, 6] }
            }),
            TableKit.configure({ resizable: true }),
            TaskList,
            TaskItem.configure({ nested: true }),
            Highlight,
            Image.configure({ allowBase64: true }),
            Placeholder.configure({ placeholder: '开始编写 Markdown...' }),
        ];

        // 语法高亮代码块（StarterKit 中已禁用默认 codeBlock，这里使用 lowlight 版本）
        if (CodeBlockLowlight && window.lowlight) {
            extensions.push(CodeBlockLowlight.configure({ lowlight: window.lowlight }));
        }

        this.editor = new Editor({
            element: document.querySelector('#editor'),
            extensions: extensions,
            onUpdate: function(args) {
                if (this._loading) return;  // setContent 触发的更新忽略
                if (!this._dirty) {
                    this._dirty = true;
                    if (this.bridge) this.bridge.contentChanged(true);
                }
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
                    this.notifyFormat();
                }
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
                headings.push({ level: node.attrs.level, text: node.textContent || '', pos: pos });
            }
        });
        this.bridge.headingsChanged(JSON.stringify(headings));
    },

    notifyFormat: function() {
        if (!this.editor || !this.bridge) return;
        var ed = this.editor;
        var format = {};
        // 标题级别
        if (ed.isActive('heading')) {
            for (var lv = 1; lv <= 6; lv++) {
                if (ed.isActive('heading', { level: lv })) { format.heading = lv; break; }
            }
        } else {
            format.heading = 0;
        }
        // 文本格式
        format.bold = !!ed.isActive('bold');
        format.italic = !!ed.isActive('italic');
        format.strike = !!ed.isActive('strike');
        format.code = !!ed.isActive('code');
        format.underline = !!ed.isActive('underline');
        format.highlight = !!ed.isActive('highlight');
        // 结构
        format.bulletList = !!ed.isActive('bulletList');
        format.orderedList = !!ed.isActive('orderedList');
        format.taskList = !!ed.isActive('taskList');
        format.blockquote = !!ed.isActive('blockquote');
        format.codeBlock = !!ed.isActive('codeBlock');
        // 链接和表格
        format.link = !!ed.isActive('link');
        format.table = !!ed.isActive('table');

        this.bridge.formatChanged(JSON.stringify(format));
    },

    // C++ 侧调用的命令
    getContent: function() { return this.editor ? this.editor.getHTML() : ''; },
    setContent: function(html) {
        if (!this.editor) return;
        this._loading = true;
        this._dirty = false;
        this.editor.commands.setContent(html);
        this._loading = false;
        if (this.bridge) this.bridge.contentChanged(false);
    },
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
    insertCodeBlock: function() {
        if (this.editor) this.editor.commands.toggleCodeBlock();
    },
    addRowBefore: function() { if (this.editor) this.editor.commands.addRowBefore(); },
    addRowAfter: function() { if (this.editor) this.editor.commands.addRowAfter(); },
    deleteRow: function() { if (this.editor) this.editor.commands.deleteRow(); },
    addColumnBefore: function() { if (this.editor) this.editor.commands.addColumnBefore(); },
    addColumnAfter: function() { if (this.editor) this.editor.commands.addColumnAfter(); },
    deleteColumn: function() { if (this.editor) this.editor.commands.deleteColumn(); },
    deleteTable: function() { if (this.editor) this.editor.commands.deleteTable(); },
    isInTable: function() { return this.editor ? this.editor.isActive('table') : false; },
    insertLink: function(url) {
        if (!this.editor) return;
        if (url) { this.editor.commands.setLink({ href: url }); return; }
        var u = prompt('输入链接地址:');
        if (u) this.editor.commands.setLink({ href: u });
    },
    insertImage: function(path) { if (this.editor) this.editor.commands.setImage({ src: path }); },
    insertHorizontalRule: function() { if (this.editor) this.editor.commands.setHorizontalRule(); },
    toggleSourceMode: function() {
        if (!this.editor) return;
        var editorDiv = document.getElementById('editor');
        var sourceView = document.getElementById('source-view');
        if (this.isSourceMode) {
            // 切回 WYSIWYG
            this._loading = true;
            this._dirty = false;
            this.editor.commands.setContent(sourceView.value);
            this._loading = false;
            sourceView.style.display = 'none';
            editorDiv.style.display = '';
            this.isSourceMode = false;
        } else {
            // 切到源码视图
            sourceView.value = this.editor.getHTML();
            editorDiv.style.display = 'none';
            sourceView.style.display = '';
            this.isSourceMode = true;
        }
    },
    scrollToPos: function(pos) { if (this.editor) this.editor.commands.setTextSelection(pos); },
    undo: function() { if (this.editor) this.editor.commands.undo(); },
    redo: function() { if (this.editor) this.editor.commands.redo(); },
    setTheme: function(theme) {
        if (theme === 'dark') document.body.classList.add('dark');
        else document.body.classList.remove('dark');
    },
    setFont: function(family, size) {
        if (family) document.body.style.fontFamily = family;
        if (size) document.body.style.fontSize = size + 'px';
    },
    markClean: function() {
        this._dirty = false;
        if (this.bridge) this.bridge.contentChanged(false);
    },
};

// DOM 加载完成后初始化
document.addEventListener('DOMContentLoaded', function() {
    window.editorApi.init();
    // init 内部可能触发 onUpdate，强制重置为未修改
    window.editorApi._dirty = false;
    window.editorApi._loading = false;
    // QWebChannel 初始化
    if (typeof QWebChannel !== 'undefined') {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.editorApi.bridge = channel.objects.bridge;
            // 桥接就绪后同步脏状态（新文档始终是干净的）
            window.editorApi._notifyDirty();
        });
    } else {
        console.warn('QWebChannel 未连接：请在浏览器中独立测试时忽略此警告。');
    }
});
