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
        // using drawScene as the frame boundary basically. refresh stuff once,
        // let the game render normal, then log counters after everything is done.
        onFrameStart();
        CCDirector::drawScene();
        logMetricsIfNeeded();
    }
};

class $modify(OptimizedNode, CCNode) {
    void setPosition(CCPoint const& pos) {
        // only skip this if it's literally the same value. gd and cocos call
        // these a lot, and messing with near-equal values could still mess up
        // movement or camera interpolation.
        if (shouldSkipPositionUpdate(this, pos)) {
            return;
        }

        CCNode::setPosition(pos);
    }

    void setRotation(float rot) {
        // same idea as position. this is just for repeated extra calls,
        // not for changing rotation values that actually matter in gameplay.
        if (shouldSkipRotationUpdate(this, rot)) {
            return;
        }

        CCNode::setRotation(rot);
    }

    void setScale(float scale) {
        // regular scale is separate from x/y scale because a lot of nodes only
        // use one path, and cocos does not really combine that for you.
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
        // visibility changes can move sprite state around in batches, so
        // skipping no-op writes here is a pretty easy safe win.
        if (shouldSkipVisibilityUpdate(this, visible)) {
            return;
        }

        CCSprite::setVisible(visible);
    }

    void setOpacity(GLubyte opacity) {
        // opacity is the one sprite property we actually rewrite on purpose,
        // because some settings trade a little visual quality for fewer alpha states.
        auto optimized = optimizeSpriteOpacity(this, opacity);
        if (g_config.cacheValues && this->getOpacity() == optimized) {
            return;
        }

        CCSprite::setOpacity(optimized);
    }

    void setColor(ccColor3B const& color) {
        // color stays exact. the only optimization here is skipping it when
        // nothing actually changed.
        if (shouldSkipColorUpdate(this, color)) {
            return;
        }

        CCSprite::setColor(color);
    }

    void draw() {
        // draw skipping is opt-in, and it's mostly for stuff that's basically
        // invisible already or glow passes that can be spread out a bit.
        if (shouldSkipSpriteDraw(this) || shouldThrottleGlowDraw(this)) {
            return;
        }

        CCSprite::draw();
    }
};

class $modify(OptimizedParticles, CCParticleSystemQuad) {
    void setTotalParticles(unsigned int totalParticles) {
        // change particle capacity before the engine does the full alloc so we
        // do not ask for work the config already decided it doesn't want.
        CCParticleSystemQuad::setTotalParticles(optimizeParticleCount(this, totalParticles));
    }
};

$on_mod(Loaded) {
    // load once on startup, then let drawScene refresh the live settings now
    // and then for the options that don't need a full restart.
    loadConfiguration();
    initializeRuntime();
    applyStartupOptimizations();
    applyTargetFrameRate();

    log::info("OptimizerPlus loaded successfully (v1.0.4)");
    log::info("Windows startup tweaks and safe runtime hooks enabled for transforms, cached sprite state, particles, and frame pacing.");
}
