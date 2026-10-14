// The 3D view's camera, and camera-relative movement. Plain C on types.h and
// r3d.h (no game state), unit tested in CI.
#ifndef ZELDA3_CAM3D_H_
#define ZELDA3_CAM3D_H_

#include "types.h"
#include "r3d.h"

// One local player's camera (display only: never in the game state, a
// snapshot or the joypad word).
typedef struct Cam3d {
  bool first_person;
  float yaw;                 // third person, degrees: 0 the original view, + turned right
  float pitch;               // third person, degrees below the horizon (45: the original view)
  float zoom;                // third person, 0.75-2
  float fp_yaw;              // first person, degrees: 0 looks north, 90 east
  float fp_pitch;            // first person, degrees below the horizon (-60..45)
  float blend;               // 0 third person .. 1 first person (eased over 0.3 s)
  float idle;                // seconds without camera input
  float rate_yaw, rate_pitch;  // eased stick rates, degrees per second
} Cam3d;

// One local player's camera-relative d-pad.
typedef struct Cam3dRemap {
  float latched_yaw;         // third person: the yaw when a direction was first pressed
  bool latched;
  bool latched_fp;           // the latch is first person's (the live view steers)
  int8 sector;               // the last of the 8 directions picked (-1: none)
  uint16 last_dirs;          // the raw direction bits it was picked for
} Cam3dRemap;

typedef struct Cam3dTuning {
  uint8 perspective;         // 0-100 (50)
  uint8 fov;                 // first person vertical field of view, degrees (50-90)
  uint8 stick_speed;         // percent (25-200)
  uint8 mouse_speed;         // 0-100 (50)
  bool invert_y;
  bool camera_returns;       // third person: back to the default view 2 s after the last input
} Cam3dTuning;

// This update's camera input.
typedef struct Cam3dInput {
  float stick_x, stick_y;    // right stick -1..1, y down (the deadzone is applied here)
  float mouse_dx, mouse_dy;  // pixels moved (mouse look or right-drag)
  float key_turn, key_tilt;  // held camera keys: -1, 0, 1 (turn + right, tilt + looks up)
  int wheel;                 // zoom steps (+ closer)
} Cam3dInput;

// Where the camera is anchored this frame (hd3d.c fills it).
typedef struct Cam3dPose {
  float target[3];           // third person: the world point at the middle of the original screen
  float eye[3];              // first person
  float shake[2];            // the screen shake, world px
  int w, h;                  // the picture
} Cam3dPose;

void Cam3d_Reset(Cam3d *c);                                  // third person, the default view
void Cam3d_SetFirstPerson(Cam3d *c, bool on, int facing);    // facing: RAM 0x2F (0 up, 2 down, 4 left, 6 right)
void Cam3d_Update(Cam3d *c, const Cam3dInput *in, const Cam3dTuning *t, float dt);
bool Cam3d_IsDefaultView(const Cam3d *c);                    // third person within 0.5 degrees of the original
bool Cam3d_IsTurned(const Cam3d *c);                         // third person more than 10 degrees from it
void Cam3d_MakeView(const Cam3d *c, const Cam3dPose *p, const Cam3dTuning *t, R3dCamera *out);
void Cam3d_MakeTestView(const Cam3dPose *p, R3dCamera *out);  // (Tests) parallel oblique: exactly the 2D picture

// Turns the d-pad bits of input (0x10 up, 0x20 down, 0x40 left, 0x80 right)
// so that up walks where camera c looks. (move_x, move_y): the left stick's
// direction on the screen (y down), or 0,0 to use the bits. Only bits 0xF0
// change, never to both of an opposite pair; with the yaw under 0.5 degrees
// it returns input.
uint16 Cam3d_Remap(Cam3dRemap *r, const Cam3d *c, uint16 input, float move_x, float move_y);
void Cam3d_RemapReset(Cam3dRemap *r);
// The rotation itself (yaw in degrees), for the tests.
uint16 Cam3d_Rotate(Cam3dRemap *r, uint16 input, float move_x, float move_y, float yaw);

#endif  // ZELDA3_CAM3D_H_
