#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCTexture2D.hpp>

using namespace geode::prelude;

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
