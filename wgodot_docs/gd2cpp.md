# gd2cpp

What this is not: This is not a simple drop-in replacement for official godot/gdscript.

Once you port your entire project from official godot to wgodot, you will get lots of analyzer errors.

However, solving them is easy, all you have to do is keep your gdscript code strongly typed (see [strict-type-checking](./features.md#strict-type-checking)).


## How it works

Basically this is a transpiler that translates your gdscript code into sane c++, and that c++ code then should be used as a "main game" module and compiled with the engine. Then you can use that as the "custom template" to finalize your game with its resources (such as scenes, textures, etc).

So your entire game project will be split into two parts:
  - Engine AND your translated code -> compiled into machine code
  - resources: as .pck file (which can be embedded into your executable as well. however, there is no guarantee that they won't be decompiled/reverse-engineered)

