#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCTexture2D.hpp>

using namespace geode::prelude;

namespace MemoryManager {
    void executeAggressivePurge() {
        if (auto textureCache = CCTextureCache::sharedTextureCache()) {
            textureCache->removeUnusedTextures();
        }

        if (auto spriteCache = CCSpriteFrameCache::sharedSpriteFrameCache()) {
            spriteCache->removeUnusedSpriteFrames();
        }

        if (auto director = CCDirector::sharedDirector()) {
            director->purgeCachedData();
        }
    }
}

class $modify(RelentlessPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        MemoryManager::executeAggressivePurge();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        MemoryManager::executeAggressivePurge();
        PlayLayer::resetLevel();
    }
};

class $modify(RelentlessMenuLayer, MenuLayer) {
    bool init() {
        MemoryManager::executeAggressivePurge();
        return MenuLayer::init();
    }
};

class $modify(RelentlessTexture, CCTexture2D) {
    void setAntiAliasTexParameters() {
        this->setAliasTexParameters();
    }
};
