/**
 * 构建 dsh-pet 的离线桌宠运行包，并生成宿主侧可校验的 manifest。
 *
 * 运行方式：
 *   node scripts/build-pet-runtime.mjs
 *   node scripts/build-pet-runtime.mjs --output E:/.../pet
 *
 * 脚本默认使用固定上游提交和本地代理，避免首次启动时联网下载或执行未审计的运行包。
 */

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { cp, mkdir, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SOURCE = resolve(process.env.PI_PET_SOURCE ?? join(ROOT, 'third_party/dsh-pet/dsh-pet'));
const OUTPUT = resolve(process.env.PI_PET_OUTPUT ?? join(ROOT, 'build/pet'));
const CACHE = resolve(process.env.PI_PET_ELECTRON_CACHE ?? join(ROOT, 'third_party/.cache/dsh-pet'));
const EXPECTED_COMMIT = '631c5310b047931404978152fdc7865413c153ec';
const ELECTRON_VERSION = process.env.DSH_PET_ELECTRON_VERSION ?? '43.3.0';
const PROXY = process.env.PI_DEP_PROXY ?? 'http://127.0.0.1:7897';

/** 执行命令并保留上游脚本的诊断输出。 */
function run(command, args, env = {}) {
  execFileSync(command, args, {
    shell: process.platform === 'win32',
    cwd: SOURCE,
    env: {
      ...process.env,
      ...env,
      HTTP_PROXY: PROXY,
      HTTPS_PROXY: PROXY,
      ALL_PROXY: PROXY,
      npm_config_proxy: PROXY,
      npm_config_https_proxy: PROXY,
      DSH_HOME: CACHE,
      DSH_PET_ELECTRON_VERSION: ELECTRON_VERSION,
    },
    stdio: 'inherit',
  });
}

/** 读取并校验上游仓库提交，防止素材和 helper 与协议实现错配。 */
function sourceCommit() {
  return execFileSync('git', ['rev-parse', 'HEAD'], { cwd: SOURCE, encoding: 'utf8' }).trim();
}

/** 对上游 helper 应用 Pi Desktop 所需的 userData 隔离和窗口显示兜底补丁。 */
async function patchHelperMain(file) {
  let source = await readFile(file, 'utf8');
  const userDataMarker = "app.setName('dsh-pet-electron-helper');";
  const userDataPatch = `${userDataMarker}\n\n/** 使用宿主分配的独立目录，避免多个 Pi Desktop 实例共享 Electron 锁。 */\nif (process.env.DSH_PET_REMOTE_DEBUG_PORT) {\n  app.commandLine.appendSwitch('remote-debugging-port', process.env.DSH_PET_REMOTE_DEBUG_PORT);\n}\nconst configuredUserData = process.env.DSH_PET_USER_DATA;\nif (configuredUserData) {\n  try {\n    require('node:fs').mkdirSync(configuredUserData, { recursive: true });\n    app.setPath('userData', configuredUserData);\n  } catch (error) {\n    process.stderr.write('[dsh-pet helper] userData 初始化失败：' + String(error) + '\\n');\n  }\n}`;
  if (!source.includes(userDataMarker) || source.includes('const configuredUserData = process.env.DSH_PET_USER_DATA;')) {
    throw new Error('helper userData 补丁定位失败或重复应用');
  }
  source = source.replace(userDataMarker, userDataPatch);

  const showMarker = "    win.once('ready-to-show', () => win.show());";
  const showPatch = "    const showWindow = () => {\n      if (!win.isDestroyed() && !win.isVisible()) win.show();\n    };\n    win.once('ready-to-show', showWindow);";
  if (!source.includes(showMarker)) throw new Error('helper 窗口显示补丁定位失败');
  source = source.replace(showMarker, showPatch);
  const loadMarker = "        if (!win.isDestroyed() && !win.isVisible()) win.show();";
  const loadPatch = "        showWindow();";
  if (!source.includes(loadMarker)) throw new Error('helper 显示兜底补丁定位失败');
  source = source.replace(loadMarker, loadPatch);
  const promiseMarker = "      .catch((error) => {";
  const promisePatch = "      .then(() => {\n        // 某些 Windows/DWM 组合不会派发 ready-to-show，loadFile 完成后再补一次显示。\n        setTimeout(showWindow, 1000);\n      })\n      .catch((error) => {";
  if (!source.includes(promiseMarker)) throw new Error('helper loadFile 补丁定位失败');
  source = source.replace(promiseMarker, promisePatch);
  await writeFile(file, source, 'utf8');
}

/** 删除 JSONC 注释并去掉尾逗号，生成 Qt 可直接解析的 JSON 成品。 */
function jsoncToJson(source) {
  let output = '';
  let string = false;
  let escaped = false;
  let lineComment = false;
  let blockComment = false;
  for (let i = 0; i < source.length; i += 1) {
    const current = source[i];
    const next = source[i + 1];
    if (lineComment) {
      if (current === '\n') {
        lineComment = false;
        output += current;
      }
      continue;
    }
    if (blockComment) {
      if (current === '*' && next === '/') {
        blockComment = false;
        i += 1;
      } else if (current === '\n') {
        output += '\n';
      }
      continue;
    }
    if (string) {
      output += current;
      if (escaped) escaped = false;
      else if (current === '\\') escaped = true;
      else if (current === '"') string = false;
      continue;
    }
    if (current === '"') {
      string = true;
      output += current;
    } else if (current === '/' && next === '/') {
      lineComment = true;
      i += 1;
    } else if (current === '/' && next === '*') {
      blockComment = true;
      i += 1;
    } else {
      output += current;
    }
  }
  return output.replace(/,\s*([}\]])/g, '$1');
}

/** 递归收集运行包文件并生成稳定的相对路径清单。 */
async function listFiles(root, current = root) {
  const entries = await readdir(current, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    if (entry.name === 'manifest.json') continue;
    const path = join(current, entry.name);
    if (entry.isDirectory()) files.push(...(await listFiles(root, path)));
    else if (entry.isFile()) files.push(relative(root, path).replaceAll('\\', '/'));
  }
  return files.sort();
}

/** 为每个运行文件计算 SHA-256，供宿主在加载前确认包内容完整。 */
async function buildManifest() {
  const files = await listFiles(OUTPUT);
  const hashes = {};
  for (const file of files) {
    const data = await readFile(join(OUTPUT, file));
    hashes[file] = createHash('sha256').update(data).digest('hex');
  }
  return {
    version: 'dsh-pet-0.2.12',
    upstreamCommit: EXPECTED_COMMIT,
    electronVersion: ELECTRON_VERSION,
    files,
    sha256: hashes,
  };
}

/** 复制上游 helper、Electron、动画和字体，形成独立可启动的运行包。 */
async function main() {
  const commit = sourceCommit();
  if (commit !== EXPECTED_COMMIT) {
    throw new Error(`dsh-pet 提交不匹配：需要 ${EXPECTED_COMMIT}，实际 ${commit}`);
  }
  await rm(OUTPUT, { recursive: true, force: true });
  await mkdir(OUTPUT, { recursive: true });

  // npm ci 不执行 prepare，随后显式构建宿主不需要的类型和桌面共享核心。
  run('npm', ['ci', '--ignore-scripts']);
  run('npm', ['run', 'bundle']);
  run('npm', ['run', 'build:desktop-core']);
  run('npm', ['run', 'types']);
  run('npm', ['run', 'ensure:electron']);

  await cp(join(SOURCE, 'runtime/electron-helper'), join(OUTPUT, 'helper'), { recursive: true });
  await patchHelperMain(join(OUTPUT, 'helper/main.js'));
  await cp(join(CACHE, 'electron'), join(OUTPUT, 'runtime'), { recursive: true });
  await mkdir(join(OUTPUT, 'licenses'), { recursive: true });
  await cp(join(SOURCE, 'LICENSE'), join(OUTPUT, 'licenses/dsh-pet-LICENSE'), { force: true });
  await cp(join(CACHE, 'electron/LICENSE'), join(OUTPUT, 'licenses/electron-LICENSE'), { force: true });
  await cp(join(CACHE, 'electron/LICENSES.chromium.html'),
            join(OUTPUT, 'licenses/chromium-LICENSES.html'), { force: true });
  for (const directory of ['webm', 'fonts', 'pic', 'memes']) {
    await cp(join(SOURCE, 'assets', directory), join(OUTPUT, 'assets', directory), { recursive: true });
  }
  const configJsonc = await readFile(join(SOURCE, 'assets/config.jsonc'), 'utf8');
  const configJson = jsoncToJson(configJsonc);
  const defaultConfig = JSON.parse(configJson);
  // 上游 readAllConfig 的 GET 结果按配置实例包裹为 { main: ... }，桌面 helper 消费该成品形状。
  const aggregatedConfig = { main: defaultConfig };
  await mkdir(join(OUTPUT, 'assets'), { recursive: true });
  await writeFile(join(OUTPUT, 'assets/config.json'), `${JSON.stringify(aggregatedConfig, null, 2)}\n`, 'utf8');
  await writeFile(join(OUTPUT, 'assets/config.jsonc'), configJsonc, 'utf8');

  const manifest = await buildManifest();
  await writeFile(join(OUTPUT, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
  console.log(`[build-pet-runtime] 已生成 ${OUTPUT}`);
  console.log(`[build-pet-runtime] 文件数=${manifest.files.length} Electron=${ELECTRON_VERSION}`);
}

await main();
