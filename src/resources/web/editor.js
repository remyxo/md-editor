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
// 扩展 CodeBlockLowlight，附加自定义 NodeView（语言标签 + 复制按钮 + 语言切换）
var CodeBlockLowlightWithNodeView = TipTapCodeBlockLowlight && TipTapCodeBlockLowlight.extend({
    addNodeView: function() { return codeBlockNodeView; }
});
// 扩展 Image，附加自定义 NodeView（attrs.src 存原始路径保往返保真，渲染时经 _resolveImageSrc 解析为 localimg://）
// 必须用 addNodeView 而非 editorProps.nodeViews：TipTap 会用各扩展的 addNodeView 生成 nodeViews
// 并传给 EditorView，覆盖 editorProps.nodeViews（实测 image 不会进入 view.props.nodeViews）
var ImageWithNodeView = TipTapImage && TipTapImage.extend({
    addNodeView: function() { return imageNodeView; }
});
var TipTapMathematics = window.TipTapExtensionMathematics;
var TipTapSubscript = window.TipTapExtensionSubscript;
var TipTapSuperscript = window.TipTapExtensionSuperscript;
var TipTapTextAlign = window.TipTapExtensionTextAlign;
var TipTapExtension = window.TipTapExtension;
var TipTapInputRule = window.TipTapInputRule;
var TipTapNode = window.TipTapNode;
var TipTapMark = window.TipTapMark;

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
// 正则容忍 = 周围可选空白、双引号/单引号，覆盖 Mermaid 各版本输出格式差异。
function _sanitizeMermaidSvg(svgString) {
    if (!svgString) return svgString;
    // 修复负宽（高）的 <rect> / <foreignObject> 等元素（CJK 字符宽度偏差）
    // 同时匹配 width/height，双引号/单引号，= 周围可选空白
    // clamp 为 1 而非 0：Gantt 甘特图等 task bar 需要可见（0 宽完全不可见）
    svgString = svgString
        .replace(/\b(width|height)\s*=\s*"-\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"/g, '$1="1"')
        .replace(/\b(width|height)\s*=\s*'-(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)'/g, "$1='1'");
    // 后置 DOM 修正兜底：innerHTML 解析后若仍有漏网负值 rect，
    // 在 _fixNegativeRects 中 clamp 为 1（regex 漏网 + mermaid 动态 SVG 均能兜住）
    svgString = svgString.replace(/<svg\b/, '<svg data-sanitized="1"');
    // mermaid 输出 width="100%"，在宽容器中填满导致等比放大（字体远超正文）。
    // 提取 <svg> 元素自身的 viewBox 宽度替换之，使 SVG 按 1:1 像素映射渲染（自然尺寸）。
    // 必须锚定在 <svg 开标签内：Gantt 等图表的 <defs>/<pattern> 也可能有 viewBox，
    // 错误匹配子元素的 viewBox 会导致 width 被设为一个极小值（如图表缩成一小块）。
    var vb = svgString.match(/<svg\b[^>]*\bviewBox\s*=\s*"([\d.\s-]+)"/);
    if (vb) {
        var vbNums = vb[1].match(/[\d.]+/g);
        if (vbNums && vbNums.length >= 4) {
            // 只替换 <svg> 元素自身的 width 属性，不动子元素
            svgString = svgString.replace(/(<svg\b[^>]*?)\swidth\s*=\s*"[^"]*"/, '$1 width="' + vbNums[2] + '"');
        }
    }
    return svgString;
}

// DOM 后置修正：将 <rect> 的负 width/height 归零（regex 漏网与动态修改场景兜底）。
// 在 innerHTML 解析后调用，不再触发浏览器 "A negative value is not valid" 控制台错误
// （因为已经是 DOM 属性赋值，而非标记解析）。
function _fixNegativeRects(svgEl) {
    if (!svgEl) return;
    var rects = svgEl.querySelectorAll('rect');
    for (var i = 0; i < rects.length; i++) {
        var r = rects[i];
        var w = r.getAttribute('width');
        var h = r.getAttribute('height');
        if (w !== null && parseFloat(w) < 0) r.setAttribute('width', '1');
        if (h !== null && parseFloat(h) < 0) r.setAttribute('height', '1');
    }
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
        // SVG 按 viewBox 真实尺寸显示（非 100% 等比放大），16px 比例协调
        themeVariables: { fontSize: '16px' }
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

// 解析 colorSpan 的 style 字符串 → { color, bg }（缺省属性省略）。
// 注意：用 (^|;) 锚定 color 前边界，避免匹配到 "background-color" 的尾部 color。
function _parseColorStyle(styleStr) {
    var o = {};
    if (!styleStr) return o;
    var bg = styleStr.match(/background-color\s*:\s*([^;]+)/i);
    var fg = styleStr.match(/(^|;)\s*color\s*:\s*([^;]+)/i);
    if (bg) o.bg = bg[1].trim();
    if (fg) o.color = fg[2].trim();
    return o;
}

// { color, bg } → 'color:..;background-color:..'（固定属性顺序，保证往返稳定）。
function _stringifyColorStyle(o) {
    var parts = [];
    if (o.color) parts.push('color:' + o.color);
    if (o.bg) parts.push('background-color:' + o.bg);
    return parts.join(';');
}

// 构造 colorSpan mark：承载 <span style="..."> 的内联样式（颜色为主，底层保真任意 style）。
// - parseHTML 仅认带 style 的 span；无 style 的 span（脚注 marker/content）返回 false 放行
// - 属性级 renderHTML 负责将 style attrs 转为 DOM style 属性，产物经 getRenderedAttributes
//   注入到 HTMLAttributes 后传入 mark 级 renderHTML（TipTap v3 签名为 ({mark, HTMLAttributes})）
function buildColorSpanExtension() {
    return TipTapMark.create({
        name: 'colorSpan',
        addAttributes: function() {
            return {
                style: {
                    default: '',
                    parseHTML: function(el) {
                        return el.getAttribute('style') || '';
                    },
                    renderHTML: function(attrs) {
                        return attrs.style ? { style: attrs.style } : {};
                    }
                }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'span',
                getAttrs: function(el) {
                    var s = el.getAttribute('style') || '';
                    // 仅匹配含颜色或背景色的 span，纯字体样式（font-weight /
                    // font-style / font-size）是 QTextDocument 的内联渲染，
                    // 应由 Bold / Italic mark 处理，而非 colorSpan。
                    return (/\bcolor\s*:/.test(s) || /\bbackground-color\s*:/.test(s)) ? {} : false;
                }
            }];
        },
        renderHTML: function(props) {
            return ['span', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}

// 构造 kbd mark：键盘按键内联样式，渲染为 <kbd> 标签（等宽字体 + 边框圆角）。
// 无 Markdown 语法；仅用于手写 HTML <kbd> 的解析与往返保真。
function buildKbdExtension() {
    return TipTapMark.create({
        name: 'kbd',
        parseHTML: function() {
            return [{ tag: 'kbd' }];
        },
        renderHTML: function(props) {
            return ['kbd', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}

// 构造 small mark：小字内联样式，渲染为 <small> 标签（浏览器默认 font-size: smaller）。
// 无 Markdown 语法；仅用于手写 HTML <small> 的解析与往返保真。
function buildSmallExtension() {
    return TipTapMark.create({
        name: 'small',
        parseHTML: function() {
            return [{ tag: 'small' }];
        },
        renderHTML: function(props) {
            return ['small', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}

// 构造 divBlock 节点：块级容器（仅包裹对内联），渲染为 <div> 标签。
// - 排除 data-type 属性的 div（内部结构：脚注容器/数学块等由专有扩展处理）
// - content: inline* 表示只包裹文本与内联标记，不包裹段落/列表
// - group: block 使 divBlock 能直接出现在文档根层级（与 paragraph/heading 同级）
function buildDivBlockExtension() {
    return TipTapNode.create({
        name: 'divBlock',
        group: 'block',
        content: 'inline*',
        addAttributes: function() {
            return {
                style: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('style') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.style ? { style: attrs.style } : {};
                    }
                },
                align: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('align') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.align ? { align: attrs.align } : {};
                    }
                },
                class: {
                    default: '',
                    parseHTML: function(el) { return el.getAttribute('class') || ''; },
                    renderHTML: function(attrs) {
                        return attrs.class ? { class: attrs.class } : {};
                    }
                }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'div',
                getAttrs: function(el) {
                    // 排除内部结构的 div（脚注/数学块等已有专属扩展）
                    if (el.hasAttribute('data-type')) return false;
                    // 排除任务项内的 div 包装器：taskItem 渲染为
                    // <li><label>checkbox</label><div>content</div></li>，
                    // 该 div 是 taskItem 的内容容器，不应被 divBlock 截获。
                    if (el.closest && el.closest('li[data-type="taskItem"]')) return false;
                    return {};
                }
            }];
        },
        renderHTML: function(props) {
            return ['div', (props && props.HTMLAttributes) || {}, 0];
        }
    });
}

// 构造自定义 HardBreak：仅匹配普通 <br>（不含 data-brk 属性），
// 与 brk 扩展分工：<br> → hardBreak 节点（用户手写），<br data-brk> → brk 节点（BRMARKER 生成）
function buildCustomHardBreak() {
    return TipTapNode.create({
        name: 'hardBreak',
        group: 'inline',
        inline: true,
        selectable: false,
        parseHTML: function() {
            return [{ tag: 'br:not([data-brk])' }];
        },
        renderHTML: function(props) {
            return ['br', (props && props.HTMLAttributes) || {}];
        }
    });
}

// 构造 brk 节点：BRMARKER 生成的硬换行标记，带 data-brk 属性以区分用户手写 <br>
function buildBrkExtension() {
    return TipTapNode.create({
        name: 'brk',
        group: 'inline',
        inline: true,
        selectable: false,
        parseHTML: function() {
            return [{ tag: 'br[data-brk]' }];
        },
        renderHTML: function() {
            return ['br', { 'data-brk': '' }];
        }
    });
}

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

// 构造搜索扩展：包装自写 ProseMirror plugin（见 searchPlugin.js）
function buildSearchExtension() {
    return TipTapExtension.create({
        name: 'search',
        addProseMirrorPlugins: function () {
            return [window.searchPlugin.createSearchPlugin()];
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

// 构造脚注引用节点（行内原子节点，显示为上标标记）
// - id 为字面标记文本（"1" / "注1" / "附录A"）
// - 渲染 <sup data-type="footnoteRef" data-id="{id}">{id}</sup>
// - 自定义 NodeView：双击触发改名（与 insertLink 的 prompt 风格一致）
function buildFootnoteReference() {
    return TipTapNode.create({
        name: 'footnoteReference',
        group: 'inline',
        inline: true,
        atom: true,
        selectable: true,
        addAttributes: function() {
            return {
                id: { default: '' }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'sup[data-type=footnoteRef]',
                // priority 高于 StarterKit Superscript 的 tag:'sup' 规则，
                // 否则脚注引用被解析为普通上标 mark，data-type/data-id 丢失
                priority: 1000,
                getAttrs: function(el) {
                    return { id: el.getAttribute('data-id') || '' };
                }
            }];
        },
        renderHTML: function(ref) {
            var id = ref.node.attrs.id;
            return ['sup', { 'data-type': 'footnoteRef', 'data-id': id }, id];
        },
        addNodeView: function() {
            return function(ref) {
                var node = ref.node;
                var dom = document.createElement('sup');
                dom.setAttribute('data-type', 'footnoteRef');
                function render() {
                    dom.setAttribute('data-id', node.attrs.id);
                    dom.textContent = node.attrs.id;
                }
                render();
                _setI18nTitle(dom, 'footnote.editId');
                dom.addEventListener('dblclick', function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    var cur = node.attrs.id;
                    var v = prompt(i18n('footnote.prompt'), cur);
                    if (v != null) {
                        v = v.trim();
                        // 禁止空串与脚注语法字符
                        if (v && v !== cur && !/[\[\]\s:]/.test(v)) {
                            window.editorApi.renameFootnote(cur, v);
                        }
                    }
                });
                return {
                    dom: dom,
                    update: function(n) {
                        if (n.type !== node.type) return false;
                        node = n;
                        render();
                        return true;
                    },
                    selectNode: function() { dom.classList.add('ProseMirror-selectednode'); },
                    deselectNode: function() { dom.classList.remove('ProseMirror-selectednode'); }
                };
            };
        }
    });
}

// 构造脚注容器节点（文末块容器，仅含若干 footnoteDef）
function buildFootnoteContainer() {
    return TipTapNode.create({
        name: 'footnoteContainer',
        group: 'block',
        content: 'footnoteDef+',
        isolating: true,
        defining: true,
        parseHTML: function() {
            return [{ tag: 'div[data-type=footnoteContainer]' }];
        },
        renderHTML: function() {
            return ['div', { 'data-type': 'footnoteContainer' }, 0];
        }
    });
}

// 构造单条脚注定义（容器子节点，单段内联富文本）
// marker span 显示标记前缀（只读），正文包入 footnote-content span（parseHTML getContentElement 定位）
function buildFootnoteDef() {
    return TipTapNode.create({
        name: 'footnoteDef',
        content: 'inline*',
        defining: true,
        addAttributes: function() {
            return {
                id: { default: '' }
            };
        },
        parseHTML: function() {
            return [{
                tag: 'div[data-type=footnoteDef]',
                getAttrs: function(el) {
                    return { id: el.getAttribute('data-id') || '' };
                },
                // prosemirror ParseRule 字段名为 contentElement（非 getContentElement，
                // 写错时规则被忽略，marker 文本混入正文且每次往返叠加）
                contentElement: function(el) {
                    return el.querySelector('.footnote-content') || el;
                }
            }];
        },
        renderHTML: function(ref) {
            var id = ref.node.attrs.id;
            return ['div', { 'data-type': 'footnoteDef', 'data-id': id },
                ['span', { class: 'footnote-marker', contenteditable: 'false' }, id],
                ['span', { class: 'footnote-content' }, 0]];
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
    _setI18nTitle(dom, 'mermaid.editTip');

    function paint(source) {
        dom.innerHTML = '';
        if (!(source && source.trim())) {
            // 空态：创建带 data-i18n 的元素，setLanguage 时全量重刷
            var empty = document.createElement('div');
            empty.className = 'mermaid-empty';
            _setI18n(empty, 'mermaid.empty');
            dom.appendChild(empty);
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
                _fixNegativeRects(svg);
                // _sanitizeMermaidSvg 已将 width="100%" 替换为
                // viewBox 宽度 → 窄图按自然尺寸显示；
                // max-width:100% 保证宽图（甘特图等）不超出编辑区宽度
                svg.style.maxWidth = '100%';
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

// 代码块高亮主题同步：亮色用 hljs-light.css，暗色用 hljs-dark.css
function _syncHighlightTheme() {
    var dark = document.body.classList.contains('dark');
    var light = document.getElementById('hljs-light');
    var darkEl = document.getElementById('hljs-dark');
    if (light) light.disabled = dark;
    if (darkEl) darkEl.disabled = !dark;
}

// 复制文本到剪贴板：execCommand('copy') 优先（QWebEngineView 兼容性好），
// navigator.clipboard.writeText 兜底（需要安全上下文）
function _copyText(text, done) {
    done = done || function(){};
    try {
        var ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed'; ta.style.opacity = '0';
        ta.style.left = '-9999px'; ta.style.top = '-9999px';
        document.body.appendChild(ta);
        ta.focus();
        ta.select();
        var ok = document.execCommand('copy');
        document.body.removeChild(ta);
        if (ok) { done(true); return; }
    } catch (e) { /* execCommand 失败，尝试 clipboard API */ }
    // 兜底：navigator.clipboard（需要安全上下文）
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(
            function(){ done(true); },
            function(){ console.warn('clipboard.writeText 失败'); done(false); }
        );
    } else {
        console.warn('复制失败：execCommand 和 clipboard API 均不可用');
        done(false);
    }
}

// 代码块 NodeView：外壳 DOM + 工具栏交互；语法高亮由 CodeBlockLowlight decorations 插件负责
// 图片 NodeView：attrs.src 存原始路径（相对或绝对，保往返保真），渲染 <img>.src 经 _resolveImageSrc
// 解析为 localimg://，由 C++ LocalImageSchemeHandler 读盘返回字节（绕过 qrc 页面加载 file:// 的限制）。
// data-src 标记原始路径，供 setDocDir 在文档目录变更时重新解析已渲染图片。
function imageNodeView(ref) {
    var node = ref.node;
    var raw = node.attrs.src || '';
    var img = document.createElement('img');
    img.setAttribute('data-src', raw);
    img.src = window.editorApi._resolveImageSrc(raw);
    img.alt = node.attrs.alt || '';
    if (node.attrs.title) img.title = node.attrs.title;
    return {
        dom: img,
        update: function(n) {
            if (n.type.name !== 'image') return false;
            var nr = n.attrs.src || '';
            if (img.getAttribute('data-src') !== nr) {
                img.setAttribute('data-src', nr);
                img.src = window.editorApi._resolveImageSrc(nr);
            }
            img.alt = n.attrs.alt || '';
            if (n.attrs.title) img.title = n.attrs.title;
            else img.removeAttribute('title');
            return true;
        }
    };
}

function codeBlockNodeView(ref) {
    var node = ref.node, getPos = ref.getPos;

    var dom = document.createElement('div');
    dom.className = 'code-block';

    // 工具栏（contenteditable=false，ProseMirror 不管理其内容）
    var toolbar = document.createElement('div');
    toolbar.className = 'code-block-toolbar';
    toolbar.setAttribute('contenteditable', 'false');

    var label = document.createElement('span');
    label.className = 'code-lang-label';
    label.textContent = 'auto';
    _setI18nTitle(label, 'codeblock.changeLang');

    var copyBtn = document.createElement('button');
    copyBtn.type = 'button';
    copyBtn.className = 'code-copy-btn';
    _setI18nTitle(copyBtn, 'codeblock.copy');
    // 双层方块复制图标（内联 SVG）；按钮可见文字用 data-i18n span，setLanguage 时可重刷
    copyBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" ' +
        'stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" ' +
        'style="vertical-align:middle;margin-right:2px">' +
        '<rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>' +
        '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>' +
        '</svg><span data-i18n="codeblock.copyBtn">' + i18n('codeblock.copyBtn') + '</span>';

    toolbar.appendChild(label);
    toolbar.appendChild(copyBtn);
    dom.appendChild(toolbar);

    var pre = document.createElement('pre');
    var code = document.createElement('code');
    pre.appendChild(code);
    dom.appendChild(pre);

    function renderLabel() {
        label.textContent = node.attrs.language || 'auto';
    }
    renderLabel();

    // 复制按钮：阻止 ProseMirror 抢焦点，获取 code.textContent 执行复制
    copyBtn.addEventListener('mousedown', function(e){ e.preventDefault(); e.stopPropagation(); });
    copyBtn.addEventListener('click', function(e){
        e.preventDefault(); e.stopPropagation();
        var text = code.textContent;
        _copyText(text, function(ok){
            if (!ok) return;
            copyBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" ' +
                'stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" ' +
                'style="vertical-align:middle;margin-right:2px">' +
                '<polyline points="20 6 9 17 4 12"/>' +
                '</svg><span data-i18n="codeblock.copied">' + i18n('codeblock.copied') + '</span>';
            setTimeout(function(){
                copyBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" ' +
                    'stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" ' +
                    'style="vertical-align:middle;margin-right:2px">' +
                    '<rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>' +
                    '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>' +
                    '</svg><span data-i18n="codeblock.copyBtn">' + i18n('codeblock.copyBtn') + '</span>';
            }, 1500);
        });
    });

    // 双击语言标签 → 打开语言选择浮层
    label.addEventListener('mousedown', function(e){ e.preventDefault(); e.stopPropagation(); });
    label.addEventListener('dblclick', function(e){
        e.preventDefault(); e.stopPropagation();
        var api = window.editorApi;
        if (api && api._readOnly) return;
        if (window.editorApi && window.editorApi._codeLangPicker) {
            window.editorApi._codeLangPicker.open(label, node.attrs.language || null, applyLanguage);
        }
    });

    function applyLanguage(lang) {
        var ed = window.editorApi && window.editorApi.editor;
        if (!ed || !getPos) return;
        try {
            var pos = getPos();
            if (typeof pos !== 'number') return;
            ed.view.dispatch(ed.state.tr.setNodeMarkup(pos, undefined, { language: lang || null }));
            // 浮层打开期间编辑器失焦、原选择已丢失，无参 focus() 会把光标与滚动
            // 带回文档头——显式把光标放回本代码块内部并滚动到可视区再交还焦点
            ed.commands.setTextSelection(pos + 1);
            ed.view.dispatch(ed.state.tr.scrollIntoView());
        } catch (err) { console.warn('设置代码块语言失败:', err); }
        ed.commands.focus();
    }

    return {
        dom: dom,
        contentDOM: code,
        update: function(newNode) {
            if (newNode.type !== node.type) return false;
            node = newNode;
            renderLabel();
            return true;
        },
        selectNode: function(){ dom.classList.add('ProseMirror-selectednode'); },
        deselectNode: function(){ dom.classList.remove('ProseMirror-selectednode'); },
        stopEvent: function(ev){
            return !!(ev.target && ev.target.closest && ev.target.closest('.code-block-toolbar'));
        },
        ignoreMutation: function(mut){
            return !this.contentDOM || !this.contentDOM.contains(mut.target);
        }
    };
}

// 判断是否为绝对路径：Unix（/foo）、Windows 盘符（C:\foo 或 C:/foo）、
// Windows 根相对/UNC（\foo、\\server\share）。带协议 URL / 相对路径返回 false。
function isAbsolutePath(p) {
    return p.charAt(0) === '/' || p.charAt(0) === '\\' || /^[a-zA-Z]:[\\/]/.test(p);
}

// 磁盘路径 → URL 斜杠形式：反斜杠统一为 '/';Windows 盘符绝对路径前置 '/'，
// 避免 'C:' 被 QUrl 解析为 host（localimg:///C:/...，path() 返回 /C:/...）。
function toSlashPath(p) {
    var s = p.replace(/\\/g, '/');
    if (/^[a-zA-Z]:\//.test(s)) s = '/' + s;
    return s;
}

// 界面文案字典（源语言中文；emoji 分类名保持中文不在此列）
var I18N = {
  zh_CN: {
    'placeholder.empty': '开始编写 Markdown...',
    'common.cancel': '取消',
    'common.ok': '确定',
    'common.close': '关闭',
    'link.editTitle': '编辑超链接',
    'link.text': '链接文字',
    'link.url': '链接地址',
    'link.remove': '删除链接',
    'link.tip.click': '点击访问',
    'link.tip.ctrlClick': 'Ctrl+点击访问',
    'math.blockTag': '块级公式',
    'math.inlineTag': '行内公式',
    'math.latexLabel': 'LaTeX 源码',
    'math.preview': '预览',
    'math.previewHint': '（输入 LaTeX 公式）',
    'mermaid.editTitle': '编辑 Mermaid 图表',
    'mermaid.insertTitle': '插入 Mermaid 图表',
    'mermaid.srcLabel': 'Mermaid 源码（如 graph TD; A-->B）',
    'mermaid.hint': '支持 flowchart / sequence / class / state / pie / gantt 等，详见 mermaid.js.org',
    'mermaid.previewHint': '（输入 Mermaid 源码预览图表）',
    'mermaid.renderFail': '渲染失败：',
    'mermaid.empty': '（空图表，双击编辑）',
    'codeLang.search': '搜索语言...',
    'codeLang.auto': '自动检测',
    'codeLang.current': ' (当前)',
    'codeblock.copy': '复制代码',
    'codeblock.changeLang': '双击修改语言',
    'footnote.editId': '双击修改脚注标记',
    'emoji.search': '搜索表情（如 笑 / heart / +1）',
    'emoji.freq': '常用表情符号',
    'emoji.empty': '无匹配表情',
    'editor.failInit': '编辑器内核加载失败：未找到 window.TipTap。请检查 tiptap-all.js。',
    // 简报字典之外的补充键（自检补漏，均为用户可见文案）
    'footnote.prompt': '脚注标记:',
    'mermaid.editTip': '双击编辑图表源码',
    'codeblock.copyBtn': '复制',
    'codeblock.copied': '已复制'
  },
  en_US: {
    'placeholder.empty': 'Start writing Markdown...',
    'common.cancel': 'Cancel',
    'common.ok': 'OK',
    'common.close': 'Close',
    'link.editTitle': 'Edit Link',
    'link.text': 'Link Text',
    'link.url': 'Link URL',
    'link.remove': 'Remove Link',
    'link.tip.click': 'Click to visit',
    'link.tip.ctrlClick': 'Ctrl+Click to visit',
    'math.blockTag': 'Block Formula',
    'math.inlineTag': 'Inline Formula',
    'math.latexLabel': 'LaTeX Source',
    'math.preview': 'Preview',
    'math.previewHint': '(Enter a LaTeX formula)',
    'mermaid.editTitle': 'Edit Mermaid Diagram',
    'mermaid.insertTitle': 'Insert Mermaid Diagram',
    'mermaid.srcLabel': 'Mermaid Source (e.g. graph TD; A-->B)',
    'mermaid.hint': 'Supports flowchart / sequence / class / state / pie / gantt, etc. See mermaid.js.org',
    'mermaid.previewHint': '(Enter Mermaid source to preview)',
    'mermaid.renderFail': 'Render failed: ',
    'mermaid.empty': '(Empty diagram, double-click to edit)',
    'codeLang.search': 'Search language...',
    'codeLang.auto': 'Auto Detect',
    'codeLang.current': ' (current)',
    'codeblock.copy': 'Copy code',
    'codeblock.changeLang': 'Double-click to change language',
    'footnote.editId': 'Double-click to edit footnote marker',
    'emoji.search': 'Search emoji (e.g. smile / heart / +1)',
    'emoji.freq': 'Frequently used',
    'emoji.empty': 'No matching emoji',
    'editor.failInit': 'Editor core failed to load: window.TipTap not found. Check tiptap-all.js.',
    // 简报字典之外的补充键（自检补漏，均为用户可见文案）
    'footnote.prompt': 'Footnote marker:',
    'mermaid.editTip': 'Double-click to edit diagram source',
    'codeblock.copyBtn': 'Copy',
    'codeblock.copied': 'Copied'
  }
};

// 当前界面语言（与 C++ LanguageManager 同步；默认中文）
var _uiLang = 'zh_CN';

function i18n(key) {
  var t = I18N[_uiLang];
  if (t && t[key] !== undefined) return t[key];
  return I18N.zh_CN[key] !== undefined ? I18N.zh_CN[key] : key;
}

// 把元素文本设为 i18n(key) 并打 data-i18n 标记（setLanguage 时全量重刷）
function _setI18n(el, key) {
  el.setAttribute('data-i18n', key);
  el.textContent = i18n(key);
}

// 把元素 title 设为 i18n(key) 并打 data-i18n-title 标记（setLanguage 时全量重刷）
function _setI18nTitle(el, key) {
  el.setAttribute('data-i18n-title', key);
  el.title = i18n(key);
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
    _onSourceInput: null,
    _readOnly: false,
    _lang: 'zh_CN',          // 当前界面语言（setLanguage 更新，与 C++ LanguageManager 同步）
    _imageSeq: 0,            // 占位图 requestId 自增计数
    _imagePending: {},       // requestId -> marker（占位图 src，回调时据此定位节点）
    _docDir: '',             // 当前文档目录（绝对路径），把相对图片 src 解析为 localimg://

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
                hardBreak: false,
                heading: { levels: [1, 2, 3, 4, 5, 6] },
                link: { openOnClick: false }
            }),
            TipTapTableKit.configure({ resizable: true }),
            TipTapTaskList,
            TipTapTaskItem.configure({ nested: true }),
            TipTapHighlight,
            ImageWithNodeView.configure({ allowBase64: true }),
            TipTapPlaceholder.configure({ placeholder: function() { return i18n('placeholder.empty'); } }),
        ];
        if (CodeBlockLowlightWithNodeView && window.lowlight)
            exts.push(CodeBlockLowlightWithNodeView.configure({ lowlight: window.lowlight }));
        if (TipTapMathematics)
            exts.push(TipTapMathematics);
        if (TipTapSubscript)
            exts.push(TipTapSubscript);
        if (TipTapSuperscript)
            exts.push(TipTapSuperscript);
        // 文字对齐：作用于段落与标题（对齐信息不在 .md 中保存，往返有损）
        // 工具栏已移除两端对齐按钮，这里限定 alignments 防御性禁用 justify
        if (TipTapTextAlign)
            exts.push(TipTapTextAlign.configure({ types: ['heading', 'paragraph'], alignments: ['left', 'center', 'right'] }));
        // Emoji 短代码 InputRule（:shortcode: → Unicode 字符）
        if (TipTapExtension && TipTapInputRule)
            exts.push(buildEmojiExtension());
        // 颜色 span（<span style> 内联样式 mark，parseHTML 仅认带 style 的 span）
        if (TipTapMark)
            exts.push(buildColorSpanExtension());
        // kbd / small 内联标签（手写 HTML 解析 + 往返保真，无 Markdown 语法）
        if (TipTapMark) {
            exts.push(buildKbdExtension());
            exts.push(buildSmallExtension());
        }
        // div 块级容器（仅包裹内联内容，排除 data-type div）
        if (TipTapNode)
            exts.push(buildDivBlockExtension());
        // 自定义 HardBreak（普通 <br>）+ brk 节点（<br data-brk>，BRMARKER 标记）
        if (TipTapNode) {
            exts.push(buildCustomHardBreak());
            exts.push(buildBrkExtension());
        }
        // Mermaid 块节点（自定义 NodeView，priority 高于 codeBlock）
        if (TipTapNode) {
            exts.push(buildMermaidNode());
            exts.push(buildFootnoteReference());
            exts.push(buildFootnoteContainer());
            exts.push(buildFootnoteDef());
        }
        // 搜索高亮（自写 ProseMirror plugin，TipTap Extension 包装）
        if (TipTapExtension && window.searchPlugin)
            exts.push(buildSearchExtension());
        return exts;
    },

    _createEditor: function(container, content) {
        if (!TipTapEditor) {
            console.error('TipTap bundle 未加载，请确认 tiptap-all.js 已正确引入。');
            container.textContent = i18n('editor.failInit');
            return null;
        }
        var self = this;
        var config = {
            element: container,
            extensions: this._buildExtensions(),
            editorProps: {
                handlePaste: function(view, event) {
                    return window.editorApi._handleImageClipboard(view, event, false);
                },
                handleDrop: function(view, event) {
                    return window.editorApi._handleImageClipboard(view, event, true);
                }
            },
            onUpdate: function(args) {
                if (self._shouldIgnoreUpdate()) return;
                // 仅内容变更事务标记脏状态（Focus 等插件会派发 meta-only 事务）
                if (args.transaction.docChanged) {
                    self._markDirty();
                }
                self.notifyHeadings();
                self.notifyWordCount();
                self.notifyHistory();
                self._notifySearchCount();
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
        var editor = new TipTapEditor(config);
        return editor;
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
            document.querySelector('#editor').textContent = i18n('editor.failInit');
            return;
        }
        var el = document.querySelector('#editor');
        // 抑制构造期 onUpdate（Focus 等插件会在加载后异步派发事务）
        this._loading = true;
        this.editor = this._createEditor(el);
        this._loading = false;
        _syncHighlightTheme();  // 初始化时同步代码块高亮主题
        if (this.editor) {
            this.notifyHeadings();
            this._attachMathClickHandler(this.editor.view.dom);
            this._syncLinkHandlers();
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
        // 文字对齐：优先检查 paragraph/heading 的 textAlign（TipTap TextAlign 扩展设置），
        // 其次检查 divBlock 的 align 属性（手写 HTML <div align="..."> 解析），
        // 再次检查表格单元格的 align 属性（GFM 列对齐，加载自 :-: / ---:），
        // 未显式设置时默认左对齐
        format.textAlign = ed.getAttributes('paragraph').textAlign
            || ed.getAttributes('heading').textAlign
            || ed.getAttributes('divBlock').align
            || ed.getAttributes('tableCell').align
            || ed.getAttributes('tableHeader').align
            || 'left';
        // 颜色 span 的 style 字符串（文字色/背景色回显）
        format.colorSpanStyle = ed.getAttributes('colorSpan').style || '';
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
            // setContent 在 _loading 锁内重建编辑器，onUpdate/onSelectionUpdate 均
            // 被抑制，需在此主动通知大纲（否则要等用户点击编辑区才触发）
            this.notifyHeadings();
        }
        this._attachMathClickHandler(this.editor.view.dom);
        this._syncLinkHandlers();
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
    // 表格整列设置对齐：GFM 列对齐是列属性（分隔行 :-: / --: / :-），
    // 与加载侧 toHtml 的整列提升对偶，工具栏必须更新整列而非单个单元格
    // （只改光标格会让列内残留旧值，保存侧"列内首个非空对齐"被表头旧值
    // 覆盖，用户操作往返丢失）。列号按行内单元格序号定位——常规表格
    // 均为 1x1 单元格，序号即列号；合并单元格场景为近似。
    setTableColumnAlign: function(align) {
        var ed = this.editor;
        if (!ed) return false;
        var state = ed.state, $from = state.selection.$from;
        // 1) 找光标所在单元格（tableCell/tableHeader）与所在表格
        var cellDepth = -1, tableDepth = -1;
        for (var d = $from.depth; d > 0; d--) {
            var name = $from.node(d).type.name;
            if ((name === 'tableCell' || name === 'tableHeader') && cellDepth < 0) cellDepth = d;
            if (name === 'table') { tableDepth = d; break; }
        }
        if (cellDepth < 0 || tableDepth < 0) return false;
        // 2) 光标单元格在其行内的序号（= 列号）
        var cellNode = $from.node(cellDepth);
        var col = 0;
        $from.node(cellDepth - 1).forEach(function(child, _off, i) {
            if (child === cellNode) col = i;
        });
        // 3) 遍历表格各行，更新该序号单元格的 align（仅属性变化，位置不变）
        var tr = state.tr;
        var tablePos = $from.before(tableDepth);
        $from.node(tableDepth).forEach(function(row, rowOff) {
            var idx = 0;
            row.forEach(function(cell, cellOff) {
                if (idx === col) {
                    var pos = tablePos + 1 + rowOff + 1 + cellOff;
                    tr.setNodeMarkup(pos, null,
                        Object.assign({}, cell.attrs, { align: align }));
                }
                idx++;
            });
        });
        ed.view.dispatch(tr);
        return true;
    },
    setTextAlign: function(alignment) {
        if (!this.editor) return;
        // 光标在 divBlock 内 → 更新 divBlock 的 align 属性
        // （TipTap TextAlign 扩展仅作用于 paragraph/heading，不处理 divBlock）
        var divAttrs = this.editor.getAttributes('divBlock');
        // 光标在表格单元格内 → 整列更新 align（GFM 列对齐语义，保存时由
        // fromHtml 还原为分隔行 :---: / ---:；段落级对齐在表格内无法往返，
        // 且单元格内 <p style="text-align"> 会触发 ZZA 破坏表格）
        var inCell = this.editor.isActive('tableHeader') || this.editor.isActive('tableCell');
        if (divAttrs.align !== undefined) {
            var newAlign = (alignment === 'left') ? null : alignment;
            this.editor.chain().updateAttributes('divBlock', {align: newAlign}).run();
        } else if (inCell && this.setTableColumnAlign((alignment === 'left') ? null : alignment)) {
            // 整列已更新（setNodeMarkup 触发 onUpdate 脏标记）
        } else {
            this.editor.commands.setTextAlign(alignment);
        }
        // updateAttributes 只触发 onUpdate（不触发 onSelectionUpdate），
        // 而 onUpdate 内部不调用 notifyFormat，因此手动通知确保工具栏立即同步
        this.notifyFormat();
    },
    // === 内联颜色（colorSpan mark 的样式合并命令） ===
    applyTextColor: function(color) {
        if (!this.editor) return;
        if (!color) { this.clearTextColor(); return; }
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        cur.color = color;
        this.editor.commands.setMark('colorSpan', { style: _stringifyColorStyle(cur) });
    },
    applyBgColor: function(color) {
        if (!this.editor) return;
        if (!color) { this.clearBgColor(); return; }
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        cur.bg = color;
        this.editor.commands.setMark('colorSpan', { style: _stringifyColorStyle(cur) });
    },
    clearTextColor: function() {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        delete cur.color;
        var s = _stringifyColorStyle(cur);
        if (s) this.editor.commands.setMark('colorSpan', { style: s });
        else this.editor.commands.unsetMark('colorSpan');
    },
    clearBgColor: function() {
        if (!this.editor) return;
        var cur = _parseColorStyle(this.editor.getAttributes('colorSpan').style || '');
        delete cur.bg;
        var s = _stringifyColorStyle(cur);
        if (s) this.editor.commands.setMark('colorSpan', { style: s });
        else this.editor.commands.unsetMark('colorSpan');
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
    insertCodeBlock: function(language) {
        if (!this.editor) return;
        var attrs = language ? { language: language } : undefined;
        this.editor.commands.toggleCodeBlock(attrs);
    },
    addRowBefore: function() { if (this.editor) this.editor.commands.addRowBefore(); },
    addRowAfter: function() { if (this.editor) this.editor.commands.addRowAfter(); },
    deleteRow: function() { if (this.editor) this.editor.commands.deleteRow(); },
    addColumnBefore: function() { if (this.editor) this.editor.commands.addColumnBefore(); },
    addColumnAfter: function() { if (this.editor) this.editor.commands.addColumnAfter(); },
    deleteColumn: function() { if (this.editor) this.editor.commands.deleteColumn(); },
    deleteTable: function() { if (this.editor) this.editor.commands.deleteTable(); },
    isInTable: function() { return this.editor ? this.editor.isActive('table') : false; },
    insertLink: function() {
        // 工具栏/菜单「插入链接」：与双击编辑链接共用 _linkEditor 对话框
        // （链接文字 + 链接地址两字段），确认后在光标处插入 [文字](URL)
        if (!this.editor) return;
        this._linkEditor.open();
    },
    insertImage: function(path) {
        if (this.editor) this.editor.commands.setImage({ src: path });
    },
    insertHorizontalRule: function() { if (this.editor) this.editor.commands.setHorizontalRule(); },

    // === 图片粘贴/拖拽落盘 ===
    _mimeToExt: function(mime) {
        var map = { 'image/png':'png', 'image/jpeg':'jpg', 'image/jpg':'jpg',
                    'image/gif':'gif', 'image/webp':'webp', 'image/bmp':'bmp',
                    'image/svg+xml':'svg', 'image/x-icon':'ico', 'image/tiff':'tiff' };
        return map[mime] || 'png';
    },

    // ProseMirror handlePaste/handleDrop 共用入口
    _handleImageClipboard: function(view, event, isDrop) {
        if (!this.bridge) return false;                       // 桥未就绪，交默认
        var dt = isDrop ? event.dataTransfer : event.clipboardData;
        if (!dt || !dt.files || !dt.files.length) return false;
        var handled = false;
        for (var i = 0; i < dt.files.length; ++i) {
            var f = dt.files[i];
            if (!f.type || f.type.indexOf('image/') !== 0) continue;  // 仅图片
            handled = true;
            this._ingestImageFile(f);
        }
        if (handled) event.preventDefault();
        return handled;
    },

    _ingestImageFile: function(file) {
        var requestId = 'img_' + (++this._imageSeq);
        var marker = '__pending_' + requestId + '__';
        // 同步插入占位图（位置正确 + 即时反馈）
        if (this.editor) this.editor.chain().focus().setImage({ src: marker, alt: file.name || '' }).run();
        this._imagePending[requestId] = marker;

        var self = this;
        var reader = new FileReader();
        reader.onload = function() {
            var dataURL = reader.result || '';
            var base64 = dataURL.split(',')[1] || '';
            var ext = self._mimeToExt(file.type);
            try {
                self.bridge.requestImageSave(requestId, base64, ext, file.name || '');
            } catch (e) {                                 // 桥异常：清理占位图
                self._removeImageBySrc(marker);
                delete self._imagePending[requestId];
            }
        };
        reader.onerror = function() {                               // 读取失败：清理占位图
            self._removeImageBySrc(marker);
            delete self._imagePending[requestId];
        };
        reader.readAsDataURL(file);
    },

    _removeImageBySrc: function(src) {
        if (!this.editor) return;
        var pos = null, size = 0;
        this.editor.state.doc.descendants(function(node, p) {
            if (pos !== null) return false;
            if (node.type.name === 'image' && node.attrs.src === src) { pos = p; size = node.nodeSize; return false; }
            return true;
        });
        if (pos !== null) this.editor.view.dispatch(this.editor.state.tr.delete(pos, pos + size));
    },

    // C++ 回调：成功替换占位图 src；失败删除占位图
    _onImageSaveResult: function(requestId, result) {
        var marker = this._imagePending[requestId];
        delete this._imagePending[requestId];
        if (!this.editor || !marker) return;

        var pos = null, node = null;
        this.editor.state.doc.descendants(function(n, p) {
            if (node) return false;
            if (n.type.name === 'image' && n.attrs.src === marker) { node = n; pos = p; return false; }
            return true;
        });
        if (!node) return;

        if (result && result.ok && result.path) {
            var attrs = Object.assign({}, node.attrs, { src: result.path });
            this.editor.view.dispatch(this.editor.state.tr.setNodeMarkup(pos, null, attrs));
        } else {
            this.editor.view.dispatch(this.editor.state.tr.delete(pos, pos + node.nodeSize));
        }
    },

    // C++ 在打开/保存/会话恢复时注入文档目录；并刷新已渲染图片的显示 src
    setDocDir: function(dir) {
        this._docDir = dir || '';
        var root = this.editor && this.editor.view && this.editor.view.dom;
        if (!root) return;
        var imgs = root.querySelectorAll('img[data-src]');
        for (var i = 0; i < imgs.length; ++i) {
            imgs[i].src = this._resolveImageSrc(imgs[i].getAttribute('data-src'));
        }
    },

    // 相对路径 → localimg://<文档目录>/<相对>；绝对路径（Unix / Windows）→ localimg://<路径>；
    // 由 C++ LocalImageSchemeHandler 读盘返回字节（绕过 qrc 页面加载 file:// 的限制）；
    // 占位 marker / 网络 URL 原样返回。attrs.src 始终存原始路径，保往返保真。
    _resolveImageSrc: function(raw) {
        var out;
        if (!raw) out = '';
        else if (raw.indexOf('__pending_') === 0) out = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7'; // 占位图 marker：渲染为透明 1x1，避免被浏览器当作相对 URL 去 qrc:/web/ 加载而报 "QResource not found"（落盘回调后 attrs.src 被替换为真实路径，nodeView 重新解析为 localimg://）
        else if (/^(data:|blob:|https?:|file:|qrc:)/i.test(raw)) out = raw; // 带协议的 URL
        else if (isAbsolutePath(raw)) out = 'localimg://' + toSlashPath(raw);    // 绝对路径（Unix / Windows 盘符 / UNC）→ localimg://（Handler 读盘；attrs.src 仍存原始路径保往返保真）
        else { var base = this._docDir || ''; out = base ? ('localimg://' + toSlashPath(base).replace(/\/+$/, '') + '/' + raw.replace(/[\\/]+/g, '/').replace(/^\/+/, '')) : raw; } // 相对路径 / 文档目录未知：原样返回
        return out;
    },


    // 清除样式：选中文字（或无选中时光标所在段落）→ 剥离所有 inline marks 及
    // 自定义 block 节点（divBlock 等），重置为正文段落，得到最干净的"正文"
    clearFormatting: function() {
        if (!this.editor) return;
        var ed = this.editor;
        var state = ed.state;
        var sel = state.selection;
        var from = sel.from;
        var to = sel.to;

        // 无选中 → 扩展到当前文本块全文
        if (from === to) {
            var $pos = state.doc.resolve(from);
            if ($pos.parent.isTextblock) {
                from = $pos.start();
                to = $pos.end();
            }
        }

        // 阶段 1: 链式清除 marks + block → paragraph。
        // 不调用 setTextAlign：clearNodes 内部的 setBlockType(paragraph)
        // 已重置节点属性为默认值；显式 setTextAlign('left') 反而给 paragraph
        // 加上 text-align:left，触发 fromHtml ZZA 机制重包为 <div align="left">。
        ed.chain()
            .setTextSelection({ from: from, to: to })
            .unsetAllMarks()
            .clearNodes()
            .run();

        // 阶段 2: clearNodes 仅处理标准 block（heading/blockquote/codeBlock），
        // 不处理自定义 divBlock。用 tr.replaceWith 替换 divBlock → paragraph。
        state = ed.state;
        sel = state.selection;
        from = sel.from;
        to = sel.to;
        if (from === to) {
            var $pos2 = state.doc.resolve(from);
            if ($pos2.parent.isTextblock) {
                from = $pos2.start();
                to = $pos2.end();
            }
        }

        var tr = state.tr;
        var changed = false;
        var pType = state.schema.nodes.paragraph;
        if (pType) {
            state.doc.nodesBetween(from, to, function(node, pos) {
                if (node.type.name === 'divBlock') {
                    tr.replaceWith(pos, pos + node.nodeSize,
                                  pType.create(null, node.content));
                    changed = true;
                    return false;
                }
            });
        }
        if (changed) ed.view.dispatch(tr);

        this._markDirty();
    },

    // 进入源码模式（C++ 侧传入已转换好的 Markdown）
    enterSourceMode: function(md) {
        if (!this.editor) return;
        this._sourceValue = md;
        var sourceView = document.getElementById('source-view');
        var editorDiv = document.getElementById('editor');
        sourceView.value = md;
        editorDiv.style.display = 'none';
        sourceView.style.display = '';
        if (this._readOnly) sourceView.setAttribute('readonly', '');
        else sourceView.removeAttribute('readonly');
        this.isSourceMode = true;
        // 绑定 input 监听：源码区编辑时实时标记脏状态（对齐 WYSIWYG 的 onUpdate 实时标记）。
        // 程序化 sourceView.value = md 不会触发 input 事件，初始填充安全。
        var api = this;
        this._onSourceInput = function() { api._markDirty(); };
        sourceView.addEventListener('input', this._onSourceInput);
    },

    // 退出源码模式（C++ 侧传入已转换好的 HTML）
    exitSourceMode: function(html) {
        if (!this.editor) return;
        var sourceView = document.getElementById('source-view');
        var editorDiv = document.getElementById('editor');
        var changed = sourceView.value !== this._sourceValue;
        // 解绑 input 监听，避免退出后残留引用。
        if (this._onSourceInput) {
            sourceView.removeEventListener('input', this._onSourceInput);
            this._onSourceInput = null;
        }
        this._loading = true;
        this.editor.commands.setContent(html);
        this._loading = false;
        sourceView.style.display = 'none';
        editorDiv.style.display = '';
        this.isSourceMode = false;
        if (changed && !this._dirty) this._markDirty();
        this.notifyFormat();
        this.notifyHeadings();
        this.notifyWordCount();
        this.notifyHistory();
    },

    // 获取源码区当前内容（C++ 侧用于 MD→HTML 转换）
    getSourceContent: function() {
        var sv = document.getElementById('source-view');
        return sv ? sv.value : '';
    },
    scrollToPos: function(pos) {
        if (!this.editor) return;
        var dom = this.editor.view.domAtPos(Math.min(pos + 1, this.editor.state.doc.content.size));
        if (dom && dom.node) {
            var el = dom.node.nodeType === 3 ? dom.node.parentElement : dom.node;
            if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    },
    // === 搜索 ===
    startSearch: function (query, caseSensitive, regex, wholeWord) {
        if (!this.editor || this.isSourceMode) return;
        var sp = window.searchPlugin; if (!sp) return;
        sp.requestSearch(this.editor.view, query || '', {
            regex: !!regex, wholeWord: !!wholeWord, caseSensitive: !!caseSensitive
        });
        this._notifySearchCount();
    },
    // 切回标签页恢复搜索：插件 state 逐 tab 独立，若查询参数未变则保留
    // 当前匹配位置（current），仅刷新计数；参数变了才重新搜索。
    resumeSearch: function (query, caseSensitive, regex, wholeWord) {
        if (!this.editor || this.isSourceMode) return;
        var sp = window.searchPlugin; if (!sp) return;
        var st = sp.searchKey.getState(this.editor.state);
        var same = st && st.query === (query || '')
            && !!st.flags.regex === !!regex
            && !!st.flags.wholeWord === !!wholeWord
            && !!st.flags.caseSensitive === !!caseSensitive;
        if (!same) {
            sp.requestSearch(this.editor.view, query || '', {
                regex: !!regex, wholeWord: !!wholeWord, caseSensitive: !!caseSensitive
            });
        }
        this._notifySearchCount();
    },
    searchNext: function () {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.searchNext(this.editor.view);
        this._focusAfterSearchNav();
        this._notifySearchCount();
    },
    searchPrev: function () {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.searchPrev(this.editor.view);
        this._focusAfterSearchNav();
        this._notifySearchCount();
    },
    // 搜索导航后聚焦：可编辑用 focus 命令（显示选区）；
    // 只读模式下 TipTap focus 命令内部 editable && 才聚焦、ProseMirror 选区
    // 同步（selectionToDOM）也要求编辑器在焦点链上，均不生效——须 view.focus()
    // 强制聚焦 DOM，选区才能写入并显示；再主动滚动到当前匹配（tr.scrollIntoView
    // 依赖选区宿主，聚焦前已派发的事务滚动可能丢失）。
    _focusAfterSearchNav: function () {
        if (this._readOnly) {
            this.editor.view.focus();
            var st = window.searchPlugin.searchKey.getState(this.editor.state);
            if (st && st.matches.length && st.current >= 0) {
                var m = st.matches[st.current];
                var pos = this.editor.view.domAtPos(m.from);
                var el = pos.node.nodeType === 3 ? pos.node.parentElement : pos.node;
                if (el) el.scrollIntoView({ behavior: 'smooth', block: 'center' });
            }
        } else {
            this.editor.commands.focus();   // 焦点移入编辑器，显示匹配处选区
        }
    },
    replaceOne: function (rep) {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.replaceOne(this.editor.view, rep || '');
        this._notifySearchCount();
    },
    replaceAll: function (rep) {
        if (!this.editor || this.isSourceMode) return;
        window.searchPlugin.replaceAll(this.editor.view, rep || '');
        this._notifySearchCount();
    },
    stopSearch: function () {
        if (!this.editor) return;
        if (window.searchPlugin) window.searchPlugin.clearSearch(this.editor.view);
        this._notifySearchCount();
    },
    _notifySearchCount: function () {
        if (!this.editor || !this.bridge) return;
        var sp = window.searchPlugin;
        var st = sp ? sp.searchCurrent(this.editor.state) : { total: 0, current: 0 };
        this.bridge.searchMatchChanged(st.total, st.current);
    },
    undo: function() { if (this.editor) this.editor.commands.undo(); },
    redo: function() { if (this.editor) this.editor.commands.redo(); },
    setTheme: function(theme) {
        if (theme === 'dark') document.body.classList.add('dark');
        else document.body.classList.remove('dark');
        _syncHighlightTheme();
        // 重置 mermaid 初始化标记，使后续渲染采用新主题；并重绘已存在的图表
        window.__mermaidInited = false;
        try { _repaintMermaidViews(); } catch (e) { /* 主题切换不应被图表渲染阻断 */ }
    },

    setLanguage: function(lang) {
        this._lang = lang;
        _uiLang = lang;
        // 全量重刷带 data-i18n 标记的静态标签（NodeView tooltip/空态/弹窗按钮等）
        var els = document.querySelectorAll('[data-i18n]');
        for (var i = 0; i < els.length; i++) {
            var key = els[i].getAttribute('data-i18n');
            if (key) els[i].textContent = i18n(key);
        }
        // 再刷一遍带 data-i18n-title 标记的 title（NodeView tooltip 类标签）
        var titleEls = document.querySelectorAll('[data-i18n-title]');
        for (var j = 0; j < titleEls.length; j++) {
            var tk = titleEls[j].getAttribute('data-i18n-title');
            if (tk) titleEls[j].title = i18n(tk);
        }
        // 占位符：TipTap Placeholder 配置为函数读取当前语言，派发一个空事务触发重渲染
        if (this.editor) {
            var ed = this.editor;
            ed.view.dispatch(ed.state.tr);  // 无内容变更，仅触发视图/装饰重算
        }
    },
    applyCustomCss: function(css) {
        var style = document.getElementById('custom-css');
        if (css) {
            if (!style) {
                style = document.createElement('style');
                style.id = 'custom-css';
                document.head.appendChild(style);
            }
            style.textContent = css;
        } else if (style) {
            style.parentNode.removeChild(style);
        }
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

    // 确保链接悬浮提示 DOM 存在
    _ensureLinkTooltip: function() {
        if (this._linkTooltip) return;
        var tip = document.createElement('div');
        tip.className = 'link-tooltip';
        document.body.appendChild(tip);
        this._linkTooltip = tip;
    },

    // 根据当前 _readOnly 状态安装或移除链接处理器
    _syncLinkHandlers: function() {
        if (!this.editor) return;
        var el = this.editor.view.dom;
        // 先清理旧处理器
        if (this._ctrlLinkHandler) {
            el.removeEventListener('mousedown', this._ctrlLinkHandler, true);
            this._ctrlLinkHandler = null;
        }
        if (this._linkHandler) {
            el.removeEventListener('mousedown', this._linkHandler, true);
            this._linkHandler = null;
        }
        if (this._linkHoverIn) {
            el.removeEventListener('mouseover', this._linkHoverIn, true);
            this._linkHoverIn = null;
        }
        if (this._linkHoverMove) {
            el.removeEventListener('mousemove', this._linkHoverMove, true);
            this._linkHoverMove = null;
        }
        if (this._linkHoverOut) {
            el.removeEventListener('mouseout', this._linkHoverOut, true);
            this._linkHoverOut = null;
        }
        if (this._linkDblClick) {
            el.removeEventListener('dblclick', this._linkDblClick, true);
            this._linkDblClick = null;
        }
        this._ensureLinkTooltip();
        var tip = this._linkTooltip;
        // 查找事件目标是否在链接内
        function findLink(target, root) {
            var t = target;
            while (t && t !== root) {
                if (t.tagName === 'A' && t.href) return t;
                t = t.parentElement;
            }
            return null;
        }
        // 显示提示（编辑模式提示 Ctrl+点击，只读模式提示直接点击）
        var readOnly = this._readOnly;
        var showTip = function(link, x, y) {
            if (!link || !link.href) return;
            var label = readOnly ? i18n('link.tip.click') : i18n('link.tip.ctrlClick');
            tip.innerHTML = '<span class="tip-label">' + label + '</span>' +
                '<span class="tip-url">' + _escapeText(link.href) + '</span>';
            tip.style.display = 'block';
            tip.style.left = (x + 12) + 'px';
            tip.style.top = (y + 16) + 'px';
        };
        var hideTip = function() {
            tip.style.display = 'none';
        };
        // 悬浮提示：mouseover 显示，mousemove 跟随，mouseout 隐藏
        this._linkHoverIn = function(e) {
            var link = findLink(e.target, el);
            if (link) showTip(link, e.clientX, e.clientY);
        };
        this._linkHoverMove = function(e) {
            var link = findLink(e.target, el);
            if (link) showTip(link, e.clientX, e.clientY);
            else hideTip();
        };
        this._linkHoverOut = function(e) {
            var link = findLink(e.relatedTarget || e.toElement, el);
            if (!link) hideTip();
        };
        el.addEventListener('mouseover', this._linkHoverIn, true);
        el.addEventListener('mousemove', this._linkHoverMove, true);
        el.addEventListener('mouseout', this._linkHoverOut, true);

        if (this._readOnly) {
            // 阅读模式：mousedown 拦截任意链接点击，通知 C++ 打开
            this._linkHandler = function(e) {
                var link = findLink(e.target, el);
                if (link) {
                    e.preventDefault();
                    e.stopPropagation();
                    if (window.editorApi.bridge)
                        window.editorApi.bridge.openUrl(link.href);
                }
            };
            el.addEventListener('mousedown', this._linkHandler, true);
        } else {
            // 编辑模式：Ctrl+左键点击链接 → 浏览器打开
            this._ctrlLinkHandler = function(e) {
                if (!(e.ctrlKey || e.metaKey)) return;
                if (e.button !== 0) return;
                var link = findLink(e.target, el);
                if (link) {
                    e.preventDefault();
                    e.stopPropagation();
                    if (window.editorApi.bridge)
                        window.editorApi.bridge.openUrl(link.href);
                }
            };
            el.addEventListener('mousedown', this._ctrlLinkHandler, true);

            // 双击链接 → 打开编辑超链接对话框
            this._linkDblClick = function(e) {
                var link = findLink(e.target, el);
                if (!link) return;
                e.preventDefault();
                e.stopPropagation();
                var ed = window.editorApi.editor;
                if (!ed) return;
                // 将选区定位到链接文本内并扩选 link mark，确保 _linkEditor.open()
                // 以 isActive('link') 识别为「编辑已有链接」而非「新建」
                var pos = ed.view.posAtDOM(link, 0);
                if (typeof pos === 'number') {
                    var inner = pos + 1;
                    var max = ed.state.doc.content.size;
                    if (inner > max) inner = max;
                    ed.chain().focus().setTextSelection(inner).extendMarkRange('link').run();
                }
                window.editorApi._linkEditor.open();
            };
            el.addEventListener('dblclick', this._linkDblClick, true);
        }
    },

    setEditable: function(editable) {
        if (!this.editor) return;
        this._loading = true;
        this.editor.setEditable(!!editable);
        this._loading = false;
        this._readOnly = !editable;
        var sv = document.getElementById('source-view');
        if (!editable) {
            if (sv) sv.setAttribute('readonly', '');
        } else {
            if (sv) sv.removeAttribute('readonly');
        }
        this._syncLinkHandlers();
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

    // 插入脚注（C++ 侧调用）：光标处插引用，文末容器追加空定义，焦点留正文
    insertFootnote: function() {
        var ed = this.editor;
        if (!ed) return;
        var schema = ed.state.schema;
        // 1) 扫描最大数字 id +1；无数字则从 1 起
        var maxNum = 0;
        ed.state.doc.descendants(function(node) {
            if (node.type.name === 'footnoteReference' || node.type.name === 'footnoteDef') {
                if (/^\d+$/.test(node.attrs.id)) {
                    var n = parseInt(node.attrs.id, 10);
                    if (n > maxNum) maxNum = n;
                }
            }
        });
        var newId = String(maxNum + 1);
        // 2) 光标处插入引用（insertContentAt 友好处理选区）
        ed.chain().focus().insertContentAt(
            ed.state.selection.from,
            [{ type: 'footnoteReference', attrs: { id: newId } }]
        ).run();
        // 3) 确保 footerContainer 存在并追加空定义
        ed.chain().command(function(ref) {
            var tr = ref.tr, state = ref.state, dispatch = ref.dispatch;
            var doc = state.doc;
            var last = doc.lastChild;
            var def = schema.nodes.footnoteDef.create({ id: newId });
            if (last && last.type.name === 'footnoteContainer') {
                // 容器内末尾（闭合标签前）插入 def
                tr.insert(doc.content.size - 1, def);
            } else {
                var container = schema.nodes.footnoteContainer.create(null, def);
                tr.insert(doc.content.size, container);
            }
            tr.scrollIntoView();
            dispatch(tr);
            return true;
        }).run();
    },

    // 改名（NodeView 双击触发）：遍历文档同步所有同 id 的 reference 与 def
    renameFootnote: function(fromId, toId) {
        var ed = this.editor;
        if (!ed) return;
        ed.chain().command(function(ref) {
            var tr = ref.tr, state = ref.state, dispatch = ref.dispatch;
            state.doc.descendants(function(node, pos) {
                if ((node.type.name === 'footnoteReference'
                     || node.type.name === 'footnoteDef')
                    && node.attrs.id === fromId) {
                    tr.setNodeMarkup(pos, null, { id: toId });
                }
            });
            dispatch(tr);
            return true;
        }).run();
    },

    // 打开 emoji 选择器浮层（C++ 侧传入定位坐标）
    openEmojiPicker: function(x, y) {
        window.editorApi._emojiPicker.open(x, y);
    },

    // 在光标处插入单个 emoji（供其它入口/测试复用）
    insertEmoji: function(ch) {
        if (this.editor) this.editor.commands.insertContent(ch);
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
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
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
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
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

            // 遮罩（仅视觉遮挡，阻止与底层编辑区交互，不响应点击关闭）
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
            tag.textContent = isBlock ? i18n('math.blockTag') : i18n('math.inlineTag');
            titleBar.appendChild(tag);

            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            _setI18nTitle(closeBtn, 'common.close');
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
            _setI18n(label, 'math.latexLabel');
            this.popover.appendChild(label);
            this.popover.appendChild(this.input);

            // 实时预览
            var prevLabel = document.createElement('div');
            prevLabel.className = 'math-editor-label';
            _setI18n(prevLabel, 'math.preview');
            this.popover.appendChild(prevLabel);
            this.preview = document.createElement('div');
            this.preview.className = 'math-editor-preview';
            this.popover.appendChild(this.preview);

            // 按钮
            var actions = document.createElement('div');
            actions.className = 'math-editor-actions';

            this.cancelBtn = document.createElement('button');
            _setI18n(this.cancelBtn, 'common.cancel');
            this.cancelBtn.addEventListener('click', function() { self.close(); });
            actions.appendChild(this.cancelBtn);

            this.okBtn = document.createElement('button');
            this.okBtn.className = 'primary';
            _setI18n(this.okBtn, 'common.ok');
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

            // Escape 关闭对话框（popover 层兜底，input/textarea 失焦时仍生效）
            this.popover.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { e.preventDefault(); self.close(); }
            });

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
                this.preview.innerHTML = '<span style="color:#888;font-size:17px;">' + i18n('math.previewHint') + '</span>';
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
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
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
        _bodyScrollTop: 0,
        _bodyOverflow: '',
        _escHandler: null,     // document 级 Escape 捕获监听引用

        // 双击打开（编辑模式）
        open: function(getPos, source) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._dirtyBefore = window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            // 模态：锁定 body 滚动（记录位置，恢复时还原）
            this._bodyScrollTop = document.documentElement.scrollTop || document.body.scrollTop;
            this._bodyOverflow = document.body.style.overflow;
            document.body.style.overflow = 'hidden';
            // 注册 document 级 Escape 捕获（capture phase，最优先，不受焦点影响）
            this._registerEsc();
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
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            this._bodyScrollTop = document.documentElement.scrollTop || document.body.scrollTop;
            this._bodyOverflow = document.body.style.overflow;
            document.body.style.overflow = 'hidden';
            this._registerEsc();
            this.getPos = null;
            this.mode = 'create';
            this.buildDom();
            this.show('');
            ed.view.dom.blur();
        },

        // 注册 document 级 Escape 捕获监听（capture phase 最先收到，无视焦点）
        _registerEsc: function() {
            if (this._escHandler) return; // 防止重复注册
            var self = this;
            this._escHandler = function(e) {
                if (e.key === 'Escape') {
                    e.preventDefault();
                    e.stopPropagation();
                    self.close();
                }
            };
            document.addEventListener('keydown', this._escHandler, true);
        },

        // 注销 document 级 Escape 监听
        _unregisterEsc: function() {
            if (this._escHandler) {
                document.removeEventListener('keydown', this._escHandler, true);
                this._escHandler = null;
            }
        },

        buildDom: function() {
            this.cleanupDom();
            var self = this;
            this.overlay = document.createElement('div');
            this.overlay.className = 'math-editor-overlay';

            this.popover = document.createElement('div');
            this.popover.className = 'math-editor-popover mermaid-popover';

            // 标题栏
            var titleBar = document.createElement('div');
            titleBar.className = 'math-editor-titlebar';
            var tag = document.createElement('span');
            tag.className = 'math-editor-type-tag';
            tag.textContent = this.mode === 'edit' ? i18n('mermaid.editTitle') : i18n('mermaid.insertTitle');
            titleBar.appendChild(tag);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            _setI18nTitle(closeBtn, 'common.close');
            closeBtn.addEventListener('click', function(e) { e.stopPropagation(); self.close(); });
            titleBar.appendChild(closeBtn);
            this.popover.appendChild(titleBar);

            // 源码输入
            var label = document.createElement('div');
            label.className = 'math-editor-label';
            _setI18n(label, 'mermaid.srcLabel');
            this.popover.appendChild(label);
            this.input = document.createElement('textarea');
            this.input.className = 'math-editor-input block mermaid-source';
            this.input.rows = 8;
            this.input.spellcheck = false;
            this.popover.appendChild(this.input);

            // 实时预览
            var prevLabel = document.createElement('div');
            prevLabel.className = 'math-editor-label';
            _setI18n(prevLabel, 'mermaid.preview');
            this.popover.appendChild(prevLabel);
            this.preview = document.createElement('div');
            this.preview.className = 'math-editor-preview mermaid-preview';
            this.popover.appendChild(this.preview);

            // 语法提示
            var hint = document.createElement('div');
            hint.className = 'mermaid-hint';
            _setI18n(hint, 'mermaid.hint');
            this.popover.appendChild(hint);

            // 按钮
            var actions = document.createElement('div');
            actions.className = 'math-editor-actions';
            this.cancelBtn = document.createElement('button');
            _setI18n(this.cancelBtn, 'common.cancel');
            this.cancelBtn.addEventListener('click', function() { self.close(); });
            actions.appendChild(this.cancelBtn);
            this.okBtn = document.createElement('button');
            this.okBtn.className = 'primary';
            _setI18n(this.okBtn, 'common.ok');
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

            // Escape 关闭对话框（popover 层兜底，textarea 失焦时仍生效）
            this.popover.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { e.preventDefault(); self.close(); }
            });

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
                p.innerHTML = '<span style="color:#888;font-size:14px;">' + i18n('mermaid.previewHint') + '</span>';
                return;
            }
            if (!window.mermaid) { p.textContent = src; return; }
            _ensureMermaidInited();
            var id = 'mermaid-prev-' + (++_mermaidIdSeq);
            window.mermaid.render(id, src).then(function(res) {
                p.innerHTML = _sanitizeMermaidSvg(res.svg || '');
                var svg = p.querySelector('svg');
                if (svg) { _fixNegativeRects(svg); svg.style.maxWidth = '100%'; svg.style.height = 'auto'; }
            }).catch(function(err) {
                p.innerHTML = '<span style="color:#c00;font-size:13px;">' + i18n('mermaid.renderFail')
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
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
            this._unregisterEsc();
            this.cleanupDom();
            this.getPos = null;
            // 恢复 body 滚动（模态锁定期间设为 hidden）
            document.body.style.overflow = this._bodyOverflow || '';
            if (this._bodyScrollTop !== undefined) {
                document.documentElement.scrollTop = this._bodyScrollTop;
                document.body.scrollTop = this._bodyScrollTop;
            }
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

    // ===== 超链接编辑面板（模态：遮罩覆盖编辑区） =====
    _linkEditor: {
        overlay: null,
        popover: null,
        textInput: null,
        urlInput: null,
        okBtn: null,
        cancelBtn: null,
        removeBtn: null,
        _href: '',
        _linkFrom: 0,
        _linkTo: 0,
        _dirtyBefore: false,
        _confirmed: false,
        _bodyOverflow: '',
        _isNew: false,  // 是否从选中文字创建（非编辑已有链接）

        // 右键菜单触发（选中文字或已有链接 → 统一入口）
        open: function() {
            var ed = window.editorApi.editor;
            if (!ed) return;

            var isLink = ed.isActive('link');
            var attrs = isLink ? ed.getAttributes('link') : null;

            if (isLink && attrs && attrs.href) {
                // 编辑已有链接：扩选到链接范围，预填 URL
                this._href = attrs.href;
                ed.chain().extendMarkRange('link').run();
                this._linkFrom = ed.state.selection.from;
                this._linkTo = ed.state.selection.to;
                this._isNew = false;
            } else {
                // 选中文字创建新链接；或光标处（无选区）直接插入新链接——
                // 无选区时 from==to==光标位置，confirm 阶段用 replaceWith 插入
                var sel = ed.state.selection;
                this._href = '';
                this._linkFrom = sel.from;
                this._linkTo = sel.to;
                this._isNew = true;
            }

            this._dirtyBefore = window.editorApi._dirty;
            this._confirmed = false;
            window.editorApi._mathEditing = true;
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            this._bodyOverflow = document.body.style.overflow;
            document.body.style.overflow = 'hidden';
            this.buildDom();
            this.show();
            ed.view.dom.blur();
        },

        buildDom: function() {
            this.cleanupDom();
            var self = this;
            this.overlay = document.createElement('div');
            this.overlay.className = 'math-editor-overlay';

            this.popover = document.createElement('div');
            this.popover.className = 'math-editor-popover link-editor-popover';

            // 标题栏
            var titleBar = document.createElement('div');
            titleBar.className = 'math-editor-titlebar';
            var tag = document.createElement('span');
            tag.className = 'math-editor-type-tag';
            _setI18n(tag, 'link.editTitle');
            titleBar.appendChild(tag);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'math-editor-close-btn';
            closeBtn.textContent = '×';
            _setI18nTitle(closeBtn, 'common.close');
            closeBtn.addEventListener('click', function(e) { e.stopPropagation(); self.close(); });
            titleBar.appendChild(closeBtn);
            this.popover.appendChild(titleBar);

            // 链接文字（可编辑）
            var textLabel = document.createElement('div');
            textLabel.className = 'math-editor-label';
            _setI18n(textLabel, 'link.text');
            this.popover.appendChild(textLabel);
            this.textInput = document.createElement('input');
            this.textInput.type = 'text';
            this.textInput.className = 'math-editor-input';
            this.textInput.spellcheck = false;
            this.popover.appendChild(this.textInput);

            // URL 输入
            var urlLabel = document.createElement('div');
            urlLabel.className = 'math-editor-label';
            _setI18n(urlLabel, 'link.url');
            this.popover.appendChild(urlLabel);
            this.urlInput = document.createElement('input');
            this.urlInput.type = 'url';
            this.urlInput.className = 'math-editor-input';
            this.urlInput.spellcheck = false;
            this.urlInput.placeholder = 'https://...';
            this.popover.appendChild(this.urlInput);

            // 按钮行（删除链接 | 取消 | 确定）
            var actions = document.createElement('div');
            actions.className = 'link-editor-actions';
            // 仅在编辑已有链接时显示"删除链接"按钮
            if (!this._isNew) {
                this.removeBtn = document.createElement('button');
                this.removeBtn.className = 'link-editor-remove-btn';
                _setI18n(this.removeBtn, 'link.remove');
                this.removeBtn.addEventListener('click', function() { self.removeLink(); });
                actions.appendChild(this.removeBtn);
            }
            var spacer = document.createElement('span');
            spacer.style.flex = '1';
            actions.appendChild(spacer);
            this.cancelBtn = document.createElement('button');
            _setI18n(this.cancelBtn, 'common.cancel');
            this.cancelBtn.addEventListener('click', function() { self.close(); });
            actions.appendChild(this.cancelBtn);
            this.okBtn = document.createElement('button');
            this.okBtn.className = 'primary';
            _setI18n(this.okBtn, 'common.ok');
            this.okBtn.addEventListener('click', function() { self.confirm(); });
            actions.appendChild(this.okBtn);
            this.popover.appendChild(actions);

            // 键盘处理
            var inputKeydown = function(e) {
                if (e.key === 'Escape') { e.preventDefault(); self.close(); return; }
                if (e.key === 'Enter') { e.preventDefault(); self.confirm(); }
            };
            this.textInput.addEventListener('keydown', inputKeydown);
            this.urlInput.addEventListener('keydown', inputKeydown);
            // 输入时清除必填错误标红
            var clearError = function(e) { e.target.classList.remove('error'); };
            this.textInput.addEventListener('input', clearError);
            this.urlInput.addEventListener('input', clearError);
            this.popover.addEventListener('click', function(e) { e.stopPropagation(); });
            this.popover.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { e.preventDefault(); self.close(); }
            });

            document.body.appendChild(this.overlay);
            document.body.appendChild(this.popover);
        },

        show: function() {
            var ed = window.editorApi.editor;
            if (ed) {
                this.textInput.value = ed.state.doc.textBetween(this._linkFrom, this._linkTo);
            }
            this.urlInput.value = this._href;
            // 文字为空（光标处新建）先聚焦文字输入；否则聚焦 URL（编辑/选中文字已有文字）
            if (this.textInput.value) {
                this.urlInput.focus();
                if (this._href) this.urlInput.select();
            } else {
                this.textInput.focus();
            }
        },

        confirm: function() {
            var newUrl = this.urlInput.value.trim();
            var newText = this.textInput.value.trim();
            // 文字与 URL 均必填：为空则标红并聚焦，保持对话框打开
            // （仅 Esc/取消/X 可关闭）
            if (!newText || !newUrl) {
                if (this.textInput) this.textInput.classList.toggle('error', !newText);
                if (this.urlInput) this.urlInput.classList.toggle('error', !newUrl);
                if (!newText) this.textInput.focus();
                else this.urlInput.focus();
                return;
            }
            var ed = window.editorApi.editor;
            if (!ed) { this.close(); return; }
            this._confirmed = true;
            window.editorApi._forceDirty();
            var state = ed.state;
            var linkMark = state.schema.marks.link.create({ href: newUrl });
            // 统一用 replaceWith：编辑已有链接时替换原文（含新 href）；
            // 新建时（含光标处无选区）from==to，即插入 [文字](URL)
            var textNode = state.schema.text(newText, [linkMark]);
            ed.view.dispatch(state.tr.replaceWith(this._linkFrom, this._linkTo, textNode));
            this.close();
        },

        removeLink: function() {
            var ed = window.editorApi.editor;
            if (!ed) { this.close(); return; }
            this._confirmed = true;
            window.editorApi._forceDirty();
            ed.commands.unsetLink();
            this.close();
        },

        cleanupDom: function() {
            if (this.overlay && this.overlay.parentNode) this.overlay.parentNode.removeChild(this.overlay);
            if (this.popover && this.popover.parentNode) this.popover.parentNode.removeChild(this.popover);
            this.overlay = null;
            this.popover = null;
            this.textInput = null;
            this.urlInput = null;
            this.okBtn = null;
            this.cancelBtn = null;
            this.removeBtn = null;
        },

        close: function() {
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
            this.cleanupDom();
            document.body.style.overflow = this._bodyOverflow || '';
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

    // ===== 代码块语言选择浮层（非模态，锚定语言标签下方，点外部/Esc 关闭） =====
    _codeLangPicker: {
        root: null, searchInput: null, list: null,
        _current: null, _onSelect: null, _outsideHandler: null, _bodyOverflow: '',

        open: function(anchorEl, currentLang, onSelect) {
            this.close();
            this._current = currentLang;
            this._onSelect = onSelect;
            this.buildDom();
            this._position(anchorEl);
            this._registerOutside();
            // 锁定 body 滚动，防止滚轮穿透到编辑区（与 _linkEditor 同模式）
            this._bodyOverflow = document.body.style.overflow;
            document.body.style.overflow = 'hidden';
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(true);
            var self = this;
            setTimeout(function(){ if (self.searchInput) self.searchInput.focus(); }, 0);
        },

        _position: function(anchorEl) {
            var rect = anchorEl.getBoundingClientRect();
            var W = window.innerWidth, H = window.innerHeight;
            this.root.style.left = '0px'; this.root.style.top = '0px';
            var w = this.root.offsetWidth || 240;
            var h = this.root.offsetHeight || 300;
            var px = rect.left, py = rect.bottom + 4;
            if (px + w > W - 8) px = Math.max(8, W - w - 8);
            if (py + h > H - 8) py = Math.max(8, rect.top - h - 4);
            px = Math.max(8, px);
            py = Math.max(8, py);
            this.root.style.left = px + 'px';
            this.root.style.top = py + 'px';
        },

        buildDom: function() {
            this._cleanupDom();
            var self = this;
            this.root = document.createElement('div');
            this.root.className = 'code-lang-picker';

            // 搜索框
            this.searchInput = document.createElement('input');
            this.searchInput.type = 'text';
            this.searchInput.className = 'code-lang-picker-search';
            this.searchInput.placeholder = i18n('codeLang.search');
            this.searchInput.addEventListener('input', function(){
                self.render(this.value.trim().toLowerCase());
            });
            this.searchInput.addEventListener('keydown', function(e){
                if (e.key === 'Escape') { e.preventDefault(); self.close(); }
                if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                    e.preventDefault();
                    var btns = self.list.querySelectorAll('button');
                    var idx = e.key === 'ArrowDown' ? 0 : btns.length - 1;
                    if (btns[idx]) btns[idx].focus();
                }
            });
            this.root.appendChild(this.searchInput);

            // 选项列表（滚动）
            this.list = document.createElement('div');
            this.list.className = 'code-lang-picker-list';
            this.root.appendChild(this.list);

            document.body.appendChild(this.root);
        },

        render: function(filter) {
            if (!this.list) return;
            this.list.innerHTML = '';
            var langs = window.lowlight ? window.lowlight.listLanguages() : [];
            var options = [{ value: null, label: i18n('codeLang.auto') }];
            // 若当前语言不在列表中（如用户输入的别名），追加到选项前
            if (this._current && langs.indexOf(this._current) < 0) {
                options.push({ value: this._current, label: this._current + i18n('codeLang.current') });
            }
            var f = filter || '';
            langs.sort();
            for (var i = 0; i < langs.length; i++) {
                if (!f || langs[i].toLowerCase().indexOf(f) >= 0) {
                    options.push({ value: langs[i], label: langs[i] });
                }
            }
            var self = this;
            for (var j = 0; j < options.length; j++) {
                var o = options[j];
                var btn = document.createElement('button');
                btn.type = 'button';
                btn.className = 'code-lang-option';
                btn.textContent = o.label;
                btn.addEventListener('click', function(val, lbl){
                    return function(e){ e.stopPropagation(); self._apply(val, lbl); };
                }(o.value, o.label));
                if (o.value === this._current) btn.classList.add('active');
                this.list.appendChild(btn);
            }
        },

        _apply: function(value, label) {
            if (this._onSelect) this._onSelect(value);
            this.close();
        },

        _registerOutside: function() {
            var self = this;
            this._outsideHandler = function(e) {
                if (self.root && !self.root.contains(e.target)) {
                    e.preventDefault();
                    self.close();
                }
            };
            document.addEventListener('mousedown', this._outsideHandler, true);
        },

        _cleanupDom: function() {
            if (this.root && this.root.parentNode) this.root.parentNode.removeChild(this.root);
            this.root = null;
            this.searchInput = null;
            this.list = null;
        },

        close: function() {
            if (this._outsideHandler) {
                document.removeEventListener('mousedown', this._outsideHandler, true);
                this._outsideHandler = null;
            }
            document.body.style.overflow = this._bodyOverflow || '';
            if (window.editorApi.bridge) window.editorApi.bridge.modalDialogChanged(false);
            this._cleanupDom();
            this._onSelect = null;
        }
    },

    // ===== emoji 选择器浮层（非模态，锚定按钮下方，点外部/Esc 关闭） =====
    _emojiPicker: {
        root: null, searchInput: null, grid: null, tabBar: null,
        freqSection: null, freqGrid: null,
        _freq: {}, _freqSeq: 0, _curCat: 'smileys', _filter: '', _highlight: -1,
        _rendered: [], _charMap: null, _outsideHandler: null,
        FREQ_KEY: 'md-editor:emoji-freq', FREQ_TOP: 8, COLS: 10,

        open: function(x, y) {
            var ed = window.editorApi.editor;
            if (!ed) return;
            this._loadFreq();
            this._filter = '';
            this.buildDom();
            this._curCat = EMOJI_CATEGORIES[0].id;
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
            px = Math.max(8, px);  // 左侧/上方溢出 → 钳制到可见区域
            py = Math.max(8, py);
            this.root.style.left = px + 'px';
            this.root.style.top = py + 'px';
        },

        buildDom: function() {
            this._cleanupDom();
            var self = this;
            this.root = document.createElement('div');
            this.root.className = 'emoji-picker';

            // ① 搜索行：搜索框 + 关闭按钮（无标题）
            var searchRow = document.createElement('div');
            searchRow.className = 'emoji-picker-searchrow';
            this.searchInput = document.createElement('input');
            this.searchInput.type = 'text';
            this.searchInput.className = 'emoji-picker-search';
            this.searchInput.placeholder = i18n('emoji.search');
            this.searchInput.addEventListener('input', function(){
                self._filter = this.value.trim().toLowerCase();
                self._curCat = self._filter ? '' : (self._curCat || EMOJI_CATEGORIES[0].id);
                self._renderTabBar();
                self.render();
            });
            this.searchInput.addEventListener('keydown', function(e){ self._onKeydown(e); });
            searchRow.appendChild(this.searchInput);
            var closeBtn = document.createElement('button');
            closeBtn.className = 'emoji-picker-close';
            closeBtn.type = 'button';
            closeBtn.textContent = '×';
            _setI18nTitle(closeBtn, 'common.close');
            closeBtn.addEventListener('click', function(e){ e.stopPropagation(); self.close(); });
            searchRow.appendChild(closeBtn);
            this.root.appendChild(searchRow);

            // ② 常用区：标签 + 频率网格（_renderFreq 控制显隐与内容）
            this.freqSection = document.createElement('div');
            this.freqSection.className = 'emoji-freq';
            var freqLabel = document.createElement('div');
            freqLabel.className = 'emoji-freq-label';
            _setI18n(freqLabel, 'emoji.freq');
            this.freqSection.appendChild(freqLabel);
            this.freqGrid = document.createElement('div');
            this.freqGrid.className = 'emoji-freq-grid';
            this.freqSection.appendChild(this.freqGrid);
            this.root.appendChild(this.freqSection);

            // ③ 当前分类网格
            this.grid = document.createElement('div');
            this.grid.className = 'emoji-picker-grid';
            this.root.appendChild(this.grid);

            // ④ 分类标签条
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
                _setI18n(empty, 'emoji.empty');
                this.grid.appendChild(empty);
            }
            this._renderFreq();
        },

        _renderTabBar: function() {
            var self = this;
            this.tabBar.innerHTML = '';
            var addTab = function(cat){
                var b = document.createElement('button');
                b.type = 'button';
                b.className = 'emoji-tab';
                b.textContent = cat.name;
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
            for (var i = 0; i < EMOJI_CATEGORIES.length; i++) addTab(EMOJI_CATEGORIES[i]);
        },

        _getCat: function(id) {
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
            this._bumpFreq(item.ch);
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
            else if (e.key === 'ArrowLeft') { e.preventDefault(); this._highlight = ((this._highlight - 1) % n + n) % n; this._applyHighlight(); }
            else if (e.key === 'ArrowDown') { e.preventDefault(); this._highlight = (this._highlight + c) % n; this._applyHighlight(); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); this._highlight = ((this._highlight - c) % n + n) % n; this._applyHighlight(); }
            else if (e.key === 'Enter') { e.preventDefault(); var it = this._rendered[this._highlight] || this._rendered[0]; if (it) this._insert(it); }
        },

        _applyHighlight: function() {
            var cells = this.grid.querySelectorAll('.emoji-cell');
            for (var i = 0; i < cells.length; i++) {
                if (i === this._highlight) { cells[i].classList.add('active'); cells[i].scrollIntoView({ block:'nearest' }); }
                else cells[i].classList.remove('active');
            }
        },

        _bumpFreq: function(ch) {
            var e = this._freq[ch] || { count: 0, seq: 0 };
            e.count++; e.seq = ++this._freqSeq;
            this._freq[ch] = e;
            this._saveFreq();
        },

        _loadFreq: function() {
            this._freq = {}; this._freqSeq = 0;
            try {
                var s = localStorage.getItem(this.FREQ_KEY);
                var obj = s ? JSON.parse(s) : null;
                if (obj && typeof obj === 'object') {
                    this._freq = obj;
                    for (var k in obj) if (obj[k] && obj[k].seq > this._freqSeq) this._freqSeq = obj[k].seq;
                }
            } catch (e) { this._freq = {}; this._freqSeq = 0; }
        },

        _saveFreq: function() {
            try { localStorage.setItem(this.FREQ_KEY, JSON.stringify(this._freq)); } catch (e) {}
        },

        _topFrequent: function() {
            var cm = this._getCharMap(), self = this;
            var keys = Object.keys(this._freq);
            keys.sort(function(a, b) {
                var fa = self._freq[a], fb = self._freq[b];
                var d = (fb ? fb.count : 0) - (fa ? fa.count : 0);   // 次数降序
                if (d) return d;
                return (fb ? fb.seq : 0) - (fa ? fa.seq : 0);        // 并列 → 最近使用(seq)在前
            });
            var out = [];
            for (var i = 0; i < keys.length && out.length < self.FREQ_TOP; i++) {
                var ch = keys[i];
                out.push(cm[ch] || { ch: ch, kw: ch });
            }
            return out;
        },

        _renderFreq: function() {
            if (!this.freqSection || !this.freqGrid) return;
            this.freqGrid.innerHTML = '';
            if (this._filter) { this.freqSection.style.display = 'none'; return; }   // 搜索时隐藏
            var top = this._topFrequent();
            if (!top.length) { this.freqSection.style.display = 'none'; return; }    // 空时隐藏
            this.freqSection.style.display = '';
            var self = this;
            for (var i = 0; i < top.length; i++) {
                (function(item){
                    var btn = document.createElement('button');
                    btn.type = 'button'; btn.className = 'emoji-cell';
                    btn.textContent = item.ch; btn.title = item.kw;
                    btn.addEventListener('click', function(e){ e.stopPropagation(); self._insert(item); });
                    self.freqGrid.appendChild(btn);
                })(top[i]);
            }
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
            this.freqSection = null; this.freqGrid = null;
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
            // init() 先于 QWebChannel 异步就绪执行，其中 notifyHeadings 因
            // bridge 未就绪而丢弃——此处补推，保证启动即显示大纲
            window.editorApi.notifyHeadings();
        });
    } else {
        console.warn('QWebChannel 未连接：请在浏览器中独立测试时忽略此警告。');
    }
});
