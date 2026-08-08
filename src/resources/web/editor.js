// TipTap 全局引用（由 tiptap-all.js 设置，模块级别持有避免 init/setContent 重复声明）
var TipTapGlobal = window.TipTap;
var TipTapEditor = TipTapGlobal && TipTapGlobal.Editor;
var TipTapStarterKit = window.TipTapStarterKit;
var TipTapTableKit = window.TipTapExtensionTableKit;
var TipTapTaskList = window.TipTapExtensionTaskList;
var TipTapTaskItem = window.TipTapExtensionTaskItem;
var TipTapHighlight = window.TipTapExtensionHighlight;
var TipTapImage = window.TipTapExtensionImage;
var TipTapPlaceholder = window.TipTapExtensionPlaceholder;
var TipTapCodeBlockLowlight = window.TipTapExtensionCodeBlockLowlight;
var TipTapMathematics = window.TipTapExtensionMathematics;
var TipTapSubscript = window.TipTapExtensionSubscript;
var TipTapSuperscript = window.TipTapExtensionSuperscript;
var TipTapTextAlign = window.TipTapExtensionTextAlign;
var TipTapExtension = window.TipTapExtension;
var TipTapInputRule = window.TipTapInputRule;
var TipTapNode = window.TipTapNode;

// Mermaid 渲染实例的 id 序列（每次 render 需唯一 id）
var _mermaidIdSeq = 0;
// 活动中的 Mermaid NodeView 画笔集合，用于主题切换后重绘
var _mermaidViews = [];

// HTML 文本转义（渲染失败回退 / 错误信息展示用）
function _escapeText(s) {
    return String(s == null ? '' : s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

// 修复 Mermaid 生成的 SVG 中负宽（高）的 <rect> 元素。
// Mermaid v11 对 CJK 字符宽度计算存在偏差（issue #4950），
// mindmap 等图的文本测量结果偏小 → 节点背景矩形 width 可能为负
// → 浏览器报 "A negative value is not valid"。
// 此处用纯字符串替换将负值 clamp 为 0（不依赖 DOMParser，避免
// Chromium 中 parseFromString 处理 SVG 命名空间的行为差异）。
function _sanitizeMermaidSvg(svgString) {
    if (!svgString) return svgString;
    if (svgString.indexOf('width="-') === -1 && svgString.indexOf('height="-') === -1)
        return svgString;
    return svgString
        .replace(/width="-[\d.]+"/g, 'width="0"')
        .replace(/height="-[\d.]+"/g, 'height="0"');
}

// 懒初始化 mermaid（按当前明暗主题），保证只初始化一次
function _ensureMermaidInited() {
    if (!window.mermaid || window.__mermaidInited) return;
    var dark = document.body.classList.contains('dark');
    window.mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'loose',
        theme: dark ? 'dark' : 'default',
        // 图表字号：编辑区正文为 18px，图表文字略小
        // 图表字号：编辑区正文为 18px，图表文字明显略小一档
        themeVariables: { fontSize: 13 }
    });
    window.__mermaidInited = true;
}

// Emoji 短代码 → Unicode 映射（输入 :shortcode: 自动替换为对应 emoji 字符）
var EMOJI_SHORTCODES = {
    // 笑脸 / 情绪
    'smile': '😄', 'laughing': '😆', 'joy': '😂', 'grin': '😁', 'wink': '😉',
    'blush': '😊', 'heart_eyes': '😍', 'kissing_heart': '😘', 'stuck_out_tongue': '😝',
    'relaxed': '☺️', 'thinking': '🤔', 'neutral_face': '😐', 'expressionless': '😑',
    'unamused': '😒', 'sweat_smile': '😅', 'sweat': '😓', 'disappointed': '😞',
    'confused': '😕', 'pensive': '😔', 'persevere': '😣', 'weary': '😩',
    'cry': '😢', 'sob': '😭', 'angry': '😠', 'rage': '😡', 'sleepy': '😴',
    'mask': '😷', 'nerd_face': '🤓', 'cold_sweat': '😰', 'scream': '😱',
    'flushed': '😳', 'dizzy_face': '😵', 'astonished': '😲', 'open_mouth': '😮',
    // 心形 / 符号
    'heart': '❤️', 'orange_heart': '🧡', 'yellow_heart': '💛', 'green_heart': '💚',
    'blue_heart': '💙', 'purple_heart': '💜', 'black_heart': '🖤', 'broken_heart': '💔',
    'heartpulse': '💗', 'sparkles': '✨', 'star': '⭐', 'stars': '🌟', 'boom': '💥',
    '100': '💯', 'anger': '💢', 'zzz': '💤',
    // 手势
    '+1': '👍', 'thumbsup': '👍', '-1': '👎', 'thumbsdown': '👎', 'ok_hand': '👌',
    'v': '✌️', 'wave': '👋', 'clap': '👏', 'pray': '🙏', 'muscle': '💪',
    'point_up': '👆', 'point_down': '👇', 'point_left': '👈', 'point_right': '👉',
    'raised_hands': '🙌', 'fist': '✊', 'hand': '✋', 'writing_hand': '✍️',
    // 自然 / 天气
    'sun_with_face': '🌞', 'sunny': '☀️', 'moon': '🌙', 'full_moon': '🌕',
    'cloud': '☁️', 'umbrella': '☂️', 'snowflake': '❄️', 'fire': '🔥', 'zap': '⚡',
    'rainbow': '🌈', 'ocean': '🌊', 'tada': '🎉', 'balloon': '🎈', 'gift': '🎁',
    'cherry_blossom': '🌸', 'rose': '🌹', 'four_leaf_clover': '🍀', 'tree': '🌳',
    // 食物 / 物品
    'coffee': '☕', 'tea': '🍵', 'beer': '🍺', 'wine': '🍷', 'pizza': '🍕',
    'burger': '🍔', 'cake': '🍰', 'apple': '🍎', 'banana': '🍌', 'icecream': '🍦',
    'book': '📖', 'books': '📚', 'memo': '📝', 'pencil2': '✏️', 'paperclip': '📎',
    'pushpin': '📌', 'bell': '🔔', 'bookmark': '🔖', 'rocket': '🚀', 'bomb': '💣',
    'art': '🎨', 'musical_note': '🎵', 'dart': '🎯', 'video_game': '🎮',
    // 动物
    'dog': '🐶', 'cat': '🐱', 'mouse': '🐭', 'hamster': '🐹', 'rabbit': '🐰',
    'fox': '🦊', 'bear': '🐻', 'panda': '🐼', 'koala': '🐨', 'tiger': '🐯',
    'lion': '🦁', 'cow': '🐮', 'pig': '🐷', 'frog': '🐸', 'monkey': '🐵',
    'unicorn': '🦄', 'bug': '🐛', 'bee': '🐝', 'turtle': '🐢', 'whale': '🐳',
    // 杂项
    'eyes': '👀', 'eye': '👁️', 'ghost': '👻', 'skull': '💀', 'poop': '💩',
    'robot': '🤖', 'alien': '👽', 'mega': '📢', 'loudspeaker': '📣',
    'white_check_mark': '✅', 'x': '❌', 'warning': '⚠️', 'question': '❓',
    'exclamation': '❗', 'information_source': 'ℹ️', 'no_entry': '⛔',
    'heavy_plus_sign': '➕', 'heavy_minus_sign': '➖', 'heavy_multiplication_x': '✖️',
    'heavy_division_sign': '➗', 'copyright': '©️', 'registered': '®️', 'tm': '™️',
    'recycle': '♻️', 'beginner': '🔰', 'clock': '⏰', 'hourglass': '⌛',
    'red_circle': '🔴', 'large_blue_circle': '🔵', 'white_circle': '⚪', 'black_circle': '⚫'
};

// 构造 Emoji 扩展：输入 :shortcode: 后替换为对应 Unicode 字符
// （TipTap InputRule 在代码块/行内代码中自动跳过，无需特殊处理）
function buildEmojiExtension() {
    return TipTapExtension.create({
        name: 'emoji',
        addInputRules: function() {
            return [new TipTapInputRule({
                find: function(text) {
                    var m = text.match(/:([a-z0-9_+-]+):$/);
                    if (!m || !EMOJI_SHORTCODES[m[1]]) return null;
                    return { text: m[0], index: m.index };
                },
                handler: function(ref) {
                    var state = ref.state, range = ref.range, match = ref.match;
                    var code = match[0].slice(1, -1);
                    state.tr.insertText(EMOJI_SHORTCODES[code], range.from, range.to);
                }
            })];
        }
    });
}

// 构造 Mermaid 块节点（原子节点，源码存于 source 属性）
// - parseHTML 抢先匹配 <pre><code class="language-mermaid">（priority 1000，优先于 codeBlock）
// - renderHTML 输出相同的 <pre><code class="language-mermaid">，与 MarkdownHandler 往返一致
// - NodeView 负责把 source 渲染为 SVG；双击唤起编辑面板
function buildMermaidNode() {
    return TipTapNode.create({
        name: 'mermaid',
        group: 'block',
        atom: true,              // 整体选中，内部不可直接编辑（双击弹层编辑）
        selectable: true,
        defining: true,
        priority: 1000,          // 高于 codeBlock，使 language-mermaid 代码块解析为本节点
        addAttributes: function() {
            return {
                source: { default: '' }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'pre',
                getAttrs: function(el) {
                    var code = el.querySelector('code');
                    if (code && /\blanguage-mermaid\b/.test(code.className || '')) {
                        return { source: code.textContent || '' };
                    }
                    return false; // 交给后续 codeBlock 规则
                }
            }];
        },
        renderHTML: function(ref) {
            var node = ref.node;
            return ['pre', ['code', { class: 'language-mermaid' }, node.attrs.source || '']];
        },
        addNodeView: function() {
            return mermaidNodeView;
        }
    });
}

// Mermaid NodeView：把 source 渲染为 SVG，库缺失/渲染失败则回退显示源码
function mermaidNodeView(ref) {
    var node = ref.node;
    var getPos = ref.getPos;
    var dom = document.createElement('div');
    dom.className = 'mermaid-block';
    dom.setAttribute('data-mermaid', '');
    dom.setAttribute('title', '双击编辑图表源码');

    function paint(source) {
        dom.innerHTML = '';
        if (!(source && source.trim())) {
            dom.innerHTML = '<div class="mermaid-empty">（空图表，双击编辑）</div>';
            return;
        }
        if (!window.mermaid) {
            // 库未加载：降级显示源码，保证可读可保存
            var pre = document.createElement('pre');
            pre.className = 'mermaid-fallback';
            var code = document.createElement('code');
            code.textContent = source;
            pre.appendChild(code);
            dom.appendChild(pre);
            return;
        }
        _ensureMermaidInited();
        var id = 'mermaid-render-' + (++_mermaidIdSeq);
        window.mermaid.render(id, source).then(function(res) {
            dom.innerHTML = _sanitizeMermaidSvg(res.svg || '');
            var svg = dom.querySelector('svg');
            if (svg) {
                // 编辑区渲染不缩放——该多大就多大
                svg.style.maxWidth = 'none';
                svg.style.height = 'auto';
            }
        }).catch(function(err) {
            dom.innerHTML = '<pre class="mermaid-fallback mermaid-error"><code>'
                + _escapeText(source)
                + '</code></pre>';
            console.warn('Mermaid 渲染失败:', err);
        });
    }

    paint(node.attrs.source);

    // 注册画笔，供主题切换后重绘
    var entry = {
        getSource: function() { return node.attrs.source; },
        paint: paint
    };
    _mermaidViews.push(entry);

    // 双击唤起编辑面板
    dom.addEventListener('dblclick', function(e) {
        e.preventDefault();
        e.stopPropagation();
        if (window.editorApi && window.editorApi._mermaidEditor) {
            window.editorApi._mermaidEditor.open(getPos, node.attrs.source);
        }
    });

    return {
        dom: dom,
        update: function(newNode) {
            if (newNode.type !== node.type) return false;
            node = newNode;
            paint(newNode.attrs.source);
            return true;
        },
        selectNode: function() { dom.classList.add('ProseMirror-selectednode'); },
        deselectNode: function() { dom.classList.remove('ProseMirror-selectednode'); },
        ignoreMutation: function() { return true; }, // SVG 内部变更不触发重渲染
        destroy: function() {
            var i = _mermaidViews.indexOf(entry);
            if (i >= 0) _mermaidViews.splice(i, 1);
        }
    };
}

// 主题切换后重绘所有活动的 Mermaid 图表（单图失败静默，不影响主题切换）
function _repaintMermaidViews() {
    if (!window.mermaid) return;
    _ensureMermaidInited();
    _mermaidViews.forEach(function(entry) {
        try { entry.paint(entry.getSource()); } catch (e) { /* 忽略 */ }
    });
}

// 全局编辑器 API 对象（C++ 侧通过 QWebChannel 调用其中的命令方法）
window.editorApi = {
    editor: null,
    bridge: null,
    _dirty: false,
    _loading: false,
    _mathEditing: false,
    isSourceMode: false,
    _sourceValue: null,
    _readOnly: false,

    // === 内部辅助方法 ===

    _shouldIgnoreUpdate: function() {
        return this._loading || this._mathEditing;
    },

    _markDirty: function() {
        if (!this._dirty) {
            this._dirty = true;
            if (this.bridge) this.bridge.contentChanged(true);
        }
    },

    _forceDirty: function() {
        this._dirty = true;
        if (this.bridge) this.bridge.contentChanged(true);
    },

    _notifyDirty: function() {
        if (this.bridge) this.bridge.contentChanged(!!this._dirty);
    },

    _buildExtensions: function() {
        if (!TipTapStarterKit) return [];
        var exts = [
            TipTapStarterKit.configure({
                codeBlock: false,
                heading: { levels: [1, 2, 3, 4, 5, 6] }
            }),
            TipTapTableKit.configure({ resizable: true }),
            TipTapTaskList,
            TipTapTaskItem.configure({ nested: true }),
            TipTapHighlight,
            TipTapImage.configure({ allowBase64: true }),
            TipTapPlaceholder.configure({ placeholder: '开始编写 Markdown...' }),
        ];
        if (TipTapCodeBlockLowlight && window.lowlight)
            exts.push(TipTapCodeBlockLowlight.configure({ lowlight: window.lowlight }));
        if (TipTapMathematics)
            exts.push(TipTapMathematics);
        if (TipTapSubscript)
            exts.push(TipTapSubscript);
        if (TipTapSuperscript)
            exts.push(TipTapSuperscript);
        // 文字对齐：作用于段落与标题（对齐信息不在 .md 中保存，往返有损）
        if (TipTapTextAlign)
            exts.push(TipTapTextAlign.configure({ types: ['heading', 'paragraph'] }));
        // Emoji 短代码 InputRule（:shortcode: → Unicode 字符）
        if (TipTapExtension && TipTapInputRule)
            exts.push(buildEmojiExtension());
        // Mermaid 块节点（自定义 NodeView，priority 高于 codeBlock）
        if (TipTapNode)
            exts.push(buildMermaidNode());
        return exts;
    },

    _createEditor: function(container, content) {
        if (!TipTapEditor) {
            console.error('TipTap bundle 未加载，请确认 tiptap-all.js 已正确引入。');
            container.textContent = '编辑器内核加载失败：未找到 window.TipTap。请检查 tiptap-all.js。';
            return null;
        }
        var self = this;
        var config = {
            element: container,
            extensions: this._buildExtensions(),
            onUpdate: function(args) {
                if (self._shouldIgnoreUpdate()) return;
                // 仅内容变更事务标记脏状态（Focus 等插件会派发 meta-only 事务）
                if (args.transaction.docChanged) {
                    self._markDirty();
                }
                self.notifyHeadings();
                self.notifyWordCount();
                self.notifyHistory();
            },
            onSelectionUpdate: function(args) {
                var ed = args.editor;
                if (self.bridge) {
                    var pos = ed.state.selection.head, line = 1, col = 1;
                    ed.state.doc.nodesBetween(0, pos, function(node, start) {
                        if (start + node.nodeSize <= pos) {
                            var l = node.textContent.slice(0, pos - start).split('\n');
                            line += l.length - 1;
                            col = l[l.length - 1].length + 1;
                        }
                    });
                    self.bridge.cursorChanged(line, col);
                    self.notifyFormat();
                }
                self.notifyHeadings();
                self.notifyHistory();
            },
        };
        if (content !== undefined) config.content = content;
        return new TipTapEditor(config);
    },

    _attachMathClickHandler: function(dom) {
        var state = { count: 0, timer: null };
        dom.addEventListener('mousedown', function(e) {
            var mathEl = e.target.closest('[data-type="block-math"], [data-type="inline-math"]');
            if (!mathEl) { state.count = 0; return; }
            // 阻止 ProseMirror 处理此事件，完全避免 dirty 污染
            e.stopPropagation();
            e.preventDefault();
            var api = window.editorApi;
            state.count++;
            if (state.count === 1) {
                api._mathClickDirtySave = api._dirty;
                state.timer = setTimeout(function() {
                    state.count = 0;
                }, 400);
            } else {
                clearTimeout(state.timer);
                state.count = 0;
                api._mathEditor.open(mathEl, api._mathClickDirtySave);
            }
        }, true);
        return dom;
    },

    // === 初始化 ===

    init: function() {
        if (!TipTapEditor) {
            console.error('TipTap bundle 未加载，请确认 tiptap-all.js 已正确引入。');
            document.querySelector('#editor').textContent =
                '编辑器内核加载失败：未找到 window.TipTap。请检查 tiptap-all.js。';
            return;
        }
        var el = document.querySelector('#editor');
        // 抑制构造期 onUpdate（Focus 等插件会在加载后异步派发事务）
        this._loading = true;
        this.editor = this._createEditor(el);
        this._loading = false;
        if (this.editor) {
            this.notifyHeadings();
            this._attachMathClickHandler(this.editor.view.dom);
        }
    },

    // === 通知方法 ===

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
        format.subscript = !!ed.isActive('subscript');
        format.superscript = !!ed.isActive('superscript');
        // 文字对齐（未显式设置时属性为 null，工具栏按左对齐高亮）
        format.textAlign = ed.getAttributes('paragraph').textAlign
            || ed.getAttributes('heading').textAlign
            || 'left';
        // 结构
        format.bulletList = !!ed.isActive('bulletList');
        format.orderedList = !!ed.isActive('orderedList');
        format.taskList = !!ed.isActive('taskList');
        format.blockquote = !!ed.isActive('blockquote');
        format.codeBlock = !!ed.isActive('codeBlock');
        // 链接和表格
        format.link = !!ed.isActive('link');
        format.table = !!ed.isActive('table');

        // 选中状态和链接 URL
        format.hasSelection = !ed.state.selection.empty;
        format.linkUrl = "";
        if (ed.isActive('link')) {
            var attrs = ed.getAttributes('link');
            if (attrs && attrs.href) format.linkUrl = attrs.href;
        }

        this.bridge.formatChanged(JSON.stringify(format));
    },

    notifyHistory: function() {
        if (!this.editor || !this.bridge) return;
        var canUndo = this.editor.can().undo();
        var canRedo = this.editor.can().redo();
        this.bridge.historyChanged(canUndo, canRedo);
    },

    notifyWordCount: function() {
        if (!this.editor || !this.bridge) return;
        var text = this.editor.state.doc.textContent;
        // 字符数：排除空白
        var chars = text.replace(/\s/g, '').length;
        // 词数：CJK 逐字计 + 拉丁按空白分词
        var words = 0;
        if (text.trim()) {
            var cjkRe = /[一-鿿㐀-䶿豈-﫿぀-ゟ゠-ヿ가-힯]/g;
            var cjkCount = (text.match(cjkRe) || []).length;
            var nonCjk = text.replace(cjkRe, ' ').trim();
            var latinCount = nonCjk ? nonCjk.split(/\s+/).length : 0;
            words = cjkCount + latinCount;
        }
        this.bridge.wordCountChanged(chars, words);
    },

    // C++ 侧调用的命令
    getContent: function() { return this.editor ? this.editor.getHTML() : ''; },

    setContent: function(html) {
        if (!TipTapEditor) return;
        // 销毁旧编辑器 → 用内容新建（清零撤销历史最可靠方式）
        var oldEl = document.querySelector('#editor');
        var parent = oldEl.parentNode;
        var newEl = document.createElement('div');
        newEl.id = 'editor';
        parent.replaceChild(newEl, oldEl);

        if (this.editor) {
            this.editor.destroy();
            this.editor = null;
        }

        // 重新创建编辑器，content 作为初始态，不产生 undo
        // _loading 抑制初始化期 onUpdate（Focus 等插件会在构造期派发事务触发 onUpdate）
        this._loading = true;
        this.editor = this._createEditor(newEl, html);
        this._loading = false;
        if (!this.editor) return;

        this._dirty = false;
        if (this.bridge) {
            this.bridge.contentChanged(false);
            this.notifyWordCount();
        }
        this._attachMathClickHandler(this.editor.view.dom);
    },

    // === 格式命令 ===

    toggleBold: function() { if (this.editor) this.editor.commands.toggleBold(); },
    toggleItalic: function() { if (this.editor) this.editor.commands.toggleItalic(); },
    toggleStrike: function() { if (this.editor) this.editor.commands.toggleStrike(); },
    toggleCode: function() { if (this.editor) this.editor.commands.toggleCode(); },
    toggleUnderline: function() { if (this.editor) this.editor.commands.toggleUnderline(); },
    toggleHighlight: function() { if (this.editor) this.editor.commands.toggleHighlight(); },
    toggleSubscript: function() { if (this.editor) this.editor.commands.toggleSubscript(); },
    toggleSuperscript: function() { if (this.editor) this.editor.commands.toggleSuperscript(); },
    setTextAlign: function(alignment) {
        if (this.editor) this.editor.commands.setTextAlign(alignment);
    },
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
            // 比较源码视图是否相对进入时有改动，避免丢失脏状态
            var changed = sourceView.value !== this._sourceValue;
            // 切回 WYSIWYG
            this._loading = true;
            this.editor.commands.setContent(sourceView.value);
            this._loading = false;
            sourceView.style.display = 'none';
            editorDiv.style.display = '';
            this.isSourceMode = false;
            // 源码视图有改动且当前未标记脏时，标记并通知
            if (changed && !this._dirty) this._markDirty();
        } else {
            // 切到源码视图，记录进入时的内容以检测改动
            this._sourceValue = this.editor.getHTML();
            sourceView.value = this._sourceValue;
            editorDiv.style.display = 'none';
            sourceView.style.display = '';
            if (this._readOnly) sourceView.setAttribute('readonly', '');
            this.isSourceMode = true;
        }
    },
    scrollToPos: function(pos) {
        if (!this.editor) return;
        var dom = this.editor.view.domAtPos(Math.min(pos + 1, this.editor.state.doc.content.size));
        if (dom && dom.node) {
            var el = dom.node.nodeType === 3 ? dom.node.parentElement : dom.node;
            if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    },
    undo: function() { if (this.editor) this.editor.commands.undo(); },
    redo: function() { if (this.editor) this.editor.commands.redo(); },
    setTheme: function(theme) {
        if (theme === 'dark') document.body.classList.add('dark');
        else document.body.classList.remove('dark');
        // 重置 mermaid 初始化标记，使后续渲染采用新主题；并重绘已存在的图表
        window.__mermaidInited = false;
        try { _repaintMermaidViews(); } catch (e) { /* 主题切换不应被图表渲染阻断 */ }
    },
    setFont: function(family, size) {
        if (family) document.body.style.fontFamily = family;
        if (size) {
            document.body.style.fontSize = size + 'px';
            var sv = document.getElementById('source-view');
            if (sv) sv.style.fontSize = size + 'px';
        }
    },
    syncFormatState: function() {
        if (!this.editor || !this.bridge) return;
        this.notifyFormat();
        this.notifyHeadings();
        this.notifyWordCount();
    },

    setEditable: function(editable) {
        if (!this.editor) return;
        this._loading = true;
        this.editor.setEditable(!!editable);
        this._loading = false;
        this._readOnly = !editable;
        var el = this.editor.view.dom;
        var sv = document.getElementById('source-view');
        if (!editable) {
            if (sv) sv.setAttribute('readonly', '');
            // 阅读模式：mousedown 拦截链接点击，通知 C++ 打开
            this._linkHandler = function(e) {
                var t = e.target;
                while (t && t !== el) {
                    if (t.tagName === 'A' && t.href) {
                        e.preventDefault();
                        e.stopPropagation();
                        if (window.editorApi.bridge)
                            window.editorApi.bridge.openUrl(t.href);
                        return;
                    }
                    t = t.parentElement;
                }
            };
            el.addEventListener('mousedown', this._linkHandler, true);
        } else {
            if (sv) sv.removeAttribute('readonly');
            if (this._linkHandler) {
                el.removeEventListener('mousedown', this._linkHandler, true);
                this._linkHandler = null;
            }
        }
    },

    setWidth: function(percent) {
        var ed = document.getElementById('editor');
        var sv = document.getElementById('source-view');
        var w = percent + '%';
        if (ed) { ed.style.maxWidth = w; ed.style.minWidth = '800px'; }
        if (sv) { sv.style.maxWidth = w; sv.style.minWidth = '800px'; }
    },

    applySettings: function(settings) {
        if (settings.lineNumbers !== undefined) {
            document.body.classList.toggle('line-numbers', !!settings.lineNumbers);
        }
        if (settings.wordWrap !== undefined) {
            document.body.classList.toggle('nowrap', !settings.wordWrap);
        }
        if (settings.tabSize !== undefined) {
            var ts = settings.tabSize;
            document.documentElement.style.setProperty('--tab-size', ts);
            var sv = document.getElementById('source-view');
            if (sv) sv.style.tabSize = ts;
        }
    },

    markClean: function() {
        this._dirty = false;
        if (this.bridge) this.bridge.contentChanged(false);
    },

    // 插入数学公式（C++ 侧调用）
    insertMath: function(type) {
        window.editorApi._mathEditor.create(type || 'inline');
    },

    // 插入 Mermaid 图表（C++ 侧调用）
    insertMermaid: function() {
        window.editorApi._mermaidEditor.create();
    },

    // 数学公式编辑面板
    _mathEditor: {
        overlay: null,
        popover: null,
        input: null,
        preview: null,
        okBtn: null,
        cancelBtn: null,
        mathEl: null,
        pos: null,
        mathType: 'inline',
        mode: 'create',

        // 双击打开（编辑模式）
        // dirtyBefore: 来自 mousedown 捕获阶段的脏状态快照，早于 ProseMirror 处理
        open: function(mathEl, dirtyBefore) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._dirtyBefore = (dirtyBefore !== undefined) ? dirtyBefore : window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            this.mathEl = mathEl;
            this.mathType = mathEl.getAttribute('data-type') === 'block-math' ? 'block' : 'inline';
            this.mode = 'edit';
            this.pos = ed.view.posAtDOM(mathEl);
            var latex = mathEl.getAttribute('data-latex') || '';
            this.buildDom(this.mathType);
            this.show(latex);
            ed.view.dom.blur();
        },

        // 新建（按钮/快捷键触发）
        create: function(type) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._dirtyBefore = window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            this.mathEl = null;
            this.pos = ed.state.selection.from;
            this.mathType = type || 'inline';
            this.mode = 'create';
            this.buildDom(this.mathType);
            this.show('');
            ed.view.dom.blur();
        },

        // 构建 popover DOM（块级 textarea，行内 input）
        buildDom: function(type) {
            this.cleanupDom();
            var isBlock = type === 'block';

            // 遮罩（不响应点击，仅作视觉遮挡）
            this.overlay = document.createElement('div');
            this.overlay.className = 'math-editor-overlay';

            // 面板
            this.popover = document.createElement('div');
            this.popover.className = 'math-editor-popover';

            // 标题栏（类型标签 + X 关闭按钮）
            var titleBar = document.createElement('div');
            titleBar.className = 'math-editor-titlebar';

            var tag = document.createElement('span');
            tag.className = 'math-editor-type-tag';
            tag.textContent = isBlock ? '块级公式' : '行内公式';
            titleBar.appendChild(tag);

            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            closeBtn.title = '关闭';
            var self = this;
            closeBtn.addEventListener('click', function(e) { e.stopPropagation(); self.close(); });
            titleBar.appendChild(closeBtn);

            this.popover.appendChild(titleBar);

            // 编辑器标签
            if (isBlock) {
                this.input = document.createElement('textarea');
                this.input.className = 'math-editor-input block';
                this.input.rows = 4;
                this.input.spellcheck = false;
            } else {
                this.input = document.createElement('input');
                this.input.type = 'text';
                this.input.className = 'math-editor-input inline';
                this.input.spellcheck = false;
            }
            var label = document.createElement('div');
            label.className = 'math-editor-label';
            label.textContent = 'LaTeX 源码';
            this.popover.appendChild(label);
            this.popover.appendChild(this.input);

            // 实时预览
            var prevLabel = document.createElement('div');
            prevLabel.className = 'math-editor-label';
            prevLabel.textContent = '预览';
            this.popover.appendChild(prevLabel);
            this.preview = document.createElement('div');
            this.preview.className = 'math-editor-preview';
            this.popover.appendChild(this.preview);

            // 按钮
            var actions = document.createElement('div');
            actions.className = 'math-editor-actions';

            this.cancelBtn = document.createElement('button');
            this.cancelBtn.textContent = '取消';
            this.cancelBtn.addEventListener('click', function() { self.close(); });
            actions.appendChild(this.cancelBtn);

            this.okBtn = document.createElement('button');
            this.okBtn.className = 'primary';
            this.okBtn.textContent = '确定';
            this.okBtn.addEventListener('click', function() { self.confirm(); });
            actions.appendChild(this.okBtn);

            this.popover.appendChild(actions);

            // 键盘
            var input = this.input;
            input.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { self.close(); return; }
                if (e.key === 'Enter') {
                    if (isBlock && !e.ctrlKey && !e.metaKey) return; // 块级 Enter 换行
                    e.preventDefault();
                    self.confirm();
                }
            });
            input.addEventListener('input', function() { self.updatePreview(); });

            // 阻止面板内点击冒泡到遮罩
            this.popover.addEventListener('click', function(e) { e.stopPropagation(); });

            document.body.appendChild(this.overlay);
            document.body.appendChild(this.popover);
        },

        // 显示面板并填充初始值
        show: function(latex) {
            this.input.value = latex;
            this.updatePreview();
            var isBlock = this.mathType === 'block';
            this.input.focus();
            if (isBlock && latex) {
                this.input.setSelectionRange(latex.length, latex.length);
            } else if (!isBlock) {
                this.input.select();
            }
        },

        // 实时 KaTeX 预览
        updatePreview: function() {
            var latex = this.input.value;
            if (!latex.trim()) {
                this.preview.innerHTML = '<span style="color:#888;font-size:17px;">（输入 LaTeX 公式）</span>';
                return;
            }
            try {
                katex.render(latex, this.preview, {
                    throwOnError: false,
                    displayMode: this.mathType === 'block'
                });
            } catch (e) {
                this.preview.textContent = latex;
            }
        },

        // 确认：更新或插入
        confirm: function() {
            var latex = this.input.value.trim();
            if (!latex) { this.close(); return; }
            var ed = window.editorApi.editor;
            if (!ed) { this.close(); return; }
            this._confirmed = true;
            // 手动标记脏（_mathEditing 抑制了 onUpdate 通知）
            window.editorApi._forceDirty();
            if (this.mode === 'edit') {
                var cmd = this.mathType === 'block' ? 'updateBlockMath' : 'updateInlineMath';
                ed.commands[cmd]({ pos: this.pos, latex: latex });
            } else {
                var cmd = this.mathType === 'block' ? 'insertBlockMath' : 'insertInlineMath';
                ed.commands[cmd]({ latex: latex });
            }
            this.close();
        },

        // 仅清理 DOM（不恢复焦点，供 buildDom 内部使用）
        cleanupDom: function() {
            if (this.overlay && this.overlay.parentNode) this.overlay.parentNode.removeChild(this.overlay);
            if (this.popover && this.popover.parentNode) this.popover.parentNode.removeChild(this.popover);
            this.overlay = null;
            this.popover = null;
            this.input = null;
            this.preview = null;
            this.okBtn = null;
            this.cancelBtn = null;
        },

        // 关闭面板（取消或确认后调用）
        close: function() {
            this.cleanupDom();
            this.mathEl = null;
            // 取消时立即恢复脏状态
            if (!this._confirmed) {
                window.editorApi._dirty = this._dirtyBefore;
                if (window.editorApi.bridge) {
                    window.editorApi.bridge.contentChanged(!!this._dirtyBefore);
                }
            }
            // 延迟恢复焦点；保持 _mathEditing 直到 focus 后续事件处理完毕
            var self = this;
            setTimeout(function() {
                var ed = window.editorApi.editor;
                if (ed) ed.view.focus();
                setTimeout(function() {
                    window.editorApi._mathEditing = false;
                    // 兜底：若 focus 触发了 onUpdate，再次恢复
                    if (!self._confirmed) {
                        window.editorApi._dirty = self._dirtyBefore;
                        if (window.editorApi.bridge) {
                            window.editorApi.bridge.contentChanged(!!self._dirtyBefore);
                        }
                    }
                }, 150);
            }, 50);
        }
    },

    // Mermaid 编辑面板（模态：遮罩覆盖编辑区，复用 _mathEditing 抑制标志）
    _mermaidEditor: {
        overlay: null,
        popover: null,
        input: null,
        preview: null,
        okBtn: null,
        cancelBtn: null,
        getPos: null,         // 编辑模式：NodeView 的 getPos 函数
        pos: null,            // 编辑模式：位置快照
        mode: 'create',
        _dirtyBefore: false,
        _confirmed: false,

        // 双击打开（编辑模式）
        open: function(getPos, source) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._dirtyBefore = window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            this.getPos = getPos;
            this.pos = (typeof getPos === 'function') ? getPos() : null;
            this.mode = 'edit';
            this.buildDom();
            this.show(source || '');
            ed.view.dom.blur();
        },

        // 新建（工具栏按钮触发）
        create: function() {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._dirtyBefore = window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            this.getPos = null;
            this.mode = 'create';
            this.buildDom();
            this.show('');
            ed.view.dom.blur();
        },

        buildDom: function() {
            this.cleanupDom();
            var self = this;
            this.overlay = document.createElement('div');
            this.overlay.className = 'math-editor-overlay';
            // 点击遮罩关闭
            this.overlay.addEventListener('click', function() { self.close(); });
            // 遮罩拦截滚轮事件，阻止穿透到下层编辑区滚动（模态核心行为）
            this.overlay.addEventListener('wheel', function(e) { e.preventDefault(); }, { passive: false });

            this.popover = document.createElement('div');
            this.popover.className = 'math-editor-popover mermaid-popover';

            // 标题栏
            var titleBar = document.createElement('div');
            titleBar.className = 'math-editor-titlebar';
            var tag = document.createElement('span');
            tag.className = 'math-editor-type-tag';
            tag.textContent = this.mode === 'edit' ? '编辑 Mermaid 图表' : '插入 Mermaid 图表';
            titleBar.appendChild(tag);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            closeBtn.title = '关闭';
            closeBtn.addEventListener('click', function(e) { e.stopPropagation(); self.close(); });
            titleBar.appendChild(closeBtn);
            this.popover.appendChild(titleBar);

            // 源码输入
            var label = document.createElement('div');
            label.className = 'math-editor-label';
            label.textContent = 'Mermaid 源码（如 graph TD; A-->B）';
            this.popover.appendChild(label);
            this.input = document.createElement('textarea');
            this.input.className = 'math-editor-input block mermaid-source';
            this.input.rows = 8;
            this.input.spellcheck = false;
            this.popover.appendChild(this.input);

            // 实时预览
            var prevLabel = document.createElement('div');
            prevLabel.className = 'math-editor-label';
            prevLabel.textContent = '预览';
            this.popover.appendChild(prevLabel);
            this.preview = document.createElement('div');
            this.preview.className = 'math-editor-preview mermaid-preview';
            this.popover.appendChild(this.preview);

            // 语法提示
            var hint = document.createElement('div');
            hint.className = 'mermaid-hint';
            hint.textContent = '支持 flowchart / sequence / class / state / pie / gantt 等，详见 mermaid.js.org';
            this.popover.appendChild(hint);

            // 按钮
            var actions = document.createElement('div');
            actions.className = 'math-editor-actions';
            this.cancelBtn = document.createElement('button');
            this.cancelBtn.textContent = '取消';
            this.cancelBtn.addEventListener('click', function() { self.close(); });
            actions.appendChild(this.cancelBtn);
            this.okBtn = document.createElement('button');
            this.okBtn.className = 'primary';
            this.okBtn.textContent = '确定';
            this.okBtn.addEventListener('click', function() { self.confirm(); });
            actions.appendChild(this.okBtn);
            this.popover.appendChild(actions);

            // 键盘：Ctrl/Cmd+Enter 确定，Esc 关闭（裸 Enter 换行）
            this.input.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { e.preventDefault(); self.close(); return; }
                if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); self.confirm(); }
            });
            this.input.addEventListener('input', function() { self.updatePreview(); });
            this.popover.addEventListener('click', function(e) { e.stopPropagation(); });
            // 对话框内部滚轮不冒泡到遮罩（否则遮罩 preventDefault 会阻止对话框滚动）
            this.popover.addEventListener('wheel', function(e) { e.stopPropagation(); });

            document.body.appendChild(this.overlay);
            document.body.appendChild(this.popover);
        },

        show: function(source) {
            this.input.value = source;
            this.updatePreview();
            this.input.focus();
            var len = source.length;
            this.input.setSelectionRange(len, len);
        },

        // 实时 Mermaid 预览
        updatePreview: function() {
            var src = this.input.value;
            var p = this.preview;
            if (!src.trim()) {
                p.innerHTML = '<span style="color:#888;font-size:14px;">（输入 Mermaid 源码预览图表）</span>';
                return;
            }
            if (!window.mermaid) { p.textContent = src; return; }
            _ensureMermaidInited();
            var id = 'mermaid-prev-' + (++_mermaidIdSeq);
            window.mermaid.render(id, src).then(function(res) {
                p.innerHTML = _sanitizeMermaidSvg(res.svg || '');
                var svg = p.querySelector('svg');
                if (svg) { svg.style.maxWidth = '100%'; svg.style.height = 'auto'; }
            }).catch(function(err) {
                p.innerHTML = '<span style="color:#c00;font-size:13px;">渲染失败：'
                    + _escapeText(err && err.message || String(err)) + '</span>';
            });
        },

        // 确认：更新或插入
        confirm: function() {
            var src = this.input.value.trim();
            if (!src) { this.close(); return; }
            var ed = window.editorApi.editor;
            if (!ed) { this.close(); return; }
            this._confirmed = true;
            window.editorApi._forceDirty();
            if (this.mode === 'edit') {
                var p = (typeof this.getPos === 'function') ? this.getPos() : this.pos;
                if (p === null || p === false) { this.close(); return; }
                ed.view.dispatch(ed.state.tr.setNodeMarkup(p, undefined, { source: src }));
            } else {
                ed.commands.insertContent({ type: 'mermaid', attrs: { source: src } });
            }
            this.close();
        },

        cleanupDom: function() {
            if (this.overlay && this.overlay.parentNode) this.overlay.parentNode.removeChild(this.overlay);
            if (this.popover && this.popover.parentNode) this.popover.parentNode.removeChild(this.popover);
            this.overlay = null;
            this.popover = null;
            this.input = null;
            this.preview = null;
            this.okBtn = null;
            this.cancelBtn = null;
        },

        close: function() {
            this.cleanupDom();
            this.getPos = null;
            if (!this._confirmed) {
                window.editorApi._dirty = this._dirtyBefore;
                if (window.editorApi.bridge) {
                    window.editorApi.bridge.contentChanged(!!this._dirtyBefore);
                }
            }
            var self = this;
            setTimeout(function() {
                var ed = window.editorApi.editor;
                if (ed) ed.view.focus();
                setTimeout(function() {
                    window.editorApi._mathEditing = false;
                    if (!self._confirmed) {
                        window.editorApi._dirty = self._dirtyBefore;
                        if (window.editorApi.bridge) {
                            window.editorApi.bridge.contentChanged(!!self._dirtyBefore);
                        }
                    }
                }, 150);
            }, 50);
        }
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
