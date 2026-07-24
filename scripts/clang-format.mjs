#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import { readdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const nativeDir = path.join(rootDir, 'native')
const check = process.argv.includes('--check')

const files = readdirSync(nativeDir)
  .filter((file) => /\.(cpp|h)$/.test(file))
  .map((file) => path.join(nativeDir, file))
  .sort()

const args = check ? ['--dry-run', '--Werror', ...files] : ['-i', ...files]
const result = spawnSync('clang-format', args, {
  cwd: rootDir,
  stdio: 'inherit',
})

if (result.error?.code === 'ENOENT') {
  console.error('clang-format was not found in PATH.')
}

process.exit(result.status ?? 1)
