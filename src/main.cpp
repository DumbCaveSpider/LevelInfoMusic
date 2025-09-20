#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

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
        bool m_isActive = true;
        static constexpr int MAX_RETRIES = 5;
    };

public:
    // the way it breaks if u exit a level so this function exist just to fix it
    void playCustomSong(const std::string &songPath, float fadeTime, bool playMid)
    {
        auto fmod = FMODAudioEngine::sharedEngine();

        // stop all music
        fmod->stopAllMusic(true);

        // play music for the mid thing
        fmod->playMusic(songPath, true, fadeTime, 1);

        if (!playMid)
        {
            log::info("Playing custom song from start: {}", songPath);
        }
        else
        {

            // Set position to middle using a delayed approach
            Loader::get()->queueInMainThread([this, songPath]()
                                             {
                if (!m_fields->m_isActive) {
                    return;
                }
                auto audioEngine = FMODAudioEngine::sharedEngine();
                if (auto channelGroup = audioEngine->m_backgroundMusicChannel) {
                    if (audioEngine && audioEngine->m_backgroundMusicChannel) {
                        log::info("Using fallback music length for positioning");
                    } else {
                        log::warn("Background music channel not available, using fallback");
                    }
                    
                    unsigned int middleMs = audioEngine->getMusicLengthMS(1) / 2;
                    log::info("Setting music position to middle: {} ms (estimated)", middleMs);
                    
                    // Get the first channel from the channel group
                    FMOD::Channel* channel = nullptr;
                    auto result = channelGroup->getChannel(0, &channel);
                    if (result == FMOD_OK && channel) {
                        // Use position unit 1 for cross-platform compatibility (typically milliseconds)
                        auto setResult = channel->setPosition(middleMs, 1);
                        log::info("Channel position set result: {}", (int)setResult);
                    } else {
                        log::warn("Failed to get channel from group, result: {}", (int)result);
                    }
                } });
            log::info("Playing custom song from middle: {}", songPath);
        }
    }

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        // Store the level reference for later use
        m_fields->m_currentLevel = level;
        m_fields->m_hasInitialized = true;
        m_fields->m_isActive = true;

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
        // @geode-ignore(unknown-setting)
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
            log::info("level custom music playback for song ID: {}", level->m_songID);

            // Try to play the song immediately if downloaded, otherwise force download
            if (musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    log::info("Custom song is available, playing immediately: {}", songPath);
                    // stop all music
                    fmod->stopAllMusic(true);
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
            // Built-in audio track - use helper function for cleaner code
            fmod->stopAllMusic(true);
            auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (!audioPath.empty())
            {
                // Ensure portable composition: convert gd::string to std::filesystem::path explicitly
                auto resourcePath = geode::dirs::getResourcesDir() / std::filesystem::path(audioPath.c_str());
                log::info("Level uses built-in audio track: {}, from path: {}", level->m_audioTrack, resourcePath.string());
                fmod->playMusic(resourcePath.string(), true, fadeTime, level->m_audioTrack);
            }
            else
            {
                log::debug("Unknown built-in track ID: {}", level->m_audioTrack);
            }
        }
    }

    void scheduleDownloadCheck()
    {
        // Check every 2 seconds for download completion
        Loader::get()->queueInMainThread([this]()
                                         {
            if (!m_fields->m_isActive) {
                return;
            }
            auto level = m_fields->m_currentLevel;
            if (!level) return;

            auto musicManager = MusicDownloadManager::sharedState();
            if (level->m_songID != 0 && musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    // @geode-ignore(unknown-setting)
                    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                    log::info("Download completed, playing custom song: {}", songPath);

                    auto fmod = FMODAudioEngine::sharedEngine();
                    // stop all music
                    fmod->stopAllMusic(true);
                    playCustomSong(songPath, fadeTime, playMid);
                }
            }
            else
            {
                // Keep checking until download completes
                if (m_fields->m_isActive) {
                    scheduleDownloadCheck();
                }
            } });
    }

    void scheduleRetryCheck()
    {
        // Reset retry count
        m_fields->m_retryCount = 0;

        // Schedule a check to ensure music is playing
        Loader::get()->queueInMainThread([this]()
                                         {
            if (!m_fields->m_isActive) {
                return;
            }
            this->checkMusicAndRetry(); });
    }

    void checkMusicAndRetry()
    {
        if (!m_fields->m_isActive)
        {
            return;
        }
        auto fmod = FMODAudioEngine::sharedEngine();

        // Check if music is actually playing
        if (m_fields->m_retryCount < Fields::MAX_RETRIES)
        {
            m_fields->m_retryCount++;
            log::warn("retry {}: Music not playing, attempting to force play again", m_fields->m_retryCount);

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
                        // @geode-ignore(unknown-setting)
                        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                        fmod->stopAllMusic(true);
                        playCustomSong(songPath, fadeTime, playMid);
                        log::info("Re-attempting custom song playback");
                    }
                }
            }
            else if (level)
            {
                // Retry built-in track using proper path resolution
                // @geode-ignore(unknown-setting)
                float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                fmod->stopAllMusic(true);

                // Use helper function to get the correct path for built-in tracks
                auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
                if (!audioPath.empty())

                {
                    auto resourcePath = geode::dirs::getResourcesDir() / std::filesystem::path(audioPath.c_str());
                    fmod->playMusic(resourcePath.string(), true, fadeTime, 0);
                    log::info("Re-attempting built-in track playback: {}", resourcePath.string());
                }
                else
                {
                    // Fallback to original method if path not found
                    fmod->playMusic("", true, fadeTime, level->m_audioTrack);
                    log::info("Re-attempting built-in track playback (fallback)");
                }
            }

            // Schedule another check if we haven't exceeded max retries
            if (m_fields->m_retryCount < Fields::MAX_RETRIES)
            {
                Loader::get()->queueInMainThread([this]()
                                                 {
                    if (!m_fields->m_isActive) {
                        return;
                    }
                    this->checkMusicAndRetry(); });
            }
        }
        else
        {
            log::error("Failed to play music after {} retries", Fields::MAX_RETRIES);
        }
    }

    void onExitTransitionDidStart()
    {
        m_fields->m_isActive = false;

        if (!PlayLayer::get())
        {
            auto fmod = FMODAudioEngine::sharedEngine();
            auto gm = GameManager::sharedState();

            // get the fadetime from the settings
            // @geode-ignore(unknown-setting)
            float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

            // Stop the current level music forcefully
            fmod->stopAllMusic(true);
            log::info("Leaving LevelInfoLayer, level music stopped");

            // Fade back to menu music
            fmod->fadeInMusic(fadeTime, m_fields->m_originalVolume);
            gm->playMenuMusic();
        }

        LevelInfoLayer::onExitTransitionDidStart();
    }

    void onEnter()
    {
        m_fields->m_isActive = true;
        LevelInfoLayer::onEnter();
        scheduleRetryCheck();
    }

    void onPlay(CCObject *sender)
    {
        m_fields->m_isActive = false;
        // Stop the current level music and ensure it stays stopped
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("onPlay triggered - stopping level music");

        // Stop all music forcefully multiple times to ensure it's stopped
        fmod->stopAllMusic(true);
        fmod->stopAllMusic(false);

        // Also stop any effects that might be playing
        fmod->stopAllEffects();

        log::info("Music stopped, effects stopped");

        LevelInfoLayer::onPlay(sender);
    }

    void onBack(CCObject *sender)
    {
        m_fields->m_isActive = false;
        auto fmod = FMODAudioEngine::sharedEngine();
        auto gm = GameManager::sharedState();
        // Stop all music and effects when backing out
        fmod->stopAllMusic(true);
        fmod->stopAllEffects();
        gm->playMenuMusic();
        log::info("onBack triggered - stopping level music & play menu music");
        LevelInfoLayer::onBack(sender);
    }
};

class $modify(MyPlayLayer, PlayLayer)
{
public:
    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        // Stop all music before initializing the play layer
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("PlayLayer init - ensuring music is stopped");
        fmod->stopAllMusic(true);

        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
};
