#include "optimizer.hpp"

#include <Geode/binding/PlayLayer.hpp>

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

bool isGameplaySceneActive() {
    return PlayLayer::get() != nullptr;
}

bool shouldApplyHeavyVisualCuts() {
    return !g_config.levelOnlyPerformanceMode || isGameplaySceneActive();
}

int getAdaptivePanicLevel() {
    if (!g_config.adaptivePanicMode || !shouldApplyHeavyVisualCuts()) {
        return 0;
    }

    auto targetMs = getTargetFrameMs();
    auto frameMs = g_metrics.avgFrameMs <= 0.01 ? targetMs : g_metrics.avgFrameMs;

    int panicLevel = 0;
    if (frameMs > targetMs * 1.08) panicLevel = 1;
    if (frameMs > targetMs * 1.18) panicLevel = 2;
    if (frameMs > targetMs * 1.32) panicLevel = 3;
    if (g_config.enableAggressiveOptimizations && frameMs > targetMs * 1.45) panicLevel = 4;

    return std::clamp(panicLevel, 0, 4);
}

bool isNodeNearVisibleArea(CCNode* node) {
    if (!node || !g_config.enableObjectCulling || !shouldApplyHeavyVisualCuts()) {
        return true;
    }

    auto director = CCDirector::sharedDirector();
    if (!director) {
        return true;
    }

    auto padding = static_cast<float>(g_config.offscreenPadding);
    if (g_config.adaptivePanicMode && g_runtime.panicLevel >= 2) {
        padding = std::max(0.0f, padding - static_cast<float>(g_runtime.panicLevel * 8));
    }

    auto origin = director->getVisibleOrigin();
    auto size = director->getVisibleSize();
    auto visibleRect = CCRect(
        origin.x - padding,
        origin.y - padding,
        size.width + padding * 2.0f,
        size.height + padding * 2.0f
    );

    return node->boundingBox().intersectsRect(visibleRect);
}

void refreshLiveConfiguration() {
    // check geode settings every couple seconds at 60 fps instead of every
    // frame, because that keeps toggles responsive without extra hook cost.
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
    // strict equality is on purpose here. the goal is removing repeated
    // setter calls, not squishing nearby values and messing up motion.
    return lhs == rhs;
}

float graphicsParticleMultiplier() {
    // each toggle adds a small multiplier so stacked settings make particle
    // heavy scenes calm down gradually instead of hitting one hard cap.
    float density = 1.0f;
    auto applyHeavyCuts = shouldApplyHeavyVisualCuts();

    if (applyHeavyCuts && g_config.reduceParticles) density *= static_cast<float>(g_config.particleDensity) / 100.0f;
    if (applyHeavyCuts && g_config.reduceBloom) density *= 0.92f;
    if (applyHeavyCuts && g_config.reduceGlow) density *= 0.90f;
    if (applyHeavyCuts && g_config.lowerTextureQuality) density *= 0.96f;
    if (applyHeavyCuts && g_config.reduceEffectSpawning) density *= 0.80f;
    if (applyHeavyCuts && g_config.lowerAnimationFrameRate) density *= 0.92f;
    if (applyHeavyCuts && g_config.particlePanicMode) density *= 0.62f;
    if (applyHeavyCuts && g_config.enableAggressiveOptimizations) density *= 0.72f;

    switch (g_runtime.panicLevel) {
        case 1: density *= 0.92f; break;
        case 2: density *= 0.82f; break;
        case 3: density *= 0.68f; break;
        case 4: density *= 0.56f; break;
        default: break;
    }

    return std::clamp(density, 0.05f, 1.0f);
}

int getFrameCadence() {
    // this is not the actual engine timestep or anything. it's more like a
    // shared "how aggressive should runtime skips be" kind of value.
    int cadence = 1;

    if (g_config.reducePhysicsFreq) cadence = std::max(cadence, g_config.physicsFrameSkip + 1);
    if (g_config.reducePositionUpdates) cadence = std::max(cadence, 2);
    if (g_config.optimizeRotationCalcs) cadence = std::max(cadence, 2);
    if (g_config.skipScaleUpdates) cadence = std::max(cadence, 2);
    if (g_config.reduceMemoryAllocations) cadence = std::max(cadence, 2);
    if (g_config.optimizeObjectPooling) cadence = std::max(cadence, 2);
    if (g_config.cacheValues) cadence = std::max(cadence, 2);
    if (shouldApplyHeavyVisualCuts() && g_config.reduceEffectSpawning) cadence = std::max(cadence, 2);
    if (shouldApplyHeavyVisualCuts() && g_config.lowerAnimationFrameRate) cadence = std::max(cadence, 2);
    if (shouldApplyHeavyVisualCuts() && g_config.decorationSleep) cadence = std::max(cadence, 3);
    if (shouldApplyHeavyVisualCuts() && g_config.enableAggressiveOptimizations) cadence = std::max(cadence, 3);
    if (g_runtime.panicLevel >= 2) cadence = std::max(cadence, g_runtime.panicLevel);

    return std::clamp(cadence, 1, 6);
}

int getAdaptiveGlowCadence() {
    if (!g_config.reduceGlow || !shouldApplyHeavyVisualCuts()) {
        return 1;
    }

    auto targetMs = getTargetFrameMs();
    auto frameMs = g_metrics.avgFrameMs <= 0.01 ? targetMs : g_metrics.avgFrameMs;

    int cadence = 1;
    if (frameMs > targetMs * 1.05) cadence = 2;
    if (frameMs > targetMs * 1.20) cadence = 3;
    if (frameMs > targetMs * 1.40) cadence = 4;
    if (g_config.ultraGlowCut) cadence = std::max(cadence, 3);
    if (g_config.enableAggressiveOptimizations && frameMs > targetMs * 1.10) cadence = std::min(cadence + 1, 5);
    cadence = std::min(cadence + g_runtime.panicLevel, 6);

    return std::clamp(cadence, 1, 6);
}

bool shouldSkipByCadence(uintptr_t identity, int cadence) {
    if (cadence <= 1) {
        return false;
    }

    // hash by address so additive effects do not all vanish on the same frame.
    // spreading them out makes the throttling look a lot less obvious.
    auto step = static_cast<uint64_t>(cadence);
    auto phase = static_cast<uint64_t>(identity) % step;
    return (g_metrics.frameCount + phase) % step != 0;
}

void applyProcessPriority(HANDLE process) {
    // above normal is the default perf preset here because high can be a bit
    // too much on some pcs if the player is doing other stuff too.
    auto requestedLevel = g_config.processPriorityLevel;
    if (g_config.startupTurboPreset) {
        requestedLevel = std::max(requestedLevel, 2);
    }

    DWORD priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
    if (requestedLevel >= 2) {
        priorityClass = HIGH_PRIORITY_CLASS;
    } else if (requestedLevel <= 0) {
        priorityClass = NORMAL_PRIORITY_CLASS;
    }

    if (!SetPriorityClass(process, priorityClass)) {
        log::warn("[OptimizerPlus] Failed to set process priority class: {}", GetLastError());
    }
}

void applyPriorityBoostSetting(HANDLE process) {
    auto disableBoost = g_config.disablePriorityBoost || g_config.startupTurboPreset;
    if (!SetProcessPriorityBoost(process, disableBoost ? TRUE : FALSE)) {
        log::warn("[OptimizerPlus] Failed to update process priority boost: {}", GetLastError());
    }
}

void applyPowerThrottlingSetting(HANDLE process) {
    // if StateMask is 0, windows basically will not apply the execution speed
    // throttle to this process when that control bit is enabled.
    auto disableThrottle = g_config.disablePowerThrottling || g_config.startupTurboPreset;
    PROCESS_POWER_THROTTLING_STATE throttlingState {};
    throttlingState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttlingState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttlingState.StateMask = disableThrottle ? 0 : PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    if (!SetProcessInformation(process, ProcessPowerThrottling, &throttlingState, sizeof(throttlingState))) {
        log::warn("[OptimizerPlus] Failed to update process power throttling: {}", GetLastError());
    }
}

void applyTimerResolutionSetting() {
    if ((!g_config.raiseTimerResolution && !g_config.startupTurboPreset) || g_runtime.timerResolutionRaised) {
        return;
    }

    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_runtime.timerResolutionRaised = true;
    } else {
        log::warn("[OptimizerPlus] Failed to raise timer resolution.");
    }
}

void logGpuPreferenceState() {
    // those exported symbols up there are more like a preference hint for
    // laptop gpu switching, not some magic guarantee for every machine.
    log::info("[OptimizerPlus] Exported high-performance GPU preference for Nvidia Optimus and AMD PowerXpress.");
    log::info("[OptimizerPlus] On Intel or other systems, Windows process tuning is still applied to reduce throttling.");
}
}  // namespace

void loadConfiguration() {
    auto mod = Mod::get();

    // clamp all the numeric settings on load so live reloads cannot push stuff
    // outside the ranges the code down here is expecting.
    g_config.reduceParticles = mod->getSettingValue<bool>("reduce-particles");
    g_config.particleDensity = std::clamp(mod->getSettingValue<int>("particle-density"), 0, 100);
    g_config.minimumParticleCount = std::clamp(mod->getSettingValue<int>("minimum-particle-count"), 0, 64);
    g_config.particleBucketSize = std::clamp(mod->getSettingValue<int>("particle-bucket-size"), 4, 64);
    g_config.particlePanicMode = mod->getSettingValue<bool>("particle-panic-mode");

    g_config.reduceGlow = mod->getSettingValue<bool>("reduce-glow");
    g_config.reduceBloom = mod->getSettingValue<bool>("reduce-bloom");
    g_config.ultraGlowCut = mod->getSettingValue<bool>("ultra-glow-cut");
    g_config.optimizeSpriteBatching = mod->getSettingValue<bool>("optimize-sprites");
    g_config.lowerTextureQuality = mod->getSettingValue<bool>("lower-texture-quality");
    g_config.opacityQuantizationStep = std::clamp(mod->getSettingValue<int>("opacity-step"), 1, 32);
    g_config.cullTransparentSprites = mod->getSettingValue<bool>("cull-transparent-sprites");
    g_config.transparentCullPlus = mod->getSettingValue<bool>("transparent-cull-plus");
    g_config.transparentOpacityThreshold = std::clamp(mod->getSettingValue<int>("transparent-opacity-threshold"), 0, 32);
    g_config.cullTinySprites = mod->getSettingValue<bool>("cull-tiny-sprites");
    g_config.tinySpritePixelThreshold = std::clamp(mod->getSettingValue<int>("tiny-sprite-threshold"), 1, 8);
    g_config.decorationSleep = mod->getSettingValue<bool>("decoration-sleep");
    g_config.enableObjectCulling = mod->getSettingValue<bool>("enable-culling");
    g_config.offscreenPadding = std::clamp(mod->getSettingValue<int>("offscreen-padding"), 0, 256);
    g_config.levelOnlyPerformanceMode = mod->getSettingValue<bool>("level-only-performance-mode");
    g_config.adaptivePanicMode = mod->getSettingValue<bool>("adaptive-panic-mode");

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
    g_config.startupTurboPreset = mod->getSettingValue<bool>("startup-turbo-preset");

    g_config.enableAggressiveOptimizations = mod->getSettingValue<bool>("aggressive-mode");
    g_config.logPerformanceMetrics = mod->getSettingValue<bool>("log-metrics");
    g_config.targetFPS = std::clamp(mod->getSettingValue<int>("target-fps"), 60, 360);
}

void initializeRuntime() {
    // reset the session counters in a way that's safe on startup or reload.
    // the real saved config still lives in g_config though.
    g_metrics = {};
    g_runtime = {};
    g_runtime.glowDrawCadence = 1;
    g_runtime.frameCadence = 1;
    g_runtime.panicLevel = 0;
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
            // simple smoothing so glow cadence reacts more to the trend
            // instead of freaking out over one random slow frame.
            g_metrics.avgFrameMs = (g_metrics.avgFrameMs * 0.90) + (dt * 0.10);
        }
    }

    g_runtime.panicLevel = getAdaptivePanicLevel();
    g_runtime.frameCadence = getFrameCadence();
    g_runtime.glowDrawCadence = getAdaptiveGlowCadence();
    if (g_runtime.panicLevel > 0) {
        g_metrics.panicFrames++;
    }
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
        "[OptimizerPlus] frames={} avgFrameMs={:.2f} panicLevel={} reducedParticles={} glowCadence={} frameCadence={} throttledGlow={} decorationSleep={} culledDraws={} transparentCull={} tinyCull={} skippedVisibility={} skippedPosition={} skippedRotation={} skippedScale={} skippedColor={}",
        g_metrics.frameCount,
        g_metrics.avgFrameMs,
        g_runtime.panicLevel,
        g_metrics.reducedParticles,
        g_runtime.glowDrawCadence,
        g_runtime.frameCadence,
        g_metrics.throttledGlowDraws,
        g_metrics.sleptDecorationDraws,
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
    if (shouldApplyHeavyVisualCuts() && g_config.lowerTextureQuality && optimized < 255) {
        // snapping alpha like this means sprites end up using fewer opacity
        // values, which is kind of a dumb but safe way to calm visual churn down.
        auto stepValue = g_config.opacityQuantizationStep;
        if (g_config.ultraGlowCut) {
            stepValue = std::max(stepValue, 12);
        }
        if (g_runtime.panicLevel >= 2) {
            stepValue = std::max(stepValue, 16);
        }
        auto step = static_cast<GLubyte>(stepValue);
        optimized = static_cast<GLubyte>((optimized / step) * step);
    }
    if (shouldApplyHeavyVisualCuts() && g_config.reduceBloom && optimized > 224) {
        optimized = g_config.ultraGlowCut ? 192 : 224;
    }
    if (shouldApplyHeavyVisualCuts() && g_config.reduceGlow && optimized > 208) {
        optimized = g_config.ultraGlowCut ? 168 : 208;
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
    if (!g_config.enableObjectCulling || !shouldApplyHeavyVisualCuts()) {
        return false;
    }

    if (!isNodeNearVisibleArea(node)) {
        g_metrics.culledDraws++;
        return true;
    }

    return false;
}

bool shouldSkipSpriteDraw(CCSprite* sprite) {
    if (!sprite) {
        return false;
    }

    if (shouldCullNodeDraw(sprite)) {
        return true;
    }

    auto transparentCullEnabled = shouldApplyHeavyVisualCuts() && (g_config.cullTransparentSprites || g_config.transparentCullPlus);
    auto opacityThreshold = g_config.transparentOpacityThreshold;
    if (g_config.transparentCullPlus) {
        opacityThreshold = std::max(opacityThreshold, 14);
    }
    if (g_runtime.panicLevel >= 2) {
        opacityThreshold = std::max(opacityThreshold, 10 + (g_runtime.panicLevel * 3));
    }

    if (transparentCullEnabled && sprite->getOpacity() <= opacityThreshold) {
        // sprites this faint are usually basically already gone visually, so
        // this skip is user opt-in instead of just being on by default.
        g_metrics.culledTransparentSprites++;
        return true;
    }

    auto tinyCullEnabled = shouldApplyHeavyVisualCuts() && (g_config.cullTinySprites || g_config.transparentCullPlus);
    if (tinyCullEnabled) {
        auto box = sprite->boundingBox();
        auto limit = static_cast<float>(g_config.tinySpritePixelThreshold);
        if (g_config.transparentCullPlus) {
            limit = std::max(limit, 4.0f);
        }
        if (g_runtime.panicLevel >= 2) {
            limit = std::max(limit, 4.0f + static_cast<float>(g_runtime.panicLevel));
        }
        // bounding boxes are already world space here, so this only catches
        // sprites that end up tiny on screen, not just tiny source textures.
        if (box.size.width <= limit && box.size.height <= limit && sprite->getOpacity() <= 96) {
            g_metrics.culledTinySprites++;
            return true;
        }
    }

    if (shouldApplyHeavyVisualCuts() && g_config.decorationSleep) {
        auto box = sprite->boundingBox();
        auto blend = sprite->getBlendFunc();
        auto looksDecorative =
            isGlowLikeBlend(blend) ||
            sprite->getOpacity() < 220 ||
            box.size.width <= 12.0f ||
            box.size.height <= 12.0f;

        if (looksDecorative) {
            auto cadence = std::clamp(g_runtime.frameCadence + 1 + (g_runtime.panicLevel / 2), 2, 6);
            if (shouldSkipByCadence(reinterpret_cast<uintptr_t>(sprite), cadence)) {
                g_metrics.sleptDecorationDraws++;
                return true;
            }
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

    // only additive-type sprites get throttled here because they are usually
    // bloom or glow stuff where missed draws are harder to notice.
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
    // placeholder for maybe doing effect cadence stuff later. returning false
    // for now means particle sim stays alone and only capacity resizing is active.
    return false;
}

unsigned int optimizeParticleCount(CCParticleSystemQuad* system, unsigned int original) {
    // this particle hook works on the requested capacity, not live particle
    // count, so it's more about alloc pressure than changing sim behavior.
    float density = graphicsParticleMultiplier();
    auto minimumCount = g_config.minimumParticleCount;
    if (shouldApplyHeavyVisualCuts() && g_config.particlePanicMode) {
        minimumCount = std::min(minimumCount, 4);
    }
    if (g_runtime.panicLevel >= 3) {
        minimumCount = std::min(minimumCount, 2);
    }

    auto minimum = static_cast<unsigned int>(minimumCount);
    auto optimized = static_cast<unsigned int>(std::max(static_cast<float>(minimum), std::round(static_cast<float>(original) * density)));

    if (g_config.reduceMemoryAllocations) {
        // round capacities upward so a bunch of tiny changes do not keep
        // forcing new buffers. this trades a little memory for steadier reuse.
        auto bucketSize = g_config.particleBucketSize;
        if (shouldApplyHeavyVisualCuts() && g_config.particlePanicMode) {
            bucketSize = std::max(bucketSize, 24);
        }
        auto bucket = static_cast<unsigned int>(bucketSize);
        optimized = std::max(minimum, ((optimized + bucket - 1u) / bucket) * bucket);
    }

    if (g_config.optimizeObjectPooling) {
        static std::unordered_map<CCParticleSystemQuad*, unsigned int> s_capacityCache;
        if (system) {
            auto it = s_capacityCache.find(system);
            if (it != s_capacityCache.end()) {
                // keep the old capacity if the new size is still within 75%,
                // so systems do not keep flapping between almost the same values.
                auto previousCapacity = it->second;
                auto holdRatio = (shouldApplyHeavyVisualCuts() && g_config.particlePanicMode) ? 0.65 : 0.75;
                auto lowerBound = static_cast<unsigned int>(std::round(static_cast<double>(previousCapacity) * holdRatio));
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
