#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

// i have to refactor the entire code because its horrible
// this time its more compact and actual readable
class $modify(LevelInfoLayer)
{
    struct Fields
    {
        unsigned int m_backgroundMusicPosition = 0;
    };

    void onEnterTransitionDidFinish()
    {
        LevelInfoLayer::onEnterTransitionDidFinish(); // call original function

        FMOD::Channel *channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto level = this->m_level;

        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");

        log::info("music used: {}", level->m_songID);

        // check if the level has custom music
        if (level && level->m_songID != 0)
        {
            auto songPath = musicManager->pathForSong(level->m_songID);

            // check if the music is not downloaded
            if (!musicManager->isSongDownloaded(level->m_songID))
            {
                log::info("song not downloaded");
                return;
            }

            if (songPath.empty())
            {
                log::warn("no custom music found for id {}", level->m_songID);
                return;
            }

            fmod->playMusic(songPath, true, fadeTime, 1);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel); // assume the channel playing the music is channel 0
            if (result == FMOD_OK && channel && playMid)
            {
                channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
            }
        }

        // uses default audio track if no custom music
        if (level && level->m_audioTrack != 0)
        {
            auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (trackPath.empty())
            {
                log::warn("no audio track found for id {}", level->m_audioTrack);
                return;
            }
            fmod->playMusic(trackPath, true, fadeTime, 1);

            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel); // assume the channel playing the music is channel 0
            if (result == FMOD_OK && channel && playMid)
            {
                channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
            }
        }
    }

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;
        // store the current background music position
        FMOD::Channel *channel = nullptr;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto resultBG = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
        if (resultBG == FMOD_OK && channel)
        {
            channel->getPosition(&m_fields->m_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
            log::info("store menu bg music position: {}", m_fields->m_backgroundMusicPosition);
        }

        return true;
    }

    // stop music if one of these are called
    void keyBackClicked()
    {
        auto level = this->m_level;
        if (MusicDownloadManager::sharedState()->isSongDownloaded(level->m_songID) || level->m_audioTrack != 0)
            this->stopCurrentMusic();
        LevelInfoLayer::keyBackClicked();
        this->returnToCurrentBGMusicPosition();
    }

    void onBack(CCObject *sender)
    {
        auto level = this->m_level;
        if (MusicDownloadManager::sharedState()->isSongDownloaded(level->m_songID) || level->m_audioTrack != 0)
            this->stopCurrentMusic();
        LevelInfoLayer::onBack(sender);
        this->returnToCurrentBGMusicPosition();
    }

    // my wacky functions :D
    void stopCurrentMusic()
    {
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        GameManager::sharedState()->playMenuMusic();
    }

    void returnToCurrentBGMusicPosition()
    {
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        GameManager::sharedState()->playMenuMusic();
        if (MusicDownloadManager::sharedState()->isSongDownloaded(this->m_level->m_songID) || this->m_level->m_audioTrack != 0)
        {
            FMOD::Channel *channel = nullptr;
            auto fmod = FMODAudioEngine::sharedEngine();
            auto resultBG = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
            if (resultBG == FMOD_OK && channel)
            {
                channel->setPosition(m_fields->m_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
                log::info("return to menu bg music position: {}", m_fields->m_backgroundMusicPosition);
            }
        }
    }
};