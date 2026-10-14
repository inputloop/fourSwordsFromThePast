// Shared by the 3D view's own files (hd3d.c, hd3d_input.c, hd3d_world.c,
// hd3d_sprites.c, hd3d_linkpose.c). Nothing else includes it.
#ifndef ZELDA3_HD3D_INTERNAL_H_
#define ZELDA3_HD3D_INTERNAL_H_

#include "types.h"
#include "r3d.h"
#include "cam3d.h"
#include "hd3d.h"
#include "snes/ppu.h"

enum {
  kHd3dMaxLocal = 4,         // local players (kSessionMaxLocal)
  kHd3dMaxPanes = 4,
  kHd3dMaxQuads = 16384,     // per pane and frame: all scenes and decals
  kHd3dMaxCards = 160,
  kHd3dMaxDecals = 96,
  kHd3dMaxLights = 48,
  kHd3dAtlasW = 512, kHd3dAtlasH = 256,
  kHd3dSlots = 6,            // rooms and areas kept, all worlds together (least recently used goes)
  kHd3dLowerFloor = -16,     // a Normal room's BG1 floor, under BG2
  kHd3dPitDepth = 32,        // the deepest painted pit, below its rim
  kHd3dFarPlane = -96,       // a parallax room's far BG1
  kHd3dFogMargin = 72,       // geometry is kept this far beyond the original screen (fading out)
  kHd3dWideMargin = 200,     // ... and this far once the camera is turned or in first person
  kHd3dMaxRelief = 64,       // the highest surface: picture rows this far above the screen can show
  kHd3dBandMax = 8,          // the widest dungeon wall band, in 8 px cells
  kHd3dMaxLinks = 4,         // players (kMpMaxPlayers)
  kHd3dPoseMax = 16,         // OAM entries of one Link drawn again (body, sword, shield, shadow...)
};

// Why a pane is drawn in 2D.
enum {
  kHd3dWhy_None, kHd3dWhy_Off, kHd3dWhy_Renderer, kHd3dWhy_Scale, kHd3dWhy_Mode, kHd3dWhy_Blank,
  kHd3dWhy_Mosaic, kHd3dWhy_Window, kHd3dWhy_Module, kHd3dWhy_Room, kHd3dWhy_Size, kHd3dWhy_Memory,
  kHd3dWhy_Flat,
  kHd3dWhy_Count,
};
// "none", "off", "renderer", "scale", "mode", "blank", "mosaic", "window", "module", "room", "size", "memory",
// "flat" (a kHd3dPaneFlat pane)
extern const char *const kHd3dWhyNames[kHd3dWhy_Count];

// What a dungeon room's BG1 is (from dung_hdr_bg2_properties, never TM/TS).
enum { kHd3dBg1_None, kHd3dBg1_LowerFloor, kHd3dBg1_Far, kHd3dBg1_OnTop, kHd3dBg1_Water, kHd3dBg1_Light };

// ---- hd3d_world.c: rooms and overworld areas as 3D scenes ----
typedef struct Hd3dScene Hd3dScene;
typedef struct Hd3dSceneInfo {
  bool indoors, ready;       // ready: geometry built (until then drawn flat: exactly the picture)
  uint8 bg1_role;            // kHd3dBg1_*
  int32 origin_x, origin_y, size;  // world px
  int16 wall_height;         // the room's east, west and south walls (px)
  int16 void_index;          // the room's filler texel, the color around it (0: none, the backdrop shows)
} Hd3dSceneInfo;

// Phase A, the world bound (reads RAM and VRAM, never writes them): the
// bound world's current scene, up to date, with its texture cells inside
// decode_rect (world px x0, y0, x1, y1) decoded. NULL: nothing to draw in
// 3D here (*why: kHd3dWhy_Room or kHd3dWhy_Memory).
Hd3dScene *Hd3dScene_Update(const int32 decode_rect[4], int *why);
// The scene the bound world showed before the current one, frozen (drawn
// only during a room-to-room scroll). NULL: none.
Hd3dScene *Hd3dScene_Previous(void);
void Hd3dScene_GetInfo(const Hd3dScene *s, Hd3dSceneInfo *out);
// The height things stand at on a layer (0 BG2, 1 BG1): 0, or kHd3dLowerFloor.
int Hd3dScene_WalkZ(const Hd3dScene *s, int layer);
// The height of the ground under picture point (x, y): outdoors the terrace
// there (on a cliff face below a ledge, partway down it); else as WalkZ.
// Something standing there is drawn at (x, y + z, z) (the relief rule).
int Hd3dScene_GroundZ(const Hd3dScene *s, int layer, int x, int y);
// Where something standing at picture point (x, y) stands: the ground, or a
// higher ground just north that the relief puts on the same spot.
int Hd3dScene_StandZ(const Hd3dScene *s, int layer, int x, int y);
// First person: where the eye goes while the Link at picture point (x, y)
// stands in a doorway (*eye_y: his eye's world y). Outdoors, in a house's
// door cells or inside its rows above them (walking out, hidden behind the
// facade), it waits in front of the door's foot; indoors, in the corridor
// of an exit door, in the room at the corridor's mouth. Else the view would
// be under the lintel or in the alcove, a wall a few px away. False: not in
// a doorway (*eye_y as it was).
bool Hd3dScene_DoorEye(const Hd3dScene *s, int layer, int x, int y, float *eye_y);
// Heights grow in (0 to 1 over 0.3 s) once a scene that was drawn flat
// becomes ready; a scene first drawn when already ready starts at 1.
float Hd3dScene_StepRise(Hd3dScene *s, float dt, bool drawn);

// Phase B (no game memory).
typedef struct Hd3dEmit {
  const R3dCamera *cam;      // culls 64 px chunks
  int w, h;
  float view_dir[2];         // horizontal unit vector from the target toward the eye (third person)
  float fp_blend;            // 0 third person .. 1 first person (every wall stands)
  float cut;                 // 0 at the classic view (walls lie flat as painted) .. 1 with the camera
                             // turned: a wall lying flat lies on a low body (the dollhouse cutaway)
  bool test_camera;          // the oblique test camera: walls stand only where they face it
  float rise;
  int32 rect[4];             // world px whose geometry is emitted
  uint8 tex_bg2, tex_bg1;    // R3dFrame.tex slots of this scene
  bool fade;                 // scenery near the segment fade_a..fade_b gets kR3dQuad_Faded
  float fade_a[3], fade_b[3];
  uint32 exit_rgb;           // indoors in first person: the daylight seen through an exit door
} Hd3dEmit;
void Hd3dScene_Textures(Hd3dScene *s, const uint32 *lut, R3dTexture *bg2, R3dTexture *bg1);
int Hd3dScene_Emit(Hd3dScene *s, const Hd3dEmit *e, R3dQuad *out, int max);
void Hd3dScene_Dump(const Hd3dScene *s, const char *ppm_path);  // (Debug) classes and heights

// ---- hd3d_sprites.c: OAM to cards ----
typedef struct Hd3dObject {  // where an object that draws sprites stands
  uint8 kind, index;         // kHd3dTag_*, its slot (Links: the player)
  uint8 layer;               // 0 BG2, 1 BG1
  int16 x, y;                // world px: its ground point in picture coordinates
  int16 z;                   // height above the ground (jumps, throws)
} Hd3dObject;

// A Link drawn again by the game's own routine, facing the way the pane's
// camera sees him (hd3d_linkpose.c).
typedef struct Hd3dLinkPose {
  uint8 count;               // 0: none (the Link as the game drew him)
  uint8 bank;                // the extra palette bank of palette 7 (0: cgram 0xF0-0xFF, the leader's colors)
  int16 x[kHd3dPoseMax], y[kHd3dPoseMax];  // screen px, against cam_x and cam_y
  uint16 oam1[kHd3dPoseMax]; // char, name select, palette, priority, flips
  uint8 big[kHd3dPoseMax];   // 1: the large size
  uint16 order[kHd3dPoseMax];  // as Hd3dSprites_Build orders the entries the game drew
  uint16 tiles[0x200];       // VRAM 0x4000-0x41FF as the game would upload it for this pose
} Hd3dLinkPose;

typedef struct Hd3dSpriteFrame {
  // Copied by the frame hook inside ZeldaDrawPpuFrame.
  uint16 oam[0x110];
  uint16 cgram_obj[128];     // cgram 0x80-0xFF as drawn (the leader's colors in 0xF0-0xFF)
  uint16 extra_pal[4][16];
  uint16 obj_vram[0x2000];   // VRAM 0x4000-0x5FFF
  uint16 obj_adr1, obj_adr2;
  uint8 obj_size, num_sets;
  PpuExtraSpriteSet sets[kPpuMaxExtraSpriteSets];
  int8 set_player[kPpuMaxExtraSpriteSets];       // g_mp_extra_set_player
  uint8 set_link_count[kPpuMaxExtraSpriteSets];  // its first entries are that Link, the rest its projectiles
  uint8 brightness;
  bool obj_math;             // the game's color math reaches OBJ palettes 4-7 (dark rooms)
  int extra_left_right;
  // Read from the world after the 2D pass.
  uint8 tag_kind[128], tag_index[128];  // kHd3dTag_None unless the entry still holds what that object drew
  Hd3dObject objects[64];
  int num_objects;
  int16 cam_x, cam_y;        // BG2HOFS_copy2, BG2VOFS_copy2 (Link's entries are drawn against these)
  int16 shaken_x, shaken_y;  // BG2HOFS_copy, BG2VOFS_copy (everything else)
  int8 leader;               // the player whose Link is in the regular entries
  int8 hide_player;          // first person: this player's Link and shadow are not drawn (-1)
  int8 xray_player;          // this player's Link gets a silhouette where hidden (-1)
  uint16 weapon_ancillae;    // first person: the hidden player's sword sparkles (a bit per ancilla slot)
  // Phase A, once the pane's camera is known (Hd3dLinkPose_Read).
  Hd3dLinkPose poses[kHd3dMaxLinks];
  uint8 pose_facing[kHd3dMaxLinks];  // (kept between frames) the facing last shown, + 1 (0: none)
} Hd3dSpriteFrame;

void Hd3dSprites_Capture(Ppu *ppu, Hd3dSpriteFrame *out);  // the frame hook
void Hd3dSprites_ReadWorld(Hd3dSpriteFrame *out);          // phase A, after the 2D pass
// Phase B: cards (sprites standing on the scene) and shadow decals (quads on
// the atlas texture). atlas: kHd3dAtlasW x kHd3dAtlasH indices; lut: 0
// see-through, 1-128 cgram 0x80-0xFF, 129-176 the extra palette banks 1-3.
// cam: the picture's camera (a first-person weapon is laid out in front of it).
int Hd3dSprites_Build(const Hd3dSpriteFrame *sf, const Hd3dScene *scene, const R3dCamera *cam, uint8 atlas_slot,
                      uint8 *atlas, uint32 *lut, R3dCard *cards, int max_cards,
                      R3dQuad *decals, int max_decals, int *num_decals);

// ---- hd3d_linkpose.c: Links facing the way the camera sees them ----
// Phase A, after Hd3dSprites_ReadWorld and hide_player: fills sf->poses for
// the Links whose facing seen from cam differs from their facing in the
// world. Runs the game's Link drawing and puts back every byte it touched.
// cam NULL (the oblique test camera): none.
void Hd3dLinkPose_Read(Hd3dSpriteFrame *sf, const Cam3d *cam);

// ---- hd3d_input.c: cameras, input, panes ----
extern Cam3d g_hd3d_cam[kHd3dMaxLocal];
void Hd3dInput_Tuning(Cam3dTuning *t);           // from g_hd3d
int Hd3dInput_PaneLocal(int view);               // whose camera pane view uses (Hd3d_SetPanes; -1: a flat pane)
bool Hd3dInput_PaneFlat(int view);               // pane view is always drawn in 2D (kHd3dPaneFlat)
int Hd3dInput_PaneCount(void);
void Hd3dInput_ResetAll(void);
// First person: local's view turns to where its Link faces (RAM 0x2F) when
// it looks more than 50 degrees away from that.
void Hd3dInput_FaceLink(int local, int facing);

// ---- hd3d.c: drawing ----
bool Hd3d_LocalSees3D(int local);                // local's pane was drawn in 3D last frame (remap gate)
bool Hd3d_TestCameraOn(void);

#endif  // ZELDA3_HD3D_INTERNAL_H_
