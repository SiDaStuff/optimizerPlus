#include "optimizer.hpp"

#ifndef _WIN32
#error OptimizerPlus currently supports Windows only.
#endif

#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCSprite.hpp>

using namespace geode::prelude;

class $modify(OptimizedDirector, CCDirector) {
    void drawScene() {
        onFrameStart();
        CCDirector::drawScene();
        logMetricsIfNeeded();
    }
};

class $modify(OptimizedNode, CCNode) {
    void setPosition(CCPoint const& pos) {
        if (shouldSkipPositionUpdate(this, pos)) {
            return;
        }

        CCNode::setPosition(pos);
    }

    void setRotation(float rot) {
        if (shouldSkipRotationUpdate(this, rot)) {
            return;
        }

        CCNode::setRotation(rot);
    }

    void setScale(float scale) {
        if (shouldSkipUniformScaleUpdate(this, scale)) {
            return;
        }

        CCNode::setScale(scale);
    }

    void setScaleX(float scale) {
        if (shouldSkipScaleXUpdate(this, scale)) {
            return;
        }

        CCNode::setScaleX(scale);
    }

    void setScaleY(float scale) {
        if (shouldSkipScaleYUpdate(this, scale)) {
            return;
        }

        CCNode::setScaleY(scale);
    }
};

class $modify(OptimizedSprite, CCSprite) {
    void setVisible(bool visible) {
        if (shouldSkipVisibilityUpdate(this, visible)) {
            return;
        }

        CCSprite::setVisible(visible);
    }

    void setOpacity(GLubyte opacity) {
        auto optimized = optimizeSpriteOpacity(this, opacity);
        if (g_config.cacheValues && this->getOpacity() == optimized) {
            return;
        }

        CCSprite::setOpacity(optimized);
    }

    void setColor(ccColor3B const& color) {
        if (shouldSkipColorUpdate(this, color)) {
            return;
        }

        CCSprite::setColor(color);
    }

    void draw() {
        if (shouldSkipSpriteDraw(this) || shouldThrottleGlowDraw(this)) {
            return;
        }

        CCSprite::draw();
    }
};

class $modify(OptimizedParticles, CCParticleSystemQuad) {
    void setTotalParticles(unsigned int totalParticles) {
        CCParticleSystemQuad::setTotalParticles(optimizeParticleCount(this, totalParticles));
    }
};

$on_mod(Loaded) {
    loadConfiguration();
    initializeRuntime();
    applyStartupOptimizations();
    applyTargetFrameRate();

    log::info("OptimizerPlus loaded successfully (v1.0.2)");
    log::info("Windows startup tweaks and safe runtime hooks enabled for transforms, cached sprite state, particles, and frame pacing.");
}
