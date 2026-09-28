# commonmark 规范语料（vendored）

- 来源: https://spec.commonmark.org/0.31.2/spec.json（652 例）
- 许可: CommonMark 规范文本以 CC-BY-SA 4.0 发布（https://creativecommons.org/licenses/by-sa/4.0/）
- 用途: test_commonmark_corpus 两趟收敛棘轮（tests/test_commonmark_corpus.cpp）；
  仅做收敛性判定，不比对参考 HTML（我们的 HTML 为 Qt 风格）
- expected-diverge.txt: 已知不收敛例登记表（棘轮，只减不增）
