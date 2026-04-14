#pragma once

#include <Geode/Geode.hpp>

#include <chrono>
#include <cstdint>

using namespace geode::prelude;

struct OptimizerConfig {
    bool reduceParticles = false;
    int particleDensity = 50;
    int minimumParticleCount = 8;
    int particleBucketSize = 16;

    bool reduceGlow = false;
    bool reduceBloom = false;
    bool optimizeSpriteBatching = true;
    bool lowerTextureQuality = false;
    int opacityQuantizationStep = 8;
    bool cullTransparentSprites = false;
    int transparentOpacityThreshold = 6;
    bool cullTinySprites = false;
    int tinySpritePixelThreshold = 2;
    bool enableObjectCulling = false;
    int offscreenPadding = 48;

    bool reducePhysicsFreq = false;
    int physicsFrameSkip = 1;
    bool reducePositionUpdates = true;
    bool optimizeRotationCalcs = true;
    bool skipScaleUpdates = true;
    bool skipColorUpdates = true;
    bool reduceMemoryAllocations = true;
    bool optimizeObjectPooling = true;
    bool cacheValues = true;
    bool reduceEffectSpawning = false;
    bool lowerAnimationFrameRate = false;

    int processPriorityLevel = 1;
    bool disablePriorityBoost = true;
    bool disablePowerThrottling = true;
    bool raiseTimerResolution = true;

    bool enableAggressiveOptimizations = false;
    bool logPerformanceMetrics = false;
    int targetFPS = 240;
};

struct PerfMetrics {
    uint64_t frameCount = 0;
    uint64_t skippedVisibilityUpdates = 0;
    uint64_t skippedPositionUpdates = 0;
    uint64_t skippedRotationUpdates = 0;
    uint64_t skippedScaleUpdates = 0;
    uint64_t skippedColorUpdates = 0;
    uint64_t culledDraws = 0;
    uint64_t culledTransparentSprites = 0;
    uint64_t culledTinySprites = 0;
    uint64_t reducedParticles = 0;
    uint64_t throttledGlowDraws = 0;
    double avgFrameMs = 0.0;
};

struct RuntimeState {
    std::chrono::steady_clock::time_point lastFrameTime {};
    bool initialized = false;
    bool windowsTweaksApplied = false;
    bool timerResolutionRaised = false;
    int glowDrawCadence = 1;
    int frameCadence = 1;
};

extern OptimizerConfig g_config;
extern PerfMetrics g_metrics;
extern RuntimeState g_runtime;

void loadConfiguration();
void initializeRuntime();
void applyStartupOptimizations();
void onFrameStart();
void applyTargetFrameRate();
void logMetricsIfNeeded();

bool shouldSkipVisibilityUpdate(CCSprite* sprite, bool visible);
GLubyte optimizeSpriteOpacity(CCSprite* sprite, GLubyte opacity);
bool shouldSkipColorUpdate(CCSprite* sprite, ccColor3B const& color);
bool shouldSkipPositionUpdate(CCNode* node, CCPoint const& pos);
bool shouldSkipRotationUpdate(CCNode* node, float rot);
bool shouldSkipUniformScaleUpdate(CCNode* node, float scale);
bool shouldSkipScaleXUpdate(CCNode* node, float scale);
bool shouldSkipScaleYUpdate(CCNode* node, float scale);
bool shouldSkipSpriteDraw(CCSprite* sprite);
bool shouldCullNodeDraw(CCNode* node);
bool shouldThrottleGlowDraw(CCSprite* sprite);
bool shouldSkipParticleUpdate(CCParticleSystemQuad* system);
unsigned int optimizeParticleCount(CCParticleSystemQuad* system, unsigned int original);
