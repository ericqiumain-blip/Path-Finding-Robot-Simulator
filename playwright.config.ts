import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests/browser',
  fullyParallel: false,
  workers: 1,
  timeout: 30000,
  use: {
    baseURL: 'http://127.0.0.1:8080',
    browserName: 'chromium',
    channel: process.env.CI ? undefined : 'chrome',
    viewport: { width: 1440, height: 1000 },
    screenshot: 'only-on-failure',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: 'node server/index.mjs',
    url: 'http://127.0.0.1:8080/api/health',
    reuseExistingServer: !process.env.CI,
    timeout: 30000,
  },
});
