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
let cmakeFlags = [
  `-DCMAKE_BUILD_TYPE=${buildType}`,
  '-DCMAKE_INSTALL_PREFIX=./out/',
  '-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15',
];

if (argv.dev) {
  cmakeFlags.push('-DELEM_DEV_LOCALHOST=1');
}

if (process.env.JUCE_WEBVIEW2_PACKAGE_LOCATION) {
  cmakeFlags.push(`-DJUCE_WEBVIEW2_PACKAGE_LOCATION=${process.env.JUCE_WEBVIEW2_PACKAGE_LOCATION}`);
}

await $`cmake ${cmakeFlags} ../..`;
await $`cmake --build . --config ${buildType} -j 4`;
