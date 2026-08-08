/*
 * mermaid 打包入口文件。
 *
 * 将 mermaid 打包成单个浏览器可直接加载的 IIFE bundle（mermaid.min.js）。
 *
 * 为什么不直接使用 mermaid 官方 dist/mermaid.min.js？
 *   官方构建是一个 esbuild IIFE 包装器，其末尾执行
 *     globalThis["mermaid"] = globalThis.__esbuild_esm_mermaid_nm["mermaid"].default;
 *   但 __esbuild_esm_mermaid_nm 是函数内的局部变量，从未挂到 globalThis，
 *   以 <script> 加载会抛 TypeError: Cannot read properties of undefined。
 *
 *   此外 mermaid v10+ 内部大量使用动态 import（按图表类型按需加载）。
 *   这里用 esbuild --format=iife --bundle 打包，动态 import 被内联为同步解析的
 *   Promise（会输出 warning，但功能正常），得到一个完全自包含的文件。
 *
 * 通过运行同目录下的 build-mermaid.sh 重新生成 mermaid.min.js。
 */
import mermaid from 'mermaid';
window.mermaid = mermaid;
