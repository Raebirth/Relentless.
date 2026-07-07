#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCTexture2D.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <pthread.h>
#include <sys/resource.h>
#include <sched.h>
#include <mimalloc.h>

using namespace geode::prelude;

void forceExtremeThreadPriority() {
    setpriority(PRIO_PROCESS, 0, -20);
    
    pthread_t thread_id = pthread_self();
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(thread_id, SCHED_FIFO, &param);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(4, &cpuset);
    CPU_SET(5, &cpuset);
    CPU_SET(6, &cpuset);
    CPU_SET(7, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

$on_mod(Loaded) {
    forceExtremeThreadPriority();
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
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::init", -10000); 
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        MemoryManager::safePurge();
        
        auto gm = GameManager::get();
        if (gm) {
            gm->setGameVariable("0023", false);
            gm->setGameVariable("0024", false);
        }
        
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
};

class $modify(RelentlessMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("MenuLayer::init", -10000); 
    }

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
    bool initWithTotalParticles(unsigned int numberOfParticles, bool b) {
        if (numberOfParticles > 50) numberOfParticles = 50;
        return CCParticleSystem::initWithTotalParticles(numberOfParticles, b);
    }
    
    void setTotalParticles(unsigned int tp) {
        if (tp > 50) tp = 50;
        CCParticleSystem::setTotalParticles(tp);
    }
};

class $modify(RelentlessAudio, FMODAudioEngine) {
    void setupAudioEngine() {
        FMODAudioEngine::setupAudioEngine();
        if (m_system) {
            m_system->setDSPBufferSize(256, 2);
            m_system->setSoftwareChannels(32);
        }
    }
};
