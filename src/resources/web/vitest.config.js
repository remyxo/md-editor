import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',            // 默认 node（纯逻辑）
    include: ['**/*.test.js']
  }
});
