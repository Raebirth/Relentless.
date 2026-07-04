#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

// Interceptamos el inicio de los niveles
class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // Ejecutamos la purga agresiva de VRAM y caché antes de cargar el nivel pesado
        CCTextureCache::sharedTextureCache()->removeUnusedTextures();
        CCDirector::sharedDirector()->purgeCachedData();
        
        log::info("Relentless Optimizer: VRAM purgada con exito al entrar al nivel.");

        // Dejamos que el nivel cargue normalmente con la memoria limpia
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
};

// Interceptamos el regreso al menú principal
class $modify(MenuLayer) {
    bool init() {
        // Volvemos a vaciar la basura gráfica acumulada durante la sesión de juego
        CCTextureCache::sharedTextureCache()->removeUnusedTextures();
        CCDirector::sharedDirector()->purgeCachedData();
        
        log::info("Relentless Optimizer: VRAM purgada con exito al volver al menu.");

        // Cargamos el menú
        return MenuLayer::init();
    }
};
