// R1.3B Phase 0B: shared motion-frame orchestration (contract §4.1).
//
// prepareFrame0:
//   sample(0) once → initialize() once → publish() once
//   NO physics tick.
//
// stepMotionFrame(frame):
//   sample(frame) once → exactly 4 tick() calls → publish() once.
//
// Both the synthetic clock calibration fixture and the real trace exporter
// must use this driver so the execution order can never drift from the
// frozen contract while the tick counter stays correct.

export const prepareFrame0 = ({ sample, initialize, publish }) => {
  sample(0);
  initialize();
  publish();
};

export const stepMotionFrame = ({ frame, sample, tick, publish }) => {
  sample(frame);
  for (let step = 0; step < 4; ++step) {
    tick();
  }
  publish();
};
