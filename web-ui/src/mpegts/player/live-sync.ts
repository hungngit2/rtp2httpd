import type { PlayerConfig } from "../config";
import Log from "../utils/logger";

const TAG = "LiveSync";

/** Each live-edge underrun raises the latency floor by this much (seconds). */
const UNDERRUN_BACKOFF_STEP = 1;
/** Upper bound for the adaptive latency increase (seconds). */
const UNDERRUN_BACKOFF_MAX = 6;
/**
 * Accumulated tolerance decays by this many seconds per second of stable
 * (non-underrunning) playback. Without this, a tolerance spike only clears
 * once latency drops under the *unpadded* target — which the capped 1.2x
 * catch-up rate rarely achieves before the next underrun on a constrained
 * device — so it would otherwise stay elevated indefinitely.
 */
const EXTRA_LATENCY_DECAY_PER_SEC = 0.05;

/** Forward buffer seconds ahead of currentTime within the containing range. */
function forwardBufferAhead(video: HTMLMediaElement): number {
  const t = video.currentTime;
  const buffered = video.buffered;
  for (let i = 0; i < buffered.length; i++) {
    if (t >= buffered.start(i) && t <= buffered.end(i)) {
      return buffered.end(i) - t;
    }
  }
  return 0;
}

/** Sets up live latency synchronization by adjusting playbackRate on timeupdate events. */
export function setupLiveSync(
  video: HTMLMediaElement,
  config: PlayerConfig,
  getLiveEdgeLatency: () => number | null,
): () => void {
  if (config.liveSync) {
    Log.v(
      TAG,
      "Live sync enabled, target latency:",
      config.liveSyncTargetLatency,
      "max latency:",
      config.liveSyncMaxLatency,
    );
  }

  let extraLatency = 0;
  // Baselined off video.currentTime (not wall clock): it naturally freezes
  // while paused or stalled, so decay only progresses during real playback
  // without needing an explicit video.paused check.
  let lastDecayTime = video.currentTime;

  function onTimeUpdate(): void {
    if (!config.liveSync) return;

    const latency = getLiveEdgeLatency();
    if (latency === null) return;

    const now = video.currentTime;
    const dt = now - lastDecayTime;
    lastDecayTime = now;
    // dt<=0 covers seeks/discontinuities; dt>=1 guards against a large gap
    // (e.g. tab backgrounded) decaying tolerance in one big jump.
    if (extraLatency > 0 && dt > 0 && dt < 1) {
      extraLatency = Math.max(0, extraLatency - EXTRA_LATENCY_DECAY_PER_SEC * dt);
    }

    if (latency > config.liveSyncMaxLatency + extraLatency) {
      const targetRate = Math.min(2, Math.max(1, config.liveSyncPlaybackRate));
      if (targetRate !== video.playbackRate) {
        Log.v(TAG, `Video playback rate set to ${targetRate}`);
        video.playbackRate = targetRate;
      }
    } else if (latency <= config.liveSyncTargetLatency + extraLatency) {
      if (video.playbackRate !== 1 && video.playbackRate !== 0) {
        video.playbackRate = 1;
        Log.v(TAG, "Video playback rate reset to 1");
      }
      // Recovered — drop adaptive backoff
      if (extraLatency > 0 && latency <= config.liveSyncTargetLatency) {
        extraLatency = 0;
      }
    }
  }

  function onWaiting(): void {
    if (!config.liveSync) return;

    // Seek/Go Live often fires waiting while data is still buffered ahead — not an underrun.
    if (video.seeking) return;

    const lag = getLiveEdgeLatency();
    if (lag === null) return;

    const ahead = forwardBufferAhead(video);
    // Near source-mode live edge AND playhead has caught up with its forward buffer.
    const atLiveEdge = lag < 0.5 && ahead < 0.5;
    if (!atLiveEdge) return;

    if (video.playbackRate !== 1 && video.playbackRate !== 0) {
      video.playbackRate = 1;
    }

    if (extraLatency < UNDERRUN_BACKOFF_MAX) {
      extraLatency = Math.min(extraLatency + UNDERRUN_BACKOFF_STEP, UNDERRUN_BACKOFF_MAX);
    }
    Log.w(
      TAG,
      `Live-edge underrun, raising latency tolerance: target ${(config.liveSyncTargetLatency + extraLatency).toFixed(1)}s, max ${(config.liveSyncMaxLatency + extraLatency).toFixed(1)}s`,
    );
  }

  video.addEventListener("timeupdate", onTimeUpdate);
  video.addEventListener("waiting", onWaiting);

  return () => {
    Log.v(TAG, "Video playback rate reset to 1, live sync disabled");
    video.removeEventListener("timeupdate", onTimeUpdate);
    video.removeEventListener("waiting", onWaiting);
    video.playbackRate = 1;
  };
}
