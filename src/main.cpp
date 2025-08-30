#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class $modify(MyLevelInfoLayer, LevelInfoLayer)
{
    struct Fields
    {
        float m_originalVolume = 0.0f;
        int m_currentLevelSongID = 0;
        void *m_musicDelegate = nullptr;
        bool m_hasInitialized = false;
        GJGameLevel *m_currentLevel = nullptr;
        int m_retryCount = 0;
        static const int MAX_RETRIES = 5;
    };

public:
    void playCustomSong(const std::string &songPath, float fadeTime, bool playMid)
    {
        auto fmod = FMODAudioEngine::sharedEngine();

        if (!playMid)
        {
            // Play from start with fade
            fmod->playMusic(songPath, true, fadeTime, 1);
            log::info("FORCED: Playing custom song from start: {}", songPath);
        }
        else
        {
            // Play from middle
            fmod->playMusic(songPath, true, fadeTime, 1);

            // Set position to middle using FMOD channel directly
            Loader::get()->queueInMainThread([this, songPath]()
                                             {
                auto audioEngine = FMODAudioEngine::sharedEngine();
                auto channelGroup = audioEngine->m_backgroundMusicChannel;
                if (channelGroup != nullptr) {
                    unsigned int lengthMs = audioEngine->getMusicLengthMS(1);
                    unsigned int middleMs = lengthMs / 2;
                    log::info("FORCED: Setting music position to middle: {} ms of {} ms", middleMs, lengthMs);
                    
                    // Get the first channel from the channel group
                    FMOD::Channel* channel = nullptr;
                    auto result = channelGroup->getChannel(0, &channel);
                    if (result == FMOD_OK && channel) {
                        auto setResult = channel->setPosition(middleMs, 1);
                        log::info("FORCED: Channel position set result: {}", (int)setResult);
                    } else {
                        log::warn("FORCED: Failed to get channel from group, result: {}", (int)result);
                    }
                } });

            log::info("FORCED: Playing custom song from middle: {}", songPath);
        }
    }

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        // Store the level reference for later use
        m_fields->m_currentLevel = level;
        m_fields->m_hasInitialized = true;

        // Initialize music on first load
        initializeLevelMusic();

        return true;
    }

    void initializeLevelMusic()
    {
        auto level = m_fields->m_currentLevel;
        if (!level)
            return;

        // check if the music for this level is downloaded
        auto musicManager = MusicDownloadManager::sharedState();

        // get da settings value
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");

        // store the current level's song ID for later use
        m_fields->m_currentLevelSongID = level->m_songID;

        // get the FMOD audio engine and store original volume
        auto fmod = FMODAudioEngine::sharedEngine();
        m_fields->m_originalVolume = fmod->getBackgroundMusicVolume();

        // FORCEFULLY play music regardless of conditions
        if (level->m_songID != 0)
        {
            log::info("FORCING level custom music playback for song ID: {}", level->m_songID);

            // Try to play the song immediately if downloaded, otherwise force download
            if (musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    log::info("Custom song is available, playing immediately: {}", songPath);
                    playCustomSong(songPath, fadeTime, playMid);

                    // Schedule a retry check to ensure music is actually playing
                    scheduleRetryCheck();
                }
                else
                {
                    log::warn("Song reported as downloaded but path is empty, forcing download: {}", level->m_songID);
                    musicManager->downloadSong(level->m_songID);
                    // Schedule check to try again once download completes
                    scheduleDownloadCheck();
                }
            }
            else
            {
                log::info("Custom song not downloaded, forcing download: {}", level->m_songID);
                musicManager->downloadSong(level->m_songID);
                // Schedule check to try again once download completes
                scheduleDownloadCheck();
            }
        }
        else
        {
            log::info("Level uses built-in audio track: {}, playing default music", level->m_audioTrack);
            // Force play the built-in track
            fmod->playMusic("", false, fadeTime, level->m_audioTrack);

            // Schedule a retry check for built-in tracks too
            scheduleRetryCheck();
        }
    }

    void scheduleDownloadCheck()
    {
        // Check every 2 seconds for download completion
        Loader::get()->queueInMainThread([this]()
                                         {
            auto level = m_fields->m_currentLevel;
            if (!level) return;

            auto musicManager = MusicDownloadManager::sharedState();
            if (level->m_songID != 0 && musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                    log::info("FORCED: Download completed, playing custom song: {}", songPath);
                    playCustomSong(songPath, fadeTime, playMid);
                }
            }
            else
            {
                // Keep checking until download completes
                scheduleDownloadCheck();
            } });
    }

    void scheduleRetryCheck()
    {
        // Reset retry count
        m_fields->m_retryCount = 0;

        // Schedule a check to ensure music is playing
        Loader::get()->queueInMainThread([this]()
                                         { this->checkMusicAndRetry(); });
    }

    void checkMusicAndRetry()
    {
        auto fmod = FMODAudioEngine::sharedEngine();

        // Check if music is actually playing
        if (m_fields->m_retryCount < m_fields->MAX_RETRIES)
        {
            m_fields->m_retryCount++;
            log::warn("FORCED RETRY {}: Music not playing, attempting to force play again", m_fields->m_retryCount);

            // Force retry music initialization
            auto level = m_fields->m_currentLevel;
            if (level && level->m_songID != 0)
            {
                auto musicManager = MusicDownloadManager::sharedState();
                if (musicManager->isSongDownloaded(level->m_songID))
                {
                    auto songPath = musicManager->pathForSong(level->m_songID);
                    if (!songPath.empty())
                    {
                        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                        playCustomSong(songPath, fadeTime, playMid);
                        log::info("FORCED RETRY: Re-attempting custom song playback");
                    }
                }
            }
            else if (level)
            {
                // Retry built-in track
                float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                fmod->playMusic("", false, fadeTime, level->m_audioTrack);
                log::info("FORCED RETRY: Re-attempting built-in track playback");
            }

            // Schedule another check if we haven't exceeded max retries
            if (m_fields->m_retryCount < m_fields->MAX_RETRIES)
            {
                Loader::get()->queueInMainThread([this]()
                                                 { this->checkMusicAndRetry(); });
            }
        }
        else
        {
            log::error("FORCED FAILURE: Failed to play music after {} retries", m_fields->MAX_RETRIES);
        }
    }

    void onBack(CCObject *sender)
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        auto gm = GameManager::sharedState();

        // get the fadetime from the settings
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

        // Stop the current level music forcefully
        fmod->stopMusic(1);
        log::info("FORCED: Leaving LevelInfoLayer, level music stopped");

        // Fade back to menu music
        fmod->fadeInMusic(fadeTime, m_fields->m_originalVolume);
        gm->playMenuMusic();

        LevelInfoLayer::onBack(sender);
    }
};
