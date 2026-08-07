# Markdown 语法参考

## 标题

使用 `#` 号标记，`#` 的数量表示标题级别（1–6）。

```markdown
# 一级标题
## 二级标题
### 三级标题
#### 四级标题
##### 五级标题
###### 六级标题
```

## 文本格式

| 语法 | 效果 |
|------|------|
| `**粗体**` | **粗体** |
| `*斜体*` | *斜体* |
| `~~删除线~~` | ~~删除线~~ |
| `==高亮==` | ==高亮== |
| `` `行内代码` `` | `行内代码` |

## 链接与图片

```markdown
[链接文字](https://example.com)

![图片替代文字](image.png)
```

## 引用块

```markdown
> 这是一段引用文字。
> 可以有多行。
```

> 这是一段引用文字。
> 可以有多行。

## 列表

无序列表使用 `-`、`*` 或 `+`：

```markdown
- 项目一
- 项目二
  - 嵌套项目
```

有序列表使用数字加 `.`：

```markdown
1. 第一步
2. 第二步
3. 第三步
```

任务列表：

```markdown
- [ ] 待办事项
- [x] 已完成事项
```

## 代码块

使用三个反引号包裹，可指定语言：

````markdown
```cpp
#include <iostream>
int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
```
````

```cpp
#include <iostream>
int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
```

## 表格

```markdown
| 列A | 列B | 列C |
|-----|-----|-----|
| A1  | B1  | C1  |
| A2  | B2  | C2  |
```

| 列A | 列B | 列C |
|-----|-----|-----|
| A1  | B1  | C1  |
| A2  | B2  | C2  |

## 分割线

使用三个或以上的 `---`、`***` 或 `___`：

---

## 数学公式

### 行内公式

使用单个 `$` 包裹，公式嵌入文字中：

```markdown
质能方程 $E=mc^2$ 是爱因斯坦提出的。
```

质能方程 $E=mc^2$ 是爱因斯坦提出的。

### 块级公式

使用两个 `$$` 包裹，公式独占一行居中显示：

```markdown
$$
E = mc^2
$$
```

$$
E = mc^2
$$

### 常用 LaTeX 语法

#### 上下标

| 语法 | 渲染 | 说明 |
|------|------|------|
| `x^2` | $x^2$ | 上标（单个字符） |
| `x^{10}` | $x^{10}$ | 上标（多个字符需 `{}` 包裹） |
| `x_i` | $x_i$ | 下标 |
| `x_{ij}` | $x_{ij}$ | 下标（多个字符） |
| `x_i^2` | $x_i^2$ | 上下标组合 |
| `x_{i}^{2}` | $x_{i}^{2}$ | 上下标组合（推荐加 `{}` 明确范围） |

> **重要**：`^` 和 `_` 默认只作用于**紧随其后的一个字符**。要作用多个字符，必须用花括号 `{...}` 包裹。
>
> - `c^22` 渲染为 $c^22$（只有第一个 `2` 是上标）
> - `c^{22}` 渲染为 $c^{22}$（`22` 整体是上标）

#### 分数

```latex
\frac{分子}{分母}
```

$$
\frac{a}{b} \qquad \frac{x+1}{y-2}
$$

#### 根号

```latex
\sqrt{被开方数}          % 平方根
\sqrt[n]{被开方数}       % n 次方根
```

$$
\sqrt{x} \qquad \sqrt[3]{8} = 2
$$

#### 求和、积分、乘积

```latex
\sum_{i=1}^{n} x_i       % 求和
\int_{a}^{b} f(x)\,dx    % 积分
\prod_{i=1}^{n} x_i      % 乘积
\lim_{x \to \infty} f(x) % 极限
```

$$
\sum_{i=1}^{n} x_i \qquad \int_{a}^{b} f(x)\,dx
$$

$$
\prod_{i=1}^{n} x_i \qquad \lim_{x \to \infty} f(x)
$$

> 在块级公式（`$$...$$`）中，求和与积分的上下限会显示在符号正上/下方；在行内公式（`$...$`）中则显示为角标。

#### 希腊字母

```latex
\alpha \beta \gamma \delta \epsilon
\theta \lambda \mu \pi \sigma
\phi \omega \Omega \Delta \Gamma
```

$$
\alpha \quad \beta \quad \gamma \quad \delta \quad \epsilon
$$
$$
\theta \quad \lambda \quad \mu \quad \pi \quad \sigma
$$
$$
\phi \quad \omega \quad \Omega \quad \Delta \quad \Gamma
$$

#### 括号

```latex
(x) \quad [x] \quad \{x\}
\left( \frac{a}{b} \right)   % 自适应高度
```

$$
(x) \quad [x] \quad \{x\} \qquad
\left( \frac{a}{b} \right)
$$

#### 矩阵

```latex
\begin{pmatrix}
a & b \\
c & d
\end{pmatrix}
```

$$
\begin{pmatrix}
a & b \\
c & d
\end{pmatrix}
$$

#### 多行公式（aligned 环境）

```latex
$$
\begin{aligned}
x &= a + b \\
y &= c + d \\
z &= x + y
\end{aligned}
$$
```

$$
\begin{aligned}
x &= a + b \\
y &= c + d \\
z &= x + y
\end{aligned}
$$

#### 常用符号速查

| 符号 | 语法 | 渲染 |
|------|------|------|
| 乘号 | `\times` | $\times$ |
| 除号 | `\div` | $\div$ |
| 正负 | `\pm` | $\pm$ |
| 点乘 | `\cdot` | $\cdot$ |
| 小于等于 | `\le` | $\le$ |
| 大于等于 | `\ge` | $\ge$ |
| 不等于 | `\neq` | $\neq$ |
| 约等于 | `\approx` | $\approx$ |
| 无穷 | `\infty` | $\infty$ |
| 属于 | `\in` | $\in$ |
| 子集 | `\subset` | $\subset$ |
| 空集 | `\emptyset` | $\emptyset$ |
| 任意 | `\forall` | $\forall$ |
| 存在 | `\exists` | $\exists$ |
| 箭头 | `\to` `\rightarrow` `\Rightarrow` | $\to$ $\rightarrow$ $\Rightarrow$ |
| 角度 | `\angle` | $\angle$ |
| 度数 | `30^\circ` | $30^\circ$ |

### 编辑数学公式

- **双击**渲染后的公式即可编辑 LaTeX 源码，实时预览
- `Ctrl+M` 插入行内公式，`Ctrl+Shift+M` 插入块级公式
- 工具栏 ∑ 按钮也可快速插入行内公式
