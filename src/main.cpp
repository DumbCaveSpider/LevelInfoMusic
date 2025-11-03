#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/CustomSongWidget.hpp>

using namespace geode::prelude;

// i have to refactor the entire code because its horrible
// this time its more compact and actual readable
// trust me this is WAY WAY better and didnt took that long to do

unsigned int g_backgroundMusicPosition = 0; // global variable to store background music position
bool g_customMusicPlayed = false;           // global variable to check if custom music was actually played

class $modify(LevelInfoLayer)
{
    void onEnterTransitionDidFinish()
    {
        LevelInfoLayer::onEnterTransitionDidFinish(); // call original function

        FMOD::Channel *channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto level = this->m_level;

        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
        bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

        log::info("music used: {}", level->m_songID);
        log::info("music length: {}", fmod->getMusicLengthMS(0));
        // check if the level has custom music
        if (level && level->m_songID != 0)
        {
            auto songPath = musicManager->pathForSong(level->m_songID);

            // check if the music is not downloaded
            if (!musicManager->isSongDownloaded(level->m_songID))
            {
                log::info("song not downloaded");
                g_customMusicPlayed = false;
                return;
            }

            if (songPath.empty())
            {
                log::warn("no custom music found for id {}", level->m_songID);
                g_customMusicPlayed = false;
                return;
            }

            log::info("playing custom music: {}", songPath);

            fmod->stopAllMusic(true);
            fmod->playMusic(songPath, true, fadeTime, 0);
            g_customMusicPlayed = true;

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel); // assume the channel playing the music is channel 0
            if (result == FMOD_OK && channel)
            {
                if (playMid)
                {
                    channel->setChannelGroup(fmod->m_backgroundMusicChannel);
                    channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
                    randomOffset = false;
                }

                if (randomOffset)
                {
                    // get a random position between 0 and the length of the music
                    unsigned int musicLength = fmod->getMusicLengthMS(0);
                    unsigned int randomPosition = GameToolbox::fast_rand() % musicLength;
                    channel->setPosition(randomPosition, FMOD_TIMEUNIT_MS);
                    log::info("random position: {}", randomPosition);
                    playMid = false;
                }
            }
        }

        // uses default audio track if no custom music
        else if (level && level->m_audioTrack != -1)
        {
            auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (trackPath.empty())
            {
                log::warn("no audio track found for id {}", level->m_audioTrack);
                g_customMusicPlayed = false;
                return;
            }
            log::info("playing default track: {}", trackPath);

            fmod->stopAllMusic(true);
            fmod->playMusic(trackPath, true, fadeTime, 0);
            g_customMusicPlayed = true;

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel); // assume the channel playing the music is channel 0
            if (result == FMOD_OK && channel)
            {
                if (playMid)
                {
                    channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
                    randomOffset = false;
                }

                if (randomOffset)
                {
                    // get a random position between 0 and the length of the music
                    unsigned int musicLength = fmod->getMusicLengthMS(0);
                    unsigned int randomPosition = GameToolbox::fast_rand() % musicLength;
                    channel->setPosition(randomPosition, FMOD_TIMEUNIT_MS);
                    log::info("random position: {}", randomPosition);
                    playMid = false;
                }
            }
        }
    }

    // store the current background music position when entering level info
    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;
        FMOD::Channel *channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto resultBG = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
        if (resultBG == FMOD_OK && channel)
        {
            channel->getPosition(&g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
            log::info("store menu bg music position: {}", g_backgroundMusicPosition);
        }

        return true;
    }

    void loadLevelStep()
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        fmod->stopAllMusic(true);
        log::info("stop music when loading level");
        LevelInfoLayer::loadLevelStep();
    }

    // stop music if one of these are called
    void keyBackClicked()
    {
        this->stopCurrentMusic(this->m_level);
        LevelInfoLayer::keyBackClicked();
    }
    void onBack(CCObject *sender)
    {
        this->stopCurrentMusic(this->m_level);
        LevelInfoLayer::onBack(sender);
    }

    // my wacky functions :D
    void stopCurrentMusic(GJGameLevel *level)
    {
        if (!g_customMusicPlayed)
        {
            log::info("custom music was not played, not stopping music");
            return;
        }

        log::info("stop custom music when exiting level info");
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        GameManager::sharedState()->playMenuMusic();

        FMOD::Channel *channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto resultBG = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
        if (resultBG == FMOD_OK && channel)
        {
            channel->setPosition(g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
            log::info("restore menu bg music position: {}", g_backgroundMusicPosition);
        }
        g_customMusicPlayed = false; // reset for next time
    }
};

class $modify(CustomSongWidget)
{
    void downloadSongFinished(int p0)
    {
        // auto play the custom music when download finishes

        auto songPath = MusicDownloadManager::sharedState()->pathForSong(this->m_customSongID);
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
        bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");
        FMODAudioEngine::sharedEngine()->playMusic(songPath, true, fadeTime, 0); // play the music
        if (playMid)
        {
            FMOD::Channel *channel = nullptr;
            auto fmod = FMODAudioEngine::sharedEngine();
            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel); // assume the channel playing the music is channel 0
            if (result == FMOD_OK && channel)
            {
                if (playMid)
                {
                    channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
                }
                if (randomOffset)
                {
                    // get a random position between 0 and the length of the music
                    unsigned int musicLength = fmod->getMusicLengthMS(0);
                    unsigned int randomPosition = GameToolbox::fast_rand() % musicLength;
                    channel->setPosition(randomPosition, FMOD_TIMEUNIT_MS);
                    log::info("random position: {}", randomPosition);
                    playMid = false;
                }
            }
        }
        CustomSongWidget::downloadSongFinished(p0);
    }

    void deleteSong()
    {
        // play the menu music when deleting the custom song
        CustomSongWidget::deleteSong();
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        GameManager::sharedState()->playMenuMusic();
        g_customMusicPlayed = false; // reset when deleting
    }
};