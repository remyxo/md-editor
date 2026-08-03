/*
 * 占位文件：lowlight
 *
 * lowlight@3.1.0 为 ESM-only 包（lib/common.js 使用 import/export 语法），
 * 无法通过经典 <script> 标签在浏览器中直接加载。
 *
 * 本项目使用 esbuild 将 lowlight 与其依赖的 highlight.js 一起打包进
 * tiptap-all.js（见同目录 build-tiptap.sh），并暴露 window.lowlight 供
 * @tiptap/extension-code-block-lowlight 使用。该文件为运行时实际使用的库。
 *
 * 此文件仅为满足任务清单中的文件列表而保留，不参与页面加载。
 * 真实源码可在以下 URL 获取：
 *   https://cdn.jsdelivr.net/npm/lowlight@3.1.0/lib/common.js
 */
