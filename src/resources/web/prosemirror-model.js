/*
 * 占位文件：prosemirror-model
 *
 * prosemirror-model@1.21.0 仅提供 CJS / ESM 构建（dist/index.cjs、dist/index.js），
 * 官方不再发布浏览器可直接加载的 UMD 构建。
 *
 * TipTap v2 的 UMD 构建要求 window.model 全局对象，但该全局无法通过
 * 独立 UMD 文件提供。因此本项目使用 esbuild 将 ProseMirror 打包进
 * tiptap-all.js（见同目录 build-tiptap.sh），该文件为运行时实际使用的库。
 *
 * 此文件仅为满足任务清单中的文件列表而保留，不参与页面加载。
 * 真实源码可在以下 URL 获取：
 *   https://cdn.jsdelivr.net/npm/prosemirror-model@1.21.0/dist/index.cjs
 */
