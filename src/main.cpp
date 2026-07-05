#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCTexture2D.hpp>
#include <Geode/modify/CCParticleSystem.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    CCTexture2D::setDefaultAlphaPixelFormat(cocos2d::kCCTexture2DPixelFormat_RGBA4444);
}

namespace MemoryManager {
    void safePurge() {
        if (auto textureCache = CCTextureCache::sharedTextureCache()) {
            textureCache->removeUnusedTextures();
        }

        if (auto director = CCDirector::sharedDirector()) {
            director->purgeCachedData();
        }
    }
}

class $modify(RelentlessPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        MemoryManager::safePurge();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
};

class $modify(RelentlessMenuLayer, MenuLayer) {
    bool init() {
        MemoryManager::safePurge();
        return MenuLayer::init();
    }
};

class $modify(RelentlessTexture, CCTexture2D) {
    void setAntiAliasTexParameters() {
        this->setAliasTexParameters();
    }
};

class $modify(RelentlessParticles, CCParticleSystem) {
    bool initWithTotalParticles(unsigned int numberOfParticles) {
        if (numberOfParticles > 50) {
            numberOfParticles = 50;
        }
        return CCParticleSystem::initWithTotalParticles(numberOfParticles);
    }

    void setTotalParticles(unsigned int tp) {
        if (tp > 50) {
            tp = 50;
        }
        CCParticleSystem::setTotalParticles(tp);
    }
};
