# reSF2 — Detailed Reverse Engineering Plan

## Original Engine Stack
- **cocos2d-x 2.2.6** (MIT-licensed, confirmed by `cocos2d-x 2.2.6` string in .s3e)
- **Marmalade SDK 8.2.1** (provides IwGx OpenGL ES 2.0 backend, s3e runtime)
- **pugixml** (XML parsing), **tinyxml** (secondary)
- **Box2D** — NOT used (physics is custom Verlet-based)
- Custom Verlet physics engine (skeleton nodes + edges + capsules + triangles)
- Resolution: 1536 folder = 1.5x scale, base resolution 1280x720 (WinWidth=1280 WinHeight=720)

## Coordinate Systems (CRITICAL)

### cocos2d-x 2.2.6 Default
- **Screen/window coords**: Y-DOWN, origin at top-left (0,0 = top-left)
- **World/scene coords**: Y-UP, origin at bottom-left (0,0 = bottom-left)
- **OpenGL projection**: `glOrthof(0, width, 0, height, -1, 1)` — Y-UP
- **Texture UV**: (0,0) = bottom-left of texture (OpenGL convention)

### Location params.xml Coordinates
- Origin: CENTER of the visible area (X can be negative)
- **Y-UP** (positive Y = up toward ceiling, negative Y = down toward floor)
- Evidence:
  - `Floor="80"`: floor is at Y=+80 (above center in Y-UP)
  - `Wall="305"`: wall height is 305
  - `Image Y="470"` pixel_1 rect (Height=500): this is the CEILING (positive Y = up)
  - `Image Y="-426"` pixel_1 rect (Height=400): this is the FLOOR (negative Y = down)
  - `PlayerPositionY="-93"`: player is 93 units BELOW center (on the floor area)
  - `layer_3_1 Y="225"`: floor tiles are at Y=225 (upper area = floor level in Y-UP)
  
  Wait — this means the floor is at POSITIVE Y (upper part of screen) and ceiling at NEGATIVE Y? That's inverted from what we'd expect. Let me reconsider.

  Actually in the original game, the camera looks at the fight from a SIDE VIEW. The "floor" where fighters stand is in the MIDDLE of the screen, and the "ceiling" is also in the middle/upper. The pixel_1 rects at Y=470 and Y=-426 are MASK rectangles that cover areas OUTSIDE the visible dojo (to hide the edges of the background tiles).

  The actual floor where fighters stand is at Y = PlayerPositionY = -93 (slightly below center). The fighters' feet rest at this Y level. The "Floor=80" attribute likely means the floor LINE is 80 units thick or at offset 80 from something.

### Skeleton/Model Local Coordinates
- **Y-UP** (0 = feet/bottom, positive Y = up toward head)
- Origin: model's local origin (NPivot is at Y≈170, head at Y≈261, feet at Y≈72)
- When placed in world: NPivot goes to PlayerPositionY, everything else scales from there

### Texture Atlas Coordinates
- **Y-DOWN** (atlas_y = top of frame in the PNG, which has top-left origin)
- cocos2d internally flips V: `v_cocos2d = 1.0 - v_png`

## Rendering Pipeline (original)

1. **CCDirector** sets up orthographic projection (Y-UP, bottom-left origin)
2. **CCScene** contains CCLayers
3. **Location background layers** (Type=1):
   - Each `<Layer>` is a CCNode with a parallax factor
   - Camera position is multiplied by Factor to create parallax scroll
   - `<Image>` elements are CCSprites positioned at (X, Y) in world coords
   - Sprite anchorPoint = (0.5, 0.5) — position is the CENTER of the sprite
4. **ModelsViewer** (Type=2):
   - Contains PlayerPositionX/Y and EnemyPositionX/Y
   - Models (fighters) are rendered here using skeletal animation
5. **SimpleEffect** (Type=1 with transparency animation):
   - Picture type = a CCSprite with alpha animation (flickering/transparency)

## Layer Rendering (parallax)

```
for each Layer in location.layers:
    if Layer.Type == 1:
        camera_offset = base_camera_pos * Layer.Factor
        for each Image in Layer.Images:
            sprite = CCSprite(atlas[Layer.Atlas], frame=Image.ClassName)
            sprite.position = (Image.X, Image.Y)  // world coords, Y-UP, center origin
            sprite.anchorPoint = (0.5, 0.5)  // center
            sprite.contentSize = (Image.Width, Image.Height)
            if Image is SimpleEffect:
                sprite.runAction(alpha_animation from <Transparency><Point> entries)
            render(sprite, camera_offset)
```

## Character/Model Rendering

### Skeleton (skeleton.xml)
- `<Nodes>`: physics simulation points (Verlet integration)
  - NTop, NNeck, NShoulder_1/2, NElbow_1/2, NWrist_1/2, NHip_1/2, NKnee_1/2, NAnkle_1/2, NToe_1/2, NPivot, NHead, etc.
  - X, Y, Z: local position (Y-UP, 0 = feet area)
  - Mass, Fixed, Visible, Collisible, Passive, Cloth, Rank: physics properties
- `<Edges>`: bone segments connecting two nodes
  - Named: EArm_1 (shoulder→elbow), EFoot_1 (toe→heel), Muscle115 (chest muscle), etc.
  - End1, End2: node names
  - Radius: collision capsule radius (for Collisible=1 edges)
  - Type="Edge" (bones) or Type="Muscle" (tension springs)
- `<GroupsOfSelection>`: hitbox groups (SG-1 through SG-9) for body part targeting

### Body Model (body.xml) — the visual mesh
- `<Nodes>`: BODY-NodeN (cloth/skin simulation points, separate from skeleton)
- `<Nodes>` MacroNode: composite nodes (BODY-MacroNodeN) = weighted blend of skeleton joints
  - ChildNode1..4 + LCC1..4 (linear combination coefficients)
- `<Edges>`: BODY-EdgeN (cloth constraints between BODY-Node points)
- `<Figures>`:
  - Capsule: (Edge name, Radius1, Radius2) — collision volume for a named skeleton edge
  - Triangle: (Node1, Node2, Node3) — visual mesh face, can reference BODY-Node, skeleton nodes, or MacroNodes

### Rendering order
1. Render skeleton bones (from skeleton.xml edges) — NOT rendered in original, physics only
2. Render body capsules (from body.xml) — collision volumes, NOT directly rendered
3. Render body triangles (from body.xml) — the VISIBLE mesh, filled with skin texture/color
4. Render equipment (weapon, armor, helm) on top

## Punching Bag Model

### skeleton_punching_bag.xml
- 16 nodes: NNeck (top, Y=199), NBottom (bottom, Y=19), NPivot (Y=109), Node4-7 (chain, Y=223-298), Node12 (Y=335, Fixed=1 = anchor point at ceiling), Node13-18 (Y=199-225, small mass 0.01 = chain links)
- 20 edges: Body (NBottom→NNeck, main bag body), Edge8-11 (chain links), Edge15 (Node12→Node7, chain to ceiling), Edge16-17 (NPivot→NNeck/NBottom, collision capsules with Radius=15), Edge18-32 (chain link constraints)
- The bag hangs from Node12 (Fixed=1, Y=335 = ceiling anchor) down through the chain (Edge15, Edge11, Edge10, Edge9, Edge8) to the bag body (NNeck→NBottom via Edge "Body")

### punching_bag.xml (Figures)
- 11 capsules referencing the edges (Edge10, Edge11, Edge15, Edge16, Edge17, Edge27-32)
- Edge16 and Edge17 have Radius=25 (the main bag body, thick)
- Other edges have Radius=2 (thin chain links)

## HUD Rendering

### Top Panel (batchPanelsTop atlas)
- Top_Panel.png: the bar background, stretched across the top
- gold.png: coin icon (left side)
- energy.png: lightning icon
- Level_bar.png: progress bar
- These are rendered in SCREEN SPACE (not world space), Y-DOWN, top-left origin

### Menu Buttons (batchButtonsMenuScreens atlas)
- Dojo_normal/active/pushed, Map_normal/active/pushed, Shop_normal/active/pushed, etc.
- Located at top-LEFT, expand into horizontal list when clicked
- Each button is a separate CCSprite

## Key Fixes Needed

### 1. Coordinate System (CRITICAL)
The original uses **Y-UP world coords** (cocos2d convention), not Y-DOWN.
- Location params.xml: Y-UP, center origin
  - Y=470 → UP (ceiling mask)
  - Y=-426 → DOWN (floor mask)
  - PlayerPositionY=-93 → slightly below center
- My current code uses Y-DOWN which INVERTS the scene

### 2. Texture V-flip
- PNG has top-left origin (Y-DOWN)
- OpenGL texture has bottom-left origin (Y-UP)
- Must flip V when loading or when setting UVs
- Software renderer: no flip needed (both Y-DOWN)
- GL renderer: flip V in SpriteBatch::draw_quad (already done: `v.v = 1.0 - v0`)
- Location code must NOT double-flip (remove the `fv0 = 1.0 - v1` in main.cpp)

### 3. Floor Gaps
The floor tiles (layer_3_1 at X=-640, -384, -128, 128, 384, 640, each 256 wide) should tile seamlessly. Gaps occur if:
- Tile width doesn't match spacing (256 vs 256 — OK)
- Center positioning has rounding errors
- Fix: use integer positions, ensure tiles touch exactly

### 4. Character Body Mesh
The body.xml triangles define the visible mesh. Current issues:
- Triangles render as 1x1 pixel rects (slow + may not fill correctly)
- Need proper triangle rasterization with barycentric interpolation
- MacroNodes must be resolved correctly (LCC weights)

### 5. Punching Bag 3D Model
Now that we have skeleton_punching_bag.xml:
- Load its nodes (NNeck, NBottom, NPivot, Node4-7, Node12, Node13-18)
- Load its edges (Body, Edge8-11, Edge15-32)
- Load punching_bag.xml capsules (Edge16/17 = main bag, Radius=25)
- Render as thick capsules: main bag body (Edge16+17, r=25*2=50px) + chain (Edge10/11/15, r=2*2=4px)
- Place at EnemyPositionX/Y with Node12 (Fixed anchor) at ceiling
