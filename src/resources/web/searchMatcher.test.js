import { describe, it, expect } from 'vitest';
import { searchMatcher, escapeRegExp, isWordChar } from './searchMatcher.js';

describe('searchMatcher', () => {
  it('字面匹配', () => {
    expect(searchMatcher('hello world', 'world', {})).toEqual([{ start: 6, end: 11 }]);
  });
  it('默认大小写不敏感', () => {
    expect(searchMatcher('Hello', 'hello', {})).toEqual([{ start: 0, end: 5 }]);
  });
  it('区分大小写开启时不匹配', () => {
    expect(searchMatcher('Hello', 'hello', { caseSensitive: true })).toEqual([]);
  });
  it('正则匹配多个', () => {
    expect(searchMatcher('a1b2c3', '\\d', { regex: true })).toEqual([
      { start: 1, end: 2 }, { start: 3, end: 4 }, { start: 5, end: 6 }
    ]);
  });
  it('非法正则返回空数组', () => {
    expect(searchMatcher('abc', '(', { regex: true })).toEqual([]);
  });
  it('全词 Unicode：中文逗号边界算全词', () => {
    expect(searchMatcher('你好，世界', '你好', { wholeWord: true })).toEqual([{ start: 0, end: 2 }]);
  });
  it('全词 Unicode：紧邻汉字仍算全词（CJK 不阻断边界）', () => {
    // 中文无空格分词，汉字不参与 word boundary 判定
    expect(searchMatcher('你好世界', '你好', { wholeWord: true })).toEqual([{ start: 0, end: 2 }]);
  });
  it('全词 ASCII：嵌入更长单词不算', () => {
    expect(searchMatcher('foo bar foobar', 'foo', { wholeWord: true })).toEqual([{ start: 0, end: 3 }]);
  });
  it('正则 + 全词组合', () => {
    expect(searchMatcher('cat dog', 'cat|dog', { regex: true, wholeWord: true })).toEqual([
      { start: 0, end: 3 }, { start: 4, end: 7 }
    ]);
  });
  it('跨块分隔符 \\u0000 不匹配（字面）', () => {
    expect(searchMatcher('ab\u0000cd', 'abc', {})).toEqual([]);
  });
  it('特殊字符按字面（转义）', () => {
    expect(searchMatcher('a.b.c', '.b', {})).toEqual([{ start: 1, end: 3 }]);
  });
  it('空 query 返回空', () => {
    expect(searchMatcher('abc', '', {})).toEqual([]);
  });
});

describe('escapeRegExp / isWordChar', () => {
  it('转义元字符', () => { expect(escapeRegExp('a.b*')).toBe('a\\.b\\*'); });
  it('汉字不是词字符（CJK 不参与 word boundary）', () => { expect(isWordChar('你')).toBe(false); });
  it('标点不是词字符', () => { expect(isWordChar('，')).toBe(false); });
  it('\\u0000 不是词字符', () => { expect(isWordChar('\u0000')).toBe(false); });
});
