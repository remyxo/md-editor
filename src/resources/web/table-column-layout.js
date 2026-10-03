// 表格列宽测量/分配共享模块（单一来源）
// 使用方：① 编辑器 editor.js（克隆离屏测量 → 百分比 colgroup + fixed 布局）；
// ② HTML/PDF 导出（HtmlExporter::tableColumnNormalizeScript 运行时读 qrc
//    内联本文件后调用）。改本文件必须同时验证两条链路（导出：ctest
//    test_htmlexporter/test_pdfexporter；编辑器：真机目检，见计划 Task 5）。
// 注意：测量主体与原 tableColumnNormalizeScript 逐字一致（紧凑风格），子串
// 断言锚定原文，勿重排版。分配主体 2026-09-23 起携 lift 偏好（超长不可断列
// 让宽，见 __allocateTableColumns 注释与 testAllocateTableColumnsLiftsOverweightColumn）。
(function (global) {
'use strict';

// 就地测量表格各网格列的 min-content / max-content 宽（px，含 colspan 展开）。
// cfg 可选：{ fontSize, padding, fontFamily, border } 测量期临时样式（导出侧传
// 打印基准；编辑器不传 ⇒ 按实际渲染样式测量）。border 为导出侧（DOCX 归一）
// 测量基准统一用：Qt toHtml 的 border="1" 呈现属性让 UA 给单元格画 1px 折叠
// 边框、每列测量 +1px，TipTap 片段无边框——'0' 须同清 td/th 与 table 两处
//（只清单元格会残留表级 UA 边框 0.5px 到首末列，w2-gamma 取证实证）；
// 编辑器不传（live 布局照旧在 editor.css 边框下测量）。测量前剥掉表格已有
// colgroup：编辑器二次测量时克隆表带着上轮的百分比 col 会污染 auto 测量；
// 导出侧旧 colgroup 本来就在写回前被移除，行为不变。返回 null 表示表格不可测。
global.__measureTableColumns=function(table,cfg){
cfg=cfg||{};
var firstRow=table.querySelector('tr');
if(!firstRow||!firstRow.children.length)return null;
var oldCg=table.querySelector('colgroup');
if(oldCg)oldCg.remove();
var saved=[],all=table.querySelectorAll('td,th');
for(var i=0;i<all.length;i++){
saved.push([all[i],all[i].getAttribute('style')]);
if(cfg.fontSize)all[i].style.fontSize=cfg.fontSize;
if(cfg.padding)all[i].style.padding=cfg.padding;
if(cfg.border)all[i].style.borderWidth=cfg.border;
all[i].style.overflowWrap='normal';
all[i].style.wordBreak='keep-all';
}
var savedTable=table.getAttribute('style');
table.style.borderCollapse='collapse';
if(cfg.fontFamily)table.style.fontFamily=cfg.fontFamily;
if(cfg.border)table.style.borderWidth=cfg.border;
table.style.tableLayout='auto';
var expand=function(){
var out=[],ci=0;
for(var i=0;i<firstRow.children.length;i++){
var span=Math.max(1,parseInt(firstRow.children[i].getAttribute('colspan')||'1',10));
var w=firstRow.children[i].getBoundingClientRect().width/span;
for(var s=0;s<span;s++)out[ci++]=w;
}
return out;
};
table.style.width='min-content';
var minW=expand();
table.style.width='max-content';
var maxW=expand();
for(i=0;i<all.length;i++){
if(saved[i][1]===null)all[i].removeAttribute('style');
else all[i].setAttribute('style',saved[i][1]);
}
if(savedTable===null)table.removeAttribute('style');
else table.setAttribute('style',savedTable);
return {minW:minW,maxW:maxW};
};

// 分配各列宽（px）。maxW=min(maxW,3×minW) 封顶（长列吸收富余但不独吞）、
// +(slack||0) 余量（百分比折算与边框折叠吃掉零点几像素，短列卡在
// max-content 边界即折行——HTML 导出实测 0.3px 一字一行）、
// floor=min(max, max(常规保底, lift 抬升))：minW≤均份额的列沿用
// min(3×min, avail/n)（行为与旧版一致）；minW 超均份额的列（内容含超长
// 不可断串，如 12 列表 @624px 的 1350px JWT URL）抬到 min(minW, lift×avail)
//（lift 第 5 参缺省 0.5：单列保底至多认领半宽）——否则全列 floor=avail/n
// 被抹成均分（实测 8.3%），长列拿不到宽度。三分支：sumMax≤avail 按 max
// 比例放大填满 / sumFloor≤avail 保底与期望间线性插值 / 压缩分支按保底比例
// 分配（lift 抬升后可达：超长列表过约束时的主路径，不再是防御分支）。
global.__allocateTableColumns=function(minW,maxW,avail,slack,lift){
var n=minW.length,floor=[],sumMax=0,sumFloor=0;
for(var j=0;j<n;j++){
if(!(maxW[j]>0))maxW[j]=(minW[j]>0)?minW[j]:1;
if(!(minW[j]>0))minW[j]=maxW[j];
if(minW[j]>0)maxW[j]=Math.min(maxW[j],minW[j]*3);
if(maxW[j]>0)maxW[j]+=(slack||0);
floor[j]=Math.min(maxW[j],Math.max(Math.min(minW[j]*3,avail/n),Math.min(minW[j],(lift||0.5)*avail)));
sumMax+=maxW[j];sumFloor+=floor[j];
}
var w=[];
if(sumMax<=avail){
for(j=0;j<n;j++)w.push(maxW[j]*avail/sumMax);
}else if(sumFloor<=avail){
var k=(avail-sumFloor)/(sumMax-sumFloor);
for(j=0;j<n;j++)w.push(floor[j]+(maxW[j]-floor[j])*k);
}else{
for(j=0;j<n;j++)w.push(floor[j]*avail/sumFloor);
}
return w;
};

// 把序列化 HTML 中按文档序的各 <colgroup>…</colgroup> 替换为给定新 HTML
//（编辑器 getExportHtml 用：序列化空 colgroup → live 布局 px colgroup，供
// DocxExporter 解析 tblGrid）。数量不匹配时原样返回（防御，不半替换）。
global.__replaceColgroupsWith=function(html,colgroupHtmls){
var count=0;
html.replace(/<colgroup[\s\S]*?<\/colgroup>/g,function(){count++;return '';});
if(count!==colgroupHtmls.length)return html;
var idx=0;
return html.replace(/<colgroup[\s\S]*?<\/colgroup>/g,function(m){
return colgroupHtmls[idx++];
});
};
})(typeof window!=='undefined'?window:globalThis);
