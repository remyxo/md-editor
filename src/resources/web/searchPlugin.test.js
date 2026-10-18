// @vitest-environment jsdom
import { describe, it, expect, beforeEach } from 'vitest';
import { Schema } from '@tiptap/pm/model';
import { EditorState, TextSelection } from '@tiptap/pm/state';
import { EditorView } from '@tiptap/pm/view';
import {
  createSearchPlugin, searchKey, requestSearch,
  searchNext, searchPrev, replaceOne, replaceAll, clearSearch, searchCurrent
} from './searchPlugin.js';

// 最小 schema：doc / paragraph / text + 行内原子（hardBreak / image）
const schema = new Schema({
  nodes: {
    doc: { content: 'block+' },
    paragraph: { group: 'block', content: '(text|hardBreak|image)*', toDOM: () => ['p', 0] },
    text: { group: 'inline' },
    hardBreak: { group: 'inline', inline: true, atom: true, toDOM: () => ['br'] },
    image: { group: 'inline', inline: true, atom: true, toDOM: () => ['img'] }
  }
});

function makeDoc(lines) {
  const paras = lines.map(l => schema.node('paragraph', null, l ? schema.text(l) : []));
  return schema.node('doc', null, paras);
}

function makeInlineDoc(parts) {
  const children = parts.map(p =>
    typeof p === 'string' ? schema.text(p) : p);
  return schema.node('doc', null, [schema.node('paragraph', null, children)]);
}

let view, plugin;
beforeEach(() => {
  plugin = createSearchPlugin();
  view = new EditorView(document.createElement('div'), {
    state: EditorState.create({ doc: makeDoc(['hello hello']), plugins: [plugin] })
  });
});

describe('searchPlugin', () => {
  it('requestSearch 计算匹配区间', () => {
    requestSearch(view, 'hello', {});
    const st = searchKey.getState(view.state);
    expect(st.matches.length).toBe(2);
    expect(st.current).toBe(0);
  });

  it('decorations 数量 == 匹配数，当前匹配高亮', () => {
    requestSearch(view, 'hello', {});
    const decos = plugin.props.decorations(view.state).find();
    expect(decos.length).toBe(2);
    const classes = decos.map(d => d.type.attrs.class).sort();
    expect(classes).toEqual(['search-match', 'search-match-current']);
  });

  it('searchNext/searchPrev 回绕', () => {
    requestSearch(view, 'hello', {});
    searchNext(view); expect(searchKey.getState(view.state).current).toBe(1);
    searchNext(view); expect(searchKey.getState(view.state).current).toBe(0); // 回绕
    searchPrev(view); expect(searchKey.getState(view.state).current).toBe(1); // 回绕
  });

  it('导航后选中匹配区间', () => {
    requestSearch(view, 'hello', {});
    searchNext(view);
    const sel = view.state.selection;
    expect(sel.from).toBeLessThan(sel.to);
  });

  it('replaceOne 字面替换并前进', () => {
    requestSearch(view, 'hello', {});
    replaceOne(view, 'HI');
    expect(view.state.doc.textContent).toBe('HI hello');
    expect(searchKey.getState(view.state).matches.length).toBe(1);
  });

  it('replaceAll 不同长度替换正确（无位置漂移）', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['foo foo foo']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, 'foo', {});
    replaceAll(view, 'barbar');
    expect(view.state.doc.textContent).toBe('barbar barbar barbar');
  });

  it('replaceAll 正则反向引用 $1', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['a1b']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, '(\\d)', { regex: true });
    replaceAll(view, '[$1]');
    expect(view.state.doc.textContent).toBe('a[1]b');
  });

  it('doc 编辑后自动重算计数', () => {
    requestSearch(view, 'hello', {});
    view.dispatch(view.state.tr.insertText(' hello', view.state.doc.content.size - 1));
    const st = searchKey.getState(view.state);
    expect(st.matches.length).toBe(3);
  });

  it('clearSearch 清空', () => {
    requestSearch(view, 'hello', {});
    clearSearch(view);
    expect(searchKey.getState(view.state).matches.length).toBe(0);
    expect(searchCurrent(view.state)).toEqual({ total: 0, current: 0 });
  });

  it('searchCurrent 1 基计数', () => {
    requestSearch(view, 'hello', {});
    expect(searchCurrent(view.state)).toEqual({ total: 2, current: 1 });
    searchNext(view);
    expect(searchCurrent(view.state)).toEqual({ total: 2, current: 2 });
  });

  it('跨块不匹配', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({ doc: makeDoc(['hel', 'lo']), plugins: [plugin = createSearchPlugin()] })
    });
    requestSearch(view, 'hello', {});
    expect(searchKey.getState(view.state).matches.length).toBe(0);
  });

  it('跨行内原子（<br>）不匹配，也不被替换删除', () => {
    view.destroy();
    view = new EditorView(document.createElement('div'), {
      state: EditorState.create({
        doc: makeInlineDoc(['foo', schema.node('hardBreak'), 'bar']),
        plugins: [plugin = createSearchPlugin()]
      })
    });
    // 字面查询跨 hardBreak：拼接文本是 foobar，但区间含原子分隔符
    requestSearch(view, 'oob', {});
    expect(searchKey.getState(view.state).matches.length).toBe(0);
    // 正则 \S+ 同样不得跨原子
    requestSearch(view, 'o.b', { regex: true });
    expect(searchKey.getState(view.state).matches.length).toBe(0);
    // 全部替换不得删除 <br>
    requestSearch(view, 'oob', {});
    expect(replaceAll(view, 'X')).toBe(0);
    expect(view.state.doc.child(0).child(1).type.name).toBe('hardBreak');
    // 原子两侧各自的文本仍可命中
    requestSearch(view, 'foo', {});
    expect(searchKey.getState(view.state).matches.length).toBe(1);
    requestSearch(view, 'bar', {});
    expect(searchKey.getState(view.state).matches.length).toBe(1);
  });
});
