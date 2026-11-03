"""Exercise LINK's real OpenXR loader/backend with a QA-only synthetic runtime.

This does not verify a headset, Quest Link, tracking hardware, or human comfort.
XR_RUNTIME_JSON is changed only in the launched process environment. No registry,
Meta service, installed runtime, or headset setting is read or modified.
"""
from __future__ import annotations
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile

RUNTIME_NAME = "LINK Synthetic Test Runtime"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(executable: Path, runtime: Path, output: Path) -> None:
    require(executable.is_file(), f"Browser executable does not exist: {executable}")
    require(runtime.is_file(), f"Synthetic runtime DLL does not exist: {runtime}")
    output.mkdir(parents=True, exist_ok=True)
    app_report = output / "browser.json"
    runtime_report = output / "runtime.json"
    for path in (app_report, runtime_report):
        path.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="link-xr-synthetic-") as temporary:
        directory = Path(temporary)
        # Keep the loader-facing DLL path ASCII; repository paths may contain Korean.
        staged_runtime = directory / "link_xr_test_runtime.dll"
        shutil.copy2(runtime, staged_runtime)
        manifest = directory / "runtime.json"
        manifest.write_text(json.dumps({
            "file_format_version": "1.0.0",
            "runtime": {"name": RUNTIME_NAME, "library_path": str(staged_runtime), "api_version": "1.1"},
        }), encoding="utf-8")
        world = directory / "worlds" / "syntheticvr"
        world.mkdir(parents=True)
        (world / "world.link").write_text('''world {
version: "0.1"; id: "syntheticvr"; name: "Synthetic OpenXR QA";
elements: "scene.element"; style: "appearance.style"; scripts: ["beacon.script"];
entry: {position:[0,1.65,0],target:[0,1.65,-2]};
environment: {background:"#1F3041",grid:false};
walk: {min:[-2,0,-3],max:[2,3,2],eyeHeight:1.65,speed:1};
}
''', encoding="utf-8")
        (world / "appearance.style").write_text('''defaults {color:"#DFBD72"; roughness:.65;}
material ground {color:"#627F73";roughness:1;}
material marker {color:"#84B3D9";roughness:.5;}
''', encoding="utf-8")
        (world / "scene.element").write_text('''scene {
object room {
component beacon {state:{clicks:0};script:"beacon.script";
element target {geometry:{type:"box",size:[.55,.55,.3]};position:[0,1.5,-2];}
}
component surroundings {
element floor {geometry:{type:"plane",size:[4,5]};position:[0,0,-1];rotation:[-90,0,0];style:"ground";}
element asymmetric_marker {geometry:{type:"box",size:[.32,1.3,.3]};position:[-.9,.65,-2.3];style:"marker";}
}
}
}
''', encoding="utf-8")
        (world / "beacon.script").write_text('''on onStart(ctx) {ctx.setState({clicks:0});}
on onUpdate(ctx,deltaSeconds) {}
on onInteract(ctx,event) {ctx.setState({clicks:ctx.getState().clicks+1});ctx.log("synthetic beacon clicked");}
on onDestroy(ctx) {}
''', encoding="utf-8")
        environment = os.environ.copy()
        environment.pop("LINK_XR_TEST_VISIBLE_ONLY", None)
        environment["XR_RUNTIME_JSON"] = str(manifest)
        environment["LINK_XR_TEST_REPORT"] = str(runtime_report.resolve())
        process = subprocess.run([
            str(executable.resolve()), "--worlds", str(world.parent),
            "--open", "link://local/syntheticvr", "--vr-smoke", str(app_report.resolve()),
        ], env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        log = process.stdout.decode("utf-8", errors="replace")
        (output / "browser.log").write_text(log, encoding="utf-8")
        require(process.returncode == 0, f"Synthetic Browser smoke exited {process.returncode}:\n{log}")
        require(app_report.is_file(), f"Browser did not produce its report:\n{log}")
        require(runtime_report.is_file(), f"Synthetic runtime did not complete teardown:\n{log}")
        app = json.loads(app_report.read_text(encoding="utf-8-sig"))
        stats = json.loads(runtime_report.read_text(encoding="utf-8-sig"))
        require(app.get("passed") is True, f"Browser rejected synthetic smoke: {app}")
        require(RUNTIME_NAME in app.get("runtime", ""), "Browser did not identify the synthetic runtime")
        require(app.get("syntheticRuntime") is True, "Browser report must identify synthetic execution")
        require(app.get("headsetFramesVerified") is False, "Synthetic rendering cannot verify headset frames")
        require(app.get("humanComfortVerified") is False, "Synthetic rendering cannot verify human comfort")
        require(app.get("submittedFrames", 0) >= 120, "Browser must submit at least 120 stereo frames")
        require(app.get("trackedHeadSamples", 0) >= 120, "Backend must receive tracked head poses")
        require(app.get("leftControllerSamples", 0) > 0 and app.get("rightControllerSamples", 0) > 0, "Both controller actions must be handled")
        require(app.get("focusedInputFrames") == 108, "Only the 108 frames after runtime focus may accept input")
        require("OpenXR input: waiting-focus" in log and "OpenXR input: ready" in log, "Browser must expose both waiting-focus and ready states")
        require(log.index("OpenXR input: waiting-focus") < log.index("OpenXR input: ready"), "Input readiness must follow the runtime focus transition")
        require(app.get("triggerInteractions") == 2, f"Expected two trigger edges; hold/loss must not retrigger: {app}")
        require(log.count("synthetic beacon clicked") == 2, "Controller raycast must execute the beacon's Script twice")
        require(stats.get("runtime") == RUNTIME_NAME and stats.get("syntheticRuntime") is True, "Runtime report identity is invalid")
        require(stats.get("orderErrors") == 0, f"OpenXR order validation failed: {stats}")
        require(stats.get("resourcesReleased") is True, f"OpenXR teardown retained resources: {stats}")
        require(stats["waitFrames"] == stats["beginFrames"] == stats["endFrames"], "Every frame wait/begin must end")
        require(stats["submittedFrames"] == app["submittedFrames"], "Browser and runtime submission counts differ")
        require(stats["acquiredImages"] == stats["waitedImages"] == stats["releasedImages"] == stats["submittedFrames"] * 2, "Every submitted stereo image must acquire/wait/release once")
        require(stats["createdSwapchains"] == stats["destroyedSwapchains"] == 2, "Both eye swapchains must be destroyed")
        require(stats["createdSpaces"] == stats["destroyedSpaces"] == 4, "All reference/controller spaces must be destroyed")
        require(stats.get("unfocusedFrames") == 12 and stats.get("focusedFrames") == 108, "Synthetic runtime must render twelve visible unfocused frames before focus")
        require(stats.get("unfocusedInputCalls") == 0 and stats.get("focusedSyncs") == 108, "Input and feedback must wait for runtime focus")
        require(stats.get("focusHeldSamples") == 2 and stats.get("focusNeutralSamples") == 5, "Focus return must initially report held trigger, then neutral rearm")
        require(stats["haptics"] == 2, "Each accepted trigger edge must produce selection feedback")
        require(stats["trackingLossSamples"] > 0 and stats["heldAfterLossSamples"] > 0 and stats["neutralSamples"] > 0, "Tracking-loss/rearm sequence was not exercised")
        require(stats.get("eyeTexturesWritten") is True, "Both OpenGL eye textures must overwrite the known zero initialization")
        require(stats["leftEyeHash"] != "0" and stats["rightEyeHash"] != "0", "Both OpenGL eye textures must be rendered")
        require(stats["leftEyeHash"] != stats["rightEyeHash"], "Stereo eye textures must differ")
        # A runtime can render while platform UI still withholds input focus.
        # Such a run must fail the Browser smoke with actionable guidance.
        visible_app_path = output / "browser-visible-only.json"
        visible_runtime_path = output / "runtime-visible-only.json"
        for path in (visible_app_path, visible_runtime_path):
            path.unlink(missing_ok=True)
        visible_environment = environment.copy()
        visible_environment["LINK_XR_TEST_VISIBLE_ONLY"] = "1"
        visible_environment["LINK_XR_TEST_REPORT"] = str(visible_runtime_path.resolve())
        visible_process = subprocess.run([
            str(executable.resolve()), "--worlds", str(world.parent),
            "--open", "link://local/syntheticvr", "--vr-smoke", str(visible_app_path.resolve()),
        ], env=visible_environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        visible_log = visible_process.stdout.decode("utf-8", errors="replace")
        (output / "browser-visible-only.log").write_text(visible_log, encoding="utf-8")
        require(visible_process.returncode == 2, f"Visible-only runtime must fail smoke with exit 2:\n{visible_log}")
        require(visible_app_path.is_file() and visible_runtime_path.is_file(), "Visible-only smoke must report and tear down")
        visible_app = json.loads(visible_app_path.read_text(encoding="utf-8-sig"))
        visible_stats = json.loads(visible_runtime_path.read_text(encoding="utf-8-sig"))
        require(visible_app.get("passed") is False, "Rendering without input focus cannot pass the smoke")
        require(visible_app.get("syntheticRuntime") is True and visible_app.get("headsetFramesVerified") is False, "Visible-only execution remains synthetic")
        require(visible_app.get("humanComfortVerified") is False, "Visible-only execution cannot verify comfort")
        require(visible_app.get("submittedFrames") == 120 and visible_app.get("trackedHeadSamples") == 120, "Visible-only scenario must render while retaining head tracking")
        require(visible_app.get("focusedInputFrames") == 0 and visible_app.get("triggerInteractions") == 0, "Unfocused rendering must never accept interactions")
        require(visible_app.get("leftControllerSamples") == 0 and visible_app.get("rightControllerSamples") == 0, "Controller samples must wait for focus")
        require("Unknown Sources" in visible_app.get("failure", "") and "Meta Quest Link" in visible_app.get("failure", ""), "Visible-only failure must explain the platform setting")
        require("OpenXR input: waiting-focus" in visible_log and "OpenXR input: ready" not in visible_log, "Visible-only status must remain waiting-focus")
        require("synthetic beacon clicked" not in visible_log, "No Script interaction may occur without focus")
        require(visible_stats.get("visibleOnly") is True and visible_stats.get("unfocusedFrames") == 120 and visible_stats.get("focusedFrames") == 0, "Visible-only runtime must withhold focus for every rendered frame")
        require(visible_stats.get("focusedSyncs") == 0 and visible_stats.get("unfocusedInputCalls") == 0 and visible_stats.get("haptics") == 0, "Visible-only runtime must receive no input sync or feedback")
        require(visible_stats.get("orderErrors") == 0 and visible_stats.get("resourcesReleased") is True, "Focus failure must cleanly release runtime resources")
        require(visible_stats["waitFrames"] == visible_stats["beginFrames"] == visible_stats["endFrames"] == 120, "Visible-only frames must remain balanced")
        require(visible_stats["acquiredImages"] == visible_stats["waitedImages"] == visible_stats["releasedImages"] == 240, "Visible-only eye image ownership must remain balanced")
        require(visible_stats.get("eyeTexturesWritten") is True and visible_stats["leftEyeHash"] != visible_stats["rightEyeHash"], "Visible-only runtime must still render real stereo images")
        combined = {"passed": True, "kind": "synthetic-openxr-integration", "syntheticRuntime": True,
                    "headsetFramesVerified": False, "humanComfortVerified": False,
                    "browser": app, "runtime": stats,
                    "visibleOnly": {"browser": visible_app, "runtime": visible_stats}}
        (output / "validation.json").write_text(json.dumps(combined, indent=2) + "\n", encoding="utf-8")
        print(f"PASS synthetic OpenXR: {stats['submittedFrames']} stereo frames, 12 unfocused frames, focus/trigger rearm, two raycast trigger edges, tracking-loss rearm, real GL eye textures, visible-only failure guidance, balanced teardown. No headset validation.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.executable, args.runtime, args.output)


if __name__ == "__main__":
    main()
