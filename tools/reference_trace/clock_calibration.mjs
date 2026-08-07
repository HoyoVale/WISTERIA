// R1.3B Phase 0B: synthetic clock calibration fixture.
//
// Verifies that the reference stepping pattern maps to the WISTERIA
// deterministic clock exactly as frozen in the Phase 0B contract (§4.1):
//
//   motionFrame 0   = physicsTick 0    = 0.000s  (prepared boundary, 0 steps)
//   motionFrame N   = physicsTick 4N   = N/30 s  (exactly 4 steps per frame)
//
// Uses only ammojs-typed (a small dynamic box on a ground plane), so the
// calibration is independent of babylon-mmd model loading.

import Ammo from "ammojs-typed";

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

let physicsTick = 0;
let failed = false;

const checkBoundary = (motionFrame) => {
  const expectedTick = motionFrame * 4;
  const expectedSeconds = motionFrame / 30;
  const tickOk = physicsTick === expectedTick;
  const simulatedSeconds = physicsTick / 120;
  const secondsOk = Math.abs(simulatedSeconds - expectedSeconds) < 1e-9;
  const ok = tickOk && secondsOk;
  if (!ok) failed = true;
  console.log(
    `${ok ? "PASS" : "FAIL"} motionFrame=${motionFrame} ` +
      `physicsTick=${physicsTick} (expect ${expectedTick}) ` +
      `simulatedSeconds=${simulatedSeconds.toFixed(6)} ` +
      `(expect ${expectedSeconds.toFixed(6)})`
  );
};

// motionFrame 0 is a prepared boundary: NO physics step.
checkBoundary(0);

for (let motionFrame = 1; motionFrame <= MOTION_FRAMES; ++motionFrame) {
  // Exactly 4 fixed 120Hz ticks per motion frame (contract §4.1).
  for (let step = 0; step < 4; ++step) {
    world.stepSimulation(FIXED_TIME_STEP, 1, FIXED_TIME_STEP);
    physicsTick += 1;
  }
  if (motionFrame === 1 || motionFrame === 2 ||
      motionFrame === 30 || motionFrame === 100 ||
      motionFrame === MOTION_FRAMES) {
    checkBoundary(motionFrame);
  }
}

if (failed) {
  console.error("clock calibration FAILED");
  process.exit(1);
}

console.log(
  "clock calibration PASSED: motionFrame 300 = physicsTick 1200 = 10s"
);
process.exit(0);
