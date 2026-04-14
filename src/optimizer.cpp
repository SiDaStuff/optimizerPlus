#include "optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <windows.h>
#include <mmsystem.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

OptimizerConfig g_config;
PerfMetrics g_metrics;
RuntimeState g_runtime;

namespace {
double getTargetFrameMs() {
    return 1000.0 / static_cast<double>(g_config.targetFPS);
}

void refreshLiveConfiguration() {
    if (g_metrics.frameCount > 0 && g_metrics.frameCount % 120 == 0) {
        loadConfiguration();
        applyTargetFrameRate();
    }
}

bool isGlowLikeBlend(cocos2d::ccBlendFunc blend) {
    return (blend.src == GL_SRC_ALPHA && blend.dst == GL_ONE) ||
           (blend.src == GL_ONE && blend.dst == GL_ONE);
}

bool exactlyEqual(float lhs, float rhs) {
    return lhs == rhs;
}

float graphicsParticleMultiplier() {
    float density = 1.0f;

    if (g_config.reduceParticles) density *= static_cast<float>(g_config.particleDensity) / 100.0f;
    if (g_config.reduceBloom) density *= 0.92f;
    if (g_config.reduceGlow) density *= 0.90f;
    if (g_config.lowerTextureQuality) density *= 0.96f;
    if (g_config.reduceEffectSpawning) density *= 0.80f;
    if (g_config.lowerAnimationFrameRate) density *= 0.92f;
    if (g_config.enableAggressiveOptimizations) density *= 0.72f;

    return density;
}

int getFrameCadence() {
    int cadence = 1;

    if (g_config.reducePhysicsFreq) cadence = std::max(cadence, g_config.physicsFrameSkip + 1);
    if (g_config.reducePositionUpdates) cadence = std::max(cadence, 2);
    if (g_config.optimizeRotationCalcs) cadence = std::max(cadence, 2);
    if (g_config.skipScaleUpdates) cadence = std::max(cadence, 2);
    if (g_config.reduceMemoryAllocations) cadence = std::max(cadence, 2);
    if (g_config.optimizeObjectPooling) cadence = std::max(cadence, 2);
    if (g_config.cacheValues) cadence = std::max(cadence, 2);
    if (g_config.reduceEffectSpawning) cadence = std::max(cadence, 2);
    if (g_config.lowerAnimationFrameRate) cadence = std::max(cadence, 2);
    if (g_config.enableAggressiveOptimizations) cadence = std::max(cadence, 3);

    return std::clamp(cadence, 1, 6);
}

int getAdaptiveGlowCadence() {
    if (!g_config.reduceGlow) {
        return 1;
    }

    auto targetMs = getTargetFrameMs();
    auto frameMs = g_metrics.avgFrameMs <= 0.01 ? targetMs : g_metrics.avgFrameMs;

    int cadence = 1;
    if (frameMs > targetMs * 1.05) cadence = 2;
    if (frameMs > targetMs * 1.20) cadence = 3;
    if (frameMs > targetMs * 1.40) cadence = 4;
    if (g_config.enableAggressiveOptimizations && frameMs > targetMs * 1.10) cadence = std::min(cadence + 1, 5);

    return cadence;
}

bool shouldSkipByCadence(uintptr_t identity, int cadence) {
    if (cadence <= 1) {
        return false;
    }

    auto step = static_cast<uint64_t>(cadence);
    auto phase = static_cast<uint64_t>(identity) % step;
    return (g_metrics.frameCount + phase) % step != 0;
}

void applyProcessPriority(HANDLE process) {
    DWORD priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
    if (g_config.processPriorityLevel >= 2) {
        priorityClass = HIGH_PRIORITY_CLASS;
    } else if (g_config.processPriorityLevel <= 0) {
        priorityClass = NORMAL_PRIORITY_CLASS;
    }

    if (!SetPriorityClass(process, priorityClass)) {
        log::warn("[OptimizerPlus] Failed to set process priority class: {}", GetLastError());
    }
}

void applyPriorityBoostSetting(HANDLE process) {
    if (!SetProcessPriorityBoost(process, g_config.disablePriorityBoost ? TRUE : FALSE)) {
        log::warn("[OptimizerPlus] Failed to update process priority boost: {}", GetLastError());
    }
}

void applyPowerThrottlingSetting(HANDLE process) {
    PROCESS_POWER_THROTTLING_STATE throttlingState {};
    throttlingState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttlingState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttlingState.StateMask = g_config.disablePowerThrottling ? 0 : PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    if (!SetProcessInformation(process, ProcessPowerThrottling, &throttlingState, sizeof(throttlingState))) {
        log::warn("[OptimizerPlus] Failed to update process power throttling: {}", GetLastError());
    }
}

void applyTimerResolutionSetting() {
    if (!g_config.raiseTimerResolution || g_runtime.timerResolutionRaised) {
        return;
    }

    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_runtime.timerResolutionRaised = true;
    } else {
        log::warn("[OptimizerPlus] Failed to raise timer resolution.");
    }
}

void logGpuPreferenceState() {
    log::info("[OptimizerPlus] Exported high-performance GPU preference for Nvidia Optimus and AMD PowerXpress.");
    log::info("[OptimizerPlus] On Intel or other systems, Windows process tuning is still applied to reduce throttling.");
}
}  // namespace

void loadConfiguration() {
    auto mod = Mod::get();

    g_config.reduceParticles = mod->getSettingValue<bool>("reduce-particles");
    g_config.particleDensity = std::clamp(mod->getSettingValue<int>("particle-density"), 0, 100);
    g_config.minimumParticleCount = std::clamp(mod->getSettingValue<int>("minimum-particle-count"), 0, 64);
    g_config.particleBucketSize = std::clamp(mod->getSettingValue<int>("particle-bucket-size"), 4, 64);

    g_config.reduceGlow = mod->getSettingValue<bool>("reduce-glow");
    g_config.reduceBloom = mod->getSettingValue<bool>("reduce-bloom");
    g_config.optimizeSpriteBatching = mod->getSettingValue<bool>("optimize-sprites");
    g_config.lowerTextureQuality = mod->getSettingValue<bool>("lower-texture-quality");
    g_config.opacityQuantizationStep = std::clamp(mod->getSettingValue<int>("opacity-step"), 1, 32);
    g_config.cullTransparentSprites = mod->getSettingValue<bool>("cull-transparent-sprites");
    g_config.transparentOpacityThreshold = std::clamp(mod->getSettingValue<int>("transparent-opacity-threshold"), 0, 32);
    g_config.cullTinySprites = mod->getSettingValue<bool>("cull-tiny-sprites");
    g_config.tinySpritePixelThreshold = std::clamp(mod->getSettingValue<int>("tiny-sprite-threshold"), 1, 8);
    g_config.enableObjectCulling = mod->getSettingValue<bool>("enable-culling");
    g_config.offscreenPadding = std::clamp(mod->getSettingValue<int>("offscreen-padding"), 0, 256);

    g_config.reducePhysicsFreq = mod->getSettingValue<bool>("reduce-physics");
    g_config.physicsFrameSkip = std::clamp(mod->getSettingValue<int>("physics-skip"), 0, 5);
    g_config.reducePositionUpdates = mod->getSettingValue<bool>("reduce-position-updates");
    g_config.optimizeRotationCalcs = mod->getSettingValue<bool>("optimize-rotation");
    g_config.skipScaleUpdates = mod->getSettingValue<bool>("skip-scale-updates");
    g_config.skipColorUpdates = mod->getSettingValue<bool>("skip-color-updates");
    g_config.reduceMemoryAllocations = mod->getSettingValue<bool>("reduce-memory-allocs");
    g_config.optimizeObjectPooling = mod->getSettingValue<bool>("optimize-object-pooling");
    g_config.cacheValues = mod->getSettingValue<bool>("cache-values");
    g_config.reduceEffectSpawning = mod->getSettingValue<bool>("reduce-effect-spawning");
    g_config.lowerAnimationFrameRate = mod->getSettingValue<bool>("lower-animation-framerate");

    g_config.processPriorityLevel = std::clamp(mod->getSettingValue<int>("process-priority-level"), 0, 2);
    g_config.disablePriorityBoost = mod->getSettingValue<bool>("disable-priority-boost");
    g_config.disablePowerThrottling = mod->getSettingValue<bool>("disable-power-throttling");
    g_config.raiseTimerResolution = mod->getSettingValue<bool>("raise-timer-resolution");

    g_config.enableAggressiveOptimizations = mod->getSettingValue<bool>("aggressive-mode");
    g_config.logPerformanceMetrics = mod->getSettingValue<bool>("log-metrics");
    g_config.targetFPS = std::clamp(mod->getSettingValue<int>("target-fps"), 60, 360);
}

void initializeRuntime() {
    g_metrics = {};
    g_runtime = {};
    g_runtime.glowDrawCadence = 1;
    g_runtime.frameCadence = 1;
}

void applyStartupOptimizations() {
    if (g_runtime.windowsTweaksApplied) {
        return;
    }

    auto process = GetCurrentProcess();
    applyProcessPriority(process);
    applyPriorityBoostSetting(process);
    applyPowerThrottlingSetting(process);
    applyTimerResolutionSetting();
    logGpuPreferenceState();
    g_runtime.windowsTweaksApplied = true;
}

void onFrameStart() {
    refreshLiveConfiguration();

    auto now = std::chrono::steady_clock::now();
    if (!g_runtime.initialized) {
        g_runtime.lastFrameTime = now;
        g_runtime.initialized = true;
    } else {
        auto dt = std::chrono::duration<double, std::milli>(now - g_runtime.lastFrameTime).count();
        g_runtime.lastFrameTime = now;
        if (g_metrics.avgFrameMs <= 0.01) {
            g_metrics.avgFrameMs = dt;
        } else {
            g_metrics.avgFrameMs = (g_metrics.avgFrameMs * 0.90) + (dt * 0.10);
        }
    }

    g_runtime.glowDrawCadence = getAdaptiveGlowCadence();
    g_runtime.frameCadence = getFrameCadence();
    g_metrics.frameCount++;
}

void applyTargetFrameRate() {
    CCDirector::sharedDirector()->setAnimationInterval(1.0 / static_cast<double>(g_config.targetFPS));
}

void logMetricsIfNeeded() {
    if (!g_config.logPerformanceMetrics || g_metrics.frameCount % 300 != 0) {
        return;
    }

    log::info(
        "[OptimizerPlus] frames={} avgFrameMs={:.2f} reducedParticles={} glowCadence={} frameCadence={} throttledGlow={} culledDraws={} transparentCull={} tinyCull={} skippedVisibility={} skippedPosition={} skippedRotation={} skippedScale={} skippedColor={}",
        g_metrics.frameCount,
        g_metrics.avgFrameMs,
        g_metrics.reducedParticles,
        g_runtime.glowDrawCadence,
        g_runtime.frameCadence,
        g_metrics.throttledGlowDraws,
        g_metrics.culledDraws,
        g_metrics.culledTransparentSprites,
        g_metrics.culledTinySprites,
        g_metrics.skippedVisibilityUpdates,
        g_metrics.skippedPositionUpdates,
        g_metrics.skippedRotationUpdates,
        g_metrics.skippedScaleUpdates,
        g_metrics.skippedColorUpdates
    );
}

bool shouldSkipVisibilityUpdate(CCSprite* sprite, bool visible) {
    if (!g_config.optimizeSpriteBatching || !sprite) {
        return false;
    }

    if (sprite->isVisible() == visible) {
        g_metrics.skippedVisibilityUpdates++;
        return true;
    }

    return false;
}

GLubyte optimizeSpriteOpacity(CCSprite* sprite, GLubyte opacity) {
    if (g_config.cacheValues && sprite && sprite->getOpacity() == opacity) {
        return sprite->getOpacity();
    }

    GLubyte optimized = opacity;
    if (g_config.lowerTextureQuality && optimized < 255) {
        auto step = static_cast<GLubyte>(g_config.opacityQuantizationStep);
        optimized = static_cast<GLubyte>((optimized / step) * step);
    }
    if (g_config.reduceBloom && optimized > 224) {
        optimized = 224;
    }
    if (g_config.reduceGlow && optimized > 208) {
        optimized = 208;
    }
    if (g_config.enableAggressiveOptimizations && optimized < 255) {
        optimized = static_cast<GLubyte>((optimized / 16) * 16);
    }

    return optimized;
}

bool shouldSkipColorUpdate(CCSprite* sprite, ccColor3B const& color) {
    if (!g_config.skipColorUpdates || !sprite) {
        return false;
    }

    auto current = sprite->getColor();
    if (current.r == color.r && current.g == color.g && current.b == color.b) {
        g_metrics.skippedColorUpdates++;
        return true;
    }

    return false;
}

bool shouldCullNodeDraw(CCNode* node) {
    return false;
}

bool shouldSkipSpriteDraw(CCSprite* sprite) {
    if (!sprite) {
        return false;
    }

    if (g_config.cullTransparentSprites && sprite->getOpacity() <= g_config.transparentOpacityThreshold) {
        g_metrics.culledTransparentSprites++;
        return true;
    }

    if (g_config.cullTinySprites) {
        auto box = sprite->boundingBox();
        auto limit = static_cast<float>(g_config.tinySpritePixelThreshold);
        if (box.size.width <= limit && box.size.height <= limit && sprite->getOpacity() <= 96) {
            g_metrics.culledTinySprites++;
            return true;
        }
    }

    return false;
}

bool shouldThrottleGlowDraw(CCSprite* sprite) {
    if (!sprite || g_runtime.glowDrawCadence <= 1) {
        return false;
    }

    auto blend = sprite->getBlendFunc();
    if (!isGlowLikeBlend(blend)) {
        return false;
    }

    if (shouldSkipByCadence(reinterpret_cast<uintptr_t>(sprite), g_runtime.glowDrawCadence)) {
        g_metrics.throttledGlowDraws++;
        return true;
    }

    return false;
}

bool shouldSkipPositionUpdate(CCNode* node, CCPoint const& pos) {
    if (!g_config.reducePositionUpdates || !node) {
        return false;
    }

    auto current = node->getPosition();
    if (exactlyEqual(current.x, pos.x) && exactlyEqual(current.y, pos.y)) {
        g_metrics.skippedPositionUpdates++;
        return true;
    }

    return false;
}

bool shouldSkipRotationUpdate(CCNode* node, float rot) {
    if (!g_config.optimizeRotationCalcs || !node) {
        return false;
    }

    if (exactlyEqual(node->getRotation(), rot)) {
        g_metrics.skippedRotationUpdates++;
        return true;
    }

    return false;
}

bool shouldSkipUniformScaleUpdate(CCNode* node, float scale) {
    if (!g_config.skipScaleUpdates || !node) {
        return false;
    }

    if (exactlyEqual(node->getScale(), scale)) {
        g_metrics.skippedScaleUpdates++;
        return true;
    }

    return false;
}

bool shouldSkipScaleXUpdate(CCNode* node, float scale) {
    if (!g_config.skipScaleUpdates || !node) {
        return false;
    }

    if (exactlyEqual(node->getScaleX(), scale)) {
        g_metrics.skippedScaleUpdates++;
        return true;
    }

    return false;
}

bool shouldSkipScaleYUpdate(CCNode* node, float scale) {
    if (!g_config.skipScaleUpdates || !node) {
        return false;
    }

    if (exactlyEqual(node->getScaleY(), scale)) {
        g_metrics.skippedScaleUpdates++;
        return true;
    }

    return false;
}

bool shouldSkipParticleUpdate(CCParticleSystemQuad* system) {
    return false;
}

unsigned int optimizeParticleCount(CCParticleSystemQuad* system, unsigned int original) {
    float density = graphicsParticleMultiplier();
    auto minimum = static_cast<unsigned int>(g_config.minimumParticleCount);
    auto optimized = static_cast<unsigned int>(std::max(static_cast<float>(minimum), std::round(static_cast<float>(original) * density)));

    if (g_config.reduceMemoryAllocations) {
        auto bucket = static_cast<unsigned int>(g_config.particleBucketSize);
        optimized = std::max(minimum, ((optimized + bucket - 1u) / bucket) * bucket);
    }

    if (g_config.optimizeObjectPooling) {
        static std::unordered_map<CCParticleSystemQuad*, unsigned int> s_capacityCache;
        if (system) {
            auto it = s_capacityCache.find(system);
            if (it != s_capacityCache.end()) {
                auto previousCapacity = it->second;
                auto lowerBound = static_cast<unsigned int>(std::round(static_cast<double>(previousCapacity) * 0.75));
                if (optimized <= previousCapacity && optimized >= lowerBound) {
                    optimized = previousCapacity;
                }
            }
            s_capacityCache[system] = std::max(optimized, it != s_capacityCache.end() ? it->second : 0u);
        }
    }

    if (optimized < original) {
        g_metrics.reducedParticles += static_cast<uint64_t>(original - optimized);
    }

    return optimized;
}
