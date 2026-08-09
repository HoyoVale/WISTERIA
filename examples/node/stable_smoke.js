#!/usr/bin/env node
// WISTERIA Stable Runtime/Render C ABI smoke through Node N-API.
//
// Non-blocking R1.9 Phase 0E compatibility smoke: creates a Generic entity,
// steps the exact timeline, captures a checkpoint, renders a single RGBA8
// frame and verifies the status semantics are visible from JavaScript.
//
// Build first:
//   npm run build-stable        (or: node-gyp rebuild)
// Then run from the project root:
//   node examples/node/stable_smoke.js

"use strict";

const path = require("path");

const projectRoot = path.resolve(__dirname, "..", "..");
const addonPath = path.join(
    __dirname,
    "build",
    "Release",
    "wisteria_stable_demo.node"
);

let stable;
try {
    stable = require(addonPath);
} catch (error) {
    console.error(
        "[STABLE] addon not built: " + addonPath +
        "\n      run `npm run build-stable` in examples/node first."
    );
    console.error("      " + error.message);
    process.exit(1);
}

function fail(message) {
    console.error("[STABLE] FAIL: " + message);
    process.exit(1);
}

function requireOk(result, label) {
    if (!result || result.status !== 0) {
        fail(label + " -> status=" +
            (result ? result.status : "?") +
            " error=" + (result ? result.error : "?"));
    }
    return result;
}

const modelPath = path.join(
    projectRoot,
    "tests",
    "data",
    "animated_triangle.gltf"
);

const created = requireOk(stable.createContext(), "createContext");
const context = created.context;
try {
    const entityResult = requireOk(
        stable.createEntity(context, modelPath, {
            compatibility: 1,
            fixedTimeStep: 1.0 / 120.0,
            maxSubSteps: 10,
            gravity: [0.0, -98.0, 0.0],
            physicsEnabled: 1,
        }),
        "createEntity"
    );
    const entity = entityResult.entity;

    const caps = requireOk(
        stable.capabilities(context, entity),
        "capabilities"
    );
    if (caps.backend_id !== 2) {
        fail("expected Generic backend id 2, got " + caps.backend_id);
    }
    if (caps.profile_id !== 2 || caps.payload_kind !== 2) {
        fail(
            "expected Generic profile/payload 2/2, got " +
            caps.profile_id + "/" + caps.payload_kind
        );
    }

    requireOk(
        stable.prepareFrameZero(context, entity),
        "prepareFrameZero"
    );
    requireOk(
        stable.stepExact(context, entity, 1),
        "stepExact(1)"
    );
    requireOk(
        stable.stepExact(context, entity, 2),
        "stepExact(2)"
    );
    requireOk(
        stable.replayExact(context, entity, 3),
        "replayExact(3)"
    );

    const checkpointResult = requireOk(
        stable.checkpointCreate(context, entity),
        "checkpointCreate"
    );
    requireOk(
        stable.checkpointDestroy(context, checkpointResult.checkpoint),
        "checkpointDestroy"
    );

    const sessionResult = requireOk(
        stable.renderSessionCreate(context),
        "renderSessionCreate"
    );
    const frame = requireOk(
        stable.renderFrame(
            context,
            sessionResult.session,
            entity,
            64,
            64
        ),
        "renderFrame"
    );
    if (!(frame.non_zero_pixels > 0)) {
        fail("render frame is all zero");
    }
    const unsupported = stable.stepExact(context, 0xDEADBEEF, 1);
    if (!unsupported || unsupported.status !== 2) {
        fail("garbage entity must be NOT_FOUND(2), got " +
            (unsupported ? unsupported.status : "?"));
    }
    const diagnostic = stable.lastError(context);
    if (typeof diagnostic !== "string" || diagnostic.length === 0) {
        fail("lastError must return a diagnostic after NOT_FOUND");
    }

    requireOk(
        stable.destroyEntity(context, entity),
        "destroyEntity"
    );
    requireOk(
        stable.renderSessionDestroy(context, sessionResult.session),
        "renderSessionDestroy"
    );
    requireOk(stable.destroyContext(context), "destroyContext");
} catch (error) {
    fail("unexpected exception: " + error.message);
}

console.log(
    "[STABLE] PASS: Node N-API stable ABI smoke " +
    "(entity + exact step + checkpoint + render)"
);
