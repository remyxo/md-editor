<!-- corpus: heading-levels -->
# 一级标题

## 二级标题

### 三级标题

#### 四级标题与**行内加粗**

<!-- corpus: emphasis-code-mix -->
**加粗**、*斜体*、***粗斜***、~~删除线~~、==高亮==、H~2~O 下标、m^2^ 上标、行内 `code`

**`code 内粗`** 与 *`code 内斜`* 组合形态

<!-- corpus: table-align -->
| 左对齐 | 居中 | 右对齐 |
|---|:---:|---:|
| 普通单元格 | 中 | 999 |
| 中文内容混排 **粗** | y | 0.5 |

<!-- corpus: task-list -->
- [ ] 未完成任务
- [x] 已完成任务
  - [x] 嵌套已完成

<!-- corpus: footnote -->
正文中引用[^a]与[^长名]。

[^a]: 脚注 a 定义。
[^长名]: 脚注长名定义，含 **加粗**。

<!-- corpus: math -->
行内公式 $a^2+b^2=c^2$ 与字符 $x$。

块级公式：

$$
\int_0^1 f(x)\,dx
$$

<!-- corpus: nested-fences -->
````markdown
```python
def f():
    return 1
```
````

<!-- corpus: html-literal -->
按 <kbd>Ctrl</kbd>+<kbd>K</kbd>；<small>小字注记</small>；<span style="color: rgb(255,0,0)">红外观</span>

<!-- corpus: br-softbreak -->
硬换行第一行<br>硬换行第二行
软换行上行
软换行下行

<!-- corpus: blockquote-list -->
> 引用第一段
>
> - 引用内列表一
> - 引用内列表二

<!-- corpus: cjk-longline -->
甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸 唯一空格折行点 甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸甲乙丙丁戊己庚辛壬癸

<!-- corpus: hr-links -->
---

[文本链接](https://example.com/a?b=1&c=2 "提示") 与自动转换文本 https://example.com/plain

<!-- corpus: list-mixed -->
1. 有序一
2. 有序二
   - 无序嵌套
3. 有序三

- 顶层无序
  1. 嵌套有序
