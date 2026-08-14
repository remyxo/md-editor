import { describe, it, expect } from 'vitest';

describe('vitest 接入冒烟', () => {
  it('能跑断言', () => {
    expect(1 + 1).toBe(2);
  });
});
