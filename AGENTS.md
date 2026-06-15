# wgodot AGENTS guide

Rules:

1. Do changes in a way that they create the least possible conflicts with official Godot, we usually do this by creating entirely new file for our changes, and only do the hook parts from the official godot files.

2. To make sure our changes survive during upstream pull, add this comment to our changes:

```cpp
// wgodot-changes::begin

...our changes here...

// wgodot-changes::end
```

in case the entire file is for us, add this at the beginning of the file:

```cpp
// wgodot-changes::file
```

3. no need to run compile operation yourself, because that's way too heavy. I will do it myself.

4. For project settings registered with `GLOBAL_DEF`, use `GLOBAL_GET` / `GLOBAL_GET_CACHED` directly at the call site unless there is a clear reuse or compatibility reason. Do not add one-off helper functions or `has_setting` fallbacks for a single setting.


5. when we add an entirely new feature, preferably add it to `wgodot_docs/features.md` to file, so we can keep tracking our custom features.

6. DO NOT TOUCH GIT. Git is for humans. You are allowed to do read-only git operations, but DO NOT do things that will modify the repo. e.g. do NOT unstage changes. Changes are staged when they are reviewed, etc.

7. DO NOT CHANGE THE PLAN MID-WAY. When we've talked about a specific plan beforehand and you are editing files, and then you research about something and find out that that thing is not possible, DO NOT CHANGE THE PLANS and make entirely different edits. First STOP and ask the user about whether this plan is ok or not, and explain why the previous agreed-upon plan cannot be done and discuss it properly with the user BEFORE doing any new plans.


8. The main goals for GitHub workflows:
  - make sure the build time is reduced because of caches
  - don't bomb github's infrastructure with thousands of repeated caches
