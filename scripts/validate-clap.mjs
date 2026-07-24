#!/usr/bin/env zx

const rootDir = path.resolve(__dirname, '..');
const buildType = argv.dev ? 'Debug' : 'Release';
const artefactsDir = path.join(rootDir, 'native', 'build', 'scripted', 'SRVB_artefacts', buildType);
const version = '0.4.1';
const cacheDir = path.join(rootDir, '.cache', 'clap-validator', version);

const releasesBase = `https://github.com/free-audio/clap-validator/releases/download/${version}`;
const platform = {
  darwin: {
    archive: `clap-validator-${version}-127-g152b982-macos-universal.zip`,
    executable: 'clap-validator',
  },
  linux: {
    archive: `clap-validator-${version}-127-g152b982-ubuntu-22.04.zip`,
    executable: 'clap-validator',
  },
  win32: {
    archive: `clap-validator-${version}-127-g152b982-windows.zip`,
    executable: 'clap-validator.exe',
  },
}[process.platform];

if (!platform) {
  throw new Error(`Unsupported platform for clap-validator: ${process.platform}`);
}

const validator = path.join(cacheDir, platform.executable);
const pluginPath = [
  process.env.CLAP_PLUGIN_PATH,
  path.join(artefactsDir, 'SRVB.clap'),
  path.join(artefactsDir, 'CLAP', 'SRVB.clap'),
].filter(Boolean).find((candidate) => fs.existsSync(candidate));

if (!pluginPath) {
  console.error(`Could not find SRVB.clap under ${artefactsDir}`);
  console.error('Set CLAP_PLUGIN_PATH to validate a plugin bundle in another location.');
  console.error('Run `npm run build-native` first, or use `npm run validate:clap:dev` for a dev build.');
  process.exit(1);
}

if (!fs.existsSync(validator)) {
  const archive = path.join(cacheDir, platform.archive);
  const extractDir = path.join(cacheDir, 'extract');

  fs.mkdirSync(cacheDir, { recursive: true });
  fs.rmSync(extractDir, { recursive: true, force: true });
  fs.mkdirSync(extractDir, { recursive: true });

  await $`curl -L --fail -o ${archive} ${`${releasesBase}/${platform.archive}`}`;
  await $`unzip -oq ${archive} -d ${extractDir}`;

  if (process.platform === 'win32') {
    await $`cp ${path.join(extractDir, platform.executable)} ${validator}`;
  } else {
    const innerArchive = (await $`find ${extractDir} -name '*.tar.gz' -print -quit`).stdout.trim();
    await $`tar -xzf ${innerArchive} -C ${extractDir}`;
    await $`cp ${path.join(extractDir, 'binaries', platform.executable)} ${validator}`;
    await $`chmod +x ${validator}`;
  }
}

await $`${validator} validate ${pluginPath}`;
