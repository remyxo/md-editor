/*
 * esbuild 打包入口文件。
 *
 * 将 @tiptap 核心、StarterKit、各扩展、ProseMirror 依赖以及
 * lowlight/highlight.js 打包成单个浏览器可直接加载的 IIFE bundle（tiptap-all.js）。
 *
 * 通过运行同目录下的 build-tiptap.sh 重新生成 tiptap-all.js。
 */
import { Editor, Extension, InputRule } from '@tiptap/core';
import StarterKit from '@tiptap/starter-kit';
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

// v3 StarterKit 已包含 Link 和 Underline，不需要单独导入

// 供 editor.js 使用的全局对象
window.TipTap = { Editor };
// 自定义扩展构建用（Emoji 短代码 InputRule）
window.TipTapExtension = Extension;
window.TipTapInputRule = InputRule;
window.TipTapStarterKit = StarterKit;
window.TipTapExtensionTableKit = TableKit;
window.TipTapExtensionTaskList = TaskList;
window.TipTapExtensionTaskItem = TaskItem;
window.TipTapExtensionHighlight = Highlight;
window.TipTapExtensionImage = Image;
window.TipTapExtensionPlaceholder = Placeholder;
window.TipTapExtensionCodeBlockLowlight = CodeBlockLowlight;
window.TipTapExtensionMathematics = Mathematics;
window.TipTapExtensionSubscript = Subscript;
window.TipTapExtensionSuperscript = Superscript;
window.TipTapExtensionTextAlign = TextAlign;
window.TipTapMigrateMathStrings = migrateMathStrings;
window.lowlight = createLowlight(common);
