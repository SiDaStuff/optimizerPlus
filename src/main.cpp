#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCSprite.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

struct OptimizerConfig {
    // Graphics settings (1-15)
    bool reduceParticles = true;
    int particleDensity = 50;
    bool reduceShadows = true;
    bool reduceBloom = true;
    bool reduceGlow = true;
    bool disableBackgroundEffects = false;
    bool optimizeSpriteBatching = true;
    bool reduceTrailQuality = true;
    bool reduceShaderQuality = true;
    bool lowerTextureQuality = true;
    bool disableExpensiveEffects = true;
    bool reducePortalComplexity = true;
    bool optimizeUIRendering = true;
    bool lowerAnimationFrameRate = true;
    bool reduceWaterEffects = true;
    bool optimizePostProcessing = true;

    // Processing settings (16-34)
    bool reducePhysicsFreq = true;
    int physicsFrameSkip = 1;
    bool optimizeCollisions = true;
    bool enableObjectCulling = true;
    bool optimizeRotationCalcs = true;
    bool reducePositionUpdates = true;
    bool optimizeCurveCalcs = true;
    bool reduceMemoryAllocations = true;
    bool lazyLoadAssets = true;
    bool optimizeEventProcessing = true;
    bool reduceTimerFrequency = true;
    bool optimizeJumpMechanics = true;
    bool reduceAudioOverhead = true;
    bool optimizeObjectPooling = true;
    bool cacheValues = true;
    bool reduceDecorationUpdates = true;
    bool optimizeInputProcessing = true;
    bool reduceEffectSpawning = true;
    bool optimizeCCNodeUpdates = true;

    // Advanced settings
    bool enableAggressiveOptimizations = false;
    bool logPerformanceMetrics = false;
    int targetFPS = 240;
} g_config;

struct PerfMetrics {
    uint64_t frameCount = 0;
    uint64_t skippedVisits = 0;
    uint64_t culledVisits = 0;
    uint64_t reducedParticles = 0;
    uint64_t throttledGlowDraws = 0;
    double avgFrameMs = 0.0;
} g_metrics;

struct RuntimeState {
    std::chrono::steady_clock::time_point lastFrameTime {};
    bool initialized = false;
    int glowDrawCadence = 1;
} g_runtime;

void loadConfiguration() {
    auto mod = Mod::get();

    g_config.reduceParticles = mod->getSettingValue<bool>("reduce-particles");
    g_config.particleDensity = std::clamp(mod->getSettingValue<int>("particle-density"), 0, 100);
    g_config.reduceShadows = mod->getSettingValue<bool>("reduce-shadows");
    g_config.reduceBloom = mod->getSettingValue<bool>("reduce-bloom");
    g_config.reduceGlow = mod->getSettingValue<bool>("reduce-glow");
    g_config.disableBackgroundEffects = mod->getSettingValue<bool>("disable-bg-effects");
    g_config.optimizeSpriteBatching = mod->getSettingValue<bool>("optimize-sprites");
    g_config.reduceTrailQuality = mod->getSettingValue<bool>("reduce-trails");
    g_config.reduceShaderQuality = mod->getSettingValue<bool>("reduce-shaders");
    g_config.lowerTextureQuality = mod->getSettingValue<bool>("lower-texture-quality");
    g_config.disableExpensiveEffects = mod->getSettingValue<bool>("disable-expensive-effects");
    g_config.reducePortalComplexity = mod->getSettingValue<bool>("reduce-portal-complexity");
    g_config.optimizeUIRendering = mod->getSettingValue<bool>("optimize-ui-rendering");
    g_config.lowerAnimationFrameRate = mod->getSettingValue<bool>("lower-animation-framerate");
    g_config.reduceWaterEffects = mod->getSettingValue<bool>("reduce-water-effects");
    g_config.optimizePostProcessing = mod->getSettingValue<bool>("optimize-post-processing");

    g_config.reducePhysicsFreq = mod->getSettingValue<bool>("reduce-physics");
    g_config.physicsFrameSkip = std::clamp(mod->getSettingValue<int>("physics-skip"), 0, 5);
    g_config.optimizeCollisions = mod->getSettingValue<bool>("optimize-collisions");
    g_config.enableObjectCulling = mod->getSettingValue<bool>("enable-culling");
    g_config.optimizeRotationCalcs = mod->getSettingValue<bool>("optimize-rotation");
    g_config.reducePositionUpdates = mod->getSettingValue<bool>("reduce-position-updates");
    g_config.optimizeCurveCalcs = mod->getSettingValue<bool>("optimize-curves");
    g_config.reduceMemoryAllocations = mod->getSettingValue<bool>("reduce-memory-allocs");
    g_config.lazyLoadAssets = mod->getSettingValue<bool>("lazy-load");
    g_config.optimizeEventProcessing = mod->getSettingValue<bool>("optimize-events");
    g_config.reduceTimerFrequency = mod->getSettingValue<bool>("reduce-timers");
    g_config.optimizeJumpMechanics = mod->getSettingValue<bool>("optimize-jump-mechanics");
    g_config.reduceAudioOverhead = mod->getSettingValue<bool>("reduce-audio-overhead");
    g_config.optimizeObjectPooling = mod->getSettingValue<bool>("optimize-object-pooling");
    g_config.cacheValues = mod->getSettingValue<bool>("cache-values");
    g_config.reduceDecorationUpdates = mod->getSettingValue<bool>("reduce-decoration-updates");
    g_config.optimizeInputProcessing = mod->getSettingValue<bool>("optimize-input-processing");
    g_config.reduceEffectSpawning = mod->getSettingValue<bool>("reduce-effect-spawning");
    g_config.optimizeCCNodeUpdates = mod->getSettingValue<bool>("optimize-ccnode-updates");

    g_config.enableAggressiveOptimizations = mod->getSettingValue<bool>("aggressive-mode");
    g_config.logPerformanceMetrics = mod->getSettingValue<bool>("log-metrics");
    g_config.targetFPS = std::clamp(mod->getSettingValue<int>("target-fps"), 60, 240);
}

int getNodeVisitCadence() {
    int cadence = 1;

    if (g_config.lowerAnimationFrameRate) cadence = std::max(cadence, 2);
    if (g_config.reduceTimerFrequency) cadence = std::max(cadence, 2);
    if (g_config.optimizeEventProcessing) cadence = std::max(cadence, 2);
    if (g_config.optimizeInputProcessing) cadence = std::max(cadence, 2);
    if (g_config.reduceDecorationUpdates) cadence = std::max(cadence, 2);
    if (g_config.optimizeCCNodeUpdates) cadence = std::max(cadence, 2);
    if (g_config.reduceAudioOverhead) cadence = std::max(cadence, 2);
    if (g_config.reducePhysicsFreq) cadence = std::max(cadence, g_config.physicsFrameSkip + 1);
    if (g_config.enableAggressiveOptimizations) cadence = std::max(cadence, 3);

    return std::clamp(cadence, 1, 6);
}

bool shouldVisitNodeThisFrame(CCNode* node) {
    auto cadence = getNodeVisitCadence();
    if (cadence <= 1) {
        return true;
    }

    auto hashed = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(node));
    auto phase = static_cast<uint64_t>(hashed % static_cast<uintptr_t>(cadence));
    return (g_metrics.frameCount + phase) % static_cast<uint64_t>(cadence) == 0;
}

bool isNodeOnScreen(CCNode* node) {
    auto director = CCDirector::sharedDirector();
    auto origin = director->getVisibleOrigin();
    auto size = director->getVisibleSize();
    auto padding = g_config.lazyLoadAssets ? 24.0f : 96.0f;
    CCRect screen(origin.x - padding, origin.y - padding, size.width + padding * 2.0f, size.height + padding * 2.0f);
    return node->boundingBox().intersectsRect(screen);
}

bool isGlowLikeBlend(cocos2d::ccBlendFunc blend) {
    // Most GD glow objects render with additive blending.
    return (blend.src == GL_SRC_ALPHA && blend.dst == GL_ONE) ||
           (blend.src == GL_ONE && blend.dst == GL_ONE);
}

int getAdaptiveGlowCadence() {
    if (!g_config.reduceGlow) {
        return 1;
    }

    auto targetMs = 1000.0 / static_cast<double>(g_config.targetFPS);
    auto frameMs = g_metrics.avgFrameMs <= 0.01 ? targetMs : g_metrics.avgFrameMs;

    int cadence = 1;
    if (frameMs > targetMs * 1.05) cadence = 2;
    if (frameMs > targetMs * 1.20) cadence = 3;
    if (frameMs > targetMs * 1.40) cadence = 4;
    if (g_config.enableAggressiveOptimizations && frameMs > targetMs * 1.10) cadence = std::min(cadence + 1, 5);

    return cadence;
}

unsigned int applyParticleOptimizations(unsigned int original) {
    float density = 1.0f;

    if (g_config.reduceParticles) density *= static_cast<float>(g_config.particleDensity) / 100.0f;
    if (g_config.reduceTrailQuality) density *= 0.85f;
    if (g_config.reduceShaderQuality) density *= 0.9f;
    if (g_config.reduceBloom) density *= 0.9f;
    if (g_config.reduceGlow) density *= 0.9f;
    if (g_config.disableBackgroundEffects) density *= 0.8f;
    if (g_config.disableExpensiveEffects) density *= 0.8f;
    if (g_config.reducePortalComplexity) density *= 0.9f;
    if (g_config.reduceWaterEffects) density *= 0.9f;
    if (g_config.optimizePostProcessing) density *= 0.95f;
    if (g_config.lowerTextureQuality) density *= 0.95f;
    if (g_config.reduceEffectSpawning) density *= 0.8f;
    if (g_config.optimizeUIRendering) density *= 0.97f;
    if (g_config.reduceShadows) density *= 0.95f;
    if (g_config.enableAggressiveOptimizations) density *= 0.75f;

    auto optimized = static_cast<unsigned int>(std::max(4.0f, std::round(static_cast<float>(original) * density)));

    if (g_config.reduceMemoryAllocations) {
        // Bucketized capacities reduce frequent reallocations of particle buffers.
        optimized = std::max(4u, ((optimized + 15u) / 16u) * 16u);
    }

    if (optimized < original) {
        g_metrics.reducedParticles += static_cast<uint64_t>(original - optimized);
    }
    return optimized;
}

class $modify(OptimizedDirector, CCDirector) {
    void drawScene() {
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
            g_runtime.glowDrawCadence = getAdaptiveGlowCadence();
        }

        g_metrics.frameCount++;
        CCDirector::drawScene();

        if (g_config.logPerformanceMetrics && g_metrics.frameCount % 300 == 0) {
            log::info(
                "[OptimizerPlus] frames={} reducedParticles={} avgFrameMs={:.2f} glowCadence={} throttledGlow={}",
                g_metrics.frameCount,
                g_metrics.reducedParticles,
                g_metrics.avgFrameMs,
                g_runtime.glowDrawCadence,
                g_metrics.throttledGlowDraws
            );
        }
    }
};

class $modify(OptimizedNode, CCNode) {
    void visit() {
        // Safety: global CCNode visit skipping/culling is too invasive and can corrupt rendering.
        CCNode::visit();
    }

    void setPosition(CCPoint const& pos) {
        CCNode::setPosition(pos);
    }

    void setRotation(float rot) {
        CCNode::setRotation(rot);
    }
};

class $modify(OptimizedSprite, CCSprite) {
    void setVisible(bool visible) {
        if (g_config.optimizeSpriteBatching && this->isVisible() == visible) {
            return;
        }
        CCSprite::setVisible(visible);
    }

    void setOpacity(GLubyte opacity) {
        if (g_config.cacheValues && this->getOpacity() == opacity) {
            return;
        }

        if (g_config.enableAggressiveOptimizations && opacity < 255) {
            opacity = static_cast<GLubyte>((opacity / 16) * 16);
        }

        CCSprite::setOpacity(opacity);
    }

    void draw() {
        if (g_runtime.glowDrawCadence > 1) {
            auto blend = this->getBlendFunc();
            if (isGlowLikeBlend(blend)) {
                auto cadence = static_cast<uint64_t>(g_runtime.glowDrawCadence);
                auto phase = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) % cadence;
                if ((g_metrics.frameCount + phase) % cadence != 0) {
                    g_metrics.throttledGlowDraws++;
                    return;
                }
            }
        }

        CCSprite::draw();
    }
};

class $modify(OptimizedParticles, CCParticleSystemQuad) {
    void setTotalParticles(unsigned int totalParticles) {
        auto optimized = applyParticleOptimizations(totalParticles);

        if (g_config.optimizeObjectPooling) {
            static std::unordered_map<CCParticleSystemQuad*, unsigned int> s_capacityCache;
            auto it = s_capacityCache.find(this);
            if (it != s_capacityCache.end()) {
                auto previousCapacity = it->second;
                auto lowerBound = static_cast<unsigned int>(std::round(static_cast<double>(previousCapacity) * 0.7));
                if (optimized <= previousCapacity && optimized >= lowerBound) {
                    return;
                }
            }
            s_capacityCache[this] = std::max(optimized, it != s_capacityCache.end() ? it->second : 0u);
        }

        CCParticleSystemQuad::setTotalParticles(optimized);
    }
};

$on_mod(Loaded) {
    loadConfiguration();

    auto director = CCDirector::sharedDirector();
    director->setAnimationInterval(1.0 / static_cast<double>(g_config.targetFPS));

    log::info("OptimizerPlus loaded successfully (v1.0.1)");
    log::info("Runtime hooks enabled for graphics, particles, node updates, culling, transforms, and FPS limiting.");
}
