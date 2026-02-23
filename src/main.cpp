#include "Geode/cocos/cocoa/CCObject.h"
#include "Geode/loader/Log.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/CustomSongWidget.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

unsigned int g_backgroundMusicPosition = 0;
bool g_customMusicPlayed = false;

namespace {

void storeMenuBGPosition() {
    FMOD::Channel* channel = nullptr;
    auto fmod = FMODAudioEngine::sharedEngine();
    auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
    if (result == FMOD_OK && channel) {
        channel->getPosition(&g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
    }
}

void restoreMenuBGPosition() {
    FMOD::Channel* channel = nullptr;
    auto fmod = FMODAudioEngine::sharedEngine();
    auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
    if (result == FMOD_OK && channel) {
        channel->setPosition(g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
    }
}

void applyPositioning(FMODAudioEngine* fmod, FMOD::Channel* channel,
                      bool playMid, bool randomOffset) {
    if (!channel)
        return;

    unsigned int musicLength = fmod->getMusicLengthMS(0);
    if (musicLength == 0)
        return;

    if (playMid) {
        channel->setPosition(musicLength / 2, FMOD_TIMEUNIT_MS);
    }
    else if (randomOffset) {
        unsigned int randomPosition =
            geode::utils::random::generate<unsigned int>(0, musicLength - 1);
        channel->setPosition(randomPosition, FMOD_TIMEUNIT_MS);
    }
}

void stopAndRestoreMenuMusicIfCustomPlayed() {
    if (!g_customMusicPlayed)
        return;

    auto fmod = FMODAudioEngine::sharedEngine();

    fmod->stopAllMusic(true);
    GameManager::sharedState()->playMenuMusic();

    restoreMenuBGPosition();

    g_customMusicPlayed = false;
}

} // namespace

class $modify(LevelInfoLayer) {

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        storeMenuBGPosition();
        return true;
    }

    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();

        FMOD::Channel* channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto level = this->m_level;

        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
        bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

        if (level && level->m_songID != 0) {

            if (!musicManager->isSongDownloaded(level->m_songID))
                return;

            auto songPath = musicManager->pathForSong(level->m_songID);
            if (songPath.empty())
                return;

            fmod->stopAllMusic(true);
            fmod->playMusic(songPath, true, fadeTime, 0);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
            if (result == FMOD_OK && channel) {
                g_customMusicPlayed = true;
                applyPositioning(fmod, channel, playMid, randomOffset);
            }
        }
        else if (level && level->m_audioTrack != -1) {

            auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (trackPath.empty())
                return;

            fmod->stopAllMusic(true);
            fmod->playMusic(trackPath, true, fadeTime, 0);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
            if (result == FMOD_OK && channel) {
                g_customMusicPlayed = true;
                applyPositioning(fmod, channel, playMid, randomOffset);
            }
        }
    }

    void loadLevelStep() {
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        LevelInfoLayer::loadLevelStep();
    }

    void onBack(CCObject* sender) {
        stopAndRestoreMenuMusicIfCustomPlayed();
        LevelInfoLayer::onBack(sender);
    }
};

class $modify(CustomSongWidget) {

    void downloadSongFinished(int p0) {

        auto songPath =
            MusicDownloadManager::sharedState()->pathForSong(this->m_customSongID);

        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
        bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

        auto fmod = FMODAudioEngine::sharedEngine();
        fmod->playMusic(songPath, true, fadeTime, 0);

        FMOD::Channel* channel = nullptr;
        auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
        if (result == FMOD_OK && channel) {
            g_customMusicPlayed = true;
            applyPositioning(fmod, channel, playMid, randomOffset);
        }

        CustomSongWidget::downloadSongFinished(p0);
    }

    void deleteSong() {
        CustomSongWidget::deleteSong();

        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        GameManager::sharedState()->playMenuMusic();

        g_customMusicPlayed = false;
    }
};

class $modify(EditLevelLayer) {

    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level))
            return false;

        storeMenuBGPosition();

        if (!Mod::get()->getSettingValue<bool>("playEditorLevel"))
            return true;

        // always stop previous music
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);

        FMOD::Channel* channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();

        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
        bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

        if (level && level->m_songID != 0) {

            if (!musicManager->isSongDownloaded(level->m_songID))
                return true;

            auto songPath = musicManager->pathForSong(level->m_songID);
            if (songPath.empty())
                return true;

            fmod->playMusic(songPath, true, fadeTime, 0);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
            if (result == FMOD_OK && channel) {
                g_customMusicPlayed = true;
                applyPositioning(fmod, channel, playMid, randomOffset);
            }
        }
        else if (level && level->m_audioTrack != -1) {

            auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (trackPath.empty())
                return true;

            fmod->playMusic(trackPath, true, fadeTime, 0);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
            if (result == FMOD_OK && channel) {
                g_customMusicPlayed = true;
                applyPositioning(fmod, channel, playMid, randomOffset);
            }
        }

        return true;
    }

    void onPlay(CCObject* sender) {
        stopAndRestoreMenuMusicIfCustomPlayed();
        EditLevelLayer::onPlay(sender);
    }

    void onBack(CCObject* sender) {
        stopAndRestoreMenuMusicIfCustomPlayed();
        EditLevelLayer::onBack(sender);
    }
};
