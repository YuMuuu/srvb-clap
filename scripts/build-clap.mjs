#!/usr/bin/env zx

const rootDir = await path.resolve(__dirname, '..');
const buildDir = await path.join(rootDir, 'native', 'build', 'clap');
const buildType = argv.dev ? 'Debug' : 'Release';
const cmakeFlags = [
  `-DCMAKE_BUILD_TYPE=${buildType}`,
  '-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15',
  '-DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE',
  '-DSRVB_BUILD_CLAP=ON',
  '-DSRVB_BUILD_JUCE=OFF',
];

if (argv.dev) {
  cmakeFlags.push('-DELEM_DEV_LOCALHOST=1');
}

echo(`Root directory: ${rootDir}`);
echo(`Build directory: ${buildDir}`);

await fs.remove(buildDir);
await fs.ensureDir(buildDir);

cd(buildDir);

await $`cmake ${cmakeFlags} ../..`;
await $`cmake --build . --config ${buildType} --target SRVB_all -j 4`;
