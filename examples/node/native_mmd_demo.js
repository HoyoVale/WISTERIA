#!/usr/bin/env node
// WISTERIA native C ABI headless demo through Node N-API.
//
// Build the addon first:
//   npm run build          (or: node-gyp rebuild)
// Then run from the project root:
//   node examples/node/native_mmd_demo.js [--frames 720]

"use strict";

const path = require("path");

const projectRoot = path.resolve(__dirname, "..", "..");
const addonPath = path.join(
    __dirname,
    "build",
    "Release",
    "wisteria_native_demo.node"
);

let addon;
try {
    addon = require(addonPath);
} catch (error) {
    console.error(
        "[FFI] addon not built: " + addonPath +
        "\n      run `npm run build` in examples/node first."
    );
    console.error("      " + error.message);
    process.exit(1);
}

const args = process.argv.slice(2);
function argumentValue(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

const frames = Number(argumentValue("--frames") || "720");
const windowMode = args.includes("--window");

const options = {
    model: path.join(
        projectRoot,
        "assets",
        "models",
        "mmd",
        "蕾米埃尔-白",
        "蕾米埃尔-白.pmx"
    ),
    motion: path.join(
        projectRoot,
        "assets",
        "motions",
        "梦的翅膀",
        "梦的翅膀motion.vmd"
    ),
    frames,
    fps: 60,
    physicsFps: 120,
    maxSubSteps: 10
};

const result = windowMode
    ? addon.runWindowDemo(options)
    : addon.runDemo(options);

if (!result.ok) {
    console.error("[FFI] ERROR: " + result.error);
    process.exit(1);
}

if (windowMode) {
    console.log("[FFI] window opened: model + motion + physics loaded");
    for (const sample of result.samples) {
        const camera = sample.camera
            .map((value) => value.toFixed(2))
            .join(",");
        console.log(
            `[FFI] frame=${String(sample.frame).padStart(4)} ` +
            `camPos->target=(${camera}) space=${sample.space}`
        );
    }
    console.log(
        `[FFI] done frames=${frames} ` +
        `closedByUser=${result.closed ? "yes" : "no"}`
    );
    process.exit(0);
}

console.log(`[FFI] maxFrame=${result.maxFrame.toFixed(1)}`);
for (const sample of result.samples) {
    const min = sample.min.map((value) => value.toFixed(3)).join(",");
    const max = sample.max.map((value) => value.toFixed(3)).join(",");
    console.log(
        `[FFI] frame=${String(sample.frame).padStart(4)} ` +
        `motion=${sample.motionFrame.toFixed(2).padStart(7)} ` +
        `finite=${sample.finite ? 1 : 0} ` +
        `min=(${min}) max=(${max}) ` +
        `displacement=${sample.displacement.toFixed(3)} ` +
        `vertices=${sample.vertices}`
    );
}
console.log(
    `[FFI] done frames=${frames} ` +
    `finalFrame=${result.finalFrame.toFixed(2)} ` +
    `finite=${result.finite} ` +
    `maxBindDisplacement=${result.maxDisplacement.toFixed(3)} ` +
    `paused=${result.pausedFrame.toFixed(2)}`
);
