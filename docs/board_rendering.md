# The rendered board

The board view shows a ray-traced image of a 3D model of the Basys 3. The live
parts are sprites rendered from the same model in their other state, placed over
that image. QML only shows or hides them, so the board looks like a photograph
and still follows the model's state.

## Where the model comes from

`tools/board/scene.py` builds the board in Blender from the dimensions in
`tools/board/measured.py`, in millimetres:
- the board outline, its holes and pads;
- every switch, LED, button, connector, header and chip;
- the seven-segment outlines.

Those dimensions were measured from Digilent's Basys 3 STEP model and the board
photo in the reference manual. Neither file is in this repository. The model is
original geometry built from the measurements. Silkscreen labels follow the
board, with package pins from `Basys3_Master.xdc`. Vendor logos are left out on
purpose.

The renders use two assets bundled with Blender: its studio HDRI (CC0, Poly Haven)
and the Inter font (SIL OFL).

## What gets rendered

`tools/board/render.py` renders the model seven times. Camera, light, sampling
seed and denoiser are the same each time; only these parts change:

| Render | Changed parts |
| --- | --- |
| base | none: every part off, power LED lit |
| two switch renders | even switches on; odd switches on |
| two LED renders | even LEDs lit, plus DONE; odd LEDs lit |
| buttons | every button pressed |
| segments | every segment and decimal point lit |

`board.png` is the base render. Each sprite is cut from the render where its part
is on. Within any one render, a part's neighbours stay off. Cuts have feathered
edges, so sprites composite cleanly over the base in any combination. LEDs and
segments also get a soft glow, which a ray tracer does not draw.

Segment sprites use the segment's own measured outline as their mask.

Outputs, all committed so that ordinary builds never need Blender:
- `src/qt/board/board.png`: the board with every part off, 24 px per mm, with
  a translucent shadow;
- `src/qt/board/sprites/*.png`: one sprite per part state, 70 in all;
- `src/qt/BoardLayout.js`: each part's rectangle and its sprite's placement, in
  board units of 0.1 mm.

## Regenerating

You need Blender 4.2 or later; 5.2 is tested. Run:

```sh
tools/board_render.py               # 256 samples; about 30 minutes on an M1 Pro
tools/board_render.py --verify      # also checks the sprites against a real render
tools/board_render.py --check       # no Blender: the committed files agree
```

`--verify` renders one random state of switches, LEDs, buttons and segments
outright. It then composites the same state from the base image and glow-free
sprites, and prints the difference between the two as PSNR. Treat a low PSNR as
a compositing fault, never as noise to ignore.

These tests guard the committed files on every test run:
- `qt_board_widgets` checks that every part has its sprite, at the pixel size its
  rectangle gives, within the board image, and that each switch, LED, button and
  DONE sprite's feathered edge matches `board.png` where the layout places it.
  This catches a moved rectangle, a flipped offset or a swapped sprite. It also
  checks that each part's QML item shows its own sprite, exactly where the
  layout puts it and exactly while the model has the part in that state.
- `qt_board_counter_ui` checks that an unbound display is veiled, and `qt_shell`
  that without a design the DONE LED is dark and nothing is veiled.
- The launcher smoke tests fail if any board image is missing from a launcher's
  resources, since the missing image is reported as a QML warning.

`--check` makes the file checks without Qt, and also rejects stray sprite files.

To change the board's appearance, edit `scene.py` or `measured.py`. To change
where sprites are cut, edit `render.py`. Then regenerate and commit the
images together with `BoardLayout.js`.
