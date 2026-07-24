#!/usr/bin/env zx


let rootDir = await path.resolve(__dirname, '..');
let buildDir = await path.join(rootDir, 'native', 'build', 'scripted');

echo(`Root directory: ${rootDir}`);
echo(`Build directory: ${buildDir}`);

// Clean the build directory before we build
await fs.remove(buildDir);
await fs.ensureDir(buildDir);

cd(buildDir);

let buildType = argv.dev ? 'Debug' : 'Release';
let pluginFormats = process.platform === 'darwin'
  ? ['CLAP', 'VST3', 'AUV2']
  : ['CLAP', 'VST3'];
let cmakeFlags = [
  `-DCMAKE_BUILD_TYPE=${buildType}`,
  '-DCMAKE_INSTALL_PREFIX=./out/',
  '-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15',
  '-DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON',
  `-DSRVB_PLUGIN_FORMATS=${pluginFormats.join(';')}`,
];

if (argv.dev) {
  cmakeFlags.push('-DELEM_DEV_LOCALHOST=1');
}

await $`cmake ${cmakeFlags} ../..`;
await $`cmake --build . --config ${buildType} -j 4`;
