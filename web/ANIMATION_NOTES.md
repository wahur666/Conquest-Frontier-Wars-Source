# Compound Animation Notes

These notes describe the `.cmp.xml` animation formats currently supported by the web model viewer.

## Location

Compound model animations live under:

```text
Animation
  Chnl
  Script
```

`Animation/Chnl` is optional. If present, it stores named reusable channels. `Animation/Script` contains clips, usually named `Sc_*`.

## Channel Header

Every channel has:

```text
Header
Frames
```

`Header` is 12 bytes:

```cpp
uint32 frames;
float capture_rate;
uint32 type;
```

If `capture_rate < 0`, every frame starts with an explicit `float time`. If `capture_rate >= 0`, frame time is `frame_index * capture_rate`.

Channel type flags:

```text
1 = float
2 = vector
4 = quaternion
8 = event
```

Combined types are bitwise OR values. For example, `6` is `vector + quaternion`.

Quaternions are stored in engine order:

```text
w, x, y, z
```

Three.js uses:

```text
x, y, z, w
```

## Script Maps

Scripts use mapping directories:

```text
Joint map N
Object map N
Event map N
```

Each map may either embed a channel:

```text
Channel
  Header
  Frames
```

or reference a named channel:

```text
Channel name = Ch_...
```

Joint maps also contain:

```text
Parent name
Child name
```

Object maps use `Parent name` as the target object name.

## Supported Viewer Cases

The scan of `xml_dump/*.cmp.xml` found 105 compound XML files:

```text
68 files with Animation
37 files without Animation
0 unsupported non-event animation cases after implementation
```

Supported cases:

```text
Joint map + float channel + revolute joint
Joint map + float channel + prismatic joint
Joint map + vector channel + translational joint
Joint map + quaternion channel + spherical joint
Joint map + vector+quaternion channel + loose/full joint state
Object map + vector+quaternion channel
```

Event channels are parsed only enough to ignore them. They do not directly move geometry in the viewer.

## Runtime Rules

Float channels:

```text
frame = time + float value
```

For revolute joints, the C++ uses `InterpolateArc()` to interpolate across the shorter angular path. The viewer mirrors that behavior.

Vector channels:

```text
frame = time + x, y, z
```

Used by translational joints.

Quaternion channels:

```text
frame = time + qw, qx, qy, qz
```

Used by spherical joints and interpolated with slerp.

Full transform channels:

```text
frame = time + x, y, z + qw, qx, qy, qz
```

Used by loose joint tracks and object maps.

## Viewer Controls

The model viewer supports:

```text
Start
Stop
Restart
Reset
Loop
Ping-pong
```

When a model loads, the selected clip is applied at frame zero immediately. This keeps animated parts in the correct start pose before playback begins.

The default clip is the longest visual clip, which avoids selecting one-frame rest clips such as `Sc_end anim` when a longer animation exists.

