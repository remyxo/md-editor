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
                    return el.getAttribute('style') ? {} : false;
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
                    return el.hasAttribute('data-type') ? false : {};
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
                dom.title = '双击修改脚注标记';
                dom.addEventListener('dblclick', function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    var cur = node.attrs.id;
                    var v = prompt('脚注标记:', cur);
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
                getContentElement: function(el) {
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
                p.innerHTML = '<span style="color:#888;font-size:14px;">（输入 Mermaid 源码预览图表）</span>';
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
            this.searchInput.placeholder = '搜索表情（如 笑 / heart / +1）';
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
            closeBtn.title = '关闭';
            closeBtn.addEventListener('click', function(e){ e.stopPropagation(); self.close(); });
            searchRow.appendChild(closeBtn);
            this.root.appendChild(searchRow);

            // ② 常用区：标签 + 频率网格（_renderFreq 控制显隐与内容）
            this.freqSection = document.createElement('div');
            this.freqSection.className = 'emoji-freq';
            var freqLabel = document.createElement('div');
            freqLabel.className = 'emoji-freq-label';
            freqLabel.textContent = '常用表情符号';
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
                empty.textContent = '无匹配表情';
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
        });
    } else {
        console.warn('QWebChannel 未连接：请在浏览器中独立测试时忽略此警告。');
    }
});
