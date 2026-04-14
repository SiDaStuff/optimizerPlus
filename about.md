# About OptimizerPlus

OptimizerPlus is a Windows-only Geometry Dash performance mod built to stay out of the way of actual level behavior.

The main feature is GPU and process optimization on Windows:
- exports a high-performance GPU preference for Nvidia Optimus and AMD PowerXpress systems
- keeps Windows from applying power-throttling behavior to the game process
- can raise timer resolution for smoother frame pacing
- can raise the game process priority on startup

The in-game optimizations are kept intentionally safe by default:
- skip redundant visibility, position, rotation, scale, color, and opacity updates only when the incoming value is identical
- apply an optional target animation interval through `CCDirector`
- offer optional visual reductions like particle scaling or glow throttling, but these are off by default so they do not interfere with level presentation

This project targets Geode `5.0.1`, Geometry Dash `2.2081`, and Windows only.
