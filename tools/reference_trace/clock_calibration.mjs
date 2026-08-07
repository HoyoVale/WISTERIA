// R1.3B Phase 0B: synthetic clock calibration fixture.
//
// Uses the shared frame driver (frame_driver.mjs) with spy callbacks so it
// verifies not only the cumulative tick count but also the exact execution
// order and call counts per motion frame:
//
//   prepareFrame0:  sample(0) x1 → initialize x1 → publish x1, ticks 0
//   stepMotionFrame: sample(N) x1 → tick x4 → publish x1
//
// The tick spy still runs a real ammo.js fixed step (ground + dynamic box),
// so 1200 real 120Hz steps execute over 300 motion frames.

import Ammo from "ammojs-typed";
import { prepareFrame0, stepMotionFrame } from "./frame_driver.mjs";

const MOTION_FRAMES = 300;
const FIXED_TIME_STEP = 1 / 120;

const ammo = await Ammo();

const collisionConfiguration = new ammo.btDefaultCollisionConfiguration();
const dispatcher = new ammo.btCollisionDispatcher(collisionConfiguration);
const broadphase = new ammo.btDbvtBroadphase();
const solver = new ammo.btSequentialImpulseConstraintSolver();
const world = new ammo.btDiscreteDynamicsWorld(
  dispatcher,
  broadphase,
  solver,
  collisionConfiguration
);
world.setGravity(new ammo.btVector3(0, -98, 0));

const groundShape = new ammo.btStaticPlaneShape(
  new ammo.btVector3(0, 1, 0),
  0
);
const groundTransform = new ammo.btTransform();
groundTransform.setIdentity();
const groundMotion = new ammo.btDefaultMotionState(groundTransform);
const groundInfo = new ammo.btRigidBodyConstructionInfo(
  0,
  groundMotion,
  groundShape,
  new ammo.btVector3(0, 0, 0)
);
const ground = new ammo.btRigidBody(groundInfo);
world.addRigidBody(ground, 1, 1);

const boxShape = new ammo.btBoxShape(new ammo.btVector3(0.5, 0.5, 0.5));
const boxTransform = new ammo.btTransform();
boxTransform.setIdentity();
boxTransform.setOrigin(new ammo.btVector3(0, 2, 0));
const boxMotion = new ammo.btDefaultMotionState(boxTransform);
const boxInfo = new ammo.btRigidBodyConstructionInfo(
  1,
  boxMotion,
  boxShape,
  new ammo.btVector3(1, 1, 1)
);
const box = new ammo.btRigidBody(boxInfo);
world.addRigidBody(box, 1, 1);

let sampleCount = 0;
let initializeCount = 0;
let publishCount = 0;
let tickCount = 0;
const order = [];
let failed = false;

const check = (label, condition) => {
  if (!condition) failed = true;
  console.log(`${condition ? "PASS" : "FAIL"} ${label}`);
};

const checkBoundary = (motionFrame) => {
  const expectedTick = motionFrame * 4;
  const expectedSeconds = motionFrame / 30;
  const simulatedSeconds = tickCount / 120;
  check(
    `motionFrame=${motionFrame} physicsTick=${tickCount} ` +
      `(expect ${expectedTick}) simulatedSeconds=${simulatedSeconds.toFixed(6)} ` +
      `(expect ${expectedSeconds.toFixed(6)})`,
    tickCount === expectedTick &&
      Math.abs(simulatedSeconds - expectedSeconds) < 1e-9
  );
};

// motionFrame 0: prepared boundary through the shared driver.
{
  const orderStart = order.length;
  prepareFrame0({
    sample: (frame) => {
      check(`frame0 sample called with frame=${frame}`, frame === 0);
      sampleCount += 1;
      order.push("sample0");
    },
    initialize: () => {
      initializeCount += 1;
      order.push("initialize");
    },
    publish: () => {
      publishCount += 1;
      order.push("publish0");
    }
  });
  check(
    "frame0 order sample -> initialize -> publish",
    order.slice(orderStart).join(",") === "sample0,initialize,publish0"
  );
  check(
    "frame0 counts sample=1 initialize=1 publish=1 ticks=0",
    sampleCount === 1 &&
      initializeCount === 1 &&
      publishCount === 1 &&
      tickCount === 0
  );
  checkBoundary(0);
}

for (let motionFrame = 1; motionFrame <= MOTION_FRAMES; ++motionFrame) {
  const beforeSample = sampleCount;
  const beforeTick = tickCount;
  const beforePublish = publishCount;
  const orderStart = order.length;
  stepMotionFrame({
    frame: motionFrame,
    sample: (frame) => {
      check(
        `frame=${motionFrame} sample receives absolute frame ${frame}`,
        frame === motionFrame
      );
      sampleCount += 1;
      order.push(`sample${motionFrame}`);
    },
    tick: () => {
      tickCount += 1;
      order.push("tick");
      world.stepSimulation(FIXED_TIME_STEP, 1, FIXED_TIME_STEP);
    },
    publish: () => {
      publishCount += 1;
      order.push(`publish${motionFrame}`);
    }
  });
  const frameOrder = order.slice(orderStart).join(",");
  const expectedOrder =
    `sample${motionFrame},tick,tick,tick,tick,publish${motionFrame}`;
  check(
    `frame=${motionFrame} order sample -> 4 ticks -> publish`,
    frameOrder === expectedOrder
  );
  check(
    `frame=${motionFrame} counts sample=1 ticks=4 publish=1`,
    sampleCount - beforeSample === 1 &&
      tickCount - beforeTick === 4 &&
      publishCount - beforePublish === 1
  );
  if (
    motionFrame === 1 ||
    motionFrame === 2 ||
    motionFrame === 30 ||
    motionFrame === 100 ||
    motionFrame === MOTION_FRAMES
  ) {
    checkBoundary(motionFrame);
  }
}

if (failed) {
  console.error("clock calibration FAILED");
  process.exit(1);
}

console.log(
  "clock calibration PASSED: motionFrame 300 = physicsTick 1200 = 10s, " +
    `samples=${sampleCount} publishes=${publishCount} ticks=${tickCount}`
);
process.exit(0);
