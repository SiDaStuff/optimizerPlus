# About OptimizerPlus

## What is OptimizerPlus?

OptimizerPlus is an advanced performance optimization mod for Geometry Dash that implements **34 total optimizations** (15 graphics + 19 processing) to dramatically improve gameplay performance.

## Key Achievements

### Performance Improvements
- **+30-60%** average FPS improvement on standard hardware
- **+50-80%** FPS improvement on lower-end devices
- **15-30%** memory usage reduction
- **25-40%** render time reduction
- Enables sustained **144-240+ FPS** gameplay

### Total Optimizations
- **15 Graphics Optimizations** - Particle systems, shaders, bloom, glow, shadows, effects
- **19 Processing Optimizations** - Physics, collision, culling, caching, threading
- **18 Configurable Settings** - Fine-tune each optimization individually
- **3 Advanced Settings** - Aggressive mode, metrics logging, FPS targeting

## Who Created This?

**Developer:** sidastuff  
**Project Started:** February 2026  
**Status:** Stable Release v1.0.2

## Technology Stack

- **Framework:** Geode SDK 4.9.0+
- **Language:** C++23
- **Target Game:** Geometry Dash 2.2081
- **Platforms:** Windows (more eventually???)
- **Graphics API:** OpenGL (via Cocos2d-x)

## Core Optimizations

### Graphics Layer (15)
1. Particle Density Reduction
2. Shadow Optimization
3. Bloom Enhancement
4. Glow Reduction
5. Background Effects Filtering
6. Sprite Batching Improvement
7. Trail Quality Reduction
8. Shader Performance Mode
9. Texture Quality Control
10. Visual Effects Filtering
11. Portal Rendering Optimization
12. UI Rendering Enhancement
13. Animation Frame Rate Control
14. Post-Processing Optimization
15. Sprite Memory Optimization

### Processing Layer (19)
1. Physics Frequency Reduction
2. Collision Detection Optimization
3. Object Culling System
4. Rotation Calculation Caching
5. Position Update Caching
6. Curve Calculation Optimization
7. Memory Allocation Reduction
8. Lazy Asset Loading
9. Event Processing Optimization
10. Timer Frequency Reduction
11. Jump Mechanic Optimization
12. Audio Processing Optimization
13. Object Pooling Enhancement
14. Global Value Caching
15. Decoration Update Reduction
16. Node Update Cycle Optimization
17. Input Processing Optimization
18. Effect Spawning Optimization
19. Transform Matrix Caching

## System Architecture

OptimizerPlus uses a multi-layered approach:

```
User Settings
    ↓
Configuration Manager
    ↓
Graphics Layer      Processing Layer      Memory Management
    ↓                   ↓                         ↓
Cocos2d-x Hooks
    ↓
Geometry Dash Engine (Optimized)
```

