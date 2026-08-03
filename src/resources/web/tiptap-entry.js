/*
 * esbuild 打包入口文件。
 *
 * 将 @tiptap 核心、StarterKit、各扩展、ProseMirror 依赖以及
 * lowlight/highlight.js 打包成单个浏览器可直接加载的 IIFE bundle（tiptap-all.js）。
 *
 * 通过运行同目录下的 build-tiptap.sh 重新生成 tiptap-all.js。
 */
import { Editor } from '@tiptap/core';
import StarterKit from '@tiptap/starter-kit';
import Table from '@tiptap/extension-table';
import TaskList from '@tiptap/extension-task-list';
import TaskItem from '@tiptap/extension-task-item';
import Highlight from '@tiptap/extension-highlight';
import Image from '@tiptap/extension-image';
import Link from '@tiptap/extension-link';
import Placeholder from '@tiptap/extension-placeholder';
import Underline from '@tiptap/extension-underline';
import CodeBlockLowlight from '@tiptap/extension-code-block-lowlight';
import { createLowlight, common } from 'lowlight';

// 供 editor.js 使用的全局对象
window.TipTap = { Editor };
window.TipTapStarterKit = StarterKit;
window.TipTapExtensionTable = Table;
window.TipTapExtensionTaskList = TaskList;
window.TipTapExtensionTaskItem = TaskItem;
window.TipTapExtensionHighlight = Highlight;
window.TipTapExtensionImage = Image;
window.TipTapExtensionLink = Link;
window.TipTapExtensionPlaceholder = Placeholder;
window.TipTapExtensionUnderline = Underline;
window.TipTapExtensionCodeBlockLowlight = CodeBlockLowlight;
window.lowlight = createLowlight(common);
