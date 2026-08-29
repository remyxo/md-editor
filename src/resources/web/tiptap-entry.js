/*
 * esbuild 打包入口文件。
 *
 * 将 @tiptap 核心、StarterKit、各扩展、ProseMirror 依赖以及
 * lowlight/highlight.js 打包成单个浏览器可直接加载的 IIFE bundle（tiptap-all.js）。
 *
 * 通过运行同目录下的 build-tiptap.sh 重新生成 tiptap-all.js。
 */
import { Editor, Extension, InputRule, Node, Mark } from '@tiptap/core';
import StarterKit from '@tiptap/starter-kit';
import Code from '@tiptap/extension-code';
import { TableKit } from '@tiptap/extension-table';
import TaskList from '@tiptap/extension-task-list';
import TaskItem from '@tiptap/extension-task-item';
import Highlight from '@tiptap/extension-highlight';
import Image from '@tiptap/extension-image';
import Placeholder from '@tiptap/extension-placeholder';
import CodeBlockLowlight from '@tiptap/extension-code-block-lowlight';
import { Mathematics, migrateMathStrings } from '@tiptap/extension-mathematics';
import Subscript from '@tiptap/extension-subscript';
import Superscript from '@tiptap/extension-superscript';
import TextAlign from '@tiptap/extension-text-align';
import { createLowlight, common } from 'lowlight';

// 搜索（纯函数 matcher + ProseMirror plugin）
import { searchMatcher, isWordChar, escapeRegExp } from './searchMatcher.js';
import {
  createSearchPlugin, searchKey, requestSearch,
  searchNext, searchPrev, replaceOne, replaceAll, clearSearch, searchCurrent
} from './searchPlugin.js';

// v3 StarterKit 已包含 Link 和 Underline，不需要单独导入

// 供 editor.js 使用的全局对象
window.TipTap = { Editor };
// 自定义扩展构建用（Emoji 短代码 InputRule）
window.TipTapExtension = Extension;
window.TipTapInputRule = InputRule;
// 自定义节点构建用（Mermaid 块节点）
window.TipTapNode = Node;
// 自定义 mark 构建用（colorSpan 内联样式 mark）
window.TipTapMark = Mark;
window.TipTapStarterKit = StarterKit;
window.TipTapExtensionTableKit = TableKit;
window.TipTapExtensionTaskList = TaskList;
window.TipTapExtensionTaskItem = TaskItem;
window.TipTapExtensionHighlight = Highlight;
window.TipTapExtensionImage = Image;
window.TipTapExtensionPlaceholder = Placeholder;
window.TipTapExtensionCodeBlockLowlight = CodeBlockLowlight;
// 单独导出 Code mark：默认 excludes:"_" 会丢弃包裹 code 的强调/加粗 mark
//（`*`code`*` 往返塌陷），editor.js 用 Code.extend({ excludes: '' }) 放开共存。
window.TipTapExtensionCode = Code;
window.TipTapExtensionMathematics = Mathematics;
window.TipTapExtensionSubscript = Subscript;
window.TipTapExtensionSuperscript = Superscript;
window.TipTapExtensionTextAlign = TextAlign;
window.TipTapMigrateMathStrings = migrateMathStrings;
window.lowlight = createLowlight(common);
window.searchMatcher = searchMatcher;
window.searchPlugin = {
  createSearchPlugin: createSearchPlugin,
  searchKey: searchKey,
  requestSearch: requestSearch,
  searchNext: searchNext,
  searchPrev: searchPrev,
  replaceOne: replaceOne,
  replaceAll: replaceAll,
  clearSearch: clearSearch,
  searchCurrent: searchCurrent
};
