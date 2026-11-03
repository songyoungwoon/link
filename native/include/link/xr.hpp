#pragma once
// Copyright 2026 Song Youngwoon
// SPDX-License-Identifier: Apache-2.0
#include "vr.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace linkworld {
// Win32 OpenXR/OpenGL backend. No OpenXR or GL types escape this interface.
// All methods, including destruction, belong on the owning WGL/render thread.
// The HDC/HGLRC must outlive this object; their current context is restored for
// resource teardown. Runtime loss is reported by exitRequested() or an exception.
class OpenXrSession {
public:
  struct Pose : VrPose {
    bool positionValid=false,orientationValid=false;
    bool positionTracked=false,orientationTracked=false;
    bool valid() const noexcept { return positionValid&&orientationValid; }
  };
  struct Eye {
    Pose pose;
    VrFov fov;
    int width=0,height=0;
    unsigned framebuffer=0,colorTexture=0;
  };
  struct Hand {
    // active is the action's isActive flag, not an inference from a tracked pose.
    bool active=false;
    Pose aim;
    float selectValue=0;
    bool selectDown=false,selectPressed=false,selectReleased=false;
    std::array<float,2> thumbstick{0,0};
    bool stickActive=false;
  };
  struct Frame {
    // A begun frame MUST end with endFrame() or abortFrame(), even if not rendered.
    bool begun=false,shouldRender=false;
    bool inputFocused=false; // A focused session AND successful xrSyncActions for this frame.
    std::int64_t predictedDisplayTime=0;
    double dt=0;
    bool floorLevel=false; // true: STAGE floor; false: LOCAL head-relative origin.
    bool referenceSpaceChanged=false,swapchainWaitTimedOut=false;
    Pose head;
    std::array<Eye,2> eyes;
    std::array<Hand,2> hands; // 0: left, 1: right.
    bool exitPressed=false,recenterPressed=false;
  };

  OpenXrSession();
  ~OpenXrSession();
  OpenXrSession(const OpenXrSession&)=delete;
  OpenXrSession& operator=(const OpenXrSession&)=delete;
  OpenXrSession(OpenXrSession&&)=delete;
  OpenXrSession& operator=(OpenXrSession&&)=delete;

  // Requires an already-current compatibility OpenGL context. Throws an actionable
  // std::runtime_error when the runtime, headset, GL version or device is unavailable.
  void initialize(HDC,HGLRC);
  void shutdown() noexcept;
  void pollEvents();
  bool running() const noexcept;
  bool focused() const noexcept;
  const char* sessionStateName() const noexcept;
  bool exitRequested() const noexcept;
  const std::string& runtimeName() const noexcept;
  Frame beginFrame();
  void bindEye(std::size_t eyeIndex);
  // Optional desktop preview; call after both eye renders and before endFrame().
  void mirror(int desktopWidth,int desktopHeight);
  void endFrame(bool submit=true);
  void abortFrame() noexcept;
  void requestExit();
  void haptic(std::size_t handIndex,float amplitude=.35f,double seconds=.025) noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace linkworld