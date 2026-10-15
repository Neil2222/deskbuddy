// Buddy's way into Office Mode and back out.
//
//   entering  he notices (wide eyes, a little start, a blink), then his head comes toward the viewer, growing from
//             where it is on the stage, until his face screen fills the display; he closes his eyes, the screen goes
//             dark in exactly the clock's background colour, and the clock takes its place (only the digits appear)
//   leaving   the digits sink into his dark face; the screen lights up, his eyes open and blink, he steps back to his
//             place, stretches, blinks, and carries on with his mood; the menu flips back in on the page it was on
//
// Presentation only: office_switch still swaps the scenes, and nothing else turns Office Mode on or off. All requests
// (the menu, a tap on the clock, the companion) come here; one made during a transition is carried out once that
// transition has finished. The zoom is a camera: the view draws its own sprites at a larger integer scale (crisp
// nearest-neighbour pixels, no extra images), centred on his face screen.
//
// Pure logic like office_switch: the caller passes the time and Buddy's home placement, and gets a camera and a pose.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_pose.h"
#include "gfx_types.h"
#include "office_switch.h"

#ifdef __cplusplus
extern "C" {
#endif

// Timeline in ms. Entering counts from the request, leaving from the moment his dark face is on screen.
#define OFFICE_ZOOM_IN_AT 280    // after the notice and a blink
#define OFFICE_ZOOM_IN_MS 660
#define OFFICE_ENTER_MS 1260     // eyes closed, screen dark: the clock is asked for
#define OFFICE_ZOOM_OUT_AT 420   // after his eyes have opened and blinked
#define OFFICE_ZOOM_OUT_MS 660
#define OFFICE_MENU_BACK_AT 980  // the menu flips back in as he arrives
#define OFFICE_LEAVE_MS 1500     // after the stretch and a blink: his brain takes over again
#define OFFICE_ZOOM_FRAME_MS 15  // while zooming, draw as often as the display allows

typedef enum {
    OFFICE_ZOOM_NORMAL,
    OFFICE_ZOOM_ENTERING,
    OFFICE_ZOOM_CLOCK,
    OFFICE_ZOOM_LEAVING,
} office_zoom_state_t;

typedef struct {
    office_zoom_state_t state;
    bool want_office; // the last request
    bool asked;       // entering: the scene switch has been asked for the clock
    bool running;     // leaving: his face is drawn and the animation runs
    bool menu_back;   // leaving: the menu has been brought back
    uint32_t start;   // when the animation started
} office_zoom_t;

typedef struct {
    bool animate;   // draw Buddy with office_zoom_frame() instead of his brain's pose
    bool menu_hide; // entering just started: fold the menu away
    bool menu_show; // leaving: bring the menu back (once)
    bool back;      // he is back on his stage: hand him to his brain again
} office_zoom_step_t;

// The camera: scale and screen position of the art origin (as buddy_view_camera() takes them).
typedef struct {
    int scale, x0, y0;
} office_camera_t;

void office_zoom_init(office_zoom_t *z);

// Asks for the clock (true) or for Buddy (false).
void office_zoom_request(office_zoom_t *z, bool office);

// Entering or leaving: touches, reactions and new messages wait.
bool office_zoom_busy(const office_zoom_t *z);

// Advances the state and asks the scene switch for the clock or for Buddy when it is time. Call once per frame,
// before office_switch_step().
office_zoom_step_t office_zoom_step(office_zoom_t *z, office_switch_t *sw, uint32_t now);

// Buddy at now, while office_zoom_step() says animate: the camera (between home, his place on the stage, and his
// face screen filling `screen`) and his pose. Returns ms until it changes next.
uint32_t office_zoom_frame(const office_zoom_t *z, uint32_t now, office_camera_t home, gfx_rect_t screen,
                           office_camera_t *cam, buddy_pose_t *pose);

// The camera at the end of the zoom: the smallest integer scale at which his face screen covers `screen`, centred.
office_camera_t office_zoom_face_camera(gfx_rect_t screen);

#ifdef __cplusplus
}
#endif
